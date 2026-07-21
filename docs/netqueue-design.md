# NetQueue Design

Status: design agreed, implementation not started beyond skeletons.

## 1. Goals

`NetQueue` is the CX abstraction for servicing sockets across platforms. It must serve three
workloads well:

1. **A single socket** — intraserver / client connections, where simplicity and latency matter
   and there is no concurrency to exploit.
2. **Many TCP sockets** — a conventional server with per-connection state.
3. **One UDP socket, many peers** — *the dominant workload.* A server process multiplexes a
   large number of clients over a single UDP socket via `recvfrom`/`sendto`, so the OS does not
   have to track per-client socket state. This case sets most of the architecture.

Design constraints inherited from CX: no external runtime deps, `xalloc` for all allocation,
`.cxh`-generated object model, minimal boilerplate, static linking.

## 2. Core decisions

| Question | Decision |
|---|---|
| I/O model | **Completion (proactor).** NetQueue owns the buffers; events report *what happened*, not *what to do next*. Native on IOCP; emulated on readiness backends. |
| Ordering unit | **The flow**, not the socket. |
| Flow key | Stream: the connection. Datagram: `(srcaddr, srcport)`, computed by NetQueue. |
| Dispatch | Shared lock-free runqueue + per-flow atomic claim (actor-mailbox pattern). |
| Threading | NetQueue owns its worker threads; they block directly in the platform wait call. |
| Flow lifecycle | Auto-create on first packet, capped, reclaimed LRU **only under cap pressure**, with an unknown-source handler for app-driven promotion. |
| Flow teardown | `NET_FlowClosed` delivered as a terminal event on the flow's own queue, so it is ordered after all pending packets. Fires on **every** close cause, with the cause in `NetCloseReason`. |
| Flow object | A full `.cxh` object. `flow->user` needs a generated destructor and the runqueue needs a strong ref; flows are session-lifetime, so the overhead is amortized. |
| Flow state | Flows carry app `user` data, so the hot path needs no hashtable lookup in app code. `socket->user` is removed; per-socket state lives in the handler `ctx`. |
| Ergonomics | Composed `netqueueConnect()` / `netqueueListen()` helpers over the public API. Flows are never part of setup. |
| Sync API | **None.** Async only, enforced by design; polled mode covers synchronous-looking control flow. |
| Packet buffers | Pooled fixed-size buffers on a `PrQueue` freelist. Zero allocation in steady state. Size and cap configured at queue creation. |
| Pool exhaustion | **Drop the datagram, bump `droppedNoBuf`.** Never grow past the cap. Stream sockets instead stop posting receives and let TCP flow control apply backpressure. |
| Socket buffers | Stream and datagram arms in a **union** on `NetSocket`, discriminated by `type`. No subclasses, no indirection. |
| Flow inbox | Intrusive MPSC stack (two pointers), not `PrQueue`. Per-flow structures must stay small. |
| Ownership | Queue owns sockets, socket owns flows; both back-arms weak. Runqueue holds its own strong ref while a flow is queued. |
| Buffer primitives | `bufringReserve`/`Commit`, `bufchainGatherIov`, `BufPool` are **public** buffer API, not net-private. |
| Handlers | `NetHandlers`, a plain struct of function pointers. Per-flow → per-socket → queue-wide, falling through **per field**, so partial overrides are free. No closure chains. |
| Backpressure | Send fails over a high-water mark; `NET_SendReady` on drain. |
| DNS | Async on a dedicated **bounded** `TaskQueue` owned by `net` (sysq pattern, private instance), never on a net thread and never on shared `sysq`. |
| Backend order | select → IOCP → epoll → kqueue. |
| select portability | **One shared `NetQueueSelect`** over an opaque per-platform `NetSelectSet`. No Windows/Unix queue twins. |

## 3. Why the flow is the ordering unit

The original sketch said "events MUST be processed in order," which reads naturally as
per-socket ordering. That is correct for TCP and useless for the dominant workload: with one UDP
socket serving every client, per-socket ordering means a single ordering domain, so the entire
server runs on one worker and the thread pool does nothing.

So the ordering domain is the **flow**:

```
TCP socket  -> exactly one flow   (ordering domain = the connection)
UDP socket  -> N flows            (ordering domain = the peer address)
```

Both go through the same runqueue, the same claim protocol, and the same callback signature.
There is one dispatch implementation, not two.

Ordering guarantee: **events belonging to one flow are strictly ordered and never execute
concurrently.** Events in different flows may execute simultaneously on any worker. Nothing is
promised about ordering *between* flows.

Note this matches the wire protocol's own semantics — the UDP protocol carries sequence numbers
on the subset of packets that need ordering — so per-flow serialization is exactly the guarantee
the application needs, and no stronger.

## 4. Architecture

Three stages. The split exists so the stage that cannot be parallelized does as little as
possible.

```
   ┌─────────────────────────────────────────────────────────────┐
   │ INGEST                                                      │
   │   platform wait call (GQCS / epoll_wait / select)           │
   │   completion -> buffer already filled                       │
   │   compute flow key -> find or create flow                   │
   │   push buffer onto flow's queue                             │
   │   if flow was idle: push flow onto runqueue                 │
   │   repost a pooled buffer                                    │
   └──────────────────────────┬──────────────────────────────────┘
                              │  runqueue (PrQueue, lock-free MPMC)
   ┌──────────────────────────▼──────────────────────────────────┐
   │ DISPATCH (N workers)                                        │
   │   flow = runq.pop()                                         │
   │   if (!CAS(flow->claimed, 0, 1)) continue   // someone has it│
   │   drain flow->pkts  -> invoke handlers (stage 3)            │
   │   release claim; if refilled meanwhile, requeue             │
   └──────────────────────────┬──────────────────────────────────┘
                              │
   ┌──────────────────────────▼──────────────────────────────────┐
   │ APPLICATION CALLBACK                                        │
   │   runs on a worker thread, serialized per flow              │
   │   flow->user gives O(1) access to per-client state          │
   │   returns buffer to pool unless it keeps it                 │
   └─────────────────────────────────────────────────────────────┘
```

The claim protocol gives per-flow serialization *and* load balancing with no mutex, and it
naturally batches: a worker that claims a flow drains everything queued for it, amortizing the
claim CAS across a burst of packets from one client.

**Requeue race.** The subtle part is the handoff between "worker finishes draining" and "ingest
pushes a new packet." Sequence must be:

```
worker:                          ingest:
  drain until queue empty          pkts.push(p)
  store(claimed, 0)   [release]    if (exchange(queued, 1) == 0)
  if (!pkts.empty())                   runq.push(flow)
      try reclaim / requeue
```

Both sides re-check after their store so a packet arriving exactly at the release point cannot
be stranded. This is the one piece of the core that warrants a dedicated stress test.

## 5. Objects

### NetQueue (abstract, `cx/net/queue.cxh`)

Existing members stay: `flags`, `sockets` hashtable, `lock`. The `callbacks` hashtable is
**replaced** by a `NetHandlers` (§7).

Added:

```
NetHandlers handlers;      // queue-wide fallback set; by value, not a table
void* handlerCtx;
PrQueue runq;              // flows with pending work
BufPool pool;              // pooled receive buffers, capped at creation
atomic:uint64 droppedNoBuf;// datagrams dropped for lack of a buffer
Thread* workers;           // owned worker threads (empty in polled mode)
int32 nworkers;
uint32 maxflows;           // 0 = unlimited
uint32 reclaimBatch;       // flows to reclaim per cap hit
bool noReclaim;            // debug: never reclaim (notimeout / snotimeout)
atomic:uint32 nflows;
```

No separate `unknownSourceCB`: it is `handlers.flowOpen`, resolved by the same fallthrough as
everything else, and per-socket overridable for free as a result.

Existing methods keep their signatures. `tick()` in polled mode runs ingest and dispatch inline
on the caller's thread — the same code the workers run, just without the threads.

### Creation config

`netqueueCreate(int32 nthreads, flags_t flags)` has run out of room: the pool cap, buffer size,
`maxflows`, `reclaimBatch`, and the watermark defaults all have to be settable at creation and none
of them wants to be a positional argument. Follow the `TaskQueue` precedent already in the tree —
a config struct with presets:

```c
typedef struct NetQueueConfig {
    int32   nthreads;         // 0 = polled mode
    flags_t flags;

    size_t  recvBufSize;      // per-buffer size
    uint32  recvBufInitial;   // preallocated at creation
    uint32  recvBufMax;       // hard cap; the receive memory ceiling

    uint32  maxflows;         // 0 = unlimited
    uint32  reclaimBatch;
    bool    noReclaim;        // notimeout / snotimeout

    size_t  sendHigh, sendLow;   // default watermarks for sockets on this queue
} NetQueueConfig;

void netqueuePresetClient(NetQueueConfig* conf);   // polled, small pool, few flows
void netqueuePresetServer(NetQueueConfig* conf);   // threaded, large pool, many flows

NetQueue* netqueueCreate(const NetQueueConfig* conf);
```

Presets are what make this ergonomic rather than a burden — the common call stays two lines, and
the tunables are there when a deployment needs them:

```c
NetQueueConfig conf;
netqueuePresetServer(&conf);
conf.maxflows = 50000;
NetQueue* q = netqueueCreate(&conf);
```

This mirrors `tqPresetBalanced()` exactly, so it is a pattern the codebase already teaches. The
existing two-argument `netqueueCreate()` has no external callers yet, so replacing it costs
nothing.

### NetSocket (abstract, `cx/net/socket.cxh`)

Existing members mostly stand. Changes:

- Replace `recvBuf` / `sendBuf` with a single `NetSocketBufs bufs` union, discriminated by the
  existing `type` field. See "Storage layout" in §6.
- Add `hashtable:NetFlowKey:NetFlow flows` + `RWLock flowLock` for datagram sockets; stream
  sockets hold a single `NetFlow*`.
- Add `size_t sendHigh, sendLow` — backpressure watermarks.
- Add `NetHandlers* handlers` + `void* handlerCtx` — per-socket set, optional.
- Keep `atomic:bool canSend`.
- **Remove `sarray:stvar user`** — per-peer state moved to `flow->user`, per-socket state to the
  handler `ctx`. See §12.

### NetFlow (new, `cx/net/flow.cxh`)

A **full `.cxh` object** — refcounted, weak-referenceable, with a generated destructor.

```
weak:NetSocket socket;     // resolved ONCE per dispatch batch, never per packet -- see §13
NetAddr peer;              // datagram: the flow key source
atomic:uint32 claimed;     // dispatch claim
atomic:uint32 queued;      // present on runqueue
atomic:uint32 dying;       // marked for close; blocks further dispatch
atomic:ptr inbox;          // intrusive MPSC stack of pending NetMessage (see §6)
NetMessage* ready;         // consumer-private FIFO, refilled from inbox
atomic:int64 lastActive;   // LRU ordering; relaxed store on the hot path
sarray:stvar user;         // application state -- the canonical place for it
                           // stream sockets: allocated lazily, often unused
NetHandlers* handlers;     // per-flow handlers, optional (see §7)
void* handlerCtx;
```

The object overhead is justified, and the deciding argument is `user`. Since `socket->user` was
removed, the `sarray:stvar` on the flow is the *only* place per-peer application state lives —
strings, buffers, objects, anything with a destructor. A plain struct would mean hand-writing that
teardown and getting it exactly right on five different close paths (§8). The generated destructor
does it once.

Refcounting is load-bearing too, not incidental: §13 requires the runqueue to hold a strong
reference while a flow is queued, precisely so a socket close cannot free a flow out from under a
worker that is mid-batch. That is an object-system job.

The cost is affordable *for this workload*. Flows here are long-lived pseudo-connections —
thousands concurrent, but created and destroyed at session rate, not packet rate. Per-flow object
overhead is a one-time allocation amortized over the whole session; the per-*packet* path never
touches the object system at all (it pushes onto `inbox` and returns). Object cost would matter if
flows churned per-request, which is the shape this protocol specifically does not have.

`NetFlow` is a separate object rather than fields on the socket so the planned "negotiate over TCP,
then hand off to UDP" model can later point a TCP socket and a UDP flow at the same flow object
without restructuring.

## 6. Buffer strategy

This is the part that was unsettled. The resolution is that the two buffer types are not
alternatives — each is correct for a different direction, and the choice follows from whether
the data is a byte stream or a discrete message.

| Path | Structure | Rationale |
|---|---|---|
| Stream recv | `BufRing` (per socket) | Byte stream with no message boundaries; ring wraparound avoids moving data, and `bufringFeed` already reads directly into internal storage. |
| Stream send | `BufChain` (per socket) | Takes ownership of app buffers with no copy on the way in, and hands whole segments out for scatter/gather on the way out. |
| Datagram recv | Pooled `buffer`, then per-flow intrusive MPSC stack | Each packet is discrete and independently owned; ownership moves pool → flow → callback → pool with no copy anywhere. |
| Datagram send | `PrQueue` of `NetMessage` (per socket) | Needs a destination address per entry, which a byte chain cannot express. |

**The coalescing problem is solved by scatter/gather, not by copying.** On send, instead of
flattening the `BufChain` into one contiguous block, gather the head segments into a
`WSABUF[]` / `struct iovec[]` and hand the array to `WSASend`/`writev`. No reallocation, no
coalescing copy. This is what the "minimize reallocations to coalesce" note was reaching for.

### Storage layout

Stream and datagram sockets need structurally different buffers, but a socket's type is fixed at
construction and never changes. They are therefore mutually exclusive, not duplicated, and the
right expression of that is a union — not separate subclasses, and not a buffer object behind a
pointer:

```c
// net_shared.h
typedef struct NetSocketBufs {
    union {
        struct {
            BufRing  recv;       // inbound byte stream
            BufChain send;       // outbound, owns app buffers
        } stream;
        struct {
            PrQueue send;        // NetMessage entries, each with its own destination
        } dgram;
    };
} NetSocketBufs;
```

Rationale for the union over the two alternatives already considered:

- **vs. `NetSocketStream` / `NetSocketDgram` subclasses** — this was walked back for a good
  reason. Every backend would need two of everything, and the parts that genuinely differ by
  type are a handful of buffer members, not behavior. A union keeps one class per backend.
- **vs. a type-specific buffer object behind a pointer** — costs a pointer chase on the hot path
  and a second allocation per socket, to save memory on a struct that is already allocated per
  socket. A union member sits at a fixed offset, so access compiles to the same instruction a
  direct member would.

**Codegen constraints (confirmed).** `cxautogen` does not support inline unions at all — one
written directly into a class body will either fail or silently generate wrong code. It handles
POD C structs and unions fine as *opaque named types*, which is the form used here. Two rules
follow:

1. `NetSocketBufs` must be declared in C, not in a class body: either in a plain C header
   (`net_shared.h`, as above) or at file scope in a `.cxh` outside any class, where the
   C-passthrough copies it into the generated `.h` verbatim.
2. **Codegen cannot see the union's members and will not emit init or destroy code for them.**
   This is the desired behavior — which arm is live depends on `type`, and only hand-written code
   knows that — but it means construction and teardown of the correct arm must be written
   manually into `NetSocket_init()` and `NetSocket_destroy()`. Omitting either is a silent leak
   or a silent use of an uninitialized buffer, with nothing in the build to catch it.

Those functions already branch on `type` for the socket handle itself, so this adds a case to an
existing branch rather than new structure — but it is the one place in this design where the
usual "codegen handles lifetime" assumption does not hold, and it should be commented as such at
the declaration site.

**On the `PrQueue` cost specifically:** this matters far less than it looks, because the datagram
send queue is *per socket*, and the workload that motivates this design has exactly **one**
datagram socket. A few hundred bytes on the single socket carrying the entire server load is not
a cost worth designing around, in debug or release.

Where per-entity `PrQueue` cost genuinely would have mattered is `NetFlow`, which scales to
thousands of instances — and that is the one place it should not be used.

### Per-flow packet queue: intrusive MPSC, not `PrQueue`

`PrQueue` is the wrong tool for `flow->pkts` on three counts, and the third is a correctness
argument rather than a size one:

1. **Size.** Per-flow, so it multiplies by the flow cap. Segments, a mutex, growth policy, and
   (in debug) a large stats block, replicated thousands of times.
2. **Generality.** `PrQueue` is MPMC. The flow inbox is strictly **MPSC** — many ingest threads
   can push, but the claim protocol guarantees exactly one consumer at a time. That guarantee is
   the whole point of the flow abstraction, and it buys a much cheaper structure.
3. **Ordering.** `PrQueue` explicitly documents cross-thread push ordering as best-effort, with
   possible minor reordering among near-simultaneous pushes. Best-effort is fine for the
   runqueue, where order is irrelevant. It is a poor fit for the structure whose entire purpose
   is in-order delivery.

Use an intrusive Treiber stack with consumer-side reversal. `NetMessage` gains a `next` pointer;
the flow holds one atomic pointer:

```
push (any thread):     n->next = load(inbox); while (!CAS(&inbox, n->next, n)) ;
drain (owner only):    n = exchange(&inbox, NULL); reverse n into flow->ready;
```

The flow costs **one pointer plus one pointer**, allocates nothing, needs no GC pass, and after
reversal yields strict FIFO in CAS-success order — a stronger guarantee than `PrQueue` gives.
The consumer holds `ready` privately and only touches `inbox` again when `ready` runs dry, which
also folds neatly into the double-recheck in the requeue protocol (§4).

`PrQueue` stays where it earns its weight: the queue-wide runqueue (genuinely MPMC, exactly one
instance), the `BufPool` freelist, and the per-socket datagram send queue.

### New buffer primitives

These are **public API**, not private helpers. They are ordinary zero-copy operations on
low-level buffer types built for exactly this kind of use, they are no more dangerous than the
existing `bufringWriteZC` / `bufchainReadZC`, and marking them private would only guarantee the
next consumer reinvents them. Public means full `///` docs inside the existing
`@defgroup buffer_bufring` / `buffer_bufchain` groups.

1. **`bufringReserve(ring, minbytes, &ptr, &len)` / `bufringCommit(ring, len)`** —
   `bufringFeed` works for readiness backends (call `recv()` from inside the feed callback), but
   completion backends need the reserve and the commit separated in time: reserve, post the
   `WSARecv` against the reserved pointer, commit when the completion arrives. Safe because
   bufring segments are individually allocated and never move once created — an important
   property to preserve, and one the public docs must now state as a guarantee rather than an
   implementation detail. Cheap to commit to: `BufRing` was written for netqueue and netqueue is
   still its only consumer, so promoting the property costs nothing today and only constrains
   future bufring work — which is the right time to decide it, before there are other callers to
   break.

   The contract to document: the reservation is valid until committed; no other operation may be
   performed on the ring while one is outstanding; committing fewer bytes than reserved is legal
   and expected (a short read); committing zero releases it.

2. **`bufchainGatherIov(chain, iov, maxiov, &count)`** — expose head segment pointers and lengths
   without consuming, so they can be fed to a scatter/gather send. Consume afterwards with the
   existing `bufchainSkip()` once the write completes.

   This needs a vector type, and the platform ones disagree: `struct iovec` is
   `{void* base; size_t len}`, `WSABUF` is `{ULONG len; CHAR* buf}` — different order *and*
   different width. Aliasing one of them would drag `winsock2.h` / `sys/uio.h` into `buffer/`,
   which is the wrong dependency direction for a general-purpose buffer module. Define a neutral
   `BufIov { void* data; size_t len; }` and let the net layer translate into the platform array.
   The translation is a bounded loop (≤16 entries) of two stores each, immediately before a
   syscall; it does not show up in a profile.

3. **`BufPool`** — fixed-size buffer freelist over `PrQueue`. Grows on demand to a configured cap;
   past the cap, allocation fails and the caller decides what to do. IOCP requires owning
   pre-posted receive buffers regardless, so the pool is not optional complexity — it is the thing
   that already had to exist. Public as well; it is generally useful and not net-specific.

```c
void   bufpoolInit(BufPool* pool, size_t bufsz, uint32 initial, uint32 max);
buffer bufpoolGet(BufPool* pool);      // NULL when at cap and none free
void   bufpoolPut(BufPool* pool, buffer* buf);
void   bufpoolDestroy(BufPool* pool);
```

### Pool exhaustion: drop and count

The queue-wide receive `BufPool` is sized at queue creation and **never grows past its cap**. When
it is dry, the datagram path drops the packet and increments a counter; it does not allocate, does
not block, and does not stall ingest.

```
NetQueueConfig:
    size_t recvBufSize;      // per-buffer size; default ~2 KB (one MTU-ish datagram)
    uint32 recvBufInitial;   // preallocated at queue creation
    uint32 recvBufMax;       // hard cap -- the memory ceiling, in buffers

NetQueue:
    atomic:uint64 droppedNoBuf;    // packets dropped for lack of a receive buffer
```

Dropping is the *correct* behavior, not a degradation: UDP is permitted to lose packets, the
protocol already handles it, and a sender that is outrunning the receiver needs to find out. The
alternative — grow without bound — converts a transient overload into memory exhaustion, and does
it fastest under exactly the flood you least want to help along. `recvBufMax × recvBufSize` is the
whole receive-side memory ceiling, and it should be a number an operator can state.

`droppedNoBuf` is the part that must not be skipped. A silent drop is indistinguishable from a
network problem and will cost days of misdirected debugging; a monotonic counter that is nonzero
turns "the network is flaky" into "the pool is too small or a callback is too slow" immediately.
Exposed via a stats accessor, logged at whatever the app's cadence is, and worth an internal
rate-limited warning the first time it moves.

The **stream** path never drops — a lost byte is a corrupted stream. If no buffer is available, the
socket simply does not post its next receive until one is, and TCP flow control does the rest,
which is exactly what it is for. Same pool, different response, because the protocols differ in
what a drop means.

Sizing guidance for the doxygen: the default should cover the in-flight window — roughly
`(outstanding receives per socket × sockets) + (peak packets queued across all flows)`, with
headroom. Under-sizing shows up as `droppedNoBuf`; over-sizing costs only address space.

Outstanding-operation limits: **exactly one outstanding recv and one outstanding send per stream
socket** (multiple would complete out of order and corrupt the stream). Datagram sockets post
**many** outstanding `WSARecvFrom`, which is precisely where IOCP beats readiness on the hot
socket.

## 7. Events and handler resolution

`NetEvent`, `NetEventType`, `NetErrorCode` in `net_shared.h` are nearly complete and stand as
written, with one addition — flow teardown needs an event so the application can release
per-client state:

```c
NET_FlowClosed = 0x40,   ///< Flow closed; release any state hanging off flow->user
```

Delivery is described in §8: it is a terminal event on the flow's own queue, not an inline
callback from whatever thread decided to close it, and it fires on every close cause with the
cause carried as `NetCloseReason`.

### NetHandlers

Handlers are a **plain struct of function pointers**, not a closure chain.

```c
typedef struct NetHandlers {
    void (*connection) (NetEvent* ev);   ///< NET_Connection: established or failed
    void (*incoming)   (NetEvent* ev);   ///< NET_Incoming: new accepted socket
    void (*recv)       (NetEvent* ev);   ///< NET_Recv: data or datagram available
    void (*sendReady)  (NetEvent* ev);   ///< NET_SendReady: drained below low watermark
    void (*flowOpen)   (NetEvent* ev);   ///< NET_FlowOpen: unknown source accepted
    void (*flowClosed) (NetEvent* ev);   ///< NET_FlowClosed: release flow->user state
    void (*error)      (NetEvent* ev);   ///< NET_Error
} NetHandlers;
```

A network queue has exactly one consumer — the code that owns the socket — so the multi-subscriber
machinery a closure chain provides buys nothing here and costs an indirect call plus a chain walk
on the hottest path in the system. Closure chains are the right tool where several independent
parties want to observe events on a shared resource and each act on their own (the DOM-ish event
model they were patterned on); that is a UI shape, not a socket shape.

The struct form also gets designated initializers, which is the real ergonomic win — the whole
handler set is one declaration, missing entries are `NULL` by C's own rules, and it can be `static
const`:

```c
static const NetHandlers clientHandlers = {
    .recv       = onRecv,
    .flowClosed = onFlowClosed,
};
```

`NULL` means "fall through to the next level", which makes partial overrides free: a per-flow
handler set that only fills in `.recv` inherits everything else without any explicit chaining.

Handler lookup, most specific wins, per event type:

```
flow handlers  ->  socket handlers  ->  queue-wide handlers
```

Because the fallthrough is per *field* rather than per *set*, resolution is three pointer loads and
two null tests, with no allocation and no lock. The `ctx` accompanying each level travels with it,
so a handler always receives the context registered alongside it. This also lets the queue-wide set
stay what it is good for — logging and error handling — while sockets and flows override only the
events they actually care about.

This replaces the current `hashtable:uint32:sarray callbacks` on `NetQueue`, which was a hash
lookup and an array walk per event to express something a struct offset expresses for free.

The callback signature gains the flow:

```c
typedef void (*NetEventCB)(NetEvent* event);   // event->flow added to the struct
```

"Callbacks could add to a secondary list" from the original sketch: a callback that wants
deferred work pushes onto its flow's queue, which preserves the flow's ordering guarantee
automatically. No separate mechanism is needed. This is also the escape hatch for work that must
block — hand it to a `TaskQueue` and feed the result back through the flow, keeping the callback
itself non-blocking (§12).

## 8. Flow lifecycle

### Creation and reclamation

**There is no idle timeout.** Flows live indefinitely while the queue is under `maxflows`;
reclamation happens *only* under cap pressure.

```
first packet from unknown source
  ├─ nflows < maxflows  -> create flow, dispatch normally
  └─ at cap             -> reclaim up to reclaimBatch least-recently-active flows
                           ├─ reclaimed something -> create flow, dispatch
                           └─ nothing reclaimable -> handlers.flowOpen(packet)  [no allocation]
                                app validates handshake / crypto negotiation
                                app calls netqueuePromoteFlow() to admit it
```

This is a deliberate change from the first draft, which reaped on an idle timer. Memory is
bounded by `maxflows × sizeof(NetFlow)` whether or not idle flows are reaped, so timed reaping
buys NetQueue nothing. What is actually worth reclaiming is the *application's* per-client state
hanging off `flow->user` — and the application has heartbeats and protocol-level liveness
knowledge that NetQueue does not. Deciding "this client is gone" is app policy; NetQueue only
guarantees it will not exceed its cap.

The practical payoff: **under a debugger you are always below the cap, so nothing is ever
reclaimed and stalls are harmless by construction.** Neither a breakpoint on the server nor a
single-stepping client causes flow loss, with no stall-detection machinery needed. Existing
`notimeout` / `snotimeout` debug commands map to `noReclaim`, which additionally guarantees no
reclamation even under cap pressure — at the cap with `noReclaim` set, new clients are refused
via `handlers.flowOpen` rather than evicting the one being debugged. Failing loudly is the correct
behavior there.

The cap plus the unknown-source handler is what makes a public UDP port safe: a spoofed-source
flood cannot force unbounded allocation, because past the cap NetQueue stops allocating and
hands raw packets to app code that already has negotiation and encryption-setup logic.

**LRU must not touch the hot path.** Do *not* use an intrusive LRU list — moving a flow to the
front on every packet would put a lock or a contended CAS on the single hottest path in the
system. Instead the hot path does one relaxed atomic store of `lastActive`, and the reclaim path
(rare, only at the cap) scans the flow table for the oldest entries. Approximate LRU is entirely
sufficient here, and a scan costing microseconds at a cap hit is irrelevant next to a per-packet
synchronization cost.

Reclamation runs inline on the ingest path at the moment a cap hit occurs. No timer thread, and
no dependence on a worker going idle.

### Teardown ordering and resurrection

A reclaimed flow must not have its teardown race with packets the application has already been
handed. So teardown is delivered as a **terminal event on the flow's own queue**:

```
reclaim:   CAS(flow->dying, 0, 1)          // fails -> someone else got it
           flow->pkts.push(TERMINAL)
           (flow is NOT freed here)

worker:    drain: pkt, pkt, pkt, ..., TERMINAL
                                     └─> NET_FlowClosed handler runs
                                         app destroys its client state
           then: remove from flow table, release buffers, free flow
```

The teardown handler therefore runs on a worker thread, serialized against that flow like any
other event, and strictly after every packet already queued for it. The app's `user` data
destructor runs there.

**Resurrection.** If a packet arrives for a flow that is marked `dying` but whose terminal event
has not yet been delivered, the flow resurrects — `CAS(dying, 1, 0)` succeeds, the terminal event
is cancelled, and the flow continues as if nothing happened. This avoids the pathology where a
client that never actually left gets torn down and immediately recreated, forcing the app to
rebuild session state and redo crypto negotiation for no reason.

Once the terminal event *has* been delivered, the flow is gone and a subsequent packet from that
peer is a genuinely new unknown source.

### Every close is the same close

`NET_FlowClosed` fires on **every** path that ends a flow, not only cap-pressure reclaim:

| Cause | `NetCloseReason` | Resurrectable |
|---|---|---|
| Cap-pressure reclaim | `NCR_Reclaimed` | yes |
| App called `netflowClose()` | `NCR_AppClosed` | **no** |
| Stream peer closed / reset | `NCR_PeerClosed`, `NCR_Error` | no |
| Owning socket closed with flows live | `NCR_SocketClosed` | no |
| Queue shutdown | `NCR_Shutdown` | no |

One teardown path the app can rely on absolutely: if it ever saw the flow, it will see exactly one
`NET_FlowClosed` for it, on a worker, ordered after every packet already queued. The alternative —
"reclaim posts an event, but a voluntary close just frees it" — means every app has to write
cleanup twice and get the second copy right at every call site. Consistency is worth more here than
signal purity.

Because the event is no longer purely a pressure signal, the cause is carried on it as
`NetCloseReason` rather than implied. An app that genuinely wants to distinguish "I dropped this
client" from "the server dropped it under load" reads the field; an app that just wants to free its
session state ignores it. Most read only the field, and even then usually just to log.

**Resurrection is reclaim-only.** `NCR_Reclaimed` is speculative — the queue guessed this peer was
gone and can be proven wrong by an arriving packet. Every other reason is a decision, and an
arriving packet does not un-decide it. Concretely, one flag: the terminal event carries whether
`dying` may be cleared, and only reclaim sets it.

**All flows close before their socket does.** Socket close walks its flow table and marks every
flow dying with `NCR_SocketClosed`; the socket's release completes when the last terminal event has
been delivered. This is the only ordering rule the app can depend on across the two objects, and it
is the one that matters: `flow->user` teardown never runs after the socket it belonged to is gone.

The `dying` flag, the claim flag, and the terminal event interact with the requeue race in §4;
this is the second thing worth a dedicated stress test in phase 2.

## 9. Backpressure

```
netsocketSend() with queued bytes > sendHigh   -> returns false, nothing queued
queued bytes drain below sendLow               -> NET_SendReady fires on the flow
```

`NSO_Immediate` retains its documented meaning: never queue, return what was actually sent.
`canSend` stays as the cheap atomic hint that a send will not queue.

Watermarks are per socket, defaulting to something like 256 KB high / 64 KB low, settable at
creation.

## 10. Connect and name resolution

```
netsocketConnect(host, port)
  -> NS_Resolving; submit getaddrinfo to the resolver queue
  -> results populate the existing connQueue: sarray:NetAddr
  -> NS_Connecting; try addresses in order, falling through on failure
  -> NET_Connection event with NetConnectionState + NetErrorCode
```

This is what `connQueue` and `NS_Resolving` were already there for. Resolution never runs on an
ingest or dispatch thread. `net` takes a dependency on `taskqueue`; this is settled.

### The resolver queue

Resolution goes to a **dedicated, bounded, lazily-created queue owned by `net`**, not directly to
`sysq`.

`sysq` is the right *pattern* — a library-managed background queue, lazy-initialized, torn down at
`atexit` — but it is a single shared instance with a `tqPresetBalanced` pool, and `getaddrinfo` is
the worst-behaved blocking call in the standard library: an unreachable DNS server blocks it for
the full resolver timeout, typically 5–30 seconds, with no way to cancel it. A server that fires a
few hundred connects at a dead resolver would occupy every `sysq` worker for half a minute and
starve every other CX subsystem that uses it. That is exactly the failure mode this whole design
exists to prevent, so it should not be reintroduced in the resolver.

```c
// net_resolve.c
static TaskQueue* netResolverQ;      // lazyInit, atexit teardown -- mirrors sysq.c

static void netResolverInitFunc(void* unused)
{
    TaskQueueConfig conf;
    tqPresetMinimal(&conf);
    conf.pool.wMax = 4;              // hard concurrency cap on in-flight getaddrinfo
    netResolverQ = tqCreate(_S"CX Net Resolver", &conf);
    ...
}
```

The cap is the point: at most N lookups in flight, the rest queued, and a dead resolver costs
latency on connects rather than a stalled process. The queue is created on first
`netsocketConnect()` with a hostname, so programs that only ever connect to addresses never spawn
a thread. `TQ_Monitor` is enabled on it — a lookup exceeding its threshold is exactly the
condition worth logging.

Excessive queue depth is a real signal too: if pending resolutions exceed some multiple of `wMax`,
fail new resolves immediately with `NETERR_...` rather than queueing them behind a stall no one
will notice. Unbounded queueing in front of a bounded pool just moves the stall.

## 11. Backends

Build order is **select first** — it works everywhere, including Wine, and serves as the
reference implementation to validate the shared core against before adding a completion backend.

### select (`NetQueueSelect`, Windows + Unix)

Readiness emulating completion. One ingest thread runs the `select()` loop; on readable it does
the `recv`/`recvfrom` itself into pooled or ring storage and feeds the runqueue. Correct
everywhere, `FD_SETSIZE`-limited, and fine for the single-socket and low-connection-count cases.

**One shared class, not `NetQueueWinSelect` + a Unix twin.** The initial sketch split them, and
the reasons were real — `SOCKET` vs `int`, `WSAStartup`, `WSAGetLastError` vs `errno`,
`ioctlsocket` vs `fcntl` — but every one of those is a *syscall-shim* difference, and none of
them belong to the queue. Sorting them by what they actually touch:

| Difference | Where it belongs |
|---|---|
| `SOCKET` vs `int`, invalid-handle sentinel | The socket class, which is already per-platform (`NetSocketWin`, `NetSocketPosix`) |
| `WSAStartup` / `WSACleanup` | One-time platform init. Not a queue concern at all. |
| `WSAGetLastError` vs `errno`, `WSAEWOULDBLOCK` vs `EAGAIN` | A `netLastError() → NetErrorCode` shim, needed by *every* backend, not just select |
| `ioctlsocket(FIONBIO)` vs `fcntl(O_NONBLOCK)` | Socket creation, again already per-platform |
| `fd_set` representation and iteration | The only genuinely select-specific difference — see below |
| Waking a blocked `select()` | Genuinely different mechanism, and the one worth isolating |

What is left over is small enough to hide behind a primitive, and what is *shared* is the part
that matters: the readiness→completion emulation. Reserving ring space, issuing the read,
gathering iovecs for the write, synthesizing `NET_*` events, flow demux, backpressure. That is
the bulk of the backend and it is where the bugs will be. Duplicating it across two files means
fixing every one of those bugs twice, in code that is 90% identical — which is the decisive
argument regardless of how the header details land.

So: `NetQueueSelect` lives in `cx/net/`, is fully platform-independent, and holds an opaque
`NetSelectSet*` implemented per platform:

```c
NetSelectSet* nselCreate(void);
void nselDestroy(NetSelectSet** set);
void nselClear(NetSelectSet* set);
void nselAdd(NetSelectSet* set, NetSockHandle h, bool read, bool write);
int  nselWait(NetSelectSet* set, int64 timeout);           // returns ready count
bool nselNext(NetSelectSet* set, NetSockHandle* h, bool* r, bool* w);   // iterate ready
void nselWake(NetSelectSet* set);                          // interrupt a blocked wait
```

Roughly 80 lines per platform, and no winsock header escapes into `cx/net/`.

Two details drove that API shape:

- **Iteration, not per-socket query.** The obvious API is `nselReady(set, h)`, but Windows
  `fd_set` is an *array* of handles while Unix `fd_set` is a bitmask indexed by fd. Querying one
  handle at a time is O(n) per socket on Windows, so the natural loop becomes O(n²). Iterating
  ready handles instead is O(n) total on both: Windows walks `fd_array` directly, Unix scans the
  bitmask once.
- **`nselWake` is the one thing with no portable shape.** Unix uses a self-pipe or `eventfd`;
  Windows `select` accepts *only* sockets, so it needs a self-connected loopback UDP pair. This
  is exactly the kind of difference that justifies a platform primitive, and exactly the kind
  that would have been quietly duplicated in two near-identical queue classes.

**`FD_SETSIZE` is a hard cap and it is not the same cap.** On Windows it only sizes the `fd_set`
struct, so defining it higher before `winsock2.h` genuinely raises the limit. On Unix `fd_set` is
a bitmask the kernel indexes by fd number — an fd ≥ `FD_SETSIZE` (typically 1024) writes out of
bounds, and raising the define does not fix it. The backend must therefore *enforce* a socket cap
and fail `addSocket` cleanly past it rather than corrupting memory. This asymmetry is the
strongest reason select stays a fallback and never becomes the default on either platform.

The existing skeleton at `cx/platform/win/win_net_queue_select.c` becomes the `NetSelectSet`
implementation; the class it currently declares goes away.

### IOCP (`NetQueueWinIOCP`)

The primary target. Native completion. Many outstanding `WSARecvFrom` on the hot UDP socket so
the kernel hands each worker a distinct packet with no thundering herd; one outstanding
`WSARecv` per stream socket. Workers block in `GetQueuedCompletionStatus` and do ingest inline,
then dispatch — the ingest/dispatch split is logical here rather than a thread boundary.

Wine detection selects the select backend instead; `NQ_SelectOnly` forces it.

### epoll (`NetQueueEpoll`, Linux)

Readiness emulating completion, but with `recvmmsg` to drain the hot UDP socket in batches from
one ingest thread, then fan out by flow key. This is where the ingest/dispatch split earns its
keep: the un-parallelizable `recvmmsg` loop does nothing but fill buffers and push.

`SO_REUSEPORT` is a possible future optimization (kernel-side load balancing across several
sockets on one port), but it conflicts with the explicit goal of a single socket and is
Linux-only. Not in scope.

### kqueue (`NetQueueKqueue`, BSD/macOS)

Structurally the epoll backend with a different wait call and event struct. No `recvmmsg`
equivalent, so it drains with a `recvfrom` loop.

## 12. Consumer ergonomics and the async contract

Two kinds of bloat are worth separating. **Core bloat** is more concepts in the dispatch engine;
flows add none of it for TCP, since a stream socket's flow is created during socket init and the
application never constructs one. **Consumer bloat** is more steps to do a simple thing, and that
is the real risk — a single intraserver TCP connection should not feel like standing up a server.

Governing rule for everything in this section: **helpers live strictly on top of the public API,
introduce no new core concepts, and cost nothing when unused.** Anything that cannot meet that
bar is not a helper, it is a second design.

### Combined setup calls

`netqueueSocket()` is a factory that deliberately does *not* register the socket with the queue,
which makes `netqueueAddSocket()` an easy step to forget in the simple path. Rather than change
that (it is correct for callers who need the socket before it joins a queue), add two composed
entry points:

```c
NetSocket* netqueueConnect(NetQueue* q, strref host, uint16 port,
                           const NetHandlers* handlers, void* ctx);
NetSocket* netqueueListen (NetQueue* q, NetAddr* addr, int backlog,
                           const NetHandlers* handlers, void* ctx);
```

Each does factory → `addSocket` → handler registration → `connect`/`bind`+`listen`, and returns
the socket. Roughly twenty lines apiece, no new types, and `netqueueSocket()` stays for the
uncomposed case.

```c
// before
NetQueueConfig conf; netqueuePresetClient(&conf);
NetQueue* q  = netqueueCreate(&conf);
NetSocket* s = netqueueSocket(q, NST_Stream);
netqueueAddSocket(q, s);
/* ... register handlers ... */
netsocketConnect(s, host, port);

// after
NetQueueConfig conf; netqueuePresetClient(&conf);
NetQueue* q  = netqueueCreate(&conf);
NetSocket* s = netqueueConnect(q, host, port, &handlers, ctx);
```

A flow appears nowhere in either version. For stream sockets it is created implicitly and shows
up only as a field on the event, which single-connection consumers can ignore entirely.

Explicitly **not** adopted: a lazy process-wide default queue (hidden threads plus a genuinely
hard shutdown-ordering problem at process exit) and a `NetConn` wrapper object (a new public type
duplicating part of the queue/socket surface).

### Where application state lives

`socket->user` is **removed**; `flow->user` is the single canonical place for per-peer state.
This keeps one spelling across both protocols, so generic code that handles stream and datagram
sockets alike works unchanged.

```
per-peer / per-connection state  ->  flow->user
per-socket / per-listener state  ->  the ctx captured at handler registration
```

The `ctx` pointer covers what `socket->user` used to, without a second variant-array allocation
on every socket, and without two places to look for state.

### The async contract

There is deliberately **no blocking or synchronous API** — no `recvSync`, no `sendSync`, no
"just wait for this one connection" helper. This is a hard constraint, not an omission.

The motivating failure is concrete: the server applications that will consume this already suffer
from synchronous sockets used for convenience in places they should not be, and because several
of those servers are single-threaded, one unresponsive remote host stalls the entire process. A
blocking convenience API would reintroduce exactly that failure through a nicer-looking door.

Consumers that want synchronous-looking control flow use a polled-mode queue, which stays inside
the one execution model:

```c
NetQueueConfig conf; netqueuePresetClient(&conf);   // nthreads = 0 -> polled
NetQueue* q  = netqueueCreate(&conf);
NetSocket* s = netqueueConnect(q, host, port, &handlers, ctx);
while (!done)
    netqueueTick(q, 100);                   // bounded wait, never open-ended
```

Callbacks run on worker threads and must not block on anything nontrivial — no file I/O, no lock
held across a long operation, no synchronous network call to a third party. Work that must block
belongs on a `TaskQueue`, with the result fed back through the flow (see §7 on deferred work
preserving flow ordering).

To keep that contract from eroding silently, debug builds should time callback execution and warn
when one exceeds a threshold, naming the socket and event type. `TaskQueue` already establishes
the pattern with `TQ_Monitor` for stuck tasks. This is debug-only and compiled out of release, so
it costs nothing in production — but it turns "callbacks should try not to block," which was
advice in the original sketch, into something that actually gets caught.

## 13. Shutdown and lifetime

`objRelease` refcounting throughout.

### Ownership graph

```
NetQueue --strong--> NetSocket        (sockets hashtable)
NetSocket -weak----> NetQueue         (breaks the cycle)

NetSocket --strong-> NetFlow          (flow table; stream: a single NetFlow*)
NetFlow  --weak----> NetSocket        (breaks the cycle)

NetQueue runqueue --strong--> NetFlow (only while queued)
```

Flow ownership was not previously stated. It is: **the socket owns its flows.** Datagram sockets
own them through the flow hashtable, stream sockets through a single `NetFlow*` created at socket
init. The two cycle-breaks are the same shape at both levels, which is worth keeping — one
pattern to reason about, not two.

The runqueue holds its **own strong reference** for as long as a flow is queued. This is not
redundant with the flow table: reclaim removes a flow from the table while it may still be
sitting on the runqueue, and without the runqueue's reference a worker would pop freed memory.
It is also what makes the terminal-event teardown in §8 work — a reclaimed flow stays alive until
its queued packets and its `NET_FlowClosed` have been delivered, then the runqueue's release is
the last one.

### Why both weak arms point the way they do

Weak references are cheaper to *hold* and more expensive to *traverse*, so the arm direction only
pays off if the weak arm is the cold one. Naively the graph above looks wrong: `flow → socket` is
needed on every dispatch, which is the hottest path in the system. Reversing it does not work —
making `flow → socket` strong and `socket → flow` weak leaves nothing owning an idle flow, and
idle flows must persist because they carry the application's `user` state between packets.

The resolution is that neither weak arm is actually traversed per packet:

- **`flow → socket` is resolved once per dispatch batch, not once per packet.** A worker claims a
  flow, resolves the socket to a strong reference, drains every queued packet under that claim,
  then releases. One `objGetWeak` amortized across the whole batch — and the batch is exactly the
  unit the claim protocol already defines, so this falls out of the existing structure rather
  than being bolted on. It also removes a class of bug: the socket cannot be torn down mid-batch.
- **`socket → queue` is never traversed on the hot path at all.** A worker thread belongs to the
  queue and already has the queue pointer in its own context; it has no reason to ask the socket.
  That arm exists only for app-facing calls such as `netsocketSend()` invoked from outside a
  worker, where the cost is irrelevant.

So the graph is efficient as drawn, but *only* under those two rules. Both are implementation
constraints rather than consequences of the type declarations, so both need to be stated at the
worker loop, where the temptation to write `objGetWeak(flow->socket)` inside the per-packet loop
will be strongest.

`netqueueShutdown(timeout)` must, in order: stop accepting new work; cancel outstanding I/O
(`CancelIoEx` on Windows, close fds elsewhere); join workers within the timeout; drain the
runqueue; release all flows and sockets; return the pool. The current implementation removes
sockets but ignores `timeout` entirely and has no worker teardown — that is a stub, not a bug.

Cancelling I/O that references pooled buffers is the fiddly part on IOCP: buffers must not
return to the pool until their completion (successful or cancelled) has been observed.

## 14. Defects in existing code

Found while reviewing; all worth fixing regardless of which design lands.

1. **`cx/net/socket.c:68-101`, `NetSocket_recvMsgs` — infinite loop.** `done` is only assigned
   when a message was actually retrieved. Once the callback returns `true` (`done = false`), a
   subsequent iteration that finds the buffer empty leaves `done` unchanged and the `do/while`
   spins forever. Needs `done = true` at the top of each iteration, or an explicit empty-buffer
   break.
2. **`cx/platform/win/win_net.c:50` — out-of-bounds read.** `in6->sin6_addr.u.Byte[i] =
   addr->ipv6[16 - i]` reads `ipv6[16]` when `i == 0`. Should be `[15 - i]`. Separately, worth
   confirming the byte reversal is intended at all: `NetAddr` is documented as host byte order,
   but IPv6 addresses have no meaningful host order — they are a byte string — so this reversal
   may be wrong in principle as well as off by one.
3. **`cx/platform/win/win_net_socket.c:22` — wrong initial state.** `_create` stores `NS_Closed`
   on a freshly created socket, which makes `bind`/`listen` read as inverted (`!= NS_Closed`
   rejects). `NS_Init` exists and is clearly what was meant; the state checks then need updating
   to match.
4. **`cx/net/queue.c:57` — `NetQueue_shutdown` ignores `timeout`** and does no worker teardown.
   Expected, given workers do not exist yet.

## 15. Implementation plan

**Phase 1 — buffer primitives.** `BufPool`; `bufringReserve`/`bufringCommit`; `BufIov` and
`bufchainGatherIov`. All public, so all need doxygen in their existing groups and an entry in
`docs/docs.md`. Unit tests for each, including the reserve-across-time case. No net code touched.

**Phase 2 — core, backend-independent.** `NetFlow` class; flow table on `NetSocket`; runqueue
and claim protocol; handler resolution chain; flow lifecycle (cap, unknown-source, LRU reclaim,
terminal-event teardown, resurrection). Testable with a synthetic backend that injects fake
completions — do this, it is the only way to deterministically stress the requeue race and the
reclaim/resurrect race.

**Phase 3 — select backend.** The shared `NetQueueSelect` plus a `NetSelectSet` per platform, and
the `netLastError()` shim (needed by every later backend, so it is worth getting right here). Fix
the four defects in §14. First point at which real sockets move real bytes; validates the core.

**Phase 4 — polled mode and worker threads.** `tick()` running ingest+dispatch inline; then the
owned worker pool over the same code. Shutdown and I/O cancellation.

**Phase 5 — send path and backpressure.** Scatter/gather send, watermarks, `NET_SendReady`,
`canSend`, datagram send queue.

**Phase 6 — IOCP.** The performance target. Benchmark the single-UDP-socket case against the
select backend here; that number is the justification for the whole architecture.

**Phase 7 — epoll**, with `recvmmsg` batching. **Phase 8 — kqueue.**

**Phase 9 — connect and DNS** on the TaskQueue, happy-eyeballs fallback through `connQueue`.

**Phase 10 — ergonomics.** `netqueueConnect()` / `netqueueListen()` helpers and the debug-build
callback-duration warning. Deliberately last: the helpers are thin composition over the public
API, so they should be written once that API has stopped moving.

## 16. Open questions

- Reclaim scans the flow table for the oldest entries at a cap hit. With a large `maxflows` and a
  workload that sits pinned at the cap, that scan repeats often enough to matter; a coarse
  bucketed-by-age index would bound it if profiling shows it.
- Default pool sizes for `netqueuePresetClient` / `netqueuePresetServer` (§5). The *policy* is
  settled (§6); the numbers want a flood test, and `droppedNoBuf` is the instrument that produces
  them.
- Resolver-queue depth limit: what multiple of `wMax` should start failing new resolves outright
  (§10)? Wants a real number from a stalled-DNS test, not a guess.

Resolved since the first draft, recorded here so the reasoning is not relitigated: DNS on a
dedicated bounded queue (§10); `NET_FlowClosed` on every close cause (§8); `NetFlow` as a full
object (§5); `NetHandlers` as a struct of function pointers (§7); drop-and-count on pool exhaustion
with a creation-time cap (§6); the socket buffer union (§6);
the flow inbox as an intrusive MPSC stack (§6); one shared `NetQueueSelect` (§11).

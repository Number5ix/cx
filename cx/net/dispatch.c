// ---------------------------------------------------------------------------------------------
// Dispatch
//
// The runqueue and the event delivery it feeds -- the path a message takes from the moment some
// thread hands it to a flow to the moment it becomes a callback. Three things live here and only
// these three:
//
//   netqueue_submit    the runqueue's producer: push onto a flow's inbox, enqueue the flow if it
//                      was idle, wake a worker
//   netqueue_dispatch  the consumer: pop a flow, claim it, drain everything pending for it
//   netqueue_deliver   resolve a handler flow -> socket -> queue and invoke it
//
// _submit and _dispatch are two halves of one claim protocol (flow->queued, flow->claimed); each
// one's "re-check after the store" reasoning only makes sense beside the other, so they stay
// together. Flow admission and the flow table live in flow.c, the GC pass in queue.c, and the
// connect timeout sweep in connect.c.
// ---------------------------------------------------------------------------------------------

#include "net_private.h"
#include "net.h"   // NetLogChannel
#include <cx/container.h>
#include <cx/log.h>
#include <cx/thread.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

// every log call in this file belongs to the network channel
#undef LOG_CHANNEL
#define LOG_CHANNEL NetLogChannel

// ---------------------------------------------------------------------------------------------
// Handler resolution
// ---------------------------------------------------------------------------------------------

// Fetch one field out of a handler set. Kept as a switch rather than an offset table so that a
// new event type is a compile error here rather than a silent NULL at runtime.
static NetEventCB pickHandler(const NetHandlers* h, NetEventType ev)
{
    if (!h)
        return NULL;

    switch (ev) {
    case NET_Connection:
        return h->connection;
    case NET_FilterNotify:
        return h->filterNotify;
    case NET_Accepted:
        return h->accepted;
    case NET_DataReceived:
        return h->recv;
    case NET_SendReady:
        return h->sendReady;
    case NET_FlowOpen:
        return h->flowOpen;
    case NET_FlowRefused:
        return h->flowRefused;
    case NET_FlowClosed:
        return h->flowClosed;
    case NET_Error:
        return h->error;
    }

    return NULL;
}

// Resolve a handler for an event type, walking flow -> socket -> queue per field. Returns NULL if
// no level supplies one; `ctx` receives the context registered alongside whichever level won.
// Static so that every callback invocation is forced through netqueue_deliver and its timing.
_Ret_maybenull_ static NetEventCB _netResolveHandler(_In_opt_ NetFlow* flow,
                                                     _In_opt_ NetSocket* sock, _In_opt_ NetQueue* q,
                                                     NetEventType ev, _Out_ void** ctx)
{
    NetEventCB cb;

    // Most specific wins, resolved per field rather than per set, so a flow that overrides only
    // .recv inherits everything else with no explicit chaining. The ctx travels with the level
    // the handler was found at.
    if (flow && (cb = pickHandler(flow->handlers, ev))) {
        *ctx = flow->handlerCtx;
        return cb;
    }

    if (sock && (cb = pickHandler(sock->handlers, ev))) {
        *ctx = sock->handlerCtx;
        return cb;
    }

    if (q && (cb = pickHandler(&q->handlers, ev))) {
        *ctx = q->handlerCtx;
        return cb;
    }

    *ctx = NULL;
    return NULL;
}

#if DEBUG_LEVEL >= 1
// Callbacks run on the queue's workers (or inline in tick()), so one that blocks starves every
// flow behind it -- the async contract says work that must block belongs on a TaskQueue, with the
// result fed back through the flow. Dev builds time each callback and warn past this threshold so
// a violation is caught at the handler that commits it, rather than surfacing later as mysterious
// latency on unrelated flows. Compiled out of release builds entirely.
#define NET_CB_WARN_THRESHOLD timeMS(100)

STR_CONST(kNetFmtSlowCB, "Slow ${string} handler on socket ${0uint(4,hex)}: ${int} ms");

// Handler-field names from NetHandlers, so the warning points at the callback to go look at.
static strref netEventName(NetEventType ev)
{
    switch (ev) {
    case NET_Connection:
        return _SL("connection");
    case NET_FilterNotify:
        return _SL("filterNotify");
    case NET_Accepted:
        return _SL("accepted");
    case NET_DataReceived:
        return _SL("recv");
    case NET_SendReady:
        return _SL("sendReady");
    case NET_FlowOpen:
        return _SL("flowOpen");
    case NET_FlowRefused:
        return _SL("flowRefused");
    case NET_FlowClosed:
        return _SL("flowClosed");
    case NET_Error:
        return _SL("error");
    }

    return _SL("unknown");
}

// Compact identity for a socket in log output, same scheme the TaskQueue monitor uses for tasks.
static uint16 ptrHash(void* ptr)
{
    uint16 ret = 0;
    uint8* p   = (uint8*)&ptr;
    for (int i = 0; i < sizeof(void*); i += 2, p += 2) {
        ret ^= ((uint16)p[0] << 8) | p[1];
    }
    return ret;
}
#endif

_Use_decl_annotations_
void NetQueue__deliver(NetQueue* self, NetSocket* sock, NetFlow* flow, NetEvent* ev)
{
    void* ctx     = NULL;
    NetEventCB cb = _netResolveHandler(flow, sock, self, ev->event, &ctx);
    if (!cb)
        return;

    ev->queue  = self;
    ev->socket = sock;
    ev->flow   = flow;
    ev->ctx    = ctx;

#if DEBUG_LEVEL >= 1
    int64 cbStart = clockTimer();
    cb(ev);
    int64 took = clockTimer() - cbStart;
    if (took >= NET_CB_WARN_THRESHOLD) {
        logFmt(DevWarn,
               kNetFmtSlowCB,
               stvar(strref, netEventName(ev->event)),
               stvar(uint16, ptrHash(sock)),
               stvar(int64, timeToMsec(took)));
    }
#else
    cb(ev);
#endif
}

// ---------------------------------------------------------------------------------------------
// Ingest
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void NetQueue__submit(NetQueue* self, NetFlow* flow, NetMessage* msg)
{
    if (!self) {
        netpoolFreeMsg(flow->pool, &msg);
        return;
    }

    if (netflow_push(flow, msg)) {
        // The runqueue holds its own strong reference for as long as the flow is queued. This is
        // not redundant with the socket's flow table: reclaim removes a flow from the table
        // while it may still be sitting here, and without this reference a worker would pop
        // freed memory.
        objAcquire(flow);
        if (!prqPush(&self->runq, flow)) {
            // Runqueue is full and could not grow. Undo the enqueue claim so the next push
            // retries rather than leaving the flow permanently marked as queued.
            atomicStore(uint32, &flow->queued, 0, Release);
            objRelease(&flow);
        } else if (saSize(self->workers) > 0) {
            // Wake a dispatch worker. In polled mode there is no worker pool -- the caller drives
            // dispatch through tick() -- so nothing waits on the semaphore and the post is skipped.
            semaInc(&self->runqSema, 1);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------------------------

// Run every message currently pending for a flow we hold the claim on. Returns true if the flow's
// terminal event was delivered, meaning the flow is finished and must leave the table.
static bool drainFlow(NetQueue* q, NetSocket* sock, NetFlow* flow)
{
    NetMessage* msg;
    bool terminated = false;

    while ((msg = netflow_pop(flow))) {
        if (msg->kind == NMSG_Terminal) {
            // A terminal event whose flow has since resurrected is cancelled rather than
            // delivered -- the peer turned out not to be gone after all, so tearing down the
            // application's session state would be exactly the pathology resurrection exists to
            // avoid. Only cap-pressure reclaim ever sets this up to be undone.
            if (atomicLoad(uint32, &flow->dying, Acquire) == 0) {
                netpoolFreeMsg(q->pool, &msg);
                continue;
            }

            // Let the flow's filters emit their orderly-close records (a TLS close_notify) while
            // the wire is still usable, before the terminal event runs and the chain is freed. The
            // priming pass is what actually carries whatever shutdown() produced to the wire.
            if (sock && saSize(flow->filters) > 0) {
                netflow_filterShutdown(flow);
                netflow_primeFilters(flow, q, sock);
            }

            NetEvent ev     = { .event = NET_FlowClosed };
            ev.closed.reason = (NetCloseReason)msg->reason;
            netqueue_deliver(q, sock, flow, &ev);

            netpoolFreeMsg(q->pool, &msg);
            terminated = true;

            // Nothing queued behind a terminal event can be delivered: the application has
            // already released its state for this flow.
            break;
        }

        if (msg->kind == NMSG_FlowOpen) {
            // The flow was just created (auto-admitted on ingest, or by netqueuePromoteFlow).
            // Queued by netqueue_admitFlow before anything else could land in the inbox, so it
            // is always delivered ahead of the flow's first NET_DataReceived -- the handler can
            // set up flow->user state that the recv handler then relies on. Carries no payload;
            // the flow itself is the event.
            NetEvent ev = { .event = NET_FlowOpen };
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        if (msg->kind == NMSG_FilterNotify) {
            // A filter raised this from a send, on whatever thread the application called from.
            // Events only ever run on a worker, so it has to route through the packet queue.
            NetEvent ev      = { .event = NET_FilterNotify };
            ev.filter.notify = (NetFilterNotify)msg->bytes;
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        if (msg->kind == NMSG_SendReady) {
            // Delivered through the flow like a packet so it lands in order on a worker, rather
            // than inline on whatever thread flushed the send buffer. Carries no payload.
            NetEvent ev = { .event = NET_SendReady };
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        if (msg->kind == NMSG_Connect) {
            // A connect attempt resolved. It is delivered through the flow like everything else so
            // the callback runs on a worker, ordered, rather than inline on the resolver or the
            // completion thread. The outcome rides in `bytes` as a NetErrorCode.
            NetEvent ev   = { .event = NET_Connection };
            ev.conn.err   = (NetErrorCode)msg->bytes;
            ev.conn.state = ev.conn.err == NERR_None ? NCS_Connected : NCS_NotConnected;
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);

            // The transport is up, which for a stream flow is the first moment a filter has a wire
            // to write to. Prime the chain so a client-side filter can emit its opening flight; a
            // server (accepted) socket instead starts on the first inbound record, through the
            // decode pass in netflow_filterStreamRecv.
            if (sock && ev.conn.err == NERR_None && saSize(flow->filters) > 0)
                netflow_primeFilters(flow, q, sock);
            continue;
        }

        if (msg->kind == NMSG_Error) {
            // A queued send failed after the call that queued it had already returned success --
            // the async flush path has no return value left to report on, so the error arrives as
            // an event instead. The code rides in `bytes`.
            NetEvent ev  = { .event = NET_Error };
            ev.error.err = (NetErrorCode)msg->bytes;
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        if (msg->kind == NMSG_Accept) {
            // A connection was accepted on this (listening) socket. It is delivered through the
            // listener's flow so the callback runs on a worker, ordered, rather than inline on the
            // ingest or completion thread that pulled it off the backlog. The accepted socket rides
            // in `asock`; the handler acquires its own reference to keep it (or lets NQ_AutoAccept
            // have already added it to the queue), and retiring the message releases its reference.
            NetEvent ev          = { .event = NET_Accepted };
            ev.accept.newSocket  = msg->asock;
            netqueue_deliver(q, sock, flow, &ev);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        // Filtered stream data: the decode path reads the raw wire bytes out of the receive ring,
        // sends anything the pass produced back toward the wire, and decides what (if anything)
        // reaches the application. The message itself is just the "bytes arrived" marker.
        if (sock && sock->type == NST_Stream && saSize(flow->filters) > 0) {
            netflow_filterStreamRecv(flow, q, sock);
            netpoolFreeMsg(q->pool, &msg);
            continue;
        }

        // Filtered datagram: the whole packet goes through the flow's decode chain, which may
        // produce any number of application messages from it -- or none at all, when the
        // packet was a handshake record the chain consumed and answered.
        if (sock && sock->type == NST_Datagram && saSize(flow->filters) > 0 && msg->buf) {
            NetMsgQueue decoded = { 0 };
            bool ok             = netflow_filterDatagramRecv(flow, q, sock, msg, &decoded);
            msg                 = NULL;   // consumed by the chain

            // Notifications first: a stage that just finished negotiating raised NFN_Secured on
            // this very pass, and the application expects that ahead of the data it unlocked.
            netflow_filterNotify(flow, q, sock, true);

            NetMessage* out;
            while ((out = netMsgQueuePop(&decoded))) {
                NetEvent ev   = { .event = NET_DataReceived };
                ev.recv.msg   = out;
                ev.recv.bytes = out->buf ? out->buf->len : out->bytes;
                ev.recv.total = ev.recv.bytes;
                netqueue_deliver(q, sock, flow, &ev);
                netpoolFreeMsg(q->pool, &out);
            }

            if (!ok)
                netflow_close(flow, NCR_Error);
            continue;
        }

        // The message travels with the event for datagrams, because there is no per-socket
        // receive buffer for the handler to go read afterwards -- the packet is only reachable
        // here. Stream data went into the socket's ring on the way in, so the message carries no
        // buffer and the handler drains the ring with netsocketRecv() instead.
        NetEvent ev   = { .event = NET_DataReceived };
        ev.recv.msg   = msg;
        ev.recv.bytes = msg->buf ? msg->buf->len : msg->bytes;
        ev.recv.total = (sock && sock->type == NST_Stream) ? sock->bufs.stream.recv.total
                                                           : ev.recv.bytes;
        netqueue_deliver(q, sock, flow, &ev);

        netpoolFreeMsg(q->pool, &msg);
    }

    return terminated;
}

_Use_decl_annotations_
bool NetQueue__dispatch(NetQueue* self)
{
    NetFlow* flow = (NetFlow*)prqPop(&self->runq);
    if (!flow)
        return false;

    // We now own the runqueue's strong reference. Clear `queued` before attempting the claim, so
    // that a message arriving from here on re-enqueues the flow instead of being stranded.
    atomicStore(uint32, &flow->queued, 0, Release);

    uint32 expected = 0;
    if (!atomicCompareExchange(uint32, strong, &flow->claimed, &expected, 1, AcqRel, Relaxed)) {
        // Another worker holds this flow. Dropping the entry is safe: the owner re-checks the
        // inbox after releasing its claim and will requeue anything we would have handled. This
        // is the load-balancing half of the claim protocol -- workers never wait on each other.
        objRelease(&flow);
        return true;
    }

    // Resolve the socket ONCE for the whole batch, never once per packet. This is the rule that
    // makes the weak flow->socket arm affordable, and it also removes a class of bug: the socket
    // cannot be torn down underneath us mid-batch.
    NetSocket* sock = objAcquireFromWeak(NetSocket, flow->socket);
    bool terminated = false;

    for (;;) {
        terminated = drainFlow(self, sock, flow);

        atomicStore(uint32, &flow->claimed, 0, Release);

        if (terminated)
            break;

        // Re-check after releasing, so a message that arrived exactly at the release point is
        // not stranded. Both sides of the handoff re-check after their store; this is the half
        // that belongs to the worker.
        if (!atomicLoad(ptr, &flow->inbox, Acquire))
            break;

        expected = 0;
        if (!atomicCompareExchange(uint32, strong, &flow->claimed, &expected, 1, AcqRel, Relaxed))
            break;   // somebody else picked it up; it is their problem now
    }

    if (terminated && sock)
        netsocket_dropFlow(sock, flow);

    objRelease(&sock);
    objRelease(&flow);
    return true;
}

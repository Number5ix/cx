#include "net_private.h"
#include <cx/container.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

// ---------------------------------------------------------------------------------------------
// Connect path
//
// Backend-independent, mirroring the send path: the state machine (resolve -> try each address in
// order -> deliver NET_Connection) lives here, and only the two backend-specific steps -- begin one
// attempt (connectBegin) and start receiving once connected (connectArm) -- are virtual on the
// queue. Each attempt uses a fresh OS handle of the address's family, because a socket with a
// failed or pending connect cannot be reliably reconnected and a resolved list can mix IPv4/IPv6.
//
// Two threads can finish one attempt concurrently -- the backend completion (writable / ConnectEx
// done / synchronous error) and the timeout sweep. They serialize through connectLock by clearing
// connectDeadline to 0: whoever clears it owns the transition; the loser sees 0 and bails.
// ---------------------------------------------------------------------------------------------

// Forward decls for the mutually recursive helpers below.
static void connectNext(NetSocket* sock, NetErrorCode lastErr);
static bool connectIsLastOfFamily(NetSocket* sock, NetAddrType type);

// Deliver NET_Connection through the flow so the connect callback runs on a worker, ordered against
// anything already pending for the flow -- never inline on the resolver or completion thread. The
// outcome rides in the message's `bytes` slot as a NetErrorCode.
static void connectDeliver(NetQueue* q, NetSocket* sock, NetErrorCode err)
{
    if (!q || !sock->flow)
        return;

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_Connect;
    msg->bytes      = (size_t)err;
    netqueue_submit(q, sock->flow, msg);
}

// The connect has failed on every resolved address (or the socket lost its queue). Deliver the
// failing event, drop the queue's connecting count, and release the connect operation's
// self-reference. The socket returns to NS_Init rather than NS_Closed so the application's
// netsocketClose() still runs the platform close -- setting NS_Closed here would make that a no-op
// and leak the OS handle.
static void connectFinishFail(NetSocket* sock, NetQueue* q, NetErrorCode err)
{
    withMutex (&sock->connectLock)
        sock->connectDeadline = 0;
    atomicStore(uint32, &sock->state, NS_Init, Release);

    connectDeliver(q, sock, err);

    if (q)
        atomicFetchSub(uint32, &q->connecting, 1, AcqRel);

    objRelease(&sock);
}

// An attempt connected. Transition to NS_Connected, deliver the success event on the flow (queued
// before the backend starts receiving, so NET_Connection is ordered ahead of any NET_DataReceived),
// arm the receive side, drop the connecting count, and release the connect self-reference.
static void connectSucceed(NetSocket* sock)
{
    withMutex (&sock->connectLock)
        sock->connectDeadline = 0;
    atomicStore(uint32, &sock->state, NS_Connected, Release);

    NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);

    connectDeliver(q, sock, NERR_None);

    if (q) {
        atomicFetchSub(uint32, &q->connecting, 1, AcqRel);
        netqueueConnectArm(q, sock);
        objRelease(&q);
    }

    objRelease(&sock);
}

// Release the connect operation's resources without changing the socket's state or delivering an
// event -- used when the socket is being closed out from under an in-flight connect, so its held
// self-reference and the queue's connecting count do not strand on a socket that is going away.
static void connectDrop(NetSocket* sock, NetQueue* q)
{
    withMutex (&sock->connectLock)
        sock->connectDeadline = 0;
    if (q)
        atomicFetchSub(uint32, &q->connecting, 1, AcqRel);
    objRelease(&sock);
}

// Try the next resolved address, or finish the connect as failed when the list is exhausted. Runs
// only on the thread that currently owns the advance (the resolver callback for the first attempt,
// or whoever won the connectDeadline claim thereafter), so connectIdx/connQueue/remote need no lock.
// `lastErr` is the failure that ended the previous attempt; it becomes the reported error if this
// exhausts the list, so a run of dead addresses surfaces its real cause (refused, timeout, ...).
static void connectNext(NetSocket* sock, NetErrorCode lastErr)
{
    NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);

    NetAddr addr = { 0 };
    bool have    = q && sock->connectIdx < saSize(sock->connQueue);
    if (have) {
        addr = sock->connQueue.a[sock->connectIdx];
        sock->connectIdx++;
    }

    if (!have) {
        connectFinishFail(sock, q, lastErr != NERR_None ? lastErr : NERR_ConnectionRefused);
        if (q)
            objRelease(&q);
        return;
    }

    sock->remote = addr;

    // A non-final attempt (another address of the same family is still queued behind this one)
    // gets the shorter connectAttemptTimeout, if configured -- so a dead family doesn't burn a
    // full connectTimeout per address before the other family gets a turn. Whichever address is
    // the *last* remaining one of its family always gets the full connectTimeout regardless of
    // where the interleave put it, so a family's one real chance is never cut short by a dead
    // final entry that happens to belong to the other family.
    int64 deadline = q->conf.connectTimeout;
    if (q->conf.connectAttemptTimeout && !connectIsLastOfFamily(sock, addr.type))
        deadline = q->conf.connectAttemptTimeout;

    // Begin a new attempt: bump the generation (so a superseded completion can recognize itself) and
    // arm the deadline before starting, so a completion or the sweep firing the instant the attempt
    // begins finds a valid claim token.
    atomicFetchAdd(uint32, &sock->connectGen, 1, AcqRel);
    withMutex (&sock->connectLock)
        sock->connectDeadline = clockTimer() + deadline;

    // The backend begins one attempt and drives the outcome back through netsocket_connectResult --
    // synchronously for an immediate connect or failure, or later from readiness / a completion / the
    // timeout sweep.
    netqueueConnectBegin(q, sock, &addr);
    objRelease(&q);
}

void NetSocket__connectResult(_In_ NetSocket* self, NetErrorCode err)
{
    // Claim the advance for the current attempt. A nonzero connectDeadline means an attempt is live;
    // clearing it under the lock makes us the sole advancer, so the other of {completion, sweep}
    // sees 0 and returns without touching anything.
    bool claimed = false;
    withMutex (&self->connectLock) {
        if (self->connectDeadline != 0) {
            self->connectDeadline = 0;
            claimed = true;
        }
    }
    if (!claimed)
        return;

    if (err == NERR_None)
        connectSucceed(self);
    else
        connectNext(self, err);
}

bool NetSocket__readinessConnect(_In_ NetSocket* self, _Inout_ NetQueue* q, _In_ NetAddr* addr)
{
    unused_noeval(q);

    // Fresh handle of the address's family. Plain connect() does not need the wildcard bind that
    // ConnectEx requires, so bindAny is false.
    if (!netPlatformResetSocket(self, addr->type, false)) {
        netsocket_connectResult(self, NERR_Unknown);
        return true;
    }

    NetErrorCode err    = NERR_None;
    NetConnectStatus st = netSockConnect(self->handle, addr, &err);
    if (st == NETCONN_Connected)
        netsocket_connectResult(self, NERR_None);
    else if (st == NETCONN_Failed)
        netsocket_connectResult(self, err);
    // NETCONN_InProgress: leave the attempt pending; the select loop finishes it on writable/except.

    return true;
}

// RFC 8305 section 4 address-family interleave: alternate families so a dead or broken one costs
// at most one attempt before the other gets a turn, rather than exhausting every address of one
// family first. `lead` is which family goes first -- normally the first address the resolver
// returned (matching the OS/RFC 6724 destination-selection preference it already applied), or a
// forced choice from NetConnectPref. Each family keeps its own relative order; once one runs out,
// the rest of the other is appended unchanged. A no-op if fewer than two addresses are present.
static void interleaveAddrsByFamily(sa_NetAddr* addrs, NetAddrType lead)
{
    int32 n = saSize(*addrs);
    if (n < 2)
        return;

    sa_NetAddr primary, secondary;
    saInit(&primary, NetAddr, n);
    saInit(&secondary, NetAddr, n);
    for (int32 i = 0; i < n; i++) {
        if (addrs->a[i].type == lead)
            saPush(&primary, NetAddr, addrs->a[i]);
        else
            saPush(&secondary, NetAddr, addrs->a[i]);
    }

    saClear(addrs);
    int32 pi = 0, si = 0;
    while (pi < saSize(primary) || si < saSize(secondary)) {
        if (pi < saSize(primary))
            saPush(addrs, NetAddr, primary.a[pi++]);
        if (si < saSize(secondary))
            saPush(addrs, NetAddr, secondary.a[si++]);
    }

    saDestroy(&primary);
    saDestroy(&secondary);
}

// Apply the resolved socket's NetConnectPref to the raw resolver result: filter to one family for
// *Only, otherwise interleave with the requested (or resolver-order) family leading. Runs before
// the result is copied into connQueue, so the state machine always walks an already-ordered list.
static void applyConnectPref(sa_NetAddr* addrs, NetConnectPref pref)
{
    if (pref == NCP_V4Only || pref == NCP_V6Only) {
        NetAddrType keep = pref == NCP_V4Only ? NA_IPv4 : NA_IPv6;
        int32 out        = 0;
        for (int32 i = 0; i < saSize(*addrs); i++) {
            if (addrs->a[i].type == keep)
                addrs->a[out++] = addrs->a[i];
        }
        saSetSize(addrs, out);
        return;
    }

    if (saSize(*addrs) == 0)
        return;

    NetAddrType lead = addrs->a[0].type;
    if (pref == NCP_PreferV4)
        lead = NA_IPv4;
    else if (pref == NCP_PreferV6)
        lead = NA_IPv6;

    interleaveAddrsByFamily(addrs, lead);
}

// True if no address at or after connIdx in connQueue shares `type` -- i.e. addr is the last
// remaining candidate for its family. Used to decide whether an attempt gets the full
// connectTimeout (last chance for this family) or the shorter connectAttemptTimeout (another
// address of the same family is still queued behind it).
static bool connectIsLastOfFamily(NetSocket* sock, NetAddrType type)
{
    for (int32 i = sock->connectIdx; i < saSize(sock->connQueue); i++) {
        if (sock->connQueue.a[i].type == type)
            return false;
    }
    return true;
}

// Resolver callback, invoked on a resolver worker thread. Fills connQueue in the resolved order and
// kicks off the first attempt, or fails the connect on a resolution error. No attempt is in flight
// here (connectDeadline is 0), so writing connQueue/state without a lock is safe.
static void netsocketResolved(sa_NetAddr* addrs, NetErrorCode err, void* ctx)
{
    NetSocket* sock = (NetSocket*)ctx;

    // The socket may have been closed while resolution was in flight. No attempt is armed yet
    // (connectDeadline is 0), so the resolver callback is the sole owner here and can drop the
    // connect's resources directly without the deadline claim.
    if (atomicLoad(uint32, &sock->state, Relaxed) == NS_Closed) {
        NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);
        connectDrop(sock, q);
        if (q)
            objRelease(&q);
        return;
    }

    if (err != NERR_None || !addrs || saSize(*addrs) == 0) {
        NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);
        connectFinishFail(sock, q, err != NERR_None ? err : NERR_HostUnreachable);
        if (q)
            objRelease(&q);
        return;
    }

    // NCP_V4Only/NCP_V6Only may drop every address (the host has none of the requested family);
    // an interleave-only preference cannot empty the list. Either way this must run before the
    // empty check, not after, since it can be the thing that produces the empty result.
    applyConnectPref(addrs, sock->connectPref);
    if (saSize(*addrs) == 0) {
        NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);
        connectFinishFail(sock, q, NERR_HostUnreachable);
        if (q)
            objRelease(&q);
        return;
    }

    for (int32 i = 0; i < saSize(*addrs); i++)
        saPush(&sock->connQueue, NetAddr, addrs->a[i]);

    sock->connectIdx = 0;
    atomicStore(uint32, &sock->state, NS_Connecting, Release);
    connectNext(sock, NERR_None);
}

bool NetSocket_connect(_In_ NetSocket* self, _In_ strref host, uint16 port)
{
    if (self->type != NST_Stream)
        return false;

    // A connect needs the queue: the resolver, the backend, and the flow the event is delivered on
    // all live there.
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q)
        return false;

    // Only start from a fresh socket. NS_Resolving / NS_Connecting / NS_Connected already have a
    // connect in flight or established.
    uint32 expected = NS_Init;
    if (!atomicCompareExchange(uint32, strong, &self->state, &expected, NS_Resolving, AcqRel,
                               Relaxed)) {
        objRelease(&q);
        return false;
    }

    atomicFetchAdd(uint32, &q->connecting, 1, AcqRel);
    objAcquire(self);   // the connect operation holds a self-reference until it reaches a terminal state

    // A literal address needs no DNS. Resolving it inline also means a program that only ever
    // connects to addresses never spins up the resolver queue.
    NetAddr lit;
    if (netAddrFromStr(&lit, host)) {
        lit.port = port;
        saPush(&self->connQueue, NetAddr, lit);
        self->connectIdx = 0;
        atomicStore(uint32, &self->state, NS_Connecting, Release);
        connectNext(self, NERR_None);
        objRelease(&q);
        return true;
    }

    // Hostname: resolve asynchronously on the dedicated, bounded resolver queue.
    if (!_netResolveSubmit(host, port, netsocketResolved, self)) {
        atomicStore(uint32, &self->state, NS_Init, Release);
        atomicFetchSub(uint32, &q->connecting, 1, AcqRel);
        objRelease(&self);
        objRelease(&q);
        return false;
    }

    objRelease(&q);
    return true;
}

_Use_decl_annotations_
void NetSocket__connectCancel(NetSocket* self)
{
    bool claimed = false;
    withMutex (&self->connectLock) {
        if (self->connectDeadline != 0) {
            self->connectDeadline = 0;
            claimed = true;
        }
    }
    if (!claimed)
        return;

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    connectDrop(self, q);
    if (q)
        objRelease(&q);
}

// ---------------------------------------------------------------------------------------------
// Timeout sweep
//
// The third claimant of a socket's connectDeadline, alongside the backend completion (which
// arrives through netsocket_connectResult) and netsocket_connectCancel. It is a NetQueue method
// because the scan is over the queue's socket table, but everything it decides is connect-path
// state, so it lives here with the other two.
// ---------------------------------------------------------------------------------------------

// Minimum spacing between connect-timeout sweeps. Fine enough that a short per-attempt timeout in a
// test still fails over promptly, coarse enough that a busy poll loop or worker pool does not scan
// the socket table on every pass.
#define NET_CONNECT_SWEEP_INTERVAL timeMS(25)

_Use_decl_annotations_
void NetQueue__connectSweep(NetQueue* self)
{
    // The overwhelmingly common case: nothing is connecting, so this is a single relaxed load.
    if (atomicLoad(uint32, &self->connecting, Relaxed) == 0)
        return;

    uint32 now  = (uint32)clockTimer();
    uint32 last = atomicLoad(uint32, &self->connSweepLo, Relaxed);
    if ((uint32)(now - last) < (uint32)NET_CONNECT_SWEEP_INTERVAL)
        return;

    // Single-runner: whichever thread wins the timestamp claims this interval; the rest bail, so a
    // whole worker pool arriving together still runs exactly one sweep.
    if (!atomicCompareExchange(uint32, strong, &self->connSweepLo, &last, now, Relaxed, Relaxed))
        return;

    int64 tnow = clockTimer();

    // Collect the expired sockets under the read lock, then time them out after releasing it:
    // netsocket_connectResult() submits to the runqueue and resets the socket handle, neither of
    // which may run while the socket table is locked. Hold a reference on each so a concurrent
    // removeSocket cannot free one between the scan and the timeout.
    sa_ptr victims;
    saInit(&victims, ptr, 4);

    withReadLock (&self->lock) {
        foreach (hashtable, hti, self->sockets) {
            NetSocket* s = (NetSocket*)htiVal(object, hti);
            if (!s || atomicLoad(uint32, &s->state, Relaxed) != NS_Connecting)
                continue;

            int64 dl;
            withMutex (&s->connectLock)
                dl = s->connectDeadline;
            if (dl != 0 && tnow >= dl)
                saPush(&victims, ptr, objAcquire(s));
        }
    }

    for (int32 i = 0; i < saSize(victims); i++) {
        NetSocket* s = (NetSocket*)victims.a[i];
        netsocket_connectResult(s, NERR_Timeout);
        objRelease(&s);
    }

    saDestroy(&victims);
}

// Asynchronous DNS resolution, for the connect path and for netqueueResolve().
//
// getaddrinfo is the worst-behaved blocking call in the standard library: an unreachable resolver
// blocks it for the full system timeout (5-30s) with no way to cancel. It must therefore never run
// on a net I/O thread, and never on the shared sysq -- a burst of connects at a dead resolver would
// occupy every sysq worker and starve every other CX subsystem that uses it.
//
// So resolution runs on a dedicated, bounded queue owned by net: the sysq pattern (lazy-init,
// atexit teardown) but a private instance capped at a few concurrent lookups, so a dead resolver
// costs connect latency rather than a stalled process.

#include "net_private.h"
#include <cx/taskqueue.h>
#include <cx/utils/lazyinit.h>
#include <cx/time/time.h>

STR_CONST(kNetResolverName, "CX Net Resolver");

// At most this many getaddrinfo calls run at once; the rest queue behind them. The cap is the whole
// point -- a dead resolver ties up N threads, not the process.
#define NET_RESOLVER_MAX_WORKERS 4

// Lookups allowed to wait behind those, across every queue. Past this, new lookups fail at once:
// queueing without bound in front of a bounded pool only moves a dead resolver's stall somewhere
// harder to see.
#define NET_RESOLVER_MAX_PENDING (NET_RESOLVER_MAX_WORKERS * 16)

static TaskQueue* netResolverQ;
static atomic(uint32) netResolvePending;

static void netResolverExit(void)
{
    tqShutdown(netResolverQ, timeS(5));
    tqRelease(&netResolverQ);
}

static LazyInitState netResolverInitState;
static void netResolverInitFunc(void* dummy)
{
    unused_noeval(dummy);

    TaskQueueConfig conf;
    tqPresetMinimal(&conf);
    conf.pool.wMax = NET_RESOLVER_MAX_WORKERS;   // hard concurrency cap on in-flight getaddrinfo
    conf.flags |= TQ_Monitor;                    // a lookup exceeding its threshold is worth logging

    netResolverQ = tqCreate(kNetResolverName, &conf);
    if (netResolverQ && !tqStart(netResolverQ)) {
        // A queue that will not start can never run a lookup; leave netResolverQ NULL so
        // _netResolveSubmit fails fast instead of queueing tasks that nothing will service.
        tqRelease(&netResolverQ);
    }
    if (netResolverQ)
        atexit(netResolverExit);
}

static _meta_inline void netResolverInit(void)
{
    lazyInit(&netResolverInitState, netResolverInitFunc, NULL);
}

// Runs on a resolver worker thread. Does the (blocking) platform lookup, then hands the result to
// the callback -- which feeds it back into the connect state machine, never runs application code
// inline.
//
// The pending request is the closure's captured environment: the host string is copied into it at
// capture and destroyed with it, so nothing here outlives the call and there is nothing to free.
// Keys rather than positions because this is nowhere near a hot path and a keyed capture cannot be
// silently re-bound by inserting another one.
static bool netResolveTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);

    strref host     = stvlFindVal(cvars, host, strref);
    uint16 port     = stvlFindVal(cvars, port, uint16);
    NetResolveCB cb = (NetResolveCB)stvlFindPtr(cvars, cb);
    void* ctx       = stvlFindPtr(cvars, ctx);

    if (!cb)
        return false;

    sa_NetAddr addrs;
    saInit(&addrs, NetAddr, 4);

    NetErrorCode err = netPlatformResolve(host, port, &addrs);
    cb(&addrs, err, ctx);

    saDestroy(&addrs);
    atomicFetchSub(uint32, &netResolvePending, 1, Relaxed);
    return err == NERR_None;
}

_Use_decl_annotations_
bool _netResolveSubmit(strref host, uint16 port, NetResolveCB cb, void* ctx)
{
    netResolverInit();
    if (!netResolverQ)
        return false;

    if (atomicFetchAdd(uint32, &netResolvePending, 1, Relaxed) >= NET_RESOLVER_MAX_PENDING) {
        atomicFetchSub(uint32, &netResolvePending, 1, Relaxed);
        return false;
    }

    // tqCall owns the closure on both outcomes, so a queue that refuses the task still releases
    // the captured host string.
    if (!tqCall(netResolverQ,
                closureCreate(netResolveTask, stvark(host, strref, host),
                              stvark(port, uint16, port), stvark(cb, ptr, (void*)cb),
                              stvark(ctx, ptr, ctx)))) {
        atomicFetchSub(uint32, &netResolvePending, 1, Relaxed);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// netqueueResolve
//
// A lookup is a NetCtlEvent from the start: built here with the request, filled in by the resolver
// thread, and delivered through the queue's control list like any other event with no flow. Until
// it is delivered it is also indexed by id in NetQueue::resolves, which is how a cancel finds it --
// in flight on the resolver thread, or already waiting in the list.
// ---------------------------------------------------------------------------------------------

// Append a finished lookup to the queue's control list, or drop it if it was cancelled or the queue
// is shutting down. Both are decided under ctlLock, the same lock cancel and shutdown take.
static void resolveFinish(NetQueue* q, NetCtlEvent* ce)
{
    bool drop = false;
    withMutex (&q->ctlLock) {
        drop = ce->cancelled || _netqueueShuttingDown(q);
        if (drop) {
            htRemove(&q->resolves, uint64, ce->ev.resolved.id);
        } else {
            if (q->ctlTail)
                ((NetCtlEvent*)q->ctlTail)->next = ce;
            else
                q->ctlHead = ce;
            q->ctlTail = ce;
            atomicFetchAdd(uint32, &q->nctl, 1, Release);
        }
    }

    if (drop)
        _netCtlEventFree(&ce);
    else
        netqueue_kick(q);
}

// Resolver callback, on a resolver worker thread.
static void resolveDone(sa_NetAddr* addrs, NetErrorCode err, void* ctx)
{
    NetCtlEvent* ce = (NetCtlEvent*)ctx;
    NetQueue* q     = objAcquireFromWeak(NetQueue, ce->queue);
    if (!q) {
        _netCtlEventFree(&ce);
        return;
    }

    if (err == NERR_None && addrs) {
        for (int32 i = 0; i < saSize(*addrs); i++)
            saPush(&ce->addrs, NetAddr, addrs->a[i]);
    }
    ce->ev.resolved.err = err;
    resolveFinish(q, ce);
    objRelease(&q);
}

_Use_decl_annotations_
NetResolveId NetQueue_resolve(NetQueue* self, strref host, uint16 port,
                              const NetHandlers* handlers, void* ctx)
{
    if (_netqueueShuttingDown(self))
        return 0;

    NetCtlEvent* ce        = xaAllocStruct(NetCtlEvent, XA_Zero);
    ce->ev.event           = NET_Resolved;
    ce->ev.resolved.port   = port;
    ce->handlers           = handlers;
    ce->ctx                = ctx;
    strDup(&ce->host, host);
    saInit(&ce->addrs, NetAddr, 4);

    NetResolveId id;
    withMutex (&self->ctlLock) {
        id                 = ++self->resolveSerial;
        ce->ev.resolved.id = id;
        htInsert(&self->resolves, uint64, id, ptr, ce);
    }

    // A literal needs no lookup, and a program that only ever uses addresses never starts the
    // resolver threads.
    NetAddr lit;
    if (netAddrFromStr(&lit, host)) {
        lit.port = port;
        saPush(&ce->addrs, NetAddr, lit);
        resolveFinish(self, ce);
        return id;
    }

    // Once submitted, the event belongs to the resolver thread, which may already have finished
    // with it by the time this returns.
    ce->queue = objGetWeak(NetQueue, self);
    if (!_netResolveSubmit(host, port, resolveDone, ce)) {
        withMutex (&self->ctlLock)
            htRemove(&self->resolves, uint64, id);
        _netCtlEventFree(&ce);
        return 0;
    }
    return id;
}

_Use_decl_annotations_
bool NetQueue_cancelResolve(NetQueue* self, NetResolveId id)
{
    bool stopped = false;
    withMutex (&self->ctlLock) {
        htelem e = htFind(self->resolves, uint64, id, none, NULL);
        if (e) {
            NetCtlEvent* ce = (NetCtlEvent*)hteVal(self->resolves, ptr, e);
            if (!ce->cancelled) {
                ce->cancelled = true;
                stopped       = true;
            }
        }
    }
    return stopped;
}

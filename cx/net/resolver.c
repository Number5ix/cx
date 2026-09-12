// Asynchronous DNS resolution for the connect path.
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

static TaskQueue* netResolverQ;

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
    return err == NERR_None;
}

_Use_decl_annotations_
bool _netResolveSubmit(strref host, uint16 port, NetResolveCB cb, void* ctx)
{
    netResolverInit();
    if (!netResolverQ)
        return false;

    // tqCall owns the closure on both outcomes, so a queue that refuses the task still releases
    // the captured host string.
    return tqCall(netResolverQ,
                  closureCreate(netResolveTask, stvark(host, strref, host),
                                stvark(port, uint16, port), stvark(cb, ptr, (void*)cb),
                                stvark(ctx, ptr, ctx)));
}

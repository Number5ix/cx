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

// One pending resolution. Heap-allocated so it outlives the netsocketConnect() call and is freed by
// the task once the callback has run.
typedef struct NetResolveReq {
    string host;
    uint16 port;
    NetResolveCB cb;
    void* ctx;
} NetResolveReq;

// Runs on a resolver worker thread. Does the (blocking) platform lookup, hands the result to the
// callback -- which feeds it back into the connect state machine, never runs application code
// inline -- then frees the request.
static bool netResolveTask(TaskQueue* tq, void* data)
{
    unused_noeval(tq);
    NetResolveReq* req = (NetResolveReq*)data;

    sa_NetAddr addrs;
    saInit(&addrs, NetAddr, 4);

    NetErrorCode err = netPlatformResolve(req->host, req->port, &addrs);
    req->cb(&addrs, err, req->ctx);

    saDestroy(&addrs);
    strDestroy(&req->host);
    xaFree(req);
    return err == NERR_None;
}

_Use_decl_annotations_
bool _netResolveSubmit(strref host, uint16 port, NetResolveCB cb, void* ctx)
{
    netResolverInit();
    if (!netResolverQ)
        return false;

    NetResolveReq* req = xaAlloc(sizeof(NetResolveReq), XA_Zero);
    strDup(&req->host, host);
    req->port = port;
    req->cb   = cb;
    req->ctx  = ctx;

    if (!tqCall(netResolverQ, netResolveTask, req)) {
        strDestroy(&req->host);
        xaFree(req);
        return false;
    }
    return true;
}

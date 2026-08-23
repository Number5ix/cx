// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/queue.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "net_private.h"
#include <cx/platform/os.h>
#include <cx/thread.h>
#include <cx/time/time.h>
#include <cx/time/clock.h>

_objinit_guaranteed bool NetQueue_init(_In_ NetQueue* self)
{
    // Config-dependent storage. A derived factory fills the scalar config members in before
    // calling objInstInit(), so everything needed to size these is already in place.
    if (self->conf.recvBufSize == 0)
        self->conf.recvBufSize = 2048;

    // Its own object, so that a flow or a filter stage torn down after the queue is unreachable can
    // still return what it is holding; the queue's reference is just one of several.
    self->pool = netpoolCreate(&self->conf);

    prqInitDynamic(&self->runq, 64, 1024, 0, PRQ_Grow_100, PRQ_Grow_50);
    atomicStore(uint32, &self->gcLastLo, (uint32)clockTimer(), Relaxed);

    // Dispatch workers, if any, are started later by the backend factory once it has read
    // nthreads out of the config; here we only arm the semaphore they will block on.
    semaInit(&self->runqSema, 0);

    // Autogen begins -----
    htInit(&self->sockets, ptr, object, 16);
    rwlockInit(&self->lock);
    htInit(&self->timerIdx, uint64, uint32, 16);
    mutexInit(&self->timerLock);
    return true;
    // Autogen ends -------
}

void NetQueue__applyConfig(_In_ NetQueue* self, _In_opt_ const NetQueueConfig* conf)
{
    NetQueueConfig defconf;
    if (!conf) {
        netqueuePresetClient(&defconf);
        conf = &defconf;
    }

    self->conf = *conf;
    if (!self->conf.connectTimeout)
        self->conf.connectTimeout = timeS(10);
}

// ---------------------------------------------------------------------------------------------
// Dispatch worker pool
//
// Backend-independent: a worker does nothing but drain the runqueue and sleep on runqSema when it
// is empty. This is the same code tick() runs inline in polled mode, just on an owned thread. The
// select backend runs a separate ingest thread that fills the runqueue and posts the semaphore;
// IOCP will instead merge ingest and dispatch and not use this pool at all.
// ---------------------------------------------------------------------------------------------

STR_CONST(kNetWorkerName, "CX Net Worker");

static int netDispatchWorker(Thread* thr)
{
    NetQueue* q = stvlNextPtr(&thr->args);
    if (!q)
        return 1;

    while (thrLoop(thr)) {
        while (netqueue_dispatch(q))
            ;

        // Drained and about to sleep: the moment the PrQueue docs recommend for GC. Time-gated
        // internally, so all the workers piling up here still cost one pass per interval.
        netqueue_maint(q);

        // Block until ingest posts work, or wake periodically to re-check the exit flag -- a
        // bounded wait means a lost or coalesced post can never wedge a worker, and shutdown also
        // posts once per worker to release everyone promptly.
        semaTryDecTimeout(&q->runqSema, timeMS(200));
    }

    // Drain once more on the way out so terminal events queued during shutdown are delivered by a
    // worker rather than stranded for the final inline drain to mop up.
    while (netqueue_dispatch(q))
        ;

    return 0;
}

void NetQueue__startWorkers(_In_ NetQueue* self, int32 n)
{
    if (n <= 0)
        return;

    // saPush lazily allocates the zero-initialized handle on first insert, so no saInit is needed.
    // thrCreate returns a +1 reference and saPush(object) takes its own, so the create reference is
    // released straight after -- the pool array is left holding the sole reference to each worker.
    for (int32 i = 0; i < n; i++) {
        Thread* t = thrCreate(netDispatchWorker, kNetWorkerName, stvar(ptr, self));
        if (t) {
            saPush(&self->workers, object, t);
            objRelease(&t);
        }
    }
}

void NetQueue__stopWorkers(_In_ NetQueue* self, int64 timeout)
{
    int32 n = saSize(self->workers);
    if (n == 0)
        return;

    // Two passes so the pool shuts down in parallel: ask everyone to exit and release the semaphore
    // once per worker (so a worker blocked on an empty runqueue wakes at once rather than waiting
    // out its poll interval), then join.
    foreach (sarray, idx, Thread*, t, self->workers) {
        thrRequestExit(t);
    }
    semaInc(&self->runqSema, n);

    int64 wait = timeout > 0 ? timeout : timeForever;
    foreach (sarray, idx, Thread*, t, self->workers) {
        thrWait(t, wait);
    }

    // The OS threads have exited, so releasing the pool's references (which frees the Thread
    // objects) is safe. Resets the handle to empty; the autogen saDestroy at teardown is then a
    // no-op, and a second stop call sees saSize() == 0 and returns.
    saDestroy(&self->workers);
}

// ---------------------------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------------------------

// Minimum spacing between opportunistic GC passes. The dynamic queues only accrete under sustained
// load, so a quarter second is far finer than needed to keep the footprint bounded while staying
// invisible to a busy poll loop.
#define NET_GC_INTERVAL timeMS(250)

void NetQueue__maint(_In_ NetQueue* self)
{
    uint32 now  = (uint32)clockTimer();
    uint32 last = atomicLoad(uint32, &self->gcLastLo, Relaxed);

    // Unsigned delta, so it wraps cleanly at the 32-bit rollover exactly as PrQueue's own shrink
    // timing does. Not yet due: nothing to do.
    if ((uint32)(now - last) < (uint32)NET_GC_INTERVAL)
        return;

    // Single-runner: whichever thread wins the timestamp claims this interval and the rest bail
    // immediately, so a full worker pool arriving at the idle point together still runs one pass.
    if (!atomicCompareExchange(uint32, strong, &self->gcLastLo, &last, now, Relaxed, Relaxed))
        return;

    // Both are non-blocking; the pool's header freelist is a fixed queue and needs none.
    prqCollect(&self->runq);
    bufpoolCollect(&self->pool->msgbuf);
}

bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket)
{
    bool ret = false;
    devAssert(!socket->queue);

    // Refuse new sockets once shutdown has started; the queue is on its way down and a socket added
    // now would never be serviced.
    if (_netqueueShuttingDown(self))
        return false;

    withWriteLock (&self->lock) {
        ret = htInsert(&self->sockets, ptr, socket, object, socket, HT_Ignore) != 0;
    }

    if (ret) {
        socket->queue = objGetWeak(NetQueue, self);

        // A stream socket builds its one flow in its own init, before it has any queue to take a
        // pool from, so that flow's reference is filled in here instead. Datagram flows are all
        // created after this point and pick the pool up from the socket's queue themselves.
        if (socket->flow && !socket->flow->pool)
            socket->flow->pool = objAcquire(self->pool);

        // Inherit the queue's watermark defaults unless the socket has its own.
        if (socket->sendHigh == 0)
            socket->sendHigh = self->conf.sendHigh;
        if (socket->sendLow == 0)
            socket->sendLow = self->conf.sendLow;

        // Same inheritance for the connect family preference; NCP_Default (0) is both "unset" and
        // the ordinary interleave behavior, so a socket picks its own explicit preference to diverge
        // from a queue-wide non-default setting rather than reselecting Default itself.
        if (socket->connectPref == NCP_Default)
            socket->connectPref = self->conf.connectPref;
    }

    return ret;
}

bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket)
{
    bool ret        = false;
    NetSocket* temp = NULL;

    withWriteLock (&self->lock) {
        temp = objAcquire(socket);   // keep socket alive slightly longer if we are the
                                     // last reference
        ret  = htRemove(&self->sockets, ptr, socket);
    }

    // All flows close before their socket does, so that flow->user teardown never runs after the
    // socket it belonged to is gone. This is the one ordering rule the application can depend on
    // across the two objects, and it is the one that matters.
    if (ret && temp) {
        netsocket_closeFlows(temp, NCR_SocketClosed);
        objDestroyWeak(&temp->queue);
    }
    objRelease(&temp);

    return ret;
}

void NetQueue_presetClient(_Out_ NetQueueConfig* conf)
{
    *conf = (NetQueueConfig) {
        .nthreads              = 0,   // polled; the caller drives netqueueTick()
        .flags                 = NQ_None,
        .recvBufSize           = 2048,
        .recvBufInitial        = 8,
        .recvBufMax            = 64,
        .maxflows              = 64,
        .reclaimBatch          = 4,
        .noReclaim             = false,
        .sendHigh              = 256 * 1024,
        .sendLow               = 64 * 1024,
        .connectTimeout        = timeS(10),
        .connectAttemptTimeout = timeS(2),
        .connectPref           = NCP_Default,
    };
}

void NetQueue_presetServer(_Out_ NetQueueConfig* conf)
{
    *conf = (NetQueueConfig) {
        .nthreads              = 0,
        .flags                 = NQ_AutoAccept,
        .recvBufSize           = 2048,
        .recvBufInitial        = 256,
        .recvBufMax            = 4096,
        .maxflows              = 4096,
        .reclaimBatch          = 16,
        .noReclaim             = false,
        .sendHigh              = 256 * 1024,
        .sendLow               = 64 * 1024,
        .connectTimeout        = timeS(10),
        .connectAttemptTimeout = timeS(2),
        .connectPref           = NCP_Default,
    };

    // These defaults still want a flood test to pin down real numbers; droppedNoBuf is the
    // counter to watch while running one.
    conf->nthreads = max(1, min(8, osLogicalCPUs()));
}

_Ret_maybenull_ NetSocket* NetQueue_connect(_In_ NetQueue* self, _In_opt_ strref host, uint16 port,
                                            _In_opt_ const NetHandlers* handlers,
                                            _In_opt_ void* ctx)
{
    NetSocket* sock = netqueueSocket(self, NST_Stream);
    if (!sock)
        return NULL;

    if (!netqueueAddSocket(self, sock)) {
        objRelease(&sock);
        return NULL;
    }

    // Handlers go on before the connect starts, so the NET_Connection event cannot race past an
    // unregistered callback.
    if (handlers)
        netsocketSetHandlers(sock, handlers, ctx);

    if (!netsocketConnect(sock, host, port)) {
        netsocketClose(sock);   // also removes it from the queue
        objRelease(&sock);
        return NULL;
    }

    return sock;
}

_Ret_maybenull_ NetSocket* NetQueue_listen(_In_ NetQueue* self, _In_ NetAddr* addr, int backlog,
                                           _In_opt_ const NetHandlers* handlers,
                                           _In_opt_ void* ctx)
{
    NetSocket* sock = netqueueSocket(self, NST_Stream);
    if (!sock)
        return NULL;

    if (!netqueueAddSocket(self, sock)) {
        objRelease(&sock);
        return NULL;
    }

    // Registered before listen for the same reason as netqueueConnect: an inbound connection can
    // land in the backlog the moment listen() returns, and its NET_Accepted must find the handler.
    if (handlers)
        netsocketSetHandlers(sock, handlers, ctx);

    if (!netsocketBind(sock, addr) || !netsocketListen(sock, backlog)) {
        netsocketClose(sock);   // also removes it from the queue
        objRelease(&sock);
        return NULL;
    }

    return sock;
}

void NetQueue_setHandlers(_In_ NetQueue* self, _In_opt_ const NetHandlers* handlers,
                          _In_opt_ void* ctx)
{
    self->handlers   = handlers ? *handlers : (NetHandlers) { 0 };
    self->handlerCtx = ctx;
}

_Ret_maybenull_ NetFlow* NetQueue_promoteFlow(_In_ NetQueue* self, _Inout_ NetSocket* sock,
                                              _In_ NetAddr* peer)
{
    // Promotion is an application decision, so it is allowed to exceed the cap by one rather than
    // being refused a second time -- the app has already done the work of proving this peer is
    // real, and failing it here would make the flowRefused handler useless.
    NetFlow* flow = netqueue_findFlow(self, sock, peer, false);
    if (flow)
        return flow;

    return netqueue_admitFlow(self, sock, peer);
}

uint32 NetQueue_droppedNoBuf(_In_ NetQueue* self)
{
    return atomicLoad(uint32, &self->droppedNoBuf, Relaxed);
}

bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout)
{
    // Stop accepting new work first. A backend that runs an ingest thread (select) has already
    // stopped it in its own shutdown override before chaining here, so no new packets can arrive
    // past this point and the socket set is quiescent.
    atomicStore(uint32, &self->shutdownReq, 1, Release);

    sa_NetSocket socks;
    saInit(&socks, NetSocket, 8);

    // Snapshot first: closing flows submits to the runqueue, which must not happen with the
    // socket table locked.
    withWriteLock (&self->lock) {
        foreach (hashtable, hti, self->sockets) {
            saPush(&socks, NetSocket, (NetSocket*)htiVal(object, hti));
        }
        htClear(&self->sockets);
    }

    for (int32 i = 0; i < saSize(socks); i++) {
        netsocket_closeFlows(socks.a[i], NCR_Shutdown);
        objDestroyWeak(&socks.a[i]->queue);
    }

    // Join the dispatch workers within the timeout. Each drains the runqueue one last time as it
    // exits, so the terminal events just queued are delivered on a worker -- the application sees
    // its NET_FlowClosed callbacks before the queue goes away. A no-op in polled mode.
    netqueue_stopWorkers(self, timeout);

    // Final inline drain: it does the work in polled mode, and mops up any straggler a worker left
    // if it timed out.
    while (netqueue_dispatch(self))
        ;

    saDestroy(&socks);

    return true;
}

void NetQueue_destroy(_In_ NetQueue* self)
{
    // Safety net for a queue released without a shutdown: the workers hold a raw pointer to it, so
    // they must be joined before anything they touch is freed. After a normal shutdown this is a
    // no-op. A backend's own destructor (which runs first) is responsible for its ingest thread.
    // (runqSema and the now-empty workers array are torn down by the autogen block below.)
    netqueue_stopWorkers(self, timeForever);

    // Anything still on the runqueue holds a reference that has to come back.
    NetFlow* flow;
    while ((flow = (NetFlow*)prqPop(&self->runq)) != NULL) {
        objRelease(&flow);
    }
    prqDestroy(&self->runq);

    // So does anything still armed. After a normal shutdown every flow's terminal path has already
    // dropped its timers and this loop finds nothing; a queue released without one has not.
    for (uint32 i = 0; i < self->ntimers; i++)
        objRelease(&self->timers[i].flow);
    self->ntimers = self->timersCap = 0;
    xaDestroy(&self->timers);

    // Autogen begins -----
    htDestroy(&self->sockets);
    rwlockDestroy(&self->lock);
    objRelease(&self->pool);
    htDestroy(&self->timerIdx);
    mutexDestroy(&self->timerLock);
    saDestroy(&self->workers);
    semaDestroy(&self->runqSema);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// Ingest
// ---------------------------------------------------------------------------------------------

bool NetQueue__ingestDatagram(_In_ NetQueue* self, _Inout_ NetSocket* sock, _In_ NetAddr* peer, _Inout_ Buffer* buf)
{
    NetFlow* flow = netqueue_findFlow(self, sock, peer, true);

    NetMessage* msg = netpoolAllocHeader(self->pool);
    msg->kind       = NMSG_Data;
    msg->addr       = *peer;
    msg->buf        = *buf;
    msg->flags      = NMF_PoolBuf;   // the backend took this buffer from the queue's pool
    *buf            = NULL;

    if (!flow) {
        // No flow and no room to make one. Hand the raw packet to the flowRefused handler on the
        // ingest thread, without allocating a flow -- this is the path that keeps a spoofed
        // source flood from forcing unbounded allocation.
        NetEvent ev     = { .event = NET_FlowRefused };
        ev.refused.msg  = msg;
        netqueue_deliver(self, sock, NULL, &ev);

        netpoolFreeMsg(self->pool, &msg);
        return false;
    }

    netqueue_submit(self, flow, msg);
    objRelease(&flow);
    return true;
}


// Autogen begins -----
// clang-format off
void NetQueue__submit(_In_ NetQueue* self, _Inout_ NetFlow* flow, _Inout_ NetMessage* msg);
bool NetQueue__dispatch(_In_ NetQueue* self);
void NetQueue__deliver(_In_ NetQueue* self, _In_opt_ NetSocket* sock, _In_opt_ NetFlow* flow, _Inout_ NetEvent* ev);
NetTimerId NetQueue__addTimer(_In_ NetQueue* self, _Inout_ NetFlow* flow, int64 delay, flags_t flags, NetTimerFn fn, _In_opt_ void* ctx);
bool NetQueue__cancelTimer(_In_ NetQueue* self, NetTimerId id);
bool NetQueue__rearmTimer(_In_ NetQueue* self, NetTimerId id, int64 delay);
void NetQueue__cancelFlowTimers(_In_ NetQueue* self, _Inout_ NetFlow* flow);
void NetQueue__timerSweep(_In_ NetQueue* self);
int64 NetQueue__nextDeadline(_In_ NetQueue* self);
_Ret_maybenull_ NetFlow* NetQueue__findFlow(_In_ NetQueue* self, _Inout_ NetSocket* sock, _In_ NetAddr* peer, bool create);
_Ret_maybenull_ NetFlow* NetQueue__admitFlow(_In_ NetQueue* self, _Inout_ NetSocket* sock, _In_ NetAddr* peer);
uint32 NetQueue__reclaimFlows(_In_ NetQueue* self, _Inout_ NetSocket* sock);
#include "net/queue.auto.inc"
// clang-format on
// Autogen ends -------

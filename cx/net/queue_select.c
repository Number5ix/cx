// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/queue_select.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "net_private.h"
#include "cx/net/net.h"
#include <cx/thread.h>
#include <cx/time/time.h>
#include <cx/time/clock.h>

// Base methods this class overrides, called through to for the shared behaviour.
extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);

// Forward decls for the split ingest loop (defined below, after the ingest helpers).
static void selectPoll(_Inout_ NetQueueSelect* self, int64 waitUs);

// Send-pump hook installed on the base queue: the send path calls it after leaving outbound data
// queued so the parked ingest thread rebuilds its watch set with write interest instead of stalling
// for a full poll interval. Readiness watches the whole set, so the specific socket is irrelevant
// here. A no-op once the ingest thread is gone.
static void selectSendPump(void* ctx, NetSocket* sock)
{
    NetQueueSelect* self = (NetQueueSelect*)ctx;
    unused_noeval(sock);
    if (self->ingest && self->selset)
        nselWake((NetSelectSet*)self->selset);
}

// Wake hook installed on the base queue: arming a timer nearer than the deadline this pass is
// already sleeping on interrupts the wait so it recomputes its bound. Unlike the send pump this is
// installed in polled mode too, because an application thread can arm a timer while another sits
// in netqueueTick().
static void selectWake(void* ctx)
{
    NetQueueSelect* self = (NetQueueSelect*)ctx;
    if (self->selset)
        nselWake((NetSelectSet*)self->selset);
}

// The dedicated select-loop thread used in threaded mode. It only ingests -- filling the runqueue
// and posting the worker semaphore through netqueue_submit -- while the base dispatch workers drain
// it. A bounded wait plus nselWake on socket changes and shutdown keeps it responsive.
static int selectIngestThread(Thread* thr)
{
    NetQueueSelect* self = stvlNextPtr(&thr->args);
    if (!self)
        return 1;

    while (thrLoop(thr))
        selectPoll(self, timeS(1));

    return 0;
}

_objfactory_guaranteed NetQueueSelect* NetQueueSelect_create(NetQueueConfig* conf)
{
    NetQueueSelect* self = objInstCreate(NetQueueSelect);

    // Config must be applied before objInstInit so the base sizes everything from it.
    netqueue_applyConfig(self, conf);
    objInstInit(self);

    self->selset = nselCreate();
    if (!self->selset) {
        objRelease(&self);
        return NULL;
    }

    // Threaded mode: N base dispatch workers drain the runqueue, and one ingest thread here runs
    // the select() loop and feeds it. Polled mode (nthreads == 0) starts nothing and the caller
    // drives everything through tick().
    NetQueue(self)->wake    = selectWake;
    NetQueue(self)->wakeCtx = self;

    int32 nthreads = conf ? conf->nthreads : 0;
    if (nthreads > 0) {
        netqueue_startWorkers(self, nthreads);
        self->ingest = thrCreate(selectIngestThread, _S "CX Net Select", stvar(ptr, self));

        // If the ingest thread would not start there is nothing to feed the workers, so fall back
        // to a functioning polled queue rather than a half-threaded one that never ingests.
        if (!self->ingest) {
            netqueue_stopWorkers(self, timeForever);
        } else {
            // Let the send path wake the ingest thread when it queues outbound data, so write
            // interest is picked up promptly. Polled mode leaves this NULL -- tick() rebuilds the
            // set on every call anyway.
            NetQueue(self)->sendPump = selectSendPump;
            NetQueue(self)->sendCtx  = self;
        }
    }

    return self;
}

NetSocket* NetQueueSelect_socket(_In_ NetQueueSelect* self, NetSocketType type)
{
    // The concrete socket class is per-platform; the queue does not need to know which.
    unused_noeval(self);
    return netPlatformCreateSocket(type);
}

// Drain everything currently readable on a datagram socket into pooled buffers and hand each to
// the core for demux and dispatch. One readiness can cover many datagrams.
static void ingestDatagram(_Inout_ NetQueueSelect* self, _Inout_ NetSocket* sock)
{
    NetQueue* q = NetQueue(self);

    for (;;) {
        Buffer buf = bufpoolGet(&q->pool->msgbuf);
        if (!buf) {
            // Pool exhausted. Consume one datagram into a scratch and drop it, so readiness clears
            // and the loop cannot spin on the same packet, and count it so a too-small pool shows
            // up as droppedNoBuf rather than as silent loss. A datagram larger than the scratch is
            // consumed and truncated by the OS, which is fine on a path that is discarding it.
            uint8 scratch[2048];
            NetAddr src;
            NetErrorCode err;
            intptr n = netSockRecvFrom(sock->handle, scratch, sizeof(scratch), &src, &err);
            if (n < 0 && err == NERR_WouldBlock)
                break;
            atomicFetchAdd(uint32, &q->droppedNoBuf, 1, Relaxed);
            continue;
        }

        NetAddr src;
        NetErrorCode err;
        intptr n = netSockRecvFrom(sock->handle, buf->data, buf->sz, &src, &err);
        if (n < 0) {
            bufpoolPut(&q->pool->msgbuf, &buf);
            break;   // WouldBlock (drained) or a real error; nothing more this tick
        }

        buf->len = (size_t)n;
        netqueue_ingestDatagram(q, sock, &src, &buf);   // takes ownership of buf
    }
}

// Drain readiness on a connected stream socket into the socket's receive ring, then wake its
// single flow so a NET_DataReceived fires and the handler drains the ring with netsocketRecv().
static void ingestStream(_Inout_ NetQueueSelect* self, _Inout_ NetSocket* sock)
{
    NetQueue* q     = NetQueue(self);
    BufRing* ring   = &sock->bufs.stream.recv;
    size_t received = 0;
    bool closed     = false;
    NetErrorCode closeErr = NERR_None;

    withMutex (&sock->recvLock) {
        for (;;) {
            uint8* ptr;
            size_t len;
            // Reserve at least the mru so the reservation is never pathologically small; commit
            // exactly what arrived. bufringReserve hands back the whole contiguous run in len.
            bufringReserve(ring, sock->mru ? sock->mru : 1500, &ptr, &len);

            NetErrorCode err;
            intptr n = netSockRecv(sock->handle, ptr, len, &err);
            if (n > 0) {
                bufringCommit(ring, (size_t)n);
                received += (size_t)n;
                if ((size_t)n < len)
                    break;   // read less than offered: the OS receive buffer is drained
                continue;    // filled the reservation; there may be more waiting
            }

            bufringCommit(ring, 0);   // release the reservation, add nothing
            if (n == 0) {
                closed = true;   // orderly peer shutdown
            } else if (err != NERR_WouldBlock) {
                closed   = true;   // a real error
                closeErr = err;
            }
            break;
        }
    }

    if (received > 0 && sock->flow) {
        // A bufferless message: the bytes already live in the ring, and `bytes` is the only record
        // of how many arrived in this batch. The handler reads them back out through the ring.
        NetMessage* msg = netpoolAllocHeader(q->pool);
        msg->kind       = NMSG_Data;
        msg->buf        = NULL;
        msg->bytes      = received;
        msg->addr       = sock->remote;
        netqueue_submit(q, sock->flow, msg);
    }

    if (closed && sock->flow)
        netflow_close(sock->flow, closeErr == NERR_None ? NCR_PeerClosed : NCR_Error);
}

// Drain every connection currently sitting in a listening socket's backlog. One readiness can cover
// several completed connections, so accept until the OS reports it would block. Each accepted socket
// is wrapped by the platform shim and handed to the shared accept path, which admits it (under
// NQ_AutoAccept) and delivers NET_Accepted through the listener's flow.
static void acceptReady(_Inout_ NetQueueSelect* self, _Inout_ NetSocket* listener)
{
    NetQueue* q = NetQueue(self);

    for (;;) {
        if (_netqueueShuttingDown(q))
            break;

        NetSocket* ns = NULL;
        NetAddr peer;
        NetErrorCode err = netPlatformAccept(listener->handle, &ns, &peer);
        if (err == NERR_WouldBlock)
            break;   // backlog drained
        if (err != NERR_None || !ns)
            break;   // a transient error; the next readiness retries

        netsocket_accepted(listener, ns, &peer);   // takes the reference
    }
}

// Ingest half of the loop: rebuild the watch set, wait, and drain every ready socket into its
// flow. It does NOT dispatch -- in polled mode tick() dispatches right after, and in threaded mode
// the base workers do, woken through netqueue_submit. Shared by tick() and the ingest thread so the
// readiness-to-completion path is written exactly once.
static void selectPoll(_Inout_ NetQueueSelect* self, int64 waitUs)
{
    NetSelectSet* sel = (NetSelectSet*)self->selset;
    NetQueue* q       = NetQueue(self);

    // Release last pass's socket references, then rebuild the watch set and the handle->socket map
    // from the current sockets. Every socket is watched for read; one with queued outbound data is
    // also watched for write so it gets flushed when the send buffer drains. The map holds a
    // reference on each socket so one removed by another thread between here and ingest stays alive
    // until the next clear.
    htClear(&self->fdmap);
    nselClear(sel);

    withReadLock (&q->lock) {
        foreach (hashtable, hti, q->sockets) {
            NetSocket* sock = (NetSocket*)htiVal(object, hti);
            if (!sock || sock->handle == NET_INVALID_HANDLE)
                continue;

            uint32 st = atomicLoad(uint32, &sock->state, Relaxed);
            if (st == NS_Closed)
                continue;

            bool read = false, write = false;
            if (sock->type == NST_Datagram) {
                read  = true;
                write = netsocket_wantWrite(sock);
            } else if (st == NS_Connected) {
                read  = true;
                write = netsocket_wantWrite(sock);
            } else if (st == NS_Listening) {
                // A readable listener has a completed connection waiting in the backlog to accept.
                read = true;
            } else if (st == NS_Connecting) {
                // Watch for connect completion: writability signals success, and the except set
                // (which nselAdd mirrors write interest into) signals a failed connect on Windows.
                write = true;
            } else {
                continue;   // NS_Init / NS_Resolving stream socket: nothing to watch yet
            }

            nselAdd(sel, sock->handle, read, write);
            htInsert(&self->fdmap, uint64, (uint64)sock->handle, object, sock);
        }
    }

    int ready = nselWait(sel, netqueue_pollTimeout(q, waitUs));

    // Fire whatever came due. Run this even on a bare timeout (ready <= 0) -- a black-holed connect
    // never signals readiness, so its timer is the only thing that ends the attempt.
    netqueue_timerSweep(q);

    if (ready <= 0)
        return;   // timeout, wake, or error -- no application readiness

    NetSockHandle h;
    bool r, w;
    while (nselNext(sel, &h, &r, &w)) {
        htelem e        = htFind(self->fdmap, uint64, (uint64)h, none, NULL);
        NetSocket* sock = e ? (NetSocket*)hteVal(self->fdmap, object, e) : NULL;
        if (!sock)
            continue;

        // A connecting socket reported writable or excepted has resolved its attempt: writable is
        // success, excepted is failure. netSockConnectResult() reads SO_ERROR to tell them apart,
        // and the shared state machine advances (next address, or the NET_Connection event).
        if (sock->type == NST_Stream &&
            atomicLoad(uint32, &sock->state, Relaxed) == NS_Connecting) {
            if (w)
                netsocket_connectResult(sock, netSockConnectResult(sock->handle));
            continue;
        }

        if (r) {
            if (sock->type == NST_Datagram) {
                ingestDatagram(self, sock);
            } else {
                uint32 st = atomicLoad(uint32, &sock->state, Relaxed);
                if (st == NS_Connected)
                    ingestStream(self, sock);
                else if (st == NS_Listening)
                    acceptReady(self, sock);
            }
        }

        if (w) {
            // Writable: push out whatever is queued and, if the backlog crossed the low watermark,
            // let the flush fire NET_SendReady on the flow.
            netsocket_flushSend(sock, q);
        }
    }
}

// Begin one connect attempt. The shared readiness helper resets the handle to the address's family
// and issues a non-blocking connect(); waking the ingest thread makes it rebuild its watch set to
// include the new handle with connect (write + except) interest without waiting out the poll.
bool NetQueueSelect_connectBegin(_In_ NetQueueSelect* self, NetSocket* sock, NetAddr* addr)
{
    bool ret = netsocket_readinessConnect(sock, self, addr);

    if (self->ingest && self->selset)
        nselWake((NetSelectSet*)self->selset);

    return ret;
}

// The socket just became connected. Wake the loop so the next rebuild read-watches it and ingest
// begins. Polled mode rebuilds on every tick(), so the wake is only needed (and only attempted) in
// threaded mode.
void NetQueueSelect_connectArm(_In_ NetQueueSelect* self, NetSocket* sock)
{
    unused_noeval(sock);

    if (self->ingest && self->selset)
        nselWake((NetSelectSet*)self->selset);
}

// A socket just started listening. Wake the loop so the next rebuild read-watches it and begins
// accepting from its backlog. Polled mode rebuilds on every tick(), so the wake is only needed (and
// only attempted) in threaded mode -- the readiness rebuild is what actually adds the watch.
void NetQueueSelect_acceptArm(_In_ NetQueueSelect* self, NetSocket* sock)
{
    unused_noeval(sock);

    if (self->ingest && self->selset)
        nselWake((NetSelectSet*)self->selset);
}

bool NetQueueSelect_tick(_In_ NetQueueSelect* self, int64 wait)
{
    // Polled mode only. In threaded mode the ingest thread owns the select set and the workers own
    // dispatch, so the application drives neither and must not call this -- two threads in select()
    // on one fd_set would race the working sets and the iteration cursor.
    selectPoll(self, wait);

    // Fire the events that ingest produced, on the caller's thread -- polled mode has no worker
    // pool to pick them up asynchronously.
    bool any = false;
    while (netqueue_dispatch(self))
        any = true;

    // Polled mode has no worker to reach the idle GC point, so drive it here after draining.
    netqueue_maint(self);

    return any;
}

// Stop and join the ingest thread if one is running. nselWake breaks a blocked select() at once;
// the bounded poll interval bounds the wait even if that wake is somehow missed.
static void stopIngestThread(_Inout_ NetQueueSelect* self, int64 timeout)
{
    Thread* ing = self->ingest;
    if (!ing)
        return;

    // NULL the member before joining so addSocket/removeSocket stop trying to wake it; we hold the
    // reference locally until the OS thread has exited, then release it.
    self->ingest = NULL;
    thrRequestExit(ing);
    if (self->selset)
        nselWake((NetSelectSet*)self->selset);
    thrWait(ing, timeout > 0 ? timeout : timeForever);
    objRelease(&ing);
}

extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_addSocket(socket) NetQueue_addSocket((NetQueue*)(self), socket)
bool NetQueueSelect_addSocket(_In_ NetQueueSelect* self, NetSocket* socket)
{
    bool ret = NetQueue_addSocket(NetQueue(self), socket);

    // Wake the select loop so it rebuilds its watch set to include the new socket now, rather than
    // leaving it unserviced until the current wait times out.
    if (ret && self->ingest)
        nselWake((NetSelectSet*)self->selset);

    return ret;
}

extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_removeSocket(socket) NetQueue_removeSocket((NetQueue*)(self), socket)
bool NetQueueSelect_removeSocket(_In_ NetQueueSelect* self, NetSocket* socket)
{
    bool ret = NetQueue_removeSocket(NetQueue(self), socket);

    // Wake so the loop drops the removed socket from its set promptly; a stale handle left in the
    // fd_set until the next timeout would only be skipped anyway, but waking keeps it tidy.
    if (ret && self->ingest)
        nselWake((NetSelectSet*)self->selset);

    return ret;
}

extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);   // parent
#define parent_shutdown(timeout) NetQueue_shutdown((NetQueue*)(self), timeout)
bool NetQueueSelect_shutdown(_In_ NetQueueSelect* self, int64 timeout)
{
    // Stop the select loop before the base closes flows and releases sockets, so no packet is
    // ingested against a socket on its way out and the fd_set stops referencing them. This is what
    // "cancel outstanding I/O" amounts to for a readiness backend: there are no async operations in
    // flight, only a thread that must stop touching the sockets.
    atomicStore(uint32, &NetQueue(self)->shutdownReq, 1, Release);
    stopIngestThread(self, timeout);

    // The ingest thread is joined, so nothing else touches fdmap; drop its socket references now
    // rather than leaving them pinned until the queue is finally destroyed.
    htClear(&self->fdmap);

    return NetQueue_shutdown(NetQueue(self), timeout);
}

void NetQueueSelect_destroy(_In_ NetQueueSelect* self)
{
    // Safety net for a queue released without a shutdown: the ingest thread holds a raw pointer to
    // us, so join it before freeing the select set it waits on. A no-op after a normal shutdown.
    stopIngestThread(self, timeForever);

    NetSelectSet* sel = (NetSelectSet*)self->selset;
    nselDestroy(&sel);
    self->selset = NULL;

    // Autogen begins -----
    htDestroy(&self->fdmap);
    objRelease(&self->ingest);
    // Autogen ends -------
}

_objinit_guaranteed bool NetQueueSelect_init(_In_ NetQueueSelect* self)
{
    // Autogen begins -----
    htInit(&self->fdmap, uint64, object, 16);
    return true;
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "net/queue_select.auto.inc"
// clang-format on
// Autogen ends -------

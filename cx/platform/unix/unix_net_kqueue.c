// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/unix/unix_net_kqueue.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "unix_net.h"
#include "cx/net/net.h"
#include <cx/thread.h>
#include <cx/time/time.h>
#include <cx/time/clock.h>

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Base methods this class overrides, called through to for the shared behaviour.
extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);

static void setNonBlocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// Arm (or update) a socket's kqueue interest. Unlike NetSelectSet, which is rebuilt wholesale every
// pass, kevent() mutates the kernel's interest list directly, so there is no wake-and-rebuild step
// anywhere in this file except shutdown -- same shape as NetQueueEpoll's armInterest, but EVFILT_READ
// and EVFILT_WRITE are independent registrations rather than bits in one struct, so both filters are
// always resubmitted (ADD if wanted, DELETE if not) rather than tracked as a single combined state.
//
// nevents is passed as 0 here deliberately. A NULL timeout does not mean "return once the changes
// are applied" -- it means "block indefinitely until the eventlist can be filled", and that wait
// applies even to a change-only call as long as nevents > 0. Passing 0 is what makes this a pure,
// non-blocking registration; it also means per-change errors (e.g. ENOENT deleting a filter that
// was never armed) are not reported back, which is fine since that particular error is harmless and
// there is nothing to do about it anyway.
//
// Called from both the ingest thread and whatever thread adds/connects/closes a socket, so fdmap is
// always touched under fdmapLock rather than relying on the caller already holding one.
static void armInterest(_Inout_ NetQueueKqueue* self, _Inout_ NetSocket* sock, bool read, bool write)
{
    int fd = (int)sock->handle;
    if (fd < 0)
        return;

    withMutex (&self->fdmapLock) {
        bool exists = htFind(self->fdmap, uint64, (uint64)fd, none, NULL) != 0;

        if (!read && !write) {
            if (exists) {
                struct kevent chg[2];
                EV_SET(&chg[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                EV_SET(&chg[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
                kevent(self->kq, chg, 2, NULL, 0, NULL);
                htRemove(&self->fdmap, uint64, (uint64)fd);
            }
            break;
        }

        struct kevent chg[2];
        EV_SET(&chg[0], fd, EVFILT_READ, read ? EV_ADD : EV_DELETE, 0, 0, sock);
        EV_SET(&chg[1], fd, EVFILT_WRITE, write ? EV_ADD : EV_DELETE, 0, 0, sock);
        kevent(self->kq, chg, 2, NULL, 0, NULL);

        if (!exists)
            htInsert(&self->fdmap, uint64, (uint64)fd, object, sock);
    }
}

// Identical to NetQueueSelect's ingestDatagram(): drain readiness one recvfrom() at a time. FreeBSD
// has no recvmmsg equivalent, so unlike NetQueueEpoll there is no batching to add here -- kqueue's
// win over select is O(1) readiness reporting, not batched ingest.
static void ingestDatagram(_Inout_ NetQueueKqueue* self, _Inout_ NetSocket* sock)
{
    NetQueue* q = NetQueue(self);

    for (;;) {
        Buffer buf = bufpoolGet(&q->pool->msgbuf);
        if (!buf) {
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
            break;   // WouldBlock (drained) or a real error; nothing more this pass
        }

        buf->len = (size_t)n;
        netqueue_ingestDatagram(q, sock, &src, &buf);   // takes ownership of buf
    }
}

// Identical to the select/epoll backends' ingestStream(): drain readiness into the socket's receive
// ring, then wake its single flow so NET_DataReceived fires and the handler drains the ring via
// netsocketRecv(). Duplicated rather than shared across backends -- each one's ingest loop is
// short, and a shared helper would cost more in indirection than it saves in lines.
static void ingestStream(_Inout_ NetQueueKqueue* self, _Inout_ NetSocket* sock)
{
    NetQueue* q           = NetQueue(self);
    BufRing* ring         = &sock->bufs.stream.recv;
    size_t received       = 0;
    bool closed           = false;
    NetErrorCode closeErr = NERR_None;

    withMutex (&sock->recvLock) {
        for (;;) {
            uint8* ptr;
            size_t len;
            bufringReserve(ring, sock->mru ? sock->mru : 1500, &ptr, &len);

            NetErrorCode err;
            intptr n = netSockRecv(sock->handle, ptr, len, &err);
            if (n > 0) {
                bufringCommit(ring, (size_t)n);
                received += (size_t)n;
                if ((size_t)n < len)
                    break;
                continue;
            }

            bufringCommit(ring, 0);
            if (n == 0) {
                closed = true;
            } else if (err != NERR_WouldBlock) {
                closed   = true;
                closeErr = err;
            }
            break;
        }
    }

    if (received > 0 && sock->flow) {
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

// Identical to the select/epoll backends' acceptReady(): drain the listener's backlog until it
// would block, handing each accepted connection to the shared admission path.
static void acceptReady(_Inout_ NetQueueKqueue* self, _Inout_ NetSocket* listener)
{
    NetQueue* q = NetQueue(self);

    for (;;) {
        if (_netqueueShuttingDown(q))
            break;

        NetSocket* ns = NULL;
        NetAddr peer;
        NetErrorCode err = netPlatformAccept(listener->handle, &ns, &peer);
        if (err == NERR_WouldBlock)
            break;
        if (err != NERR_None || !ns)
            break;

        netsocket_accepted(listener, ns, &peer);
    }
}

// One kevent-wait pass: bound the wait to the nearest connect deadline, wait, and drain every ready
// socket into its flow. Does NOT dispatch -- in polled mode tick() dispatches right after, and in
// threaded mode the base workers do, woken through netqueue_submit. Shared by tick() and the ingest
// thread, exactly as epollPoll() is shared in unix_net_epoll.c.
static void kqueuePoll(_Inout_ NetQueueKqueue* self, int64 waitUs)
{
    NetQueue* q = NetQueue(self);

    // kevent() keeps the kernel's interest list in sync incrementally, so there is nothing to
    // rebuild here -- only the deadline heap to ask how long this pass may sleep.
    int64 waitFor = netqueue_pollTimeout(q, waitUs);

    struct kevent events[64];
    struct timespec ts;
    struct timespec* tsp = NULL;
    if (waitFor < timeForever) {
        ts.tv_sec  = (time_t)(waitFor / 1000000);
        ts.tv_nsec = (long)((waitFor % 1000000) * 1000);
        tsp        = &ts;
    }

    int n = kevent(self->kq, NULL, 0, events, 64, tsp);

    // Fire whatever came due. Runs even on a bare timeout (n <= 0) -- a black-holed connect never
    // signals readiness, so its timer is the only thing that ends the attempt.
    netqueue_timerSweep(q);

    if (n <= 0)
        return;

    for (int i = 0; i < n; i++) {
        int fd = (int)(intptr)events[i].ident;
        if (fd == self->wakeRead) {
            char sink[64];
            while (read(self->wakeRead, sink, sizeof(sink)) > 0)
                ;
            continue;
        }

        // Looked up and acquired under fdmapLock together: fdmap's own reference only lasts as long
        // as the entry is in the table, and a close on another thread can remove it (dropping that
        // reference) the instant the lock is released. Taking a reference of our own here is what
        // lets the rest of this iteration go on using sock safely after that -- otherwise a close
        // racing this pass can free the socket, ring and all, out from under ingestStream().
        NetSocket* sock = NULL;
        withMutex (&self->fdmapLock) {
            htelem e          = htFind(self->fdmap, uint64, (uint64)fd, none, NULL);
            NetSocket* found  = e ? (NetSocket*)hteVal(self->fdmap, object, e) : NULL;
            if (found)
                sock = objAcquire(found);
        }
        if (!sock)
            continue;

        bool r = events[i].filter == EVFILT_READ;
        bool w = events[i].filter == EVFILT_WRITE;

        if (sock->type == NST_Stream && atomicLoad(uint32, &sock->state, Relaxed) == NS_Connecting) {
            // Unlike select/epoll, FreeBSD kqueue does not reliably report a refused/failed connect
            // through EVFILT_WRITE alone -- both filters are armed while connecting (see
            // connectBegin), and either one firing means the attempt has resolved one way or the
            // other; SO_ERROR via netSockConnectResult() tells success from failure either way.
            netsocket_connectResult(sock, netSockConnectResult(sock->handle));
        } else {
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
                netsocket_flushSend(sock, q);

                // Level-triggered (no EV_CLEAR): drop the write filter once the backlog has drained,
                // or the next kevent() would spin on a writable-but-idle socket forever. The backlog
                // is read again after the disarm, not only before it, otherwise a backlog could get
                // stuck forever.
                if (!netsocket_wantWrite(sock)) {
                    armInterest(self, sock, true, false);
                    if (netsocket_wantWrite(sock))
                        armInterest(self, sock, true, true);
                }
            }
        }

        objRelease(&sock);
    }
}

static int kqueueIngestThread(Thread* thr)
{
    NetQueueKqueue* self = stvlNextPtr(&thr->args);
    if (!self)
        return 1;

    while (thrLoop(thr))
        kqueuePoll(self, timeS(1));

    return 0;
}

static void kqueueSendPump(void* ctx, NetSocket* sock)
{
    NetQueueKqueue* self = (NetQueueKqueue*)ctx;
    armInterest(self, sock, true, true);
}

// Defined below, next to the self-pipe it writes to.
static void wakeIngest(_Inout_ NetQueueKqueue* self);

// Wake hook installed on the base queue: arming a timer nearer than the deadline this pass is
// already sleeping on interrupts kevent() so it recomputes its bound. Installed in polled mode too,
// because an application thread can arm a timer while another sits in netqueueTick().
static void kqueueWake(void* ctx)
{
    wakeIngest((NetQueueKqueue*)ctx);
}

_objfactory_guaranteed NetQueueKqueue* NetQueueKqueue_create(NetQueueConfig* conf)
{
    NetQueueKqueue* self = objInstCreate(NetQueueKqueue);

    netqueue_applyConfig(self, conf);
    objInstInit(self);

    self->kq = kqueue();
    if (self->kq < 0) {
        objRelease(&self);
        return NULL;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        close(self->kq);
        objRelease(&self);
        return NULL;
    }
    setNonBlocking(fds[0]);
    setNonBlocking(fds[1]);
    self->wakeRead  = fds[0];
    self->wakeWrite = fds[1];

    struct kevent kev;
    EV_SET(&kev, self->wakeRead, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(self->kq, &kev, 1, NULL, 0, NULL);

    NetQueue(self)->wake    = kqueueWake;
    NetQueue(self)->wakeCtx = self;

    // The pump is needed to catch up on a backlog after the socket queue clears.
    NetQueue(self)->sendPump = kqueueSendPump;
    NetQueue(self)->sendCtx  = self;

    int32 nthreads = conf ? conf->nthreads : 0;
    if (nthreads > 0) {
        netqueue_startWorkers(self, nthreads);
        self->ingest = thrCreate(kqueueIngestThread, _S "CX Net Kqueue", stvar(ptr, self));

        if (!self->ingest)
            netqueue_stopWorkers(self, timeForever);
    }

    return self;
}

NetSocket* NetQueueKqueue_socket(_In_ NetQueueKqueue* self, NetSocketType type)
{
    unused_noeval(self);
    return netPlatformCreateSocket(type);
}

bool NetQueueKqueue_connectBegin(_In_ NetQueueKqueue* self, NetSocket* sock, const NetAddr* addr)
{
    NetSockHandle oldH = sock->handle;
    bool ret           = netsocket_readinessConnect(sock, self, addr);
    NetSockHandle newH = sock->handle;

    // netPlatformResetSocket() (inside the helper above) gives the socket a fresh fd per attempt and
    // closes the old one. The kernel drops a closed fd from kqueue's interest list on its own, so
    // there is nothing to kevent() DELETE, but the stale fdmap entry would otherwise pin a strong
    // reference to this socket under a handle number that will never fire again.
    if (oldH != newH && oldH != NET_INVALID_HANDLE) {
        withMutex (&self->fdmapLock)
            htRemove(&self->fdmap, uint64, (uint64)oldH);
    }

    // Watch both filters for connect completion: FreeBSD kqueue does not reliably deliver a refused
    // connect through EVFILT_WRITE alone the way select()'s writable+SO_ERROR convention does, so
    // EVFILT_READ is armed too as a second, more reliable way to observe the same completion (see
    // the dispatch check in kqueuePoll).
    if (newH != NET_INVALID_HANDLE)
        armInterest(self, sock, true, true);

    return ret;
}

void NetQueueKqueue_connectArm(_In_ NetQueueKqueue* self, NetSocket* sock)
{
    armInterest(self, sock, true, netsocket_wantWrite(sock));
}

void NetQueueKqueue_acceptArm(_In_ NetQueueKqueue* self, NetSocket* sock)
{
    armInterest(self, sock, true, false);
}

bool NetQueueKqueue_tick(_In_ NetQueueKqueue* self, int64 wait)
{
    // Polled mode only. In threaded mode the ingest thread owns the kqueue instance and the workers
    // own dispatch, so the application drives neither -- same polled-XOR-threaded contract as
    // NetQueueSelect_tick()/NetQueueEpoll_tick().
    kqueuePoll(self, wait);

    bool any = false;
    while (netqueue_dispatch(self))
        any = true;

    netqueue_maint(self);

    return any;
}

extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_addSocket(socket) NetQueue_addSocket((NetQueue*)(self), socket)
bool NetQueueKqueue_addSocket(_In_ NetQueueKqueue* self, NetSocket* socket)
{
    bool ret = NetQueue_addSocket(NetQueue(self), socket);
    if (!ret)
        return ret;

    bool read = false, write = false;
    uint32 st = atomicLoad(uint32, &socket->state, Relaxed);
    if (socket->type == NST_Datagram) {
        read  = true;
        write = netsocket_wantWrite(socket);
    } else if (st == NS_Connected) {
        read  = true;
        write = netsocket_wantWrite(socket);
    } else if (st == NS_Listening) {
        read = true;
    } else if (st == NS_Connecting) {
        write = true;
    }
    // NS_Init / NS_Resolving: nothing to watch yet -- connectBegin/connectArm/acceptArm arm it once
    // the socket reaches a state worth watching.

    if (read || write)
        armInterest(self, socket, read, write);

    return ret;
}

extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_removeSocket(socket) NetQueue_removeSocket((NetQueue*)(self), socket)
bool NetQueueKqueue_removeSocket(_In_ NetQueueKqueue* self, NetSocket* socket)
{
    NetSockHandle h = socket->handle;

    bool ret = NetQueue_removeSocket(NetQueue(self), socket);

    if (ret && h != NET_INVALID_HANDLE) {
        withMutex (&self->fdmapLock) {
            struct kevent chg[2];
            EV_SET(&chg[0], (int)h, EVFILT_READ, EV_DELETE, 0, 0, NULL);
            EV_SET(&chg[1], (int)h, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            kevent(self->kq, chg, 2, NULL, 0, NULL);   // nevents=0: register only, see armInterest
            htRemove(&self->fdmap, uint64, (uint64)h);
        }
    }

    return ret;
}

static void wakeIngest(_Inout_ NetQueueKqueue* self)
{
    if (self->wakeWrite >= 0) {
        // Non-blocking write; a full pipe just means a wake is already pending, which is the
        // coalescing behavior we want, so the result is deliberately not checked.
        char b        = 1;
        ssize_t wrote = write(self->wakeWrite, &b, 1);
        unused_noeval(wrote);
    }
}

static void stopIngestThread(_Inout_ NetQueueKqueue* self, int64 timeout)
{
    Thread* ing = self->ingest;
    if (!ing)
        return;

    self->ingest = NULL;
    thrRequestExit(ing);
    wakeIngest(self);
    thrWait(ing, timeout > 0 ? timeout : timeForever);
    objRelease(&ing);
}

extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);   // parent
#define parent_shutdown(timeout) NetQueue_shutdown((NetQueue*)(self), timeout)
bool NetQueueKqueue_shutdown(_In_ NetQueueKqueue* self, int64 timeout)
{
    atomicStore(uint32, &NetQueue(self)->shutdownReq, 1, Release);
    stopIngestThread(self, timeout);

    htClear(&self->fdmap);

    return NetQueue_shutdown(NetQueue(self), timeout);
}

void NetQueueKqueue_destroy(_In_ NetQueueKqueue* self)
{
    stopIngestThread(self, timeForever);

    if (self->wakeRead >= 0)
        close(self->wakeRead);
    if (self->wakeWrite >= 0)
        close(self->wakeWrite);
    if (self->kq >= 0)
        close(self->kq);

    // Autogen begins -----
    htDestroy(&self->fdmap);
    mutexDestroy(&self->fdmapLock);
    objRelease(&self->ingest);
    // Autogen ends -------
}

_objinit_guaranteed bool NetQueueKqueue_init(_In_ NetQueueKqueue* self)
{
    self->kq        = -1;
    self->wakeRead  = -1;
    self->wakeWrite = -1;

    // Autogen begins -----
    htInit(&self->fdmap, uint64, object, 16);
    mutexInit(&self->fdmapLock);
    return true;
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "platform/unix/unix_net_kqueue.auto.inc"
// clang-format on
// Autogen ends -------

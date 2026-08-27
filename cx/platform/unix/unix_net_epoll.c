// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/unix/unix_net_epoll.h"
// clang-format on
// ==================== Auto-generated section ends ======================
// recvmmsg() is a glibc extension gated on _GNU_SOURCE, defined for this one translation unit via
// CMakeLists.txt (set_source_files_properties) rather than #define here -- the auto-generated
// block above already includes system/cx headers before any hand-written line in this file could
// run, so a #define here would be too late.
#include "unix_net.h"
#include "cx/net/net.h"
#include <cx/thread.h>
#include <cx/time/time.h>
#include <cx/time/clock.h>

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// How many datagrams one recvmmsg() call tries to drain from the hot UDP socket in a single batch.
// Like the pool-size defaults in NetQueueConfig, this number wants a flood test to tune properly;
// 32 is a reasonable starting point that amortizes the syscall without holding an oversized burst
// of pooled buffers off to one side at once.
#define NET_EPOLL_MMSG_BATCH 32

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

// Arm (or update) a socket's epoll interest. Unlike NetSelectSet, which is rebuilt wholesale every
// pass, epoll_ctl mutates the kernel's interest list directly and takes effect immediately -- even
// against a concurrently blocked epoll_wait() on the same epfd -- so there is no wake-and-rebuild
// step anywhere in this file except shutdown. fdmap tracks which handles are currently registered
// (ADD vs MOD) and holds the strong reference that keeps a socket alive while epoll can still report
// it ready. Called from both the ingest thread and whatever thread adds/connects/closes a socket, so
// fdmap is always touched under fdmapLock rather than relying on the caller already holding one.
static void armInterest(_Inout_ NetQueueEpoll* self, _Inout_ NetSocket* sock, bool read, bool write)
{
    int fd = (int)sock->handle;
    if (fd < 0)
        return;

    withMutex (&self->fdmapLock) {
        bool exists = htFind(self->fdmap, uint64, (uint64)fd, none, NULL) != 0;

        if (!read && !write) {
            if (exists) {
                epoll_ctl(self->epfd, EPOLL_CTL_DEL, fd, NULL);
                htRemove(&self->fdmap, uint64, (uint64)fd);
            }
            break;
        }

        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events  = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
        ev.data.fd = fd;

        if (exists) {
            epoll_ctl(self->epfd, EPOLL_CTL_MOD, fd, &ev);
        } else {
            if (epoll_ctl(self->epfd, EPOLL_CTL_ADD, fd, &ev) == 0)
                htInsert(&self->fdmap, uint64, (uint64)fd, object, sock);
        }
    }
}

// Drain everything currently readable on a datagram socket into pooled buffers via recvmmsg,
// handing each received datagram to the core for demux and dispatch exactly as the select
// backend's per-packet recvfrom loop does. The batching is the only thing that differs.
static void ingestDatagramBatch(_Inout_ NetQueueEpoll* self, _Inout_ NetSocket* sock)
{
    NetQueue* q = NetQueue(self);

    for (;;) {
        Buffer bufs[NET_EPOLL_MMSG_BATCH];
        struct iovec iov[NET_EPOLL_MMSG_BATCH];
        struct sockaddr_storage addrs[NET_EPOLL_MMSG_BATCH];
        struct mmsghdr msgs[NET_EPOLL_MMSG_BATCH];
        unsigned got = 0;

        for (; got < NET_EPOLL_MMSG_BATCH; got++) {
            Buffer b = bufpoolGet(&q->pool->msgbuf);
            if (!b)
                break;
            bufs[got]          = b;
            iov[got].iov_base  = b->data;
            iov[got].iov_len   = b->sz;
            memset(&msgs[got], 0, sizeof(msgs[got]));
            msgs[got].msg_hdr.msg_iov     = &iov[got];
            msgs[got].msg_hdr.msg_iovlen  = 1;
            msgs[got].msg_hdr.msg_name    = &addrs[got];
            msgs[got].msg_hdr.msg_namelen = sizeof(addrs[got]);
        }

        if (got == 0) {
            // Pool exhausted: consume one datagram into a scratch and drop it, so readiness clears
            // and the loop cannot spin on the same packet, and count it -- same policy as the
            // select backend's ingestDatagram().
            uint8 scratch[2048];
            NetAddr src;
            NetErrorCode err;
            intptr n = netSockRecvFrom(sock->handle, scratch, sizeof(scratch), &src, &err);
            if (n < 0 && err == NERR_WouldBlock)
                return;
            atomicFetchAdd(uint32, &q->droppedNoBuf, 1, Relaxed);
            continue;
        }

        int n = recvmmsg((int)sock->handle, msgs, got, 0, NULL);
        if (n <= 0) {
            // WouldBlock (drained) or a real error this pass; return every buffer reserved above.
            for (unsigned i = 0; i < got; i++) bufpoolPut(&q->pool->msgbuf, &bufs[i]);
            return;
        }

        for (int i = 0; i < n; i++) {
            NetAddr src;
            netAddrFromSockaddr(&src, (struct sockaddr*)&addrs[i]);
            bufs[i]->len = msgs[i].msg_len;
            netqueue_ingestDatagram(q, sock, &src, &bufs[i]);   // takes ownership of bufs[i]
        }
        for (unsigned i = (unsigned)n; i < got; i++) bufpoolPut(&q->pool->msgbuf, &bufs[i]);

        if ((unsigned)n < got)
            return;   // drained fewer than requested: nothing more waiting this readiness
    }
}

// Identical to the select backend's ingestStream(): drain readiness into the socket's receive
// ring, then wake its single flow so NET_DataReceived fires and the handler drains the ring via
// netsocketRecv(). Duplicated rather than shared with the select backend -- each backend's ingest
// loop is short, and pulling it out into a shared helper would cost more in indirection than it
// saves in lines.
static void ingestStream(_Inout_ NetQueueEpoll* self, _Inout_ NetSocket* sock)
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

// Identical to the select backend's acceptReady(): drain the listener's backlog until it would
// block, handing each accepted connection to the shared admission path.
static void acceptReady(_Inout_ NetQueueEpoll* self, _Inout_ NetSocket* listener)
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

// One epoll_wait pass: bound the wait to the nearest connect deadline, wait, and drain every ready
// socket into its flow. Does NOT dispatch -- in polled mode tick() dispatches right after, and in
// threaded mode the base workers do, woken through netqueue_submit. Shared by tick() and the ingest
// thread, exactly as selectPoll() is shared in queue_select.c.
static void epollPoll(_Inout_ NetQueueEpoll* self, int64 waitUs)
{
    NetQueue* q = NetQueue(self);

    // epoll_ctl already keeps the kernel's interest list in sync incrementally (unlike select,
    // which rebuilds its whole watch set every pass), so there is nothing to rebuild here -- only
    // the deadline heap to ask how long this pass may sleep.
    struct epoll_event events[64];
    int n = epoll_wait(self->epfd, events, 64, (int)netqueue_pollTimeoutMsec(q, waitUs));

    // Fire whatever came due. Runs even on a bare timeout (n <= 0) -- a black-holed connect never
    // signals readiness, so its timer is the only thing that ends the attempt.
    netqueue_timerSweep(q);

    if (n <= 0)
        return;

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
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
            htelem e  = htFind(self->fdmap, uint64, (uint64)fd, none, NULL);
            NetSocket* found = e ? (NetSocket*)hteVal(self->fdmap, object, e) : NULL;
            if (found)
                sock = objAcquire(found);
        }
        if (!sock)
            continue;

        bool r = (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0;
        bool w = (events[i].events & (EPOLLOUT | EPOLLERR)) != 0;

        if (sock->type == NST_Stream && atomicLoad(uint32, &sock->state, Relaxed) == NS_Connecting) {
            if (w)
                netsocket_connectResult(sock, netSockConnectResult(sock->handle));
        } else {
            if (r) {
                if (sock->type == NST_Datagram) {
                    ingestDatagramBatch(self, sock);
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
                // Level-triggered: drop EPOLLOUT once the backlog has drained, or epoll_wait would
                // spin on a writable-but-idle socket forever.
                armInterest(self, sock, true, netsocket_wantWrite(sock));
            }
        }

        objRelease(&sock);
    }
}

static int epollIngestThread(Thread* thr)
{
    NetQueueEpoll* self = stvlNextPtr(&thr->args);
    if (!self)
        return 1;

    while (thrLoop(thr))
        epollPoll(self, timeS(1));

    return 0;
}

static void epollSendPump(void* ctx, NetSocket* sock)
{
    NetQueueEpoll* self = (NetQueueEpoll*)ctx;
    armInterest(self, sock, true, true);
}

// Defined below, next to the self-pipe it writes to.
static void wakeIngest(_Inout_ NetQueueEpoll* self);

// Wake hook installed on the base queue: arming a timer nearer than the deadline this pass is
// already sleeping on interrupts epoll_wait so it recomputes its bound. Installed in polled mode
// too, because an application thread can arm a timer while another sits in netqueueTick().
static void epollWake(void* ctx)
{
    wakeIngest((NetQueueEpoll*)ctx);
}

_objfactory_guaranteed NetQueueEpoll* NetQueueEpoll_create(NetQueueConfig* conf)
{
    NetQueueEpoll* self = objInstCreate(NetQueueEpoll);

    netqueue_applyConfig(self, conf);
    objInstInit(self);

    self->epfd = epoll_create1(0);
    if (self->epfd < 0) {
        objRelease(&self);
        return NULL;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        close(self->epfd);
        objRelease(&self);
        return NULL;
    }
    setNonBlocking(fds[0]);
    setNonBlocking(fds[1]);
    self->wakeRead  = fds[0];
    self->wakeWrite = fds[1];

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = self->wakeRead;
    epoll_ctl(self->epfd, EPOLL_CTL_ADD, self->wakeRead, &ev);

    NetQueue(self)->wake    = epollWake;
    NetQueue(self)->wakeCtx = self;

    int32 nthreads = conf ? conf->nthreads : 0;
    if (nthreads > 0) {
        netqueue_startWorkers(self, nthreads);
        self->ingest = thrCreate(epollIngestThread, _S "CX Net Epoll", stvar(ptr, self));

        if (!self->ingest) {
            netqueue_stopWorkers(self, timeForever);
        } else {
            NetQueue(self)->sendPump = epollSendPump;
            NetQueue(self)->sendCtx  = self;
        }
    }

    return self;
}

NetSocket* NetQueueEpoll_socket(_In_ NetQueueEpoll* self, NetSocketType type)
{
    unused_noeval(self);
    return netPlatformCreateSocket(type);
}

bool NetQueueEpoll_connectBegin(_In_ NetQueueEpoll* self, NetSocket* sock, NetAddr* addr)
{
    NetSockHandle oldH = sock->handle;
    bool ret           = netsocket_readinessConnect(sock, self, addr);
    NetSockHandle newH = sock->handle;

    // netPlatformResetSocket() (inside the helper above) gives the socket a fresh fd per attempt
    // and closes the old one. The kernel drops a closed fd from epoll's interest list on its own,
    // so there is nothing to epoll_ctl DEL, but the stale fdmap entry would otherwise pin a strong
    // reference to this socket under a handle number that will never fire again.
    if (oldH != newH && oldH != NET_INVALID_HANDLE) {
        withMutex (&self->fdmapLock)
            htRemove(&self->fdmap, uint64, (uint64)oldH);
    }

    if (newH != NET_INVALID_HANDLE)
        armInterest(self, sock, false, true);   // watch the fresh handle for connect completion

    return ret;
}

void NetQueueEpoll_connectArm(_In_ NetQueueEpoll* self, NetSocket* sock)
{
    armInterest(self, sock, true, netsocket_wantWrite(sock));
}

void NetQueueEpoll_acceptArm(_In_ NetQueueEpoll* self, NetSocket* sock)
{
    armInterest(self, sock, true, false);
}

bool NetQueueEpoll_tick(_In_ NetQueueEpoll* self, int64 wait)
{
    // Polled mode only. In threaded mode the ingest thread owns the epoll instance and the
    // workers own dispatch, so the application drives neither -- same polled-XOR-threaded contract
    // as NetQueueSelect_tick().
    epollPoll(self, wait);

    bool any = false;
    while (netqueue_dispatch(self))
        any = true;

    netqueue_maint(self);

    return any;
}

extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_addSocket(socket) NetQueue_addSocket((NetQueue*)(self), socket)
bool NetQueueEpoll_addSocket(_In_ NetQueueEpoll* self, NetSocket* socket)
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
    // NS_Init / NS_Resolving: nothing to watch yet -- connectBegin/connectArm/acceptArm arm it
    // once the socket reaches a state worth watching.

    if (read || write)
        armInterest(self, socket, read, write);

    return ret;
}

extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_removeSocket(socket) NetQueue_removeSocket((NetQueue*)(self), socket)
bool NetQueueEpoll_removeSocket(_In_ NetQueueEpoll* self, NetSocket* socket)
{
    NetSockHandle h = socket->handle;

    bool ret = NetQueue_removeSocket(NetQueue(self), socket);

    if (ret && h != NET_INVALID_HANDLE) {
        withMutex (&self->fdmapLock) {
            epoll_ctl(self->epfd, EPOLL_CTL_DEL, (int)h, NULL);
            htRemove(&self->fdmap, uint64, (uint64)h);
        }
    }

    return ret;
}

static void wakeIngest(_Inout_ NetQueueEpoll* self)
{
    if (self->wakeWrite >= 0) {
        // Non-blocking write; a full pipe just means a wake is already pending, which is the
        // coalescing behavior we want, so the result is deliberately not checked.
        char b        = 1;
        ssize_t wrote = write(self->wakeWrite, &b, 1);
        unused_noeval(wrote);
    }
}

static void stopIngestThread(_Inout_ NetQueueEpoll* self, int64 timeout)
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
bool NetQueueEpoll_shutdown(_In_ NetQueueEpoll* self, int64 timeout)
{
    atomicStore(uint32, &NetQueue(self)->shutdownReq, 1, Release);
    stopIngestThread(self, timeout);

    htClear(&self->fdmap);

    return NetQueue_shutdown(NetQueue(self), timeout);
}

void NetQueueEpoll_destroy(_In_ NetQueueEpoll* self)
{
    stopIngestThread(self, timeForever);

    if (self->wakeRead >= 0)
        close(self->wakeRead);
    if (self->wakeWrite >= 0)
        close(self->wakeWrite);
    if (self->epfd >= 0)
        close(self->epfd);

    // Autogen begins -----
    htDestroy(&self->fdmap);
    mutexDestroy(&self->fdmapLock);
    objRelease(&self->ingest);
    // Autogen ends -------
}

_objinit_guaranteed bool NetQueueEpoll_init(_In_ NetQueueEpoll* self)
{
    self->epfd     = -1;
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
#include "platform/unix/unix_net_epoll.auto.inc"
// clang-format on
// Autogen ends -------

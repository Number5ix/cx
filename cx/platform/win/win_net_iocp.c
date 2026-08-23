// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/win/win_net_iocp.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "win_net.h"
#include "cx/net/net.h"
#include "platform/win/win_net_socket.h"   // netsocketwinWrap, to wrap an AcceptEx'd handle
#include <cx/thread.h>
#include <cx/time/time.h>
#include <cx/time/clock.h>
#include <mswsock.h>   // LPFN_CONNECTEX/LPFN_ACCEPTEX, WSAID_*, SO_UPDATE_{CONNECT,ACCEPT}_CONTEXT

// Base methods this class overrides, called through to for the shared behaviour. Declared up front
// because addSocket() backs a failed association out through removeSocket().
extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);
extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);

// Completion key posted to wake a completion thread so it exits. A real socket completion carries
// the socket pointer as its key, which can never collide with this sentinel.
#define IOCP_KEY_EXIT ((ULONG_PTR)~(uintptr_t)0)

// Posted by iocpWake() with a NULL OVERLAPPED purely to interrupt a parked wait so it recomputes
// its timer bound. Both wait loops fall through it without doing anything else.
#define IOCP_KEY_WAKE ((ULONG_PTR)~(uintptr_t)1)

// Stream send segments gathered into a single overlapped WSASend. The send buffer's segments are
// 64 KiB, so this batches up to a megabyte per op before it must repost -- more than enough that
// gathering, not op count, is what bounds a large flush.
#define IOCP_SEND_IOV 16

// Overlapped AcceptEx operations to keep outstanding on a listening socket, so several incoming
// connections can complete in parallel without a caller in the loop. Each completion reposts one to
// keep the backlog serviced, exactly as datagram receives keep themselves topped up.
#define IOCP_ACCEPTS 4

// Address buffer length AcceptEx requires for each of the local and remote endpoints: the largest
// sockaddr plus 16 bytes, per the API contract. The op carries room for both.
#define IOCP_ACCEPT_ADDRLEN (sizeof(struct sockaddr_storage) + 16)

// One outstanding overlapped operation. OVERLAPPED must be first: a completion hands back only the
// OVERLAPPED pointer, and CONTAINING_RECORD walks back to the enclosing op from it.
typedef enum {
    IocpRecvFrom = 1,   // datagram receive into a pooled buffer
    IocpRecv     = 2,   // stream receive into the socket's ring reservation
    IocpSend     = 3,   // stream send: gathered segments of the socket's send chain
    IocpSendTo   = 4,   // datagram send: one queued NetMessage to its destination
    IocpConnect  = 5,   // overlapped ConnectEx for one connect attempt
    IocpAccept   = 6,   // overlapped AcceptEx for one incoming connection
} IocpOpType;

typedef struct IocpOp {
    OVERLAPPED ov;
    IocpOpType type;
    NetSocket*  sock;   // strong ref held for the life of the operation
    Buffer      buf;    // datagram recv: pooled buffer being filled; NULL otherwise
    WSABUF      wsabuf;
    DWORD       flags;  // WSARecv/WSARecvFrom in/out flags
    struct sockaddr_storage from;   // datagram recv: source; datagram send: destination
    INT         fromlen;

    // Send-only fields (unused by receives).
    NetMessage* sendMsg;               // datagram send: the message this op owns until completion
    size_t      sendBytes;             // bytes this send op is carrying (to skip/account on done)
    WSABUF      sendbufs[IOCP_SEND_IOV];
    DWORD       sendnbufs;

    // Connect-only: the attempt generation this op belongs to. A completion whose generation no
    // longer matches the socket's is a superseded attempt (timed out and failed over to a fresh
    // handle whose ConnectEx was cancelled) and only cleans itself up.
    uint32      connectGen;

    // Accept-only: the pre-created socket AcceptEx fills with the incoming connection (INVALID_SOCKET
    // once its ownership has moved into the wrapped NetSocket), and the address buffer AcceptEx
    // requires for the local+remote endpoints.
    SOCKET      acceptSock;
    char        acceptBuf[IOCP_ACCEPT_ADDRLEN * 2];
} IocpOp;

static bool postRecvFrom(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock);
static bool postRecv(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock);
static void postSendLocked(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock,
                           _Out_ NetErrorCode* err, _Out_ NetAddr* errAddr);
static bool postAccept(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* listener);

// Fetch (and cache) the ConnectEx extension pointer. It is a Winsock extension obtained at runtime
// via WSAIoctl on any socket rather than linked, so it is looked up once and stored on the queue.
static LPFN_CONNECTEX getConnectEx(_Inout_ NetQueueWinIOCP* self, SOCKET s)
{
    if (self->connectEx)
        return (LPFN_CONNECTEX)self->connectEx;

    GUID guid          = WSAID_CONNECTEX;
    LPFN_CONNECTEX fn  = NULL;
    DWORD nb           = 0;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn, sizeof(fn), &nb,
                 NULL, NULL) == 0) {
        // A benign race: two threads may load it at once and store the same pointer.
        self->connectEx = (void*)fn;
    }
    return (LPFN_CONNECTEX)self->connectEx;
}

// Fetch (and cache) the AcceptEx extension pointer, the accept-side counterpart of getConnectEx.
// Same one-time WSAIoctl lookup, cached on the queue so every listener reuses it.
static LPFN_ACCEPTEX getAcceptEx(_Inout_ NetQueueWinIOCP* self, SOCKET s)
{
    if (self->acceptEx)
        return (LPFN_ACCEPTEX)self->acceptEx;

    GUID guid         = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX fn  = NULL;
    DWORD nb          = 0;
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn, sizeof(fn), &nb,
                 NULL, NULL) == 0) {
        self->acceptEx = (void*)fn;
    }
    return (LPFN_ACCEPTEX)self->acceptEx;
}

// Map the system error a failed overlapped op completes with to a NetErrorCode. A completion
// reports a Win32/NTSTATUS-derived code rather than the WSAE* value the synchronous call would, so
// the common ones are translated here; the two spaces only partially overlap, and anything not
// recognized falls back to the shared WSAE* mapper.
static NetErrorCode mapCompletionError(DWORD err)
{
    switch (err) {
    case ERROR_CONNECTION_REFUSED:
        return NERR_ConnectionRefused;
    case ERROR_PORT_UNREACHABLE:   // ICMP port unreachable surfaced on a datagram op
        return NERR_ConnectionRefused;
    case ERROR_NETNAME_DELETED:    // the NT spelling of a connection reset
    case ERROR_CONNECTION_ABORTED:
        return NERR_ConnectionReset;
    case ERROR_NETWORK_UNREACHABLE:
        return NERR_NetworkUnreachable;
    case ERROR_HOST_UNREACHABLE:
        return NERR_HostUnreachable;
    case ERROR_SEM_TIMEOUT:
        return NERR_Timeout;
    default:
        return _netMapWsaError((int)err);
    }
}

// Connect flavor: anything a failed ConnectEx completes with that the mapping does not recognize is
// reported as refused, the common cause.
static NetErrorCode mapConnectError(DWORD err)
{
    NetErrorCode e = mapCompletionError(err);
    return e == NERR_Unknown ? NERR_ConnectionRefused : e;
}

// ---------------------------------------------------------------------------------------------
// Posting receives
// ---------------------------------------------------------------------------------------------

// Post one WSARecvFrom against a fresh pooled buffer. Returns false without posting when the pool is
// dry (counted as a drop -- a later completion that frees a buffer reposts and the outstanding set
// refills as the pool recovers) or the queue is shutting down.
static bool postRecvFrom(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock)
{
    NetQueue* q = NetQueue(self);
    if (_netqueueShuttingDown(q))
        return false;

    Buffer buf = bufpoolGet(&q->pool->msgbuf);
    if (!buf) {
        atomicFetchAdd(uint32, &q->droppedNoBuf, 1, Relaxed);
        return false;
    }

    IocpOp* op     = xaAlloc(sizeof(IocpOp), XA_Zero);
    op->type       = IocpRecvFrom;
    op->sock       = objAcquire(sock);
    op->buf        = buf;
    op->wsabuf.buf = (CHAR*)buf->data;
    op->wsabuf.len = (ULONG)min(buf->sz, (size_t)ULONG_MAX);
    op->fromlen    = (INT)sizeof(op->from);

    atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

    int rc = WSARecvFrom((SOCKET)sock->handle, &op->wsabuf, 1, NULL, &op->flags,
                         (struct sockaddr*)&op->from, &op->fromlen, &op->ov, NULL);
    // A return of 0 (immediate completion) still queues a completion packet, because the socket is
    // associated with the port and FILE_SKIP_COMPLETION_PORT_ON_SUCCESS is not set. Only a real
    // synchronous error means no completion will arrive, so unwind here in that case alone.
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        atomicFetchSub(uint32, &self->iops, 1, AcqRel);
        bufpoolPut(&q->pool->msgbuf, &op->buf);
        objRelease(&op->sock);
        xaFree(op);
        return false;
    }
    return true;
}

// Post the single WSARecv for a connected stream socket. Reserves contiguous ring space now and
// commits the real byte count on completion -- exactly one recv is ever outstanding per stream
// socket, so only one reservation is live at a time.
static bool postRecv(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock)
{
    NetQueue* q = NetQueue(self);
    if (_netqueueShuttingDown(q))
        return false;

    IocpOp* op = xaAlloc(sizeof(IocpOp), XA_Zero);
    op->type   = IocpRecv;
    op->sock   = objAcquire(sock);

    uint8* ptr;
    size_t len;
    withMutex (&sock->recvLock) {
        bufringReserve(&sock->bufs.stream.recv, sock->mru ? sock->mru : 1500, &ptr, &len);
    }
    op->wsabuf.buf = (CHAR*)ptr;
    op->wsabuf.len = (ULONG)min(len, (size_t)ULONG_MAX);

    atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

    int rc = WSARecv((SOCKET)sock->handle, &op->wsabuf, 1, NULL, &op->flags, &op->ov, NULL);
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        atomicFetchSub(uint32, &self->iops, 1, AcqRel);
        // Release the reservation we took but will never fill.
        withMutex (&sock->recvLock) {
            bufringCommit(&sock->bufs.stream.recv, 0);
        }
        objRelease(&op->sock);
        xaFree(op);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Posting accepts
// ---------------------------------------------------------------------------------------------

// Post one overlapped AcceptEx against a fresh, pre-created socket of the listener's family. The
// completion wraps that socket as the accepted connection; the op is counted in iops so shutdown's
// cancel-and-drain reclaims it (returning the pre-created socket) like any other. Returns false
// without posting when the queue is shutting down, the extension pointer is unavailable, or the
// socket could not be created.
static bool postAccept(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* listener)
{
    NetQueue* q = NetQueue(self);
    if (_netqueueShuttingDown(q))
        return false;

    SOCKET ls               = (SOCKET)listener->handle;
    LPFN_ACCEPTEX acceptExFn = getAcceptEx(self, ls);
    if (!acceptExFn)
        return false;

    // AcceptEx needs the accept socket created up front, and it must match the listener's family.
    struct sockaddr_storage la;
    int lalen = sizeof(la);
    int af    = AF_INET;
    if (getsockname(ls, (struct sockaddr*)&la, &lalen) == 0)
        af = la.ss_family;

    SOCKET as = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (as == INVALID_SOCKET)
        return false;

    IocpOp* op     = xaAlloc(sizeof(IocpOp), XA_Zero);
    op->type       = IocpAccept;
    op->sock       = objAcquire(listener);
    op->acceptSock = as;

    atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

    // dwReceiveDataLength 0: complete as soon as the connection is established, without waiting for
    // the first bytes -- the stream is then serviced by the normal recv path once accepted.
    DWORD recvd = 0;
    BOOL ok     = acceptExFn(ls, as, op->acceptBuf, 0, (DWORD)IOCP_ACCEPT_ADDRLEN,
                         (DWORD)IOCP_ACCEPT_ADDRLEN, &recvd, &op->ov);
    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
        atomicFetchSub(uint32, &self->iops, 1, AcqRel);
        closesocket(as);
        objRelease(&op->sock);
        xaFree(op);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Posting sends
//
// There is no writability signal on a completion port, so queued outbound data is drained by
// posting an overlapped send and continuing on its completion. Exactly one send op is outstanding
// per socket at a time (sendPending), which for a stream is required -- two would interleave and
// corrupt the byte stream -- and for a datagram simply keeps the code one shape.
// ---------------------------------------------------------------------------------------------

// Post one overlapped send for whatever this socket has queued, if nothing is already in flight.
// The caller holds sock->sendLock. A synchronous failure lands in `err` (with the dropped
// datagram's destination in `errAddr`) for the caller to hand to netsocket_sendError() once the
// lock is dropped -- that reports NET_Error and closes a broken stream's flow.
static void postSendLocked(_Inout_ NetQueueWinIOCP* self, _Inout_ NetSocket* sock,
                           _Out_ NetErrorCode* err, _Out_ NetAddr* errAddr)
{
    NetQueue* q = NetQueue(self);
    *err = NERR_None;
    memset(errAddr, 0, sizeof(NetAddr));
    if (sock->sendPending || _netqueueShuttingDown(q))
        return;

    if (sock->type == NST_Stream) {
        if (sock->bufs.stream.send.total == 0)
            return;

        IocpOp* op = xaAlloc(sizeof(IocpOp), XA_Zero);
        op->type   = IocpSend;
        op->sock   = objAcquire(sock);

        BufIov iov[IOCP_SEND_IOV];
        size_t niov = 0;
        // Pointers into the chain's own segments; valid until the bytes are skipped, which does not
        // happen until this op completes.
        op->sendBytes = bufchainGatherIov(&sock->bufs.stream.send, iov, IOCP_SEND_IOV, &niov);
        if (niov == 0) {   // nothing gatherable after all
            objRelease(&op->sock);
            xaFree(op);
            return;
        }
        for (size_t i = 0; i < niov; i++) {
            op->sendbufs[i].buf = (CHAR*)iov[i].data;
            op->sendbufs[i].len = (ULONG)min(iov[i].len, (size_t)ULONG_MAX);
        }
        op->sendnbufs = (DWORD)niov;

        sock->sendPending = true;
        atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

        int rc = WSASend((SOCKET)sock->handle, op->sendbufs, op->sendnbufs, NULL, 0, &op->ov, NULL);
        int we = rc == SOCKET_ERROR ? WSAGetLastError() : 0;
        if (rc == SOCKET_ERROR && we != WSA_IO_PENDING) {
            atomicFetchSub(uint32, &self->iops, 1, AcqRel);
            sock->sendPending = false;
            objRelease(&op->sock);
            xaFree(op);
            if (!_netqueueShuttingDown(q))
                *err = _netMapWsaError(we);
        }
    } else {   // datagram
        NetMessage* m = (NetMessage*)prqPop(&sock->bufs.dgram.send);
        if (!m)
            return;
        size_t len = m->buf ? m->buf->len : 0;
        sock->sendQueued -= len;

        IocpOp* op    = xaAlloc(sizeof(IocpOp), XA_Zero);
        op->type      = IocpSendTo;
        op->sock      = objAcquire(sock);
        op->sendMsg   = m;   // owned until completion
        op->sendBytes = len;
        op->sendbufs[0].buf = (CHAR*)(m->buf ? m->buf->data : NULL);
        op->sendbufs[0].len = (ULONG)min(len, (size_t)ULONG_MAX);
        op->sendnbufs = 1;

        int sasz = 0;
        // Reuse the op's sockaddr storage (a recv would hold the source here) for the destination.
        netAddrToSockaddr(&m->addr, &op->from, &sasz);

        sock->sendPending = true;
        atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

        int rc = WSASendTo((SOCKET)sock->handle, op->sendbufs, 1, NULL, 0,
                           (struct sockaddr*)&op->from, sasz, &op->ov, NULL);
        int we = rc == SOCKET_ERROR ? WSAGetLastError() : 0;
        if (rc == SOCKET_ERROR && we != WSA_IO_PENDING) {
            atomicFetchSub(uint32, &self->iops, 1, AcqRel);
            sock->sendPending = false;
            if (!_netqueueShuttingDown(q)) {
                *err     = _netMapWsaError(we);
                *errAddr = op->sendMsg->addr;
            }
            netpoolFreeMsg(q->pool, &op->sendMsg);   // drop this datagram on a fatal error
            objRelease(&op->sock);
            xaFree(op);
        }
    }
}

// Send-pump hook installed on the base queue. netsocketSend() calls it after its own synchronous
// flush left data queued; with no writability signal, the way forward is to post an overlapped send.
static void iocpSendPump(void* ctx, NetSocket* sock)
{
    NetQueueWinIOCP* self = (NetQueueWinIOCP*)ctx;
    NetErrorCode err      = NERR_None;
    NetAddr eaddr;
    withMutex (&sock->sendLock) {
        postSendLocked(self, sock, &err, &eaddr);
    }
    if (err != NERR_None)
        netsocket_sendError(sock, self, err, &eaddr);
}

// Wake hook installed on the base queue: arming a timer nearer than the deadline a completion
// thread is already sleeping on posts a bare completion so it recomputes its bound. A NULL
// OVERLAPPED with an ordinary key is ignored by both wait loops, which is exactly what a wake is.
// Installed in polled mode too, because an application thread can arm a timer while another sits
// in netqueueTick().
static void iocpWake(void* ctx)
{
    NetQueueWinIOCP* self = (NetQueueWinIOCP*)ctx;
    if (self->iocp)
        PostQueuedCompletionStatus((HANDLE)self->iocp, 0, IOCP_KEY_WAKE, NULL);
}

// Cap a wait (milliseconds, negative = infinite) to the nearest armed timer deadline. A wait of
// exactly 0 is an explicit non-blocking pull and is never stretched -- tick() uses it to drain a
// burst after its first blocking wait, and turning that into a 1ms sleep per completion would make
// a busy tick crawl.
static DWORD iocpWaitMs(NetQueue* q, int64 waitMs)
{
    if (waitMs == 0)
        return 0;

    int64 dl = netqueue_nextDeadline(q);
    if (dl != 0) {
        int64 usLeft = dl - clockTimer();
        int64 msLeft = usLeft <= 0 ? 0 : usLeft / 1000;
        if (waitMs < 0 || msLeft < waitMs)
            waitMs = msLeft;
        if (waitMs < 1)
            waitMs = 1;
    }

    return waitMs >= 0 ? (DWORD)min(waitMs, (int64)0x7fffffff) : INFINITE;
}

// ---------------------------------------------------------------------------------------------
// Completion handling
// ---------------------------------------------------------------------------------------------

// Handle one dequeued completion. `ok` is the GetQueuedCompletionStatus result; `err` is
// GetLastError() when it failed (ERROR_OPERATION_ABORTED distinguishes an op we cancelled from a
// genuine socket error). Always retires the op and decrements the outstanding count.
static void handleCompletion(_Inout_ NetQueueWinIOCP* self, _Inout_ OVERLAPPED* ov, DWORD bytes,
                             bool ok, DWORD err)
{
    NetQueue* q     = NetQueue(self);
    IocpOp* op      = CONTAINING_RECORD(ov, IocpOp, ov);
    NetSocket* sock = op->sock;

    bool shutting = _netqueueShuttingDown(q);
    bool closed   = atomicLoad(uint32, &sock->state, Relaxed) == NS_Closed;
    bool aborted  = !ok && err == ERROR_OPERATION_ABORTED;
    bool live     = ok && !shutting && !closed;

    if (op->type == IocpRecvFrom) {
        if (live) {
            op->buf->len = (size_t)bytes;

            NetAddr src;
            if (netAddrFromSockaddr(&src, (struct sockaddr*)&op->from))
                netqueue_ingestDatagram(q, sock, &src, &op->buf);   // takes op->buf on success

            if (op->buf)   // unrecognized source family; ingest did not take it
                bufpoolPut(&q->pool->msgbuf, &op->buf);

            // Keep the outstanding set full: replace the op we are retiring with a fresh one.
            postRecvFrom(self, sock);
        } else {
            // Aborted, errored, or torn down: return the buffer, post nothing.
            bufpoolPut(&q->pool->msgbuf, &op->buf);
        }
    } else if (op->type == IocpRecv) {   // stream receive
        withMutex (&sock->recvLock) {
            bufringCommit(&sock->bufs.stream.recv, (ok && bytes > 0) ? (size_t)bytes : 0);
        }

        if (live && bytes > 0) {
            if (sock->flow) {
                // The bytes already live in the ring; the message only records how many arrived so
                // the handler can drain them back out with netsocketRecv().
                NetMessage* msg = netpoolAllocHeader(q->pool);
                msg->kind       = NMSG_Data;
                msg->buf        = NULL;
                msg->bytes      = (size_t)bytes;
                msg->addr       = sock->remote;
                netqueue_submit(q, sock->flow, msg);
            }
            postRecv(self, sock);   // keep one recv outstanding
        } else if (ok && bytes == 0) {
            // A zero-byte stream completion is an orderly peer shutdown. Do not repost.
            if (sock->flow && !shutting)
                netflow_close(sock->flow, NCR_PeerClosed);
        } else if (!ok && !aborted && !shutting) {
            // A genuine socket error (reset), not our own cancellation and not teardown.
            if (sock->flow)
                netflow_close(sock->flow, NCR_Error);
        }
    } else if (op->type == IocpConnect) {
        // A completion whose generation no longer matches belongs to a superseded attempt -- this
        // one timed out and failed over to a fresh handle whose ConnectEx was cancelled -- so it
        // only cleans itself up (below), never advancing the state machine.
        if (op->connectGen == atomicLoad(uint32, &sock->connectGen, Acquire)) {
            if (ok && !shutting && !closed) {
                // Make the socket behave like a normally-connected one (getpeername, SO_*, etc.).
                setsockopt((SOCKET)sock->handle, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
                netsocket_connectResult(sock, NERR_None);
            } else if (!aborted && !shutting && !closed) {
                // A genuine connect failure: advance to the next resolved address. (Our own cancels
                // come back aborted and are left to the timeout sweep or teardown.)
                netsocket_connectResult(sock, mapConnectError(err));
            }
        }
    } else if (op->type == IocpAccept) {
        if (live) {
            SOCKET as = op->acceptSock;
            SOCKET ls = (SOCKET)sock->handle;

            // Make the accepted socket a first-class one: inherit the listener's properties so
            // getpeername, shutdown, and the socket options behave normally.
            setsockopt(as, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&ls, sizeof(ls));

            NetAddr peer;
            memset(&peer, 0, sizeof(peer));
            struct sockaddr_storage pa;
            int palen = sizeof(pa);
            if (getpeername(as, (struct sockaddr*)&pa, &palen) == 0)
                netAddrFromSockaddr(&peer, (struct sockaddr*)&pa);

            NetSocketWin* ns = netsocketwinWrap(as, NST_Stream, NS_Connected);
            op->acceptSock   = INVALID_SOCKET;   // ownership moved into the wrap (or closed below)
            if (ns)
                netsocket_accepted(sock, NetSocket(ns), &peer);   // takes the reference
            else
                closesocket(as);

            postAccept(self, sock);   // keep the backlog serviced
        } else if (op->acceptSock != INVALID_SOCKET) {
            // Aborted (listener removed/shut down) or torn down: drop the pre-created socket, post
            // nothing more.
            closesocket(op->acceptSock);
        }
    } else {   // IocpSend (stream) or IocpSendTo (datagram)
        // Retire what this op was carrying: a stream skips the bytes the transport accepted from its
        // chain; a datagram frees the message the op owned (noting its destination first, in case a
        // failure needs reporting to that peer's flow). Then the socket is free to send again.
        NetAddr seaddr = { 0 };
        withMutex (&sock->sendLock) {
            if (op->type == IocpSend) {
                bufchainSkip(&sock->bufs.stream.send, ok ? (size_t)bytes : 0);
            } else {
                seaddr = op->sendMsg->addr;
                netpoolFreeMsg(q->pool, &op->sendMsg);
            }
            sock->sendPending = false;
        }

        // A fatal send completion is reported with the mapped code; for a stream the byte stream is
        // broken, so netsocket_sendError also closes its flow and nothing more is posted. A datagram
        // lost only the one message and keeps going.
        if (!ok && !aborted && !shutting && !closed)
            netsocket_sendError(sock, q, mapCompletionError(err), &seaddr);

        if (aborted || shutting || closed || (op->type == IocpSend && !ok)) {
            // Torn down (accounted for above) or the stream is broken: post nothing more.
        } else {
            // Push out whatever the OS will now accept (firing NET_SendReady on the low-watermark
            // crossing) and, if the transport buffer is still full, keep one overlapped send going.
            netsocket_flushSend(sock, q);
            if (netsocket_wantWrite(sock))
                iocpSendPump(self, sock);
        }
    }

    objRelease(&op->sock);
    xaFree(op);
    atomicFetchSub(uint32, &self->iops, 1, AcqRel);
}

// ---------------------------------------------------------------------------------------------
// Completion threads and draining
// ---------------------------------------------------------------------------------------------

// A completion thread: block in GetQueuedCompletionStatus, ingest the completion inline, then
// dispatch whatever it queued -- ingest and dispatch merge here, so the base dispatch pool is not
// used. A bounded wait lets it re-check the exit flag; an exit sentinel wakes it immediately.
static int iocpWorkerThread(Thread* thr)
{
    NetQueueWinIOCP* self = stvlNextPtr(&thr->args);
    if (!self)
        return 1;

    HANDLE port = (HANDLE)self->iocp;
    NetQueue* q = NetQueue(self);

    while (thrLoop(thr)) {
        DWORD bytes     = 0;
        ULONG_PTR key   = 0;
        OVERLAPPED* ov  = NULL;

        // A completion port has no wait-set to rebuild, so the only reason to cap the sleep is a
        // timer. The 500ms floor stays as the idle heartbeat that keeps maint() and thrLoop()
        // running; an armed deadline shortens it so timers do not run up to half a second late.
        DWORD tmo       = iocpWaitMs(q, 500);
        BOOL ok         = GetQueuedCompletionStatus(port, &bytes, &key, &ov, tmo);
        DWORD err       = ok ? 0 : GetLastError();

        if (key == IOCP_KEY_EXIT)
            break;

        if (ov)
            handleCompletion(self, ov, bytes, ok != FALSE, err);

        // Fire whatever came due. Runs on the bare-timeout path too (ov == NULL): a black-holed
        // connect never completes, so its timer is the only thing that ends the attempt, and the
        // dispatch drain below then delivers the NET_Connection(timeout) it queued.
        netqueue_timerSweep(q);

        while (netqueue_dispatch(q))
            ;
        netqueue_maint(q);
    }

    return 0;
}

// Drain completions on the calling thread until the outstanding count reaches zero, so no op is
// stranded in the kernel after its socket's I/O has been cancelled. `timeout` (microseconds, <= 0 =
// forever) bounds the wait; because cancelled operations complete promptly, the bound is a
// backstop rather than the normal exit.
static void drainQuiescent(_Inout_ NetQueueWinIOCP* self, int64 timeout)
{
    HANDLE port = (HANDLE)self->iocp;
    int64 start = clockTimer();

    while (atomicLoad(uint32, &self->iops, Acquire) > 0) {
        DWORD bytes    = 0;
        ULONG_PTR key  = 0;
        OVERLAPPED* ov = NULL;
        BOOL ok        = GetQueuedCompletionStatus(port, &bytes, &key, &ov, 200);
        DWORD err      = ok ? 0 : GetLastError();

        if (ov) {
            handleCompletion(self, ov, bytes, ok != FALSE, err);
            continue;
        }

        if (timeout > 0 && (clockTimer() - start) > timeout)
            break;
    }
}

// Cancel every outstanding operation on every socket. The completions come back with
// ERROR_OPERATION_ABORTED and are drained (returning buffers and releasing socket refs) rather than
// reposted, because shutdownReq is already set.
static void cancelAllIo(_Inout_ NetQueueWinIOCP* self)
{
    NetQueue* q = NetQueue(self);
    withReadLock (&q->lock) {
        foreach (hashtable, hti, q->sockets) {
            NetSocket* sock = (NetSocket*)htiVal(object, hti);
            if (sock && sock->handle != NET_INVALID_HANDLE)
                CancelIoEx((HANDLE)sock->handle, NULL);
        }
    }
}

// Wake every completion thread with an exit sentinel and join it. Idempotent.
static void stopIoThreads(_Inout_ NetQueueWinIOCP* self, int64 timeout)
{
    int32 n = saSize(self->iothreads);
    if (n == 0)
        return;

    for (int32 i = 0; i < n; i++)
        PostQueuedCompletionStatus((HANDLE)self->iocp, 0, IOCP_KEY_EXIT, NULL);

    foreach (sarray, idx, Thread*, t, self->iothreads) {
        thrRequestExit(t);
    }

    int64 wait = timeout > 0 ? timeout : timeForever;
    foreach (sarray, idx, Thread*, t, self->iothreads) {
        thrWait(t, wait);
    }

    saClear(&self->iothreads);
}

// ---------------------------------------------------------------------------------------------
// Class methods
// ---------------------------------------------------------------------------------------------

_objfactory_guaranteed NetQueueWinIOCP* NetQueueWinIOCP_create(NetQueueConfig* conf)
{
    NetQueueWinIOCP* self = objInstCreate(NetQueueWinIOCP);

    // Config must be applied before objInstInit so the base sizes its pools from it.
    netqueue_applyConfig(self, conf);
    objInstInit(self);

    self->iocp = (void*)CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!self->iocp) {
        objRelease(&self);
        return NULL;
    }

    int32 nthreads = conf ? conf->nthreads : 0;

    // Datagram receives to keep outstanding: enough that every completion thread can hold a
    // distinct packet with headroom, floored so a polled queue still pipelines a burst.
    uint32 base       = nthreads > 0 ? (uint32)nthreads : 1;
    self->dgramRecvs  = max(4u, min(base * 4u, 64u));

    // With no writability signal, the shared send path hands queued data back through this hook so
    // the backend can post an overlapped send. Needed in polled mode too -- send() runs on the
    // caller's thread and tick() completes the posted op.
    NetQueue(self)->sendPump = iocpSendPump;
    NetQueue(self)->sendCtx  = self;

    NetQueue(self)->wake    = iocpWake;
    NetQueue(self)->wakeCtx = self;

    // Threaded mode: N completion threads block in GetQueuedCompletionStatus and both ingest and
    // dispatch. Polled mode (nthreads == 0) starts nothing; the caller drives tick().
    if (nthreads > 0) {
        for (int32 i = 0; i < nthreads; i++) {
            Thread* t = thrCreate(iocpWorkerThread, _S "CX Net IOCP", stvar(ptr, self));
            if (t) {
                saPush(&self->iothreads, object, t);
                objRelease(&t);
            }
        }
        // If not a single completion thread started there is nothing to service the port; leaving
        // the queue polled (the caller drives tick()) is better than one that never completes.
    }

    return self;
}

NetSocket* NetQueueWinIOCP_socket(_In_ NetQueueWinIOCP* self, NetSocketType type)
{
    // The concrete socket class is per-platform; the queue does not need to know which.
    unused_noeval(self);
    return netPlatformCreateSocket(type);
}

// Begin one connect attempt with an overlapped ConnectEx. The fresh handle must be bound (ConnectEx
// requires it) and associated with the completion port before the op is posted. A synchronous
// failure unwinds the op and reports the attempt failed; WSA_IO_PENDING leaves the completion to
// finish it. The op is counted in iops so shutdown's cancel-and-drain reclaims it like any other.
bool NetQueueWinIOCP_connectBegin(_In_ NetQueueWinIOCP* self, NetSocket* sock, NetAddr* addr)
{
    // Fresh bound handle of the address's family.
    if (!netPlatformResetSocket(sock, addr->type, true)) {
        netsocket_connectResult(sock, NERR_Unknown);
        return true;
    }

    SOCKET s = (SOCKET)sock->handle;

    LPFN_CONNECTEX connectEx = getConnectEx(self, s);
    if (!connectEx) {
        netsocket_connectResult(sock, NERR_Unknown);
        return true;
    }

    // Associate the fresh handle with the port (the previous attempt's handle was closed by the
    // reset, dropping its association). The completion key is the socket pointer, as elsewhere.
    if (!CreateIoCompletionPort((HANDLE)s, (HANDLE)self->iocp, (ULONG_PTR)sock, 0)) {
        netsocket_connectResult(sock, NERR_Unknown);
        return true;
    }

    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr(addr, &sa, &sasz)) {
        netsocket_connectResult(sock, NERR_Unknown);
        return true;
    }

    IocpOp* op      = xaAlloc(sizeof(IocpOp), XA_Zero);
    op->type        = IocpConnect;
    op->sock        = objAcquire(sock);
    op->connectGen  = atomicLoad(uint32, &sock->connectGen, Acquire);

    atomicFetchAdd(uint32, &self->iops, 1, AcqRel);

    BOOL ok = connectEx(s, (struct sockaddr*)&sa, sasz, NULL, 0, NULL, &op->ov);
    // As with the receive posts, a synchronous failure (not WSA_IO_PENDING) queues no completion, so
    // unwind the op here; a pending op is retired by its completion.
    if (!ok && WSAGetLastError() != WSA_IO_PENDING) {
        NetErrorCode err = netLastError();
        atomicFetchSub(uint32, &self->iops, 1, AcqRel);
        objRelease(&op->sock);
        xaFree(op);
        netsocket_connectResult(sock, err);
    }
    return true;
}

// The socket just became connected. Start the single outstanding WSARecv, exactly as addSocket()
// does for a socket that was already connected when it joined the queue.
void NetQueueWinIOCP_connectArm(_In_ NetQueueWinIOCP* self, NetSocket* sock)
{
    postRecv(self, sock);
}

// A socket just started listening (listen() called after it joined the queue). Post the initial
// AcceptEx batch. The complementary case -- a socket already listening when it is added -- is armed
// by addSocket instead; the two are mutually exclusive, so the backlog is never double-posted.
void NetQueueWinIOCP_acceptArm(_In_ NetQueueWinIOCP* self, NetSocket* sock)
{
    for (int i = 0; i < IOCP_ACCEPTS; i++)
        postAccept(self, sock);
}

extern bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_addSocket(socket) NetQueue_addSocket((NetQueue*)(self), socket)
bool NetQueueWinIOCP_addSocket(_In_ NetQueueWinIOCP* self, NetSocket* socket)
{
    NetQueue* q = NetQueue(self);
    if (!NetQueue_addSocket(q, socket))
        return false;

    // Associate the socket handle with the completion port. The completion key is the socket
    // pointer -- informational only, since the op is recovered from its OVERLAPPED.
    if (socket->handle != NET_INVALID_HANDLE) {
        HANDLE h = CreateIoCompletionPort((HANDLE)socket->handle, (HANDLE)self->iocp,
                                          (ULONG_PTR)socket, 0);
        if (!h) {
            NetQueue_removeSocket(q, socket);   // back it out rather than leave it half-registered
            return false;
        }
    }

    // Start completions flowing. A datagram socket keeps many WSARecvFrom outstanding so the kernel
    // hands each completion thread a distinct packet; a connected stream socket keeps exactly one; a
    // socket that is already listening when it joins keeps a batch of AcceptEx posted. A connecting
    // socket posts nothing here -- the connect state machine drives its ConnectEx.
    if (socket->type == NST_Datagram) {
        for (uint32 i = 0; i < self->dgramRecvs; i++)
            postRecvFrom(self, socket);
    } else {
        uint32 st = atomicLoad(uint32, &socket->state, Relaxed);
        if (st == NS_Connected) {
            postRecv(self, socket);
        } else if (st == NS_Listening) {
            for (int i = 0; i < IOCP_ACCEPTS; i++)
                postAccept(self, socket);
        }
    }

    return true;
}

extern bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket);   // parent
#define parent_removeSocket(socket) NetQueue_removeSocket((NetQueue*)(self), socket)
bool NetQueueWinIOCP_removeSocket(_In_ NetQueueWinIOCP* self, NetSocket* socket)
{
    // Cancel this socket's outstanding operations first so their completions come back aborted and
    // are drained -- returning buffers and releasing the op's socket ref -- rather than reposting on
    // a socket on its way out.
    if (socket->handle != NET_INVALID_HANDLE)
        CancelIoEx((HANDLE)socket->handle, NULL);

    return NetQueue_removeSocket(NetQueue(self), socket);
}

extern bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout);   // parent
#define parent_shutdown(timeout) NetQueue_shutdown((NetQueue*)(self), timeout)
bool NetQueueWinIOCP_shutdown(_In_ NetQueueWinIOCP* self, int64 timeout)
{
    NetQueue* q = NetQueue(self);

    // Stop reposting first, so cancelled operations drain instead of being immediately replaced.
    atomicStore(uint32, &q->shutdownReq, 1, Release);

    // Cancel outstanding I/O, stop the completion threads, then drain whatever aborted completions
    // they left behind so no pooled buffer or socket reference is stranded in the kernel.
    cancelAllIo(self);
    stopIoThreads(self, timeout);

    // A completion thread's repost-on-completion (postRecvFrom et al.) only checks shutdownReq at
    // entry, with no re-check right before the WSA* call -- so it can read the flag as not-yet-set,
    // then get preempted, and post a fresh op *after* the sweep above already ran, leaving it
    // uncancelled and never completing (drainQuiescent below would then spin forever waiting for
    // an iops decrement that never comes). stopIoThreads() just joined every completion thread, so
    // none can still be mid-repost -- a second sweep here is guaranteed to see the final, complete
    // set of outstanding ops and catch anything that slipped through the first one.
    cancelAllIo(self);
    drainQuiescent(self, timeout);

    // The base closes flows (queuing terminal events) and drains the runqueue inline on this
    // thread. The completion threads that would normally dispatch are gone, so that inline drain is
    // what delivers the NET_FlowClosed callbacks before the queue goes away.
    return NetQueue_shutdown(q, timeout);
}

bool NetQueueWinIOCP_tick(_In_ NetQueueWinIOCP* self, int64 wait)
{
    // Polled mode only: service completions on the caller's thread. Wait up to `wait` ms for the
    // first, then pull the rest non-blocking so one tick services a whole burst.
    NetQueue* q = NetQueue(self);
    HANDLE port = (HANDLE)self->iocp;

    bool any = false;
    int64 w  = wait;
    for (;;) {
        DWORD bytes    = 0;
        ULONG_PTR key  = 0;
        OVERLAPPED* ov = NULL;
        DWORD tmo      = iocpWaitMs(q, w);
        BOOL ok        = GetQueuedCompletionStatus(port, &bytes, &key, &ov, tmo);
        DWORD err      = ok ? 0 : GetLastError();

        if (key == IOCP_KEY_EXIT || key == IOCP_KEY_WAKE) {
            w = 0;
            continue;
        }
        if (!ov)
            break;   // timeout: nothing more ready this tick

        handleCompletion(self, ov, bytes, ok != FALSE, err);
        any = true;
        w   = 0;   // subsequent pulls are non-blocking
    }

    // Fire whatever came due (the polled equivalent of the completion thread's sweep).
    netqueue_timerSweep(q);

    while (netqueue_dispatch(q))
        any = true;
    netqueue_maint(q);

    return any;
}

void NetQueueWinIOCP_destroy(_In_ NetQueueWinIOCP* self)
{
    // Safety net for a queue released without a shutdown: cancel and drain outstanding I/O and stop
    // the completion threads before the port they wait on is closed. A no-op after a normal
    // shutdown (no sockets, no threads, no outstanding ops).
    atomicStore(uint32, &NetQueue(self)->shutdownReq, 1, Release);
    cancelAllIo(self);
    stopIoThreads(self, timeForever);
    // See the matching comment in NetQueueWinIOCP_shutdown(): a repost racing the sweep above can
    // slip an uncancelled op past it, so sweep again now that every completion thread is joined.
    cancelAllIo(self);
    drainQuiescent(self, timeForever);

    if (self->iocp) {
        CloseHandle((HANDLE)self->iocp);
        self->iocp = NULL;
    }

    // Autogen begins -----
    saDestroy(&self->iothreads);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "platform/win/win_net_iocp.auto.inc"
// clang-format on
// Autogen ends -------

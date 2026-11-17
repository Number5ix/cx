// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/win/win_net_socket.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "win_net.h"

// We say NAY to Nagle
static void setNoDelay(SOCKET sock)
{
    BOOL one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

// Everything a fresh handle of the socket's type and family needs before it is used, including the
// per-datagram options the socket already asked for on an earlier handle.
static void setupHandle(_In_ NetSocket* sock, SOCKET s, int af)
{
    // Every backend drives these non-blocking: the select loop must not stall in recv() on a
    // spurious readiness, and completion backends post overlapped operations.
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);

    // One family per handle, which is the Windows default anyway; set it so it does not depend on
    // that.
    if (af == AF_INET6) {
        DWORD one = 1;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&one, sizeof(one));
    }

    if (sock->osRecvBuf || sock->osSendBuf)
        netSockSetBuffers((NetSockHandle)s, sock->osRecvBuf, sock->osSendBuf);

    if (sock->type == NST_Stream) {
        setNoDelay(s);
        return;
    }

    if (sock->recvInfo)
        netSockRecvInfo((NetSockHandle)s, true);
    if (sock->dontFragment)
        netSockDontFragment((NetSockHandle)s, true);
}

_objfactory_check NetSocketWin* NetSocketWin_create(NetSocketType type)
{
    NetSocketWin* self;
    self = objInstCreate(NetSocketWin);

    self->type = type;
    self->af   = AF_INET;
    self->sock = socket(self->af, (type == NST_Stream) ? SOCK_STREAM : SOCK_DGRAM, IPPROTO_IP);

    // A freshly created socket is NS_Init, not NS_Closed. The bind/listen guards test against
    // NS_Closed, so storing NS_Closed here inverted them and rejected every bind on a new socket.
    self->handle = (NetSockHandle)self->sock;
    atomicStore(uint32, &self->state, NS_Init, Relaxed);

    if (self->sock != INVALID_SOCKET)
        setupHandle(NetSocket(self), self->sock, self->af);

    if (self->sock == INVALID_SOCKET || !objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }
    return self;
}

_objfactory_guaranteed NetSocketWin*
NetSocketWin_wrap(SOCKET sock, NetSocketType type, NetSocketState state)
{
    NetSocketWin* self;
    self = objInstCreate(NetSocketWin);

    self->type   = type;
    self->sock   = sock;
    self->handle = (NetSockHandle)sock;
    self->af     = AF_INET;
    atomicStore(uint32, &self->state, state, Relaxed);

    struct sockaddr_storage sa;
    int salen = sizeof(sa);
    if (sock != INVALID_SOCKET && getsockname(sock, (struct sockaddr*)&sa, &salen) == 0)
        self->af = sa.ss_family;

    // An accepted socket inherits the listener's blocking mode; the backend drains until
    // WouldBlock, so it must be non-blocking regardless of how it arrived.
    if (sock != INVALID_SOCKET) {
        u_long nonblocking = 1;
        ioctlsocket(sock, FIONBIO, &nonblocking);
        if (type == NST_Stream)
            setNoDelay(sock);
    }

    objInstInit(self);
    return self;
}

bool NetSocketWin_bind(_In_ NetSocketWin* self, const NetAddr* addr)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    // A handle's family is fixed when it is made, so binding to the other family takes a new one.
    // That is only possible while nothing else knows the old handle: before the socket is bound or
    // has joined a queue.
    int af = addr->type == NA_IPv6 ? AF_INET6 : AF_INET;
    if (af != self->af) {
        if (self->local.type != NA_Unknown || self->queue ||
            !netPlatformResetSocket(NetSocket(self), addr->type, false))
            return false;
    }

    BOOL one = 1;
    setsockopt(self->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

    struct sockaddr_storage so;
    int sosz = 0;
    if (netAddrToSockaddr(addr, &so, &sosz)) {
        if (bind(self->sock, (struct sockaddr*)&so, sosz) == 0) {
            struct sockaddr_storage got;
            int gotsz = sizeof(got);
            if (getsockname(self->sock, (struct sockaddr*)&got, &gotsz) == 0)
                netAddrFromSockaddr(&self->local, (struct sockaddr*)&got);
            else
                self->local = *addr;

            // A datagram socket that joined its queue unbound has had no receives posted yet.
            if (self->type == NST_Datagram) {
                NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
                if (q) {
                    netqueueConnectArm(q, NetSocket(self));
                    objRelease(&q);
                }
            }
            return true;
        }
    }
    return false;
}

bool NetSocketWin_listen(_In_ NetSocketWin* self, int backlog)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    if (backlog < 1)
        backlog = SOMAXCONN;

    if (listen(self->sock, backlog) == 0) {
        atomicStore(uint32, &self->state, NS_Listening, Relaxed);
        // Kick the backend into accepting: if the socket is already on a queue this arms it now,
        // otherwise the queue's addSocket picks up the already-listening socket when it joins.
        netsocket_listenArm(self);
        return true;
    }

    return false;
}

_Use_decl_annotations_
NetErrorCode netPlatformAccept(NetSockHandle listener, NetSocket** out, NetAddr* peer)
{
    *out = NULL;
    memset(peer, 0, sizeof(*peer));

    struct sockaddr_storage sa;
    int salen = sizeof(sa);
    SOCKET as = accept((SOCKET)listener, (struct sockaddr*)&sa, &salen);
    if (as == INVALID_SOCKET)
        return netLastError();   // NERR_WouldBlock once the backlog is drained

    netAddrFromSockaddr(peer, (struct sockaddr*)&sa);

    // Wrap the accepted handle as a connected stream socket. NetSocketWin_wrap sets it non-blocking,
    // which the ingest loops require.
    NetSocketWin* ns = netsocketwinWrap(as, NST_Stream, NS_Connected);
    if (!ns) {
        closesocket(as);
        return NERR_Unknown;
    }
    *out = NetSocket(ns);
    return NERR_None;
}

extern bool NetSocket_close(_In_ NetSocket* self);   // parent
#define parent_close() NetSocket_close((NetSocket*)(self))
bool NetSocketWin_close(_In_ NetSocketWin* self)
{
    bool listener = atomicLoad(uint32, &self->state, Acquire) == NS_Listening;

    if (!parent_close())
        return false;

    // The handle is not closed here. Winsock hands a closed socket's number to the very next socket
    // created, and a thread already holding this socket -- a completion reposting a receive, a
    // worker flushing a send -- would then issue its call on a stranger's connection. Everything
    // that can use the handle holds a reference, so closing it in destroy() instead is what makes
    // that impossible. What close owes the application happens now: the peer is told, and pending
    // operations are cancelled. The cancel comes after the base close marked the socket closed; a
    // completion backend that posts in the meantime checks for that and cancels its own operation.
    //
    // A listener is the exception: its address is often wanted again at once, and nothing posts
    // stream data on it for a reused number to be confused with.
    if (listener) {
        SOCKET s     = self->sock;
        self->sock   = INVALID_SOCKET;
        self->handle = NET_INVALID_HANDLE;
        closesocket(s);
        return true;
    }

    if (self->sock != INVALID_SOCKET) {
        if (self->type == NST_Stream)
            shutdown(self->sock, SD_BOTH);
        _netCancelSockIo(self->handle);
    }
    return true;
}

void NetSocketWin_destroy(_In_ NetSocketWin* self)
{
    netsocketwinClose(self);

    if (self->sock != INVALID_SOCKET) {
        closesocket(self->sock);
        self->sock   = INVALID_SOCKET;
        self->handle = NET_INVALID_HANDLE;
    }
}

_Use_decl_annotations_
bool netPlatformResetSocket(NetSocket* sock, NetAddrType family, bool bindAny)
{
    NetSocketWin* self = (NetSocketWin*)sock;

    int af = (family == NA_IPv6) ? AF_INET6 : AF_INET;

    // Each connect attempt needs a fresh handle: a socket with a failed or pending connect cannot be
    // reliably reconnected, and the resolved list can mix families an existing handle cannot serve.
    // A socket bound to the other family than it was made with needs one too.
    bool stream = sock->type == NST_Stream;
    SOCKET ns   = socket(af, stream ? SOCK_STREAM : SOCK_DGRAM, stream ? IPPROTO_TCP : IPPROTO_UDP);
    if (ns == INVALID_SOCKET)
        return false;

    setupHandle(sock, ns, af);

    // ConnectEx requires the socket to already be bound; a plain connect() does not.
    if (bindAny) {
        struct sockaddr_storage any;
        memset(&any, 0, sizeof(any));
        int anysz;
        if (af == AF_INET6) {
            struct sockaddr_in6* a6 = (struct sockaddr_in6*)&any;
            a6->sin6_family         = AF_INET6;
            anysz                   = sizeof(struct sockaddr_in6);
        } else {
            struct sockaddr_in* a4 = (struct sockaddr_in*)&any;
            a4->sin_family         = AF_INET;
            a4->sin_addr.s_addr    = htonl(INADDR_ANY);
            anysz                  = sizeof(struct sockaddr_in);
        }
        if (bind(ns, (struct sockaddr*)&any, anysz) != 0) {
            closesocket(ns);
            return false;
        }
    }

    // Publish the new handle before closing the old one. The select backend reads the handle from
    // another thread when it builds its watch set; closing first would leave a window, as long as
    // this thread stays descheduled, in which every pass hands select() a closed descriptor and
    // fails at once instead of waiting.
    SOCKET old   = self->sock;
    self->sock   = ns;
    self->af     = af;
    self->handle = (NetSockHandle)ns;
    if (old != INVALID_SOCKET)
        closesocket(old);
    return true;
}

// Autogen begins -----
// clang-format off
#include "platform/win/win_net_socket.auto.inc"
// clang-format on
// Autogen ends -------

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

_objfactory_check NetSocketWin* NetSocketWin_create(NetSocketType type)
{
    NetSocketWin* self;
    self = objInstCreate(NetSocketWin);

    self->type = type;
    self->sock = socket(AF_INET, (type == NST_Stream) ? SOCK_STREAM : SOCK_DGRAM, IPPROTO_IP);

    // A freshly created socket is NS_Init, not NS_Closed. The bind/listen guards test against
    // NS_Closed, so storing NS_Closed here inverted them and rejected every bind on a new socket.
    self->handle = (NetSockHandle)self->sock;
    atomicStore(uint32, &self->state, NS_Init, Relaxed);

    // Every backend drives these non-blocking: the select loop must not stall in recv() on a
    // spurious readiness, and completion backends post overlapped operations.
    if (self->sock != INVALID_SOCKET) {
        u_long nonblocking = 1;
        ioctlsocket(self->sock, FIONBIO, &nonblocking);
    }

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
    atomicStore(uint32, &self->state, state, Relaxed);

    // An accepted socket inherits the listener's blocking mode; the backend drains until
    // WouldBlock, so it must be non-blocking regardless of how it arrived.
    if (sock != INVALID_SOCKET) {
        u_long nonblocking = 1;
        ioctlsocket(sock, FIONBIO, &nonblocking);
    }

    objInstInit(self);
    return self;
}

bool NetSocketWin_bind(_In_ NetSocketWin* self, NetAddr* addr)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    BOOL one = 1;
    setsockopt(self->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

    struct sockaddr_storage so;
    int sosz = 0;
    if (netAddrToSockaddr(addr, &so, &sosz)) {
        if (bind(self->sock, (struct sockaddr*)&so, sosz) == 0)
            return true;
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
    if (!parent_close())
        return false;

    closesocket(self->sock);
    return true;
}

void NetSocketWin_destroy(_In_ NetSocketWin* self)
{
    netsocketwinClose(self);
}

_Use_decl_annotations_
bool netPlatformResetSocket(NetSocket* sock, NetAddrType family, bool bindAny)
{
    NetSocketWin* self = (NetSocketWin*)sock;

    int af = (family == NA_IPv6) ? AF_INET6 : AF_INET;

    // Each connect attempt needs a fresh handle: a socket with a failed or pending connect cannot be
    // reliably reconnected, and the resolved list can mix families an existing handle cannot serve.
    SOCKET ns = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (ns == INVALID_SOCKET)
        return false;

    u_long nonblocking = 1;
    ioctlsocket(ns, FIONBIO, &nonblocking);

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

    // Swap the new handle in for the old, closing the one the previous attempt (if any) left behind.
    if (self->sock != INVALID_SOCKET)
        closesocket(self->sock);
    self->sock   = ns;
    self->handle = (NetSockHandle)ns;
    return true;
}

// Autogen begins -----
// clang-format off
#include "platform/win/win_net_socket.auto.inc"
// clang-format on
// Autogen ends -------

// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/wasm/wasm_net_socket.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "wasm_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>

// Every backend drives sockets non-blocking: the select loop must not stall in recv() on a
// spurious readiness, and neither wants a blocking accept()/connect() either.
static void setNonBlocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

_objfactory_check NetSocketPosix* NetSocketPosix_create(NetSocketType type)
{
    NetSocketPosix* self;
    self = objInstCreate(NetSocketPosix);

    self->type = type;
    self->fd   = socket(AF_INET, (type == NST_Stream) ? SOCK_STREAM : SOCK_DGRAM, 0);

    // A freshly created socket is NS_Init, not NS_Closed -- the bind/listen guards test against
    // NS_Closed, so storing NS_Closed here would reject every bind on a new socket.
    self->handle = (NetSockHandle)self->fd;
    atomicStore(uint32, &self->state, NS_Init, Relaxed);

    if (self->fd >= 0)
        setNonBlocking(self->fd);

    if (self->fd < 0 || !objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }
    return self;
}

_objfactory_guaranteed NetSocketPosix*
NetSocketPosix_wrap(int fd, NetSocketType type, NetSocketState state)
{
    NetSocketPosix* self;
    self = objInstCreate(NetSocketPosix);

    self->type   = type;
    self->fd     = fd;
    self->handle = (NetSockHandle)fd;
    atomicStore(uint32, &self->state, state, Relaxed);

    // An accepted fd inherits the listener's blocking mode; the backend drains until WouldBlock,
    // so it must be non-blocking regardless of how it arrived.
    if (fd >= 0)
        setNonBlocking(fd);

    objInstInit(self);
    return self;
}

bool NetSocketPosix_bind(_In_ NetSocketPosix* self, NetAddr* addr)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    int one = 1;
    setsockopt(self->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_storage so;
    int sosz = 0;
    if (netAddrToSockaddr(addr, &so, &sosz)) {
        if (bind(self->fd, (struct sockaddr*)&so, (socklen_t)sosz) == 0)
            return true;
    }
    return false;
}

bool NetSocketPosix_listen(_In_ NetSocketPosix* self, int backlog)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    if (backlog < 1)
        backlog = SOMAXCONN;

    if (listen(self->fd, backlog) == 0) {
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

    // No accept4()/SOCK_NONBLOCK fast path, unlike unix_net_socket.c's Linux branch -- accept4 is
    // a glibc/Linux extension not assumed present under Emscripten's syscall emulation. Plain
    // accept() plus NetSocketPosix_wrap's own setNonBlocking() call below covers it in one extra
    // fcntl(), which is not worth a conditional for.
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    int as           = accept((int)listener, (struct sockaddr*)&sa, &salen);
    if (as < 0)
        return netLastError();   // NERR_WouldBlock once the backlog is drained

    netAddrFromSockaddr(peer, (struct sockaddr*)&sa);

    NetSocketPosix* ns = netsocketposixWrap(as, NST_Stream, NS_Connected);
    if (!ns) {
        close(as);
        return NERR_Unknown;
    }
    *out = NetSocket(ns);
    return NERR_None;
}

extern bool NetSocket_close(_In_ NetSocket* self);   // parent
#define parent_close() NetSocket_close((NetSocket*)(self))
bool NetSocketPosix_close(_In_ NetSocketPosix* self)
{
    if (!parent_close())
        return false;

    close(self->fd);
    return true;
}

void NetSocketPosix_destroy(_In_ NetSocketPosix* self)
{
    netsocketposixClose(self);
}

_Use_decl_annotations_
bool netPlatformResetSocket(NetSocket* sock, NetAddrType family, bool bindAny)
{
    NetSocketPosix* self = (NetSocketPosix*)sock;

    int af = (family == NA_IPv6) ? AF_INET6 : AF_INET;

    // Each connect attempt needs a fresh handle: a socket with a failed or pending connect cannot
    // be reliably reconnected, and the resolved list can mix families an existing handle cannot
    // serve.
    int ns = socket(af, SOCK_STREAM, 0);
    if (ns < 0)
        return false;

    setNonBlocking(ns);

    if (bindAny) {
        struct sockaddr_storage any;
        memset(&any, 0, sizeof(any));
        socklen_t anysz;
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
            close(ns);
            return false;
        }
    }

    // Swap the new handle in for the old, closing the one the previous attempt (if any) left behind.
    if (self->fd >= 0)
        close(self->fd);
    self->fd     = ns;
    self->handle = (NetSockHandle)ns;
    return true;
}

// Autogen begins -----
// clang-format off
#include "platform/wasm/wasm_net_socket.auto.inc"
// clang-format on
// Autogen ends -------

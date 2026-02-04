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
    atomicStore(uint32, &self->state, NS_Closed, Relaxed);

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

    self->type = type;
    self->sock = sock;
    atomicStore(uint32, &self->state, state, Relaxed);

    objInstInit(self);
    return self;
}

bool NetSocketWin_send(_In_ NetSocketWin* self, _In_ uint8* data, size_t len, _In_opt_ NetAddr* dest, flags_t flags)
{
    return false;
}

bool NetSocketWin_bind(_In_ NetSocketWin* self, NetAddr* addr)
{
    if (atomicLoad(uint32, &self->state, Relaxed) != NS_Closed)
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
    if (atomicLoad(uint32, &self->state, Relaxed) != NS_Closed)
        return false;

    if (backlog < 1)
        backlog = SOMAXCONN;

    if (listen(self->sock, backlog) == 0) {
        atomicStore(uint32, &self->state, NS_Listening, Relaxed);
        return true;
    }

    return false;
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

// Autogen begins -----
// clang-format off
#include "platform/win/win_net_socket.auto.inc"
// clang-format on
// Autogen ends -------

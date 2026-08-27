// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "nettestobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include <cx/net.h>
#include <cx/net/net_private.h>

_objfactory_guaranteed NetSocketTest* NetSocketTest_create(NetSocketType type)
{
    NetSocketTest* self = objInstCreate(NetSocketTest);

    self->type = type;

    objInstInit(self);
    return self;
}

extern bool NetSocket_send(_In_ NetSocket* self, _In_ const uint8* data, size_t len, _In_opt_ const NetAddr* dest, flags_t flags);   // parent
#define parent_send(data, len, dest, flags) NetSocket_send((NetSocket*)(self), data, len, dest, flags)
bool NetSocketTest_send(_In_ NetSocketTest* self, _In_ const uint8* data, size_t len, _In_opt_ const NetAddr* dest, flags_t flags)
{
    self->sentBytes += len;
    self->sentCount++;

    return true;
}

bool NetSocketTest_bind(_In_ NetSocketTest* self, const NetAddr* addr)
{
    self->local = *addr;
    atomicStore(uint32, &self->state, NS_Connected, Relaxed);
    return true;
}

bool NetSocketTest_listen(_In_ NetSocketTest* self, int backlog)
{
    atomicStore(uint32, &self->state, NS_Listening, Relaxed);
    return true;
}

_objfactory_guaranteed NetQueueTest* NetQueueTest_create(NetQueueConfig* conf)
{
    NetQueueTest* self = objInstCreate(NetQueueTest);

    netqueue_applyConfig(self, conf);

    objInstInit(self);
    return self;
}

NetSocket* NetQueueTest_socket(_In_ NetQueueTest* self, NetSocketType type)
{
    return NetSocket(netsockettestCreate(type));
}

// The synthetic queue drives no real sockets, so it never begins a connect. These exist only to
// satisfy the abstract connect hooks on the base; the connect path is exercised end to end by the
// select and IOCP backends over loopback instead.
bool NetQueueTest_connectBegin(_In_ NetQueueTest* self, NetSocket* sock, const NetAddr* addr)
{
    unused_noeval(self);
    unused_noeval(sock);
    unused_noeval(addr);
    return false;
}

void NetQueueTest_connectArm(_In_ NetQueueTest* self, NetSocket* sock)
{
    unused_noeval(self);
    unused_noeval(sock);
}

// The synthetic queue accepts no real connections; this satisfies the abstract accept hook. The
// accept path is exercised end to end by the select and IOCP backends over loopback instead.
void NetQueueTest_acceptArm(_In_ NetQueueTest* self, NetSocket* sock)
{
    unused_noeval(self);
    unused_noeval(sock);
}

bool NetQueueTest_tick(_In_ NetQueueTest* self, int64 wait)
{
    // Polled mode runs ingest and dispatch inline on the caller's thread -- the same code the
    // workers run, just without the threads. There is no ingest here because the test injects
    // packets directly, so this is only the timer sweep a real backend runs after its wait, plus
    // the dispatch half.
    netqueue_timerSweep(self);

    bool ran = false;
    while (netqueue_dispatch(self))
        ran = true;

    return ran;
}

// Autogen begins -----
// clang-format off
#include "nettestobj.auto.inc"
// clang-format on
// Autogen ends -------

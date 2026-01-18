// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/win/win_net_queue_select.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed NetQueueWinSelect* NetQueueWinSelect_create(int32 nthreads, flags_t flags)
{
    NetQueueWinSelect* self;
    self = objInstCreate(NetQueueWinSelect);

    self->flags = flags;

    objInstInit(self);
    return self;
}

NetSocket* NetQueueWinSelect_socket(_In_ NetQueueWinSelect* self, NetSocketType type)
{
    return NULL; // TODO
}

// Autogen begins -----
// clang-format off
#include "platform/win/win_net_queue_select.auto.inc"
// clang-format on
// Autogen ends -------

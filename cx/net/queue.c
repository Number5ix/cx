// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/queue.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "net/socket.h"

_objinit_guaranteed bool NetQueue_init(_In_ NetQueue* self)
{
    // Autogen begins -----
    htInit(&self->callbacks, uint32, sarray, 16);
    htInit(&self->sockets, object, none, 16);
    rwlockInit(&self->lock);
    return true;
    // Autogen ends -------
}

bool NetQueue_addSocket(_In_ NetQueue* self, NetSocket* socket)
{
    bool ret = false;
    devAssert(!socket->queue);

    withWriteLock (&self->lock) {
        ret           = htInsert(&self->sockets, object, socket, none, HT_Ignore);
        socket->queue = objGetWeak(NetQueue, self);
    }

    return ret;
}

bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket)
{
    bool ret = false;
    withWriteLock (&self->lock) {
        ret = htRemove(&self->sockets, object, socket);
        if (ret) {
            objDestroyWeak(&socket->queue);
        }
    }

    return ret;
}

void NetQueue_destroy(_In_ NetQueue* self)
{
    // Autogen begins -----
    htDestroy(&self->callbacks);
    htDestroy(&self->sockets);
    rwlockDestroy(&self->lock);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "net/queue.auto.inc"
// clang-format on
// Autogen ends -------

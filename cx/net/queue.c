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
        ret = htInsert(&self->sockets, object, socket, none, HT_Ignore);
    }

    if (ret)
        socket->queue = objGetWeak(NetQueue, self);

    return ret;
}

bool NetQueue_removeSocket(_In_ NetQueue* self, NetSocket* socket)
{
    bool ret        = false;
    NetSocket* temp = NULL;

    withWriteLock (&self->lock) {
        temp = objAcquire(socket);   // keep socket alive slightly longer if we are the
                                     // last reference
        ret  = htRemove(&self->sockets, object, socket);
    }

    if (ret && temp)
        objDestroyWeak(&temp->queue);
    objRelease(&temp);

    return ret;
}

bool NetQueue_shutdown(_In_ NetQueue* self, int64 timeout)
{
    // remove all sockets from queue
    withWriteLock (&self->lock) {
        foreach (hashtable, hti, self->sockets) {
            NetSocket* socket = (NetSocket*)htiKey(object, hti);
            objDestroyWeak(&socket->queue);
        }
        htClear(&self->sockets);
    }

    return true;
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

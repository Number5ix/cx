// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/socket.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "cx/buffer/bufring_private.h"

_objinit_guaranteed bool NetSocket_init(_In_ NetSocket* self)
{
    if (self->mru == 0)
        self->mru = 1500;   // default to ethernet packet size

    // TODO: Make the buffer segment size configurable
    bufringInit(&self->recvBuf, 65536);
    bufchainInit(&self->sendBuf, 65536);

    // Autogen begins -----
    saInit(&self->user, stvar, 1);
    mutexInit(&self->recvLock);
    mutexInit(&self->sendLock);
    return true;
    // Autogen ends -------
}

size_t NetSocket_recv(_In_ NetSocket* self, _Out_ uint8* buf, size_t bufsz, _Out_opt_ NetAddr* src,
                      flags_t flags)
{
    size_t copied  = 0;
    size_t msgread = 0;
    NetMessage msg;

    withMutex (&self->recvLock) {
        if (self->type == NST_Stream) {
            // Stream sockets store the raw data in the buffer
            copied = bufringRead(&self->recvBuf, buf, bufsz);
        } else if (self->type == NST_Datagram) {
            // Datagram sockets store a NetMessage Structure in the buffer
            msgread = bufringRead(&self->recvBuf, (uint8*)&msg, sizeof(NetMessage));
            devAssert(msgread == 0 || msgread == sizeof(NetMessage));
        }
    }

    // For datagram sockets, we have to copy out the data but don't need the lock for that
    if (self->type == NST_Datagram && msgread == sizeof(NetMessage)) {
        copied = min(msg.buf->len, bufsz);
        memcpy(buf, msg.buf->data, copied);
        if (src)
            *src = msg.addr;

        bufDestroy(&msg.buf);
    }

    return copied;
}

bool NetSocket_recvMsgs(_In_ NetSocket* self, socketRecvCB cb, _In_opt_ void* ctx)
{
    bool done = true;
    bool ret  = false;

    do {
        NetMessage msg = { 0 };

        size_t msgread = 0;

        withMutex (&self->recvLock) {
            if (self->type == NST_Stream) {
                // Steal a buffer if we can
                msg.buf = _bufringStealHead(&self->recvBuf);
                if (!msg.buf) {
                    // probably the head wasn't 0-aligned...
                    // slow path, allocate a new buffer
                    size_t avail = _bufringReadContigAvail(&self->recvBuf);
                    if (avail > 0) {
                        msg.buf = bufCreate(avail);
                        bufringRead(&self->recvBuf, msg.buf->data, avail);
                    }
                }
            } else if (self->type == NST_Datagram) {
                // Datagram sockets store a NetMessage Structure in the buffer
                msgread = bufringRead(&self->recvBuf, (uint8*)&msg, sizeof(NetMessage));
                devAssert(msgread == 0 || msgread == sizeof(NetMessage));
            }
        }

        if ((self->type == NST_Datagram && msgread == sizeof(NetMessage)) ||
            (self->type == NST_Stream && msg.buf)) {
            done = !cb(self, &msg, ctx);
            ret  = true;
        }

        // if the callback took ownership, it will have set msg.buf to NULL, making this a no-op
        bufDestroy(&msg.buf);
    } while (!done);

    return ret;
}

bool NetSocket_close(_In_ NetSocket* self)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    NetQueue* queue = objAcquireFromWeak(NetQueue, self->queue);
    if (queue) {
        netqueueRemoveSocket(queue, self);
        objRelease(&queue);
    }

    atomicStore(uint32, &self->state, NS_Closed, Relaxed);

    return true;
}

void NetSocket_destroy(_In_ NetSocket* self)
{
    if (self->type == NST_Datagram) {
        // Free any remaining messages in the receive buffer
        NetMessage msg;
        while (bufringRead(&self->recvBuf, (uint8*)&msg, sizeof(NetMessage)) ==
               sizeof(NetMessage)) {
            bufDestroy(&msg.buf);
        }
    }

    bufchainDestroy(&self->recvBuf);
    bufchainDestroy(&self->sendBuf);

    // Autogen begins -----
    objDestroyWeak(&self->queue);
    saDestroy(&self->user);
    bufringDestroy(&self->recvBuf);
    bufchainDestroy(&self->sendBuf);
    mutexDestroy(&self->recvLock);
    mutexDestroy(&self->sendLock);
    saDestroy(&self->connQueue);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "net/socket.auto.inc"
// clang-format on
// Autogen ends -------

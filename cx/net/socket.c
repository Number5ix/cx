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

_objinit_guaranteed bool NetSocket_init(_In_ NetSocket* self)
{
    // TODO: Make the buffer segment size configurable
    bufchainInit(&self->recvBuf, 65536);
    bufchainInit(&self->sendBuf, 65536);

    // Autogen begins -----
    saInit(&self->user, stvar, 1);
    mutexInit(&self->recvLock);
    mutexInit(&self->sendLock);
    return true;
    // Autogen ends -------
}

size_t NetSocket_recv(_In_ NetSocket* self, _Out_ uint8* buf, size_t bufsz, _Out_opt_ NetAddr* src)
{
    size_t copied  = 0;
    size_t msgread = 0;
    NetMessage msg;

    withMutex (&self->recvLock) {
        if (self->type == NET_Stream) {
            // Stream sockets store the raw data in the buffer
            copied = bufchainRead(&self->recvBuf, buf, bufsz);
        } else if (self->type == NET_Datagram) {
            // Datagram sockets store a NetMessage Structure in the buffer
            msgread = bufchainRead(&self->recvBuf, (uint8*)&msg, sizeof(NetMessage));
            devAssert(msgread == 0 || msgread == sizeof(NetMessage));
        }
    }

    // For datagram sockets, we have to copy out the data but don't need the lock for that
    if (self->type == NET_Datagram && msgread == sizeof(NetMessage)) {
        copied = min(msg.len, bufsz);
        memcpy(buf, msg.data, copied);
        if (src)
            *src = msg.addr;

        xaFree(msg.data);
    }

    return copied;
}

void NetSocket_destroy(_In_ NetSocket* self)
{
    if (self->type == NET_Datagram) {
        // Free any remaining messages in the receive buffer
        NetMessage msg;
        while (bufchainRead(&self->recvBuf, (uint8*)&msg, sizeof(NetMessage)) ==
               sizeof(NetMessage)) {
            xaFree(msg.data);
        }
    }

    bufchainDestroy(&self->recvBuf);
    bufchainDestroy(&self->sendBuf);

    // Autogen begins -----
    objDestroyWeak(&self->queue);
    saDestroy(&self->user);
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

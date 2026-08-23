// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/pool.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "net_private.h"

_objfactory_guaranteed NetPool* NetPool_create(_In_ const NetQueueConfig* conf)
{
    NetPool* self;
    self = objInstCreate(NetPool);

    // msghdr is only a freelist cache: allocHeader falls back to xaAlloc when it is empty and
    // freeMsg falls back to xaFree when it is full, so a fixed cap is correctness-safe and just
    // bounds the cache depth. A fixed queue also skips all the dynamic bookkeeping (access/inuse/
    // reserved atomics per op, GC, gcmtx) that a freelist never needs -- nothing ever calls
    // prqCollect() on it. msgbuf is the one with a cap that actually means something.
    prqInitFixed(&self->msghdr, 256);
    bufpoolInit(&self->msgbuf, conf->recvBufSize, conf->recvBufInitial, conf->recvBufMax);

    objInstInit(self);
    return self;
}

// Every entry point below takes a NULL `self`, which is the state of a socket that was never added
// to a queue: there is no pool to draw on, so the heap stands in and the ceiling simply does not
// apply. Keeping that here rather than at the call sites is what lets the send path stay branchless
// about whether it has a queue.

NetMessage* NetPool_allocHeader(_In_ NetPool* self)
{
    NetMessage* msg = self ? (NetMessage*)prqPop(&self->msghdr) : NULL;

    if (!msg)
        msg = xaAlloc(sizeof(NetMessage), XA_Zero);
    else
        memset(msg, 0, sizeof(NetMessage));

    return msg;
}

_Check_return_ _Ret_maybenull_ NetMessage* NetPool_allocMsg(_In_ NetPool* self)
{
    // A filter's output rides the same pooled buffers as everything else on the datagram path, so a
    // busy chain allocates nothing in steady state -- and is held to the same ceiling, which is why
    // running dry is reported rather than allocated around.
    if (!self)
        return NULL;

    Buffer buf = bufpoolGet(&self->msgbuf);
    if (!buf)
        return NULL;

    NetMessage* msg = NetPool_allocHeader(self);
    msg->kind       = NMSG_Data;
    msg->buf        = buf;
    msg->flags      = NMF_PoolBuf;

    return msg;
}

_At_(*msg, _Pre_notnull_ _Post_null_) void NetPool_freeMsg(_In_ NetPool* self, _Inout_ NetMessage** msg)
{
    NetMessage* m = *msg;
    *msg          = NULL;

    // Where the payload goes back to travels with the message: a wire packet and a filter's own
    // output came from a pool, an oversized send payload from the heap. Returning a heap buffer to
    // the pool would hand out a short buffer as a full-sized one later; destroying a pooled buffer
    // permanently shrinks the pool.
    if (m->buf) {
        if (self && (m->flags & NMF_PoolBuf))
            bufpoolPut(&self->msgbuf, &m->buf);
        else
            bufDestroy(&m->buf);
    }

    // An NMSG_Accept carries one reference to the accepted socket; release it here so every retire
    // path -- delivered or dropped undelivered -- reclaims it. The kind has to be checked rather
    // than the pointer: `asock` shares storage with NMSG_Timer's id, and a timer id read as a
    // pointer is a wild release.
    if (m->kind == NMSG_Accept && m->asock)
        objRelease(&m->asock);

    // A full pool just means we allocated past the steady state at some point; drop the header
    // rather than growing the freelist without bound.
    if (!self || !prqPush(&self->msghdr, m))
        xaFree(m);
}

void NetPool_freeMsgQueue(_In_ NetPool* self, _Inout_ NetMsgQueue* mq)
{
    NetMessage* msg;
    while ((msg = netMsgQueuePop(mq))) NetPool_freeMsg(self, &msg);
}

void NetPool_destroy(_In_ NetPool* self)
{
    // Nothing is left holding a message by the time the last reference goes -- that is the whole
    // point of everything that can hold one keeping a reference to this -- so the freelist only has
    // spare headers on it, and the buffer pool only has buffers nobody took.
    NetMessage* msg;
    while ((msg = (NetMessage*)prqPop(&self->msghdr)) != NULL) {
        xaFree(msg);
    }
    prqDestroy(&self->msghdr);

    bufpoolDestroy(&self->msgbuf);
}

// Autogen begins -----
// clang-format off
#include "net/pool.auto.inc"
// clang-format on
// Autogen ends -------

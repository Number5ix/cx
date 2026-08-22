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

#include "net_private.h"
#include "cx/buffer/bufring_private.h"

_objinit_guaranteed bool NetSocket_init(_In_ NetSocket* self)
{
    if (self->mru == 0)
        self->mru = 1500;   // default to ethernet packet size

    // NetSocketBufs is a union whose live arm is fixed by `type`, and codegen cannot see its
    // members (see NetSocketBufs), so the correct arm is built by hand here alongside the flow
    // storage that also depends on type. A stream socket buffers an inbound byte stream and owns
    // an outbound chain, and has exactly one flow created up front so single-connection consumers
    // never construct one. A datagram socket has no receive ring at all -- packets are pooled
    // buffers delivered on flow inboxes -- only a send queue, and it demultiplexes many peers
    // into a table.
    if (self->type == NST_Datagram) {
        // Outbound datagram queue: netsocketSend() pushes here when the OS will not take a datagram
        // immediately, and the send path drains it on write-readiness. Whole NetMessages, each with
        // its own destination address.
        prqInitDynamic(&self->bufs.dgram.send, 32, 256, 0, PRQ_Grow_100, PRQ_Grow_50);
        htInit(&self->flows, NetAddr, object, 64);
    } else {
        // TODO: Make the buffer segment size configurable
        bufringInit(&self->bufs.stream.recv, 65536);
        bufchainInit(&self->bufs.stream.send, 65536);
        self->flow = netflowCreate(self, &self->remote);
    }

    // The connect fallback list is [noinit] (built by hand like the other type-specific storage);
    // NetSocket_destroy tears it down unconditionally, so it must be initialized unconditionally.
    saInit(&self->connQueue, NetAddr, 4);

    // The send buffer starts empty, so a send can go out immediately without queuing.
    atomicStore(bool, &self->canSend, true, Relaxed);

    // Autogen begins -----
    mutexInit(&self->recvLock);
    mutexInit(&self->sendLock);
    rwlockInit(&self->flowLock);
    mutexInit(&self->connectLock);
    return true;
    // Autogen ends -------
}

// Stream receive only. A datagram is a discrete message with no per-socket receive ring behind it
// (NetSocketBufs has no dgram.recv arm) -- it is delivered whole on the NET_DataReceived event as
// event->recv.msg, the only place it exists, so a pull from here has nothing to return.

size_t NetSocket_recv(_In_ NetSocket* self, _Out_ uint8* buf, size_t bufsz, _Out_opt_ NetAddr* src, flags_t flags)
{
    unused_noeval(src);
    unused_noeval(flags);

    if (self->type != NST_Stream)
        return 0;

    // When a filter is installed the socket's receive ring holds raw wire bytes; the application
    // reads decoded plaintext out of the flow chain's application end instead (the decode pass
    // already ran on the worker before this event was delivered).
    NetFlow* flow = self->flow;
    size_t copied = 0;

    if (flow && saSize(flow->filters) > 0) {
        withMutex (&flow->filterLock) {
            NetStreamFilter* head = saSize(flow->filters) > 0
                ? objDynCast(NetStreamFilter, flow->filters.a[0])
                : NULL;
            if (head)
                copied = bufringRead(&head->decOut, buf, bufsz);
        }
    } else {
        withMutex (&self->recvLock)
            copied = bufringRead(&self->bufs.stream.recv, buf, bufsz);
    }

    return copied;
}

bool NetSocket_recvMsgs(_In_ NetSocket* self, socketRecvCB cb, _In_opt_ void* ctx)
{
    if (self->type != NST_Stream)
        return false;

    NetFlow* flow  = self->flow;
    bool filtered  = flow && saSize(flow->filters) > 0;
    bool done      = false;
    bool ret       = false;

    do {
        NetMessage msg = { 0 };

        // With a filter installed the application drains decoded plaintext from the chain's
        // application end, under the flow's filter lock; otherwise it drains the raw receive ring
        // directly, under the receive lock.
        Mutex* lock = filtered ? &flow->filterLock : &self->recvLock;

        withMutex (lock) {
            BufRing* ring = &self->bufs.stream.recv;
            if (filtered) {
                NetStreamFilter* head = saSize(flow->filters) > 0
                    ? objDynCast(NetStreamFilter, flow->filters.a[0])
                    : NULL;
                ring = head ? &head->decOut : NULL;
            }

            // Hand out the ring's own head segment when it is 0-aligned, which is the common case
            // and costs no copy. When alignment prevents the steal, fall back to copying the
            // contiguous run into a fresh buffer.
            msg.buf = ring ? _bufringStealHead(ring) : NULL;
            if (ring && !msg.buf) {
                size_t avail = _bufringReadContigAvail(ring);
                if (avail > 0) {
                    msg.buf = bufCreate(avail);
                    bufringRead(ring, msg.buf->data, avail);
                }
            }
        }

        if (msg.buf) {
            done = !cb(self, &msg, ctx);
            ret  = true;
        } else {
            done = true;   // ring drained
        }

        // If the callback took ownership it set msg.buf to NULL, making this a no-op.
        bufDestroy(&msg.buf);
    } while (!done);

    return ret;
}

// ---------------------------------------------------------------------------------------------
// Accept path
//
// Backend-independent, mirroring the connect and send paths: a backend pulls a connection off a
// listening socket's backlog and wraps it in a platform NetSocket, then hands it here. This decides
// admission (NQ_AutoAccept) and delivers NET_Accepted through the listener's flow, so the callback
// runs on a worker, ordered against everything else on that listener. Only the OS accept and the
// backend's watch/post are platform-specific (netPlatformAccept / the acceptArm hook).
// ---------------------------------------------------------------------------------------------

void NetSocket__accepted(_In_ NetSocket* self, _Inout_ NetSocket* newSock, _In_opt_ NetAddr* peer)
{
    if (!newSock)
        return;

    if (peer)
        newSock->remote = *peer;

    // The listener must have a queue and a flow to deliver on -- both are true for any stream socket
    // that reached NS_Listening through a queue. If the listener is being torn down (its weak queue
    // arm already broken), there is nowhere to deliver, so drop the accepted socket: releasing the
    // platform factory's reference closes its handle.
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q || !self->flow) {
        if (q)
            objRelease(&q);
        objRelease(&newSock);
        return;
    }

    // An accepted connection inherits the listener's filters.
    for (int32 i = 0; i < saSize(self->filters); i++) {
        if (self->filters.a[i])
            netsocketAddFilter(newSock, self->filters.a[i]);
    }

    // NQ_AutoAccept: register the socket with the queue up front so the application receives it
    // already managed (associated with the backend and being serviced for receive). The add
    // acquires its own reference; the platform factory's reference still travels on the message.
    if ((q->conf.flags & NQ_AutoAccept) && !_netqueueShuttingDown(q))
        netqueueAddSocket(q, newSock);

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_Accept;
    msg->asock      = newSock;   // transfers the platform factory's reference to the message
    if (peer)
        msg->addr = *peer;
    netqueue_submit(q, self->flow, msg);

    objRelease(&q);
}

void NetSocket__listenArm(_In_ NetSocket* self)
{
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (q) {
        netqueueAcceptArm(q, self);
        objRelease(&q);
    }
}

bool NetSocket_close(_In_ NetSocket* self)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    // If a connect is in flight, abort it so its held self-reference and the queue's connecting
    // count are released rather than stranded on a socket that is going away.
    uint32 cst = atomicLoad(uint32, &self->state, Relaxed);
    if (cst == NS_Connecting || cst == NS_Resolving)
        netsocket_connectCancel(self);

    NetQueue* queue = objAcquireFromWeak(NetQueue, self->queue);
    if (queue) {
        // removeSocket() closes the flows on the way out, so that flow->user teardown never runs
        // after the socket it belonged to is gone.
        netqueueRemoveSocket(queue, self);
        objRelease(&queue);
    } else {
        netsocket_closeFlows(self, NCR_SocketClosed);
    }

    atomicStore(uint32, &self->state, NS_Closed, Relaxed);

    return true;
}

void NetSocket_setHandlers(_In_ NetSocket* self, _In_opt_ const NetHandlers* handlers,
                           _In_opt_ void* ctx)
{
    self->handlers   = (NetHandlers*)handlers;
    self->handlerCtx = ctx;
}

void NetSocket_destroy(_In_ NetSocket* self)
{
    // Tear down the live union arm by hand -- codegen cannot see the members, and destroying the
    // wrong arm is a silent leak or a read of uninitialized storage. Mirror NetSocket_init(): a
    // datagram socket has a send queue that may still hold queued NetMessages (each with its own
    // buffer); a stream socket has the receive ring and send chain. Undelivered *received*
    // datagrams are not here -- they live on flow inboxes and are freed when the flows are.
    if (self->type == NST_Datagram) {
        NetMessage* m;
        while ((m = (NetMessage*)prqPop(&self->bufs.dgram.send)) != NULL) {
            bufDestroy(&m->buf);
            xaFree(m);
        }
        prqDestroy(&self->bufs.dgram.send);
    } else {
        bufringDestroy(&self->bufs.stream.recv);
        bufchainDestroy(&self->bufs.stream.send);
    }

    // Autogen begins -----
    objDestroyWeak(&self->queue);
    mutexDestroy(&self->recvLock);
    mutexDestroy(&self->sendLock);
    htDestroy(&self->flows);
    rwlockDestroy(&self->flowLock);
    objRelease(&self->flow);
    saDestroy(&self->filters);
    saDestroy(&self->connQueue);
    mutexDestroy(&self->connectLock);
    // Autogen ends -------
}

bool NetSocket_addFilter(_In_ NetSocket* self, _In_ NetFilter* filter)
{
    if (!filter || atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    // The filter gets one say in whether it belongs on this socket at all, before any flow of it
    // exists. After this the answer is assumed for every flow the socket ever opens.
    if (!netfilterCanFilter(filter, self->type))
        return false;

    // Appended, so the first filter attached is the stage closest to the application. The socket
    // acquires its own reference: the same filter object commonly serves every accepted connection
    // on a server, so attaching must not consume the caller's.
    saPush(&self->filters, object, filter);

    // Flows that already exist need the matching stage right now, otherwise a filter attached to a
    // live socket (an accepted connection, a datagram peer already talking) would silently apply to
    // nothing.
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    sa_NetFlow flows;
    netflow_snapshotFlows(self, &flows);

    for (int32 i = 0; i < saSize(flows); i++) {
        netflow_addFilter(flows.a[i], self, filter);
        netflow_primeFilters(flows.a[i], q, self);
    }

    saDestroy(&flows);
    objRelease(&q);

    return true;
}

void NetSocket_removeFilters(_In_ NetSocket* self)
{
    saDestroy(&self->filters);   // releases the socket's reference to each factory

    // The stages the factories produced live on the flows, so dropping the socket's list is only
    // half the job -- without this a flow would go on encoding for a socket that no longer has any
    // filters at all.
    sa_NetFlow flows;
    netflow_snapshotFlows(self, &flows);

    for (int32 i = 0; i < saSize(flows); i++) netflow_clearFilters(flows.a[i]);

    saDestroy(&flows);
}

// Autogen begins -----
// clang-format off
bool NetSocket_send(_In_ NetSocket* self, _In_ uint8* data, size_t len, _In_opt_ NetAddr* dest, flags_t flags);
bool NetSocket_connect(_In_ NetSocket* self, _In_ strref host, uint16 port);
bool NetSocket__wantWrite(_In_ NetSocket* self);
void NetSocket__flushSend(_In_ NetSocket* self, _In_opt_ NetQueue* q);
void NetSocket__sendError(_In_ NetSocket* self, _In_opt_ NetQueue* q, NetErrorCode err, _In_ NetAddr* peer);
void NetSocket__connectResult(_In_ NetSocket* self, NetErrorCode err);
bool NetSocket__readinessConnect(_In_ NetSocket* self, _Inout_ NetQueue* q, _In_ NetAddr* addr);
void NetSocket__connectCancel(_In_ NetSocket* self);
void NetSocket__dropFlow(_In_ NetSocket* self, _Inout_ NetFlow* flow);
void NetSocket__closeFlows(_In_ NetSocket* self, NetCloseReason reason);
#include "net/socket.auto.inc"
// clang-format on
// Autogen ends -------

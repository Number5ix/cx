// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/flow.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "net_private.h"
#include <cx/time/clock.h>

_objinit_guaranteed bool NetFlow_init(_In_ NetFlow* self)
{
    // Stamp activity at birth: a zero lastActive would make a brand-new flow the oldest thing
    // in the table -- the preferred reclaim victim and instantly past any reclaimMinIdle bar.
    atomicStore(uint32, &self->lastActive, _netLruTick(), Relaxed);

    // Autogen begins -----
    rwlockInit(&self->handlerLock);
    mutexInit(&self->filterLock);
    return true;
    // Autogen ends -------
}

_objfactory_guaranteed NetFlow* NetFlow_create(NetSocket* socket, NetAddr* peer)
{
    NetFlow* self = objInstCreate(NetFlow);

    self->socket = objGetWeak(NetSocket, socket);
    if (peer)
        self->peer = *peer;

    // Take the pool now and hold it, rather than resolving it through the socket at each use: by
    // the time a flow is being torn down, either weak arm may already be broken, and that is
    // exactly when it still has messages to hand back. A stream socket creates its flow during its
    // own init and has no queue yet -- netqueueAddSocket() fills that case in.
    NetQueue* q = objAcquireFromWeak(NetQueue, socket->queue);
    if (q) {
        self->pool = objAcquire(q->pool);
        objRelease(&q);
    }

    objInstInit(self);
    return self;
}

bool NetFlow_close(_In_ NetFlow* self)
{
    return netflow_close(self, NCR_AppClosed);
}

void NetFlow_setHandlers(_In_ NetFlow* self, _In_opt_ const NetHandlers* handlers,
                         _In_opt_ void* ctx)
{
    withWriteLock (&self->handlerLock) {
        objDestroyWeak(&self->handlerWeak);
        self->handlers   = (NetHandlers*)handlers;
        self->handlerCtx = ctx;
    }
}

void NetFlow_setHandlersObj(_In_ NetFlow* self, _In_opt_ const NetHandlers* handlers,
                            _In_opt_ ObjInst* ctx)
{
    ObjInst_WeakRef* weak = ctx ? objGetWeak(ObjInst, ctx) : NULL;

    withWriteLock (&self->handlerLock) {
        objDestroyWeak(&self->handlerWeak);
        self->handlers    = (NetHandlers*)handlers;
        self->handlerCtx  = NULL;
        self->handlerWeak = weak;
    }
}

void NetFlow_destroy(_In_ NetFlow* self)
{
    // Anything still sitting in the inbox or the ready list at this point belongs to a flow
    // nobody will ever dispatch again, so the messages have to be released here rather than
    // drained. This is not the normal teardown path -- normally the terminal event is delivered
    // first and both lists are already empty. The flow's own pool reference is what makes doing it
    // here safe: these are pooled receive buffers, and destroying rather than returning them would
    // take that capacity away from every socket on the queue permanently.
    NetMessage* msg = (NetMessage*)atomicExchange(ptr, &self->inbox, NULL, AcqRel);
    while (msg) {
        NetMessage* next = msg->next;
        netpoolFreeMsg(self->pool, &msg);
        msg = next;
    }

    msg = self->ready;
    while (msg) {
        NetMessage* next = msg->next;
        netpoolFreeMsg(self->pool, &msg);
        msg = next;
    }
    self->ready = self->readytail = NULL;

    // Similar to the inbox teardown above; normally the filter chain buffer is empty at this
    // point, but if it isn't, we need to clear it out. Anything still here was never encoded
    // or sent.
    netpoolFreeMsgQueue(self->pool, &self->encInMsgs);
    if (self->encIn) {
        bufringDestroy(self->encIn);
        xaFree(self->encIn);
        self->encIn = NULL;
    }

    // Autogen begins -----
    objDestroyWeak(&self->socket);
    objRelease(&self->pool);
    saDestroy(&self->user);
    objDestroyWeak(&self->handlerWeak);
    rwlockDestroy(&self->handlerLock);
    saDestroy(&self->timers);
    saDestroy(&self->filters);
    mutexDestroy(&self->filterLock);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// Inbox: a simple lock-free stack that any thread can push onto without taking a lock.
//
// Many ingest threads can push messages at once, but the claim protocol guarantees only one
// worker ever reads from a given flow at a time, so the reading side needs no locking either.
// Messages land on the stack in reverse order, so the reader flips them back into arrival order
// before handing them out.
// ---------------------------------------------------------------------------------------------

_Check_return_ bool NetFlow__push(_In_ NetFlow* self, _Inout_ NetMessage* msg)
{
    devAssert(msg && !msg->next);

    void* head = atomicLoad(ptr, &self->inbox, Relaxed);
    do {
        msg->next = (NetMessage*)head;
    } while (!atomicCompareExchange(ptr, weak, &self->inbox, &head, msg, Release, Relaxed));

    // Claim responsibility for putting the flow on the runqueue only if it was not already
    // there. The exchange must happen after the push so that a consumer draining right now
    // either sees the message or leaves the flow enqueued for someone who will.
    return atomicExchange(uint32, &self->queued, 1, AcqRel) == 0;
}

_Ret_maybenull_ NetMessage* NetFlow__pop(_In_ NetFlow* self)
{
    if (!self->ready) {
        // Ready list is dry; take the whole inbox in one exchange and reverse it into FIFO
        // order. The consumer holds `ready` privately and only touches `inbox` again when it
        // runs out, so the common case is a plain pointer chase with no atomics at all.
        NetMessage* stack = (NetMessage*)atomicExchange(ptr, &self->inbox, NULL, AcqRel);
        NetMessage* fifo  = NULL;
        NetMessage* tail  = NULL;

        while (stack) {
            NetMessage* next = stack->next;
            stack->next      = fifo;
            if (!fifo)
                tail = stack;
            fifo  = stack;
            stack = next;
        }

        self->ready     = fifo;
        self->readytail = tail;
    }

    NetMessage* msg = self->ready;
    if (!msg)
        return NULL;

    self->ready = msg->next;
    if (!self->ready)
        self->readytail = NULL;
    msg->next = NULL;

    return msg;
}

bool NetFlow__close(_In_ NetFlow* self, NetCloseReason reason)
{
    uint32 expected = 0;
    if (!atomicCompareExchange(uint32, strong, &self->dying, &expected, 1, AcqRel, Relaxed))
        return false;   // somebody else is already closing it

    self->closeReason = (uint8)reason;

    NetQueue* q     = netflow_queue(self);
    NetMessage* msg = netpoolAllocHeader(self->pool);
    msg->kind       = NMSG_Terminal;
    msg->reason     = (uint8)reason;

    // The terminal event goes through the same path as a packet, which is what makes it land
    // behind everything already queued for this flow rather than racing it.
    netqueue_submit(q, self, msg);

    objRelease(&q);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------------------------

NetTimerId NetFlow_addTimer(_In_ NetFlow* self, int64 delay, flags_t flags)
{
    NetQueue* q = netflow_queue(self);
    if (!q)
        return 0;

    NetTimerId id = netqueue_addTimer(q, self, delay, flags, NULL, NULL);
    objRelease(&q);
    return id;
}

bool NetFlow_cancelTimer(_In_ NetFlow* self, NetTimerId id)
{
    NetQueue* q = netflow_queue(self);
    if (!q)
        return false;

    bool ret = netqueue_cancelTimer(q, id);
    objRelease(&q);
    return ret;
}

bool NetFlow_rearmTimer(_In_ NetFlow* self, NetTimerId id, int64 delay)
{
    NetQueue* q = netflow_queue(self);
    if (!q)
        return false;

    bool ret = netqueue_rearmTimer(q, id, delay);
    objRelease(&q);
    return ret;
}

_Ret_maybenull_ NetQueue* NetFlow__queue(_In_ NetFlow* self)
{
    NetSocket* sock = objAcquireFromWeak(NetSocket, self->socket);
    if (!sock)
        return NULL;

    NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);
    objRelease(&sock);
    return q;
}

// ---------------------------------------------------------------------------------------------
// Flow table and lifecycle
// ---------------------------------------------------------------------------------------------

void NetFlow__snapshotFlows(_In_ NetSocket* sock, _Out_ sa_NetFlow* out)
{
    saInit(out, NetFlow, 16);

    if (sock->type == NST_Datagram) {
        withReadLock (&sock->flowLock) {
            foreach (hashtable, hti, sock->flows) {
                NetFlow* f = (NetFlow*)htiVal(object, hti);
                if (f)
                    saPush(out, NetFlow, f);
            }
        }
    } else if (sock->flow) {
        saPush(out, NetFlow, sock->flow);
    }
}

void NetSocket__dropFlow(_In_ NetSocket* self, _Inout_ NetFlow* flow)
{
    if (self->type == NST_Datagram) {
        withWriteLock (&self->flowLock) {
            // Only remove the entry if it is still ours. A flow that was torn down and whose
            // peer has since come back will have a different object under the same key.
            htelem e = htFind(self->flows, NetAddr, flow->peer, none, NULL);
            if (e && (NetFlow*)hteVal(self->flows, object, e) == flow)
                htRemove(&self->flows, NetAddr, flow->peer);
        }
    } else if (self->flow == flow) {
        objRelease(&self->flow);
    }

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (q) {
        atomicFetchSub(uint32, &q->nflows, 1, AcqRel);
        objRelease(&q);
    }
}

uint32 NetQueue__reclaimFlows(_In_ NetQueue* self, _Inout_ NetSocket* sock)
{
    if (self->conf.noReclaim || sock->type != NST_Datagram)
        return 0;

    uint32 want = self->conf.reclaimBatch ? self->conf.reclaimBatch : 1;
    uint32 got  = 0;

    // reclaimMinIdle, converted to lastActive's ~1s tick units, rounding up so any nonzero
    // setting means at least one tick. Flows more recently active than this are never victims:
    // when nothing qualifies, the new peer is refused (NET_FlowRefused) rather than churning a
    // live session out -- which is the point when session setup costs more than a turned-away
    // peer.
    uint32 minTicks = (uint32)((self->conf.reclaimMinIdle + ((1 << 20) - 1)) >> 20);
    uint32 now      = _netLruTick();

    // Approximate LRU by scanning for the oldest entries rather than maintaining an intrusive
    // list. Moving a flow to the front of a list on every packet would put a contended CAS on
    // the hottest path in the system; a scan costing microseconds at a cap hit is irrelevant
    // next to that, and "approximately the oldest" is entirely sufficient here.
    while (got < want) {
        NetFlow* oldest = NULL;
        uint32 oldestAt = 0;

        withReadLock (&sock->flowLock) {
            foreach (hashtable, hti, sock->flows) {
                NetFlow* f = (NetFlow*)htiVal(object, hti);
                if (!f || atomicLoad(uint32, &f->dying, Relaxed))
                    continue;

                uint32 at = atomicLoad(uint32, &f->lastActive, Relaxed);
                if (minTicks && (uint32)(now - at) < minTicks)
                    continue;   // not idle long enough to be worth evicting

                if (!oldest || at < oldestAt) {
                    oldest   = f;
                    oldestAt = at;
                }
            }
            if (oldest)
                objAcquire(oldest);
        }

        if (!oldest)
            break;

        if (netflow_close(oldest, NCR_Reclaimed))
            got++;

        objRelease(&oldest);
    }

    return got;
}

_Ret_maybenull_ NetFlow* NetQueue__admitFlow(_In_ NetQueue* self, _Inout_ NetSocket* sock, _In_ NetAddr* peer)
{
    NetFlow* flow = netflowCreate(sock, peer);
    if (!flow)
        return NULL;

    // Build the filter chain before the flow is reachable by anything else. Doing it after the
    // insert would leave a window in which an ingest thread could hand a packet to a worker that
    // finds no chain and delivers it raw -- exactly once per new peer, which is the worst possible
    // place for it.
    netflow_buildFilters(flow, sock);

    if (sock->type == NST_Datagram) {
        bool inserted = false;
        withWriteLock (&sock->flowLock) {
            htelem e = htFind(sock->flows, NetAddr, *peer, none, NULL);
            if (!e) {
                htInsert(&sock->flows, NetAddr, *peer, object, flow);
                inserted = true;
            } else {
                // Lost a race to another ingest thread; take theirs instead of ours.
                NetFlow* cur = (NetFlow*)hteVal(sock->flows, object, e);
                objRelease(&flow);
                flow = cur ? objAcquire(cur) : NULL;
            }
        }

        if (!inserted)
            return flow;   // either the winner's flow, or NULL if the table lost it
    } else {
        objRelease(&sock->flow);
        sock->flow = objAcquire(flow);
    }

    atomicFetchAdd(uint32, &self->nflows, 1, AcqRel);

    // Announce the new flow before the caller can push anything else: NET_FlowOpen is the
    // session-setup hook, so it must land in the inbox ahead of the first data packet. Only the
    // thread that actually inserted gets here (a lost race returned above), so exactly one open
    // fires per flow -- and only for datagram flows, since a stream's session start is already
    // announced by NET_Connection / NET_Accepted.
    if (sock->type == NST_Datagram) {
        NetMessage* msg = netpoolAllocHeader(self->pool);
        msg->kind       = NMSG_FlowOpen;
        netqueue_submit(self, flow, msg);
    }

    // Give the new chain its chance to open a negotiation. Queued after NET_FlowOpen so that any
    // notification it raises still lands behind the event the application sets its state up in.
    netflow_primeFilters(flow, self, sock);

    return flow;
}

_Ret_maybenull_ NetFlow* NetQueue__findFlow(_In_ NetQueue* self, _Inout_ NetSocket* sock, _In_ NetAddr* peer, bool create)
{
    NetFlow* flow = NULL;

    if (sock->type == NST_Datagram) {
        withReadLock (&sock->flowLock) {
            htelem e = htFind(sock->flows, NetAddr, *peer, none, NULL);
            if (e) {
                NetFlow* cur = (NetFlow*)hteVal(sock->flows, object, e);
                if (cur)
                    flow = objAcquire(cur);
            }
        }
    } else if (sock->flow) {
        flow = objAcquire(sock->flow);
    }

    if (flow) {
        atomicStore(uint32, &flow->lastActive, _netLruTick(), Relaxed);

        // A packet for a flow that is dying but whose terminal event has not been delivered yet
        // resurrects it, provided the close was speculative. Reclaim guessed the peer was gone
        // and has just been proven wrong; every other cause was a decision that an arriving
        // packet does not un-decide.
        if (atomicLoad(uint32, &flow->dying, Acquire)) {
            if (flow->closeReason == NCR_Reclaimed) {
                uint32 expected = 1;
                if (atomicCompareExchange(uint32,
                                          strong,
                                          &flow->dying,
                                          &expected,
                                          0,
                                          AcqRel,
                                          Relaxed))
                    flow->closeReason = NCR_None;
                // The already-queued terminal message is not removed from the inbox; the
                // consumer discards it when it finds the flow no longer dying.
            } else {
                objRelease(&flow);   // genuinely going away; caller must not use it
                return NULL;
            }
        }

        return flow;
    }

    if (!create)
        return NULL;

    // At the cap, try to make room. With noReclaim set nothing is ever evicted, so a new peer is
    // refused via the flowRefused handler instead of throwing out the one being debugged --
    // failing loudly is the right behavior there.
    if (self->conf.maxflows && atomicLoad(uint32, &self->nflows, Relaxed) >= self->conf.maxflows) {
        netqueue_reclaimFlows(self, sock);

        // Reclaim only *marks* flows dying; they do not leave the table until their terminal
        // event has been delivered, so the count may not have moved yet. Refusing here is
        // correct: the caller falls back to flowRefused, which is exactly the no-allocation path
        // that makes a public UDP port safe against a spoofed-source flood.
        if (atomicLoad(uint32, &self->nflows, Relaxed) >= self->conf.maxflows)
            return NULL;
    }

    return netqueue_admitFlow(self, sock, peer);
}

void NetSocket__closeFlows(_In_ NetSocket* self, NetCloseReason reason)
{
    // Snapshot rather than closing in place: netflow_close() submits to the runqueue, which must
    // not run with the flow table locked.
    sa_NetFlow victims;
    netflow_snapshotFlows(self, &victims);

    for (int32 i = 0; i < saSize(victims); i++) {
        netflow_close(victims.a[i], reason);
    }

    saDestroy(&victims);
}

bool NetFlow_send(_In_ NetFlow* self, _In_ uint8* data, size_t len, flags_t flags)
{
    // Purely a convenience: the socket's send path already resolves the destination's flow and runs
    // its filter chain, so sending "on a flow" is nothing more than naming the peer. Keeping the
    // one implementation is the point -- a reply through here and a send addressed to the same peer
    // take exactly the same path.
    NetSocket* sock = objAcquireFromWeak(NetSocket, self->socket);
    if (!sock)
        return false;

    bool ret = netsocketSend(sock, data, len, &self->peer, flags);

    objRelease(&sock);
    return ret;
}

// Autogen begins -----
// clang-format off
void NetFlow__addFilter(_In_ NetFlow* self, _Inout_ NetSocket* sock, _In_ NetFilter* filter);
void NetFlow__buildFilters(_In_ NetFlow* self, _Inout_ NetSocket* sock);
void NetFlow__clearFilters(_In_ NetFlow* self);
void NetFlow__filterShutdown(_In_ NetFlow* self);
void NetFlow__filterNotify(_In_ NetFlow* self, _In_opt_ NetQueue* q, _In_opt_ NetSocket* sock, bool onWorker);
bool NetFlow__filterStreamSend(_In_ NetFlow* self, _In_opt_ NetQueue* q, _Inout_ NetSocket* sock, _In_reads_bytes_opt_(len) const uint8* data, size_t len);
void NetFlow__filterStreamRecv(_In_ NetFlow* self, _In_opt_ NetQueue* q, _Inout_ NetSocket* sock);
bool NetFlow__filterDatagramEncode(_In_ NetFlow* self, _In_opt_ NetQueue* q, _In_reads_bytes_opt_(len) const uint8* data, size_t len, _Inout_ NetMsgQueue* out, _Out_opt_ bool* fatalp);
bool NetFlow__filterDatagramSend(_In_ NetFlow* self, _In_opt_ NetQueue* q, _Inout_ NetSocket* sock, _In_reads_bytes_opt_(len) const uint8* data, size_t len);
bool NetFlow__filterDatagramRecv(_In_ NetFlow* self, _In_opt_ NetQueue* q, _Inout_ NetSocket* sock, _Inout_ NetMessage* msg, _Inout_ NetMsgQueue* out);
void NetFlow__primeFilters(_In_ NetFlow* self, _In_opt_ NetQueue* q, _Inout_ NetSocket* sock);
#include "net/flow.auto.inc"
// clang-format on
// Autogen ends -------

// ---------------------------------------------------------------------------------------------
// Data plane
//
// Everything between the application's bytes and the wire, in both directions. A subsystem in its
// own right, separate from the socket's lifecycle: the socket object only supplies the handle, the
// two buffer arms, and the send lock.
//
// The two halves below have to stay in one translation unit. The filter drivers run the chain and
// then flush with the socket's send lock already held (flushLocked), and the datagram side builds
// its messages out of the same pooled-buffer helpers the unfiltered path uses -- pulling them apart
// would export a lock precondition across a file boundary, where it could no longer be checked by
// reading the code around it.
// ---------------------------------------------------------------------------------------------

#include "net_private.h"
#include "cx/buffer/bufring_private.h"
#include <cx/container.h>

// ---------------------------------------------------------------------------------------------
// Send path
//
// Backend-independent: the queueing, watermark, and scatter/gather logic lives here, and only the
// syscall differs (netSockSendv / netSockSendTo). A readiness backend watches sockets with queued
// data for write-readiness and calls netsocket_flushSend() to push more out; the completion backend
// will post the write and commit on completion instead.
// ---------------------------------------------------------------------------------------------

// Bytes currently queued to send. The stream chain tracks its own total; the datagram queue does not.
static size_t sendQueuedBytes(NetSocket* sock)
{
    return sock->type == NST_Stream ? sock->bufs.stream.send.total : sock->sendQueued;
}

// Push as much of the stream send chain as the OS will accept, using scatter/gather so no segment
// is copied or coalesced. Runs under sendLock. Stops on WouldBlock (send buffer full) or a short
// write.
static void flushStreamBytes(NetSocket* sock, NetErrorCode* err)
{
    *err = NERR_None;

    while (sock->bufs.stream.send.total > 0) {
        BufIov iov[NET_MAX_IOV];
        size_t niov = 0;
        bufchainGatherIov(&sock->bufs.stream.send, iov, NET_MAX_IOV, &niov);
        if (niov == 0)
            break;

        NetErrorCode e;
        intptr n = netSockSendv(sock->handle, iov, niov, &e);
        if (n < 0) {
            if (e != NERR_WouldBlock)
                *err = e;   // a fatal send error; the caller reports it and closes the flow
            break;
        }
        if (n == 0)
            break;

        bufchainSkip(&sock->bufs.stream.send, (size_t)n);
        if ((size_t)n < iov[0].len)
            break;   // partial write: the send buffer is full, nothing more will go right now
    }
}

// Drain queued datagrams to their destinations. Peek before pop so a datagram the OS will not
// accept stays at the head in order; a fatal per-datagram error drops that one and keeps draining.
// Runs under sendLock, which is the only thing that touches the datagram send queue, so peek/pop is
// safe. `errAddr` receives the destination of the (last) dropped datagram alongside its error in
// `err`, so the caller can route the report to that peer's flow.
static void flushDgramBytes(NetQueue* q, NetSocket* sock, NetErrorCode* err, NetAddr* errAddr)
{
    *err = NERR_None;

    for (;;) {
        NetMessage* m = (NetMessage*)prqPeek(&sock->bufs.dgram.send, 0);
        if (!m)
            break;

        size_t len = m->buf ? m->buf->len : 0;
        NetErrorCode e;
        intptr n = netSockSendTo(sock->handle, m->buf ? m->buf->data : NULL, len, &m->addr, &e);
        if (n < 0 && e == NERR_WouldBlock)
            break;   // send buffer full; leave this datagram queued at the head

        prqPop(&sock->bufs.dgram.send);
        sock->sendQueued -= len;
        if (n < 0) {
            *err     = e;   // dropped this datagram on a fatal error, but keep draining the rest
            *errAddr = m->addr;
        }
        netpoolFreeMsg(_netqueuePool(q), &m);
    }
}

// Flush the live arm, update canSend, and report whether NET_SendReady should now fire. Runs under
// sendLock. The event is gated on sendBlocked so it is a real edge -- fired only after a send was
// actually refused at the high watermark. A fatal flush error lands in `err` (with the failed
// datagram's destination in `errAddr`) for the caller to report through netsocket_sendError() after
// the lock is dropped -- events are never generated from under sendLock.
static bool flushLocked(NetQueue* q, NetSocket* sock, NetErrorCode* err, NetAddr* errAddr)
{
    *err = NERR_None;

    // A completion backend with an overlapped send in flight owns the head of the send buffer until
    // that op completes; flushing synchronously here would send those same bytes a second time.
    // Skip the flush and just recompute the watermark state -- the completion will drain and
    // re-post.
    if (!sock->sendPending) {
        if (sock->type == NST_Stream)
            flushStreamBytes(sock, err);
        else
            flushDgramBytes(q, sock, err, errAddr);
    }

    size_t queued = sendQueuedBytes(sock);
    atomicStore(bool, &sock->canSend, queued == 0, Relaxed);

    if (sock->sendBlocked && queued <= sock->sendLow) {
        sock->sendBlocked = false;
        return true;
    }
    return false;
}

// Deliver NET_SendReady through the flow's own queue so it runs on a worker, serialized after every
// packet already pending for the flow -- never inline on the thread that happened to flush. Stream
// sockets have exactly one flow; datagram send-ready has no single natural flow and is deferred.
static void fireSendReady(NetQueue* q, NetSocket* sock)
{
    if (!q || sock->type != NST_Stream || !sock->flow)
        return;

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_SendReady;
    netqueue_submit(q, sock->flow, msg);
}

void NetSocket__sendError(_In_ NetSocket* self, _In_opt_ NetQueue* q, NetErrorCode err, _In_ NetAddr* peer)
{
    if (!q)
        return;

    NetFlow* flow = NULL;
    if (self->type == NST_Stream) {
        if (self->flow)
            flow = objAcquire(self->flow);
    } else {
        withReadLock (&self->flowLock) {
            htelem e = htFind(self->flows, NetAddr, *peer, none, NULL);
            if (e) {
                NetFlow* f = (NetFlow*)hteVal(self->flows, object, e);
                if (f)
                    flow = objAcquire(f);
            }
        }
    }
    if (!flow)
        return;

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_Error;
    msg->bytes      = (size_t)err;
    netqueue_submit(q, flow, msg);

    if (self->type == NST_Stream)
        netflow_close(flow, NCR_Error);

    objRelease(&flow);
}

bool NetSocket__wantWrite(_In_ NetSocket* self)
{
    return sendQueuedBytes(self) > 0;
}

void NetSocket__flushSend(_In_ NetSocket* self, _In_opt_ NetQueue* q)
{
    bool ready       = false;
    NetErrorCode err = NERR_None;
    NetAddr eaddr    = { 0 };
    withMutex (&self->sendLock) {
        ready = flushLocked(q, self, &err, &eaddr);
    }
    if (err != NERR_None)
        netsocket_sendError(self, q, err, &eaddr);
    if (ready)
        fireSendReady(q, self);
}

// Hand outbound data to the OS on a stream socket, unfiltered. Runs the whole locked section of an
// ordinary send: watermark check, copy into the outbound chain, flush what the OS will take.
static bool sendStreamRaw(NetQueue* q, NetSocket* sock, const uint8* data, size_t len, bool immediate)
{
    bool ret   = false;
    bool ready = false;   // NET_SendReady should fire after we drop the lock
    bool wake  = false;   // outbound data was left queued; poke the ingest loop to watch for write

    NetErrorCode ferr = NERR_None;   // fatal error from the inline flush, reported after unlock
    NetAddr feaddr    = { 0 };

    withMutex (&sock->sendLock) {
        size_t queued = sock->bufs.stream.send.total;

        if (immediate) {
            // Never queue. Only send if there is no backlog, otherwise sending now would jump
            // ahead of already-queued bytes and corrupt the stream.
            if (queued == 0) {
                BufIov iov = { .data = (uint8*)data, .len = len };
                NetErrorCode e;
                intptr n = netSockSendv(sock->handle, &iov, 1, &e);
                ret      = (n >= 0 && (size_t)n == len);
            }
        } else if (sock->sendHigh && queued >= sock->sendHigh) {
            sock->sendBlocked = true;   // over the high watermark; refuse and wait for drain
        } else {
            bufchainWrite(&sock->bufs.stream.send, data, len);
            ready = flushLocked(q, sock, &ferr, &feaddr);
            wake  = sock->bufs.stream.send.total > 0;
            ret   = true;
        }
    }

    if (ferr != NERR_None)
        netsocket_sendError(sock, q, ferr, &feaddr);
    if (ready)
        fireSendReady(q, sock);
    if (wake && q && q->sendPump)
        q->sendPump(q->sendCtx, sock);

    return ret;
}

// Wrap an application payload in a NetMessage addressed to `dest`. A payload that fits a pooled
// buffer rides one, like everything else on the datagram path; an oversized one falls back to a
// plain heap buffer. NMF_PoolBuf is what tells the free paths apart afterwards -- destroying a
// pooled buffer would permanently shrink the pool.
static NetMessage* dgramWrapPayload(NetQueue* q, const uint8* data, size_t len, const NetAddr* dest)
{
    NetPool* pool = _netqueuePool(q);
    NetMessage* m = NULL;

    // A payload the pool's buffers can hold takes the pooled path -- header, buffer, and the
    // NMF_PoolBuf mark that sends both back where they came from. Everything else (oversized, or a
    // pool at its cap) falls back to a bare header over a heap buffer.
    if (q && len <= q->conf.recvBufSize)
        m = netpoolAllocMsg(pool);

    if (!m) {
        m      = netpoolAllocHeader(pool);
        m->buf = bufCreate(len);
    }

    m->kind = NMSG_Data;
    m->addr = *dest;

    if (!m->buf) {
        netpoolFreeMsg(pool, &m);
        return NULL;
    }

    if (len > 0)
        memcpy(m->buf->data, data, len);
    m->buf->len = len;

    return m;
}

// Queue a datagram that could not go out now. Runs under sendLock; consumes the message either way.
static bool dgramQueueMsg(NetQueue* q, NetSocket* sock, NetMessage* msg)
{
    size_t len = msg->buf ? msg->buf->len : 0;

    if (!prqPush(&sock->bufs.dgram.send, msg)) {
        netpoolFreeMsg(_netqueuePool(q), &msg);   // queue full and could not grow; drop
        return false;
    }

    sock->sendQueued += len;
    atomicStore(bool, &sock->canSend, false, Relaxed);
    return true;
}

// Send one already-formed datagram to its own address, queueing it behind anything already waiting.
// Consumes the message. `wake` is set if the backend needs to be poked to drain the rest later.
static bool dgramSendMsg(NetQueue* q, NetSocket* sock, NetMessage* msg, bool* wake)
{
    bool ret = false;

    withMutex (&sock->sendLock) {
        bool doQueue = sock->sendQueued > 0;   // preserve order behind anything already queued

        if (!doQueue) {
            uint8* data = msg->buf ? msg->buf->data : NULL;
            size_t len  = msg->buf ? msg->buf->len : 0;
            NetErrorCode e;
            intptr n = netSockSendTo(sock->handle, data, len, &msg->addr, &e);
            if (n >= 0)
                ret = true;   // went straight out, nothing queued
            else if (e == NERR_WouldBlock)
                doQueue = true;
            // anything else is fatal for this one datagram; it is dropped below
        }

        if (doQueue) {
            ret   = dgramQueueMsg(q, sock, msg);
            msg   = NULL;
            *wake = true;
        }
    }

    if (msg)
        netpoolFreeMsg(_netqueuePool(q), &msg);

    return ret;
}

// Unfiltered datagram send: straight to the wire, or queued behind what is already waiting.
static bool sendDgramRaw(NetQueue* q, NetSocket* sock, const uint8* data, size_t len,
                         const NetAddr* dest, bool immediate)
{
    if (immediate) {
        NetErrorCode e;
        intptr n = netSockSendTo(sock->handle, data, len, dest, &e);
        return n >= 0 && (size_t)n == len;
    }

    bool over = false;
    withMutex (&sock->sendLock) {
        if (sock->sendHigh && sock->sendQueued >= sock->sendHigh) {
            sock->sendBlocked = true;
            over              = true;
        }
    }
    if (over)
        return false;

    NetMessage* m = dgramWrapPayload(q, data, len, dest);
    if (!m)
        return false;

    bool wake = false;
    bool ret  = dgramSendMsg(q, sock, m, &wake);
    if (wake && q && q->sendPump)
        q->sendPump(q->sendCtx, sock);

    return ret;
}

// Resolve the flow whose filter chain owns a datagram bound for `dest`, opening one if the peer is
// new. Sending has never created a flow before, but a filtered socket has no way to encode for a
// peer it has no chain for -- and a chain is per-peer state by definition. The flow fires
// NET_FlowOpen and gets its chain built and primed like any other; at the flow cap there is no
// chain to be had and the send is refused. Returns a reference the caller must release.
static NetFlow* dgramSendFlow(NetQueue* q, NetSocket* sock, const NetAddr* dest)
{
    if (!q)
        return NULL;   // no queue, no flow table, no way to build a chain

    return netqueue_findFlow(q, sock, (NetAddr*)dest, true);
}

bool NetSocket_send(_In_ NetSocket* self, _In_ const uint8* data, size_t len,
                    _In_opt_ NetAddr* dest, flags_t flags)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    bool immediate = (flags & NSO_Immediate) != 0;

    // Resolve the queue once: it owns the message pool the datagram path allocates from, and it
    // carries the send-ready delivery and the ingest-wake hook. A socket not yet on a queue still
    // sends -- it just falls back to plain allocation and skips the wake.
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    bool ret    = false;

    if (self->type == NST_Stream) {
        NetFlow* flow = self->flow;

        if (flow && saSize(flow->filters) > 0) {
            // Filtered: the payload goes into the flow's staging ring and the chain decides what
            // reaches the wire. NSO_Immediate is deliberately ignored here -- a filter owns the
            // framing, and letting a payload jump the chain would put plaintext in the middle of a
            // record stream.
            ret = netflow_filterStreamSend(flow, q, self, data, len);
            netflow_filterNotify(flow, q, self, false);
        } else {
            ret = sendStreamRaw(q, self, data, len, immediate);
        }
    } else if (dest) {
        NetFlow* flow = NULL;
        if (saSize(self->filters) > 0)
            flow = dgramSendFlow(q, self, dest);

        if (flow && saSize(flow->filters) > 0) {
            ret = netflow_filterDatagramSend(flow, q, self, data, len);
            netflow_filterNotify(flow, q, self, false);
        } else if (saSize(self->filters) > 0 && !flow) {
            ret = false;   // filtered socket with nowhere to build a chain: refuse rather than
                           // put the payload on the wire in the clear
        } else {
            ret = sendDgramRaw(q, self, data, len, dest, immediate);
        }

        objRelease(&flow);
    }

    objRelease(&q);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Filter data plane
//
// The choke point where application data becomes wire data and back. A chain always lives on a flow
// and is a pipeline of buffered stages: every inter-stage boundary is a ring for a stream chain, a
// message queue for a datagram one, so a stage consumes from its input and produces into its output
// at its own rate. The drivers walk the chain in the direction of flow, repeating until no stage
// has anything more to produce; whatever is left unconsumed stays in the boundary storage and
// resumes on the next wire read or application send.
//
// Each driver holds the flow's filterLock for the length of the chain walk and drops it before
// anything can reach application code, since a handler is free to call netsocketSend() straight
// back into the encode path. That lock is also what makes a stage safe to write without any
// synchronization of its own: decode runs on the worker holding the flow's claim, encode on
// whatever thread called send, and the two would otherwise meet inside the same session state.
// ---------------------------------------------------------------------------------------------

// Move everything currently readable in a ring into a chain. Used to flush the wire-end stage's
// encOut into the socket send chain.
static size_t drainRingToChain(BufRing* ring, BufChain* chain)
{
    uint8 tmp[4096];
    size_t total = 0, n;
    while ((n = bufringRead(ring, tmp, sizeof(tmp))) > 0) {
        bufchainWrite(chain, tmp, n);
        total += n;
    }
    return total;
}

// Messages waiting in a boundary queue. These hold a handful of entries at most, so counting them on
// demand costs less than a running count every filter would have to maintain correctly.
static size_t msgqCount(const NetMsgQueue* mq)
{
    size_t n = 0;
    for (const NetMessage* m = mq->head; m; m = m->next) n++;
    return n;
}

// One stage of a flow's chain, typed. Every stage of a chain is of the arm matching its socket type
// -- a factory that produced anything else answered canFilter() dishonestly -- so a failed cast means
// a broken chain, and the drivers stop rather than guess.
static NetStreamFilter* streamStage(NetFlow* flow, int32 idx)
{
    return objDynCast(NetStreamFilter, flow->filters.a[idx]);
}

static NetDatagramFilter* dgramStage(NetFlow* flow, int32 idx)
{
    return objDynCast(NetDatagramFilter, flow->filters.a[idx]);
}

// Encode chain for a stream, app -> wire: stage i reads the staging ring (i == 0) or the previous
// stage's encOut, and appends to its own, repeating until no stage has more to produce. The
// wire-end stage's output becomes real outbound bytes on the send chain. Caller holds filterLock
// and sendLock.
static bool streamEncodeChain(NetSocket* sock, NetFlow* flow)
{
    int32 n = saSize(flow->filters);

    // No chain is not a failure: netsocketRemoveFilters() can drop one between the caller's check and
    // this lock, and a socket losing its filters should not cost the flow its life.
    if (n == 0 || !flow->encIn)
        return true;

    bool progress = true;
    while (progress) {
        progress     = false;
        BufRing* src = flow->encIn;

        for (int32 i = 0; i < n; i++) {
            NetStreamFilter* f = streamStage(flow, i);
            if (!f)
                return false;

            size_t before   = src->total;
            intptr produced = netstreamfilterEncode(f, src);
            if (produced < 0)
                return false;
            if (produced > 0 || src->total != before)
                progress = true;

            src = &f->encOut;
        }
    }

    NetStreamFilter* last = streamStage(flow, n - 1);
    if (!last)
        return false;

    drainRingToChain(&last->encOut, &sock->bufs.stream.send);
    return true;
}

// Decode chain for a stream, wire -> app: the wire-end stage reads the socket's receive ring,
// every stage above it reads the decOut of the stage one hop closer to the wire, repeating until
// no stage has more to produce. What the head stage produces is what netsocketRecv() hands the
// application. Caller holds filterLock and recvLock.
static bool streamDecodeChain(NetSocket* sock, NetFlow* flow)
{
    int32 n = saSize(flow->filters);
    if (n == 0)
        return true;   // chain dropped underneath us; see streamEncodeChain

    bool progress = true;
    while (progress) {
        progress = false;

        for (int32 i = n - 1; i >= 0; i--) {
            NetStreamFilter* f = streamStage(flow, i);
            if (!f)
                return false;

            BufRing* src = &sock->bufs.stream.recv;
            if (i < n - 1) {
                NetStreamFilter* below = streamStage(flow, i + 1);
                if (!below)
                    return false;
                src = &below->decOut;
            }

            size_t before   = src->total;
            intptr produced = netstreamfilterDecode(f, src);
            if (produced < 0)
                return false;
            if (produced > 0 || src->total != before)
                progress = true;
        }
    }

    return true;
}

// Encode chain for a datagram, app -> wire. The message analog of streamEncodeChain: stage i
// consumes the flow's staging queue (i == 0) or the previous stage's encOut, and appends to its
// own, repeating until no stage has more to produce. A stage may produce more messages than it
// consumed, which is how an oversized payload is fragmented to fit the wire. Caller holds
// filterLock.
static bool dgramEncodeChain(NetQueue* q, NetFlow* flow)
{
    int32 n = saSize(flow->filters);
    if (n == 0)
        return false;

    bool progress = true;
    while (progress) {
        progress         = false;
        NetMsgQueue* src = &flow->encInMsgs;

        for (int32 i = 0; i < n; i++) {
            NetDatagramFilter* f = dgramStage(flow, i);
            if (!f)
                return false;

            size_t before   = msgqCount(src);
            intptr produced = netdatagramfilterEncodeMsg(f, src);
            if (produced < 0)
                return false;
            if (produced > 0 || msgqCount(src) != before)
                progress = true;

            src = &f->encOut;
        }
    }

    return true;
}

// Collect what the wire end of a datagram chain produced, addressed to the flow's peer. A stage has
// no say in where a flow's traffic goes -- the destination is the flow's identity, so it is stamped
// here rather than trusted from the message.
static void dgramCollectWire(NetFlow* flow, NetMsgQueue* out)
{
    int32 n = saSize(flow->filters);
    if (n == 0)
        return;

    NetDatagramFilter* last = dgramStage(flow, n - 1);
    if (!last)
        return;

    NetMessage* m;
    while ((m = netMsgQueuePop(&last->encOut))) {
        m->addr = flow->peer;
        netMsgQueuePush(out, m);
    }
}

// Push a batch of encoded datagrams to the wire. Runs with no locks held, since each one takes
// sendLock on its own.
static bool dgramFlushWire(NetQueue* q, NetSocket* sock, NetMsgQueue* wire)
{
    bool ok   = true;
    bool wake = false;
    NetMessage* m;

    while ((m = netMsgQueuePop(wire))) {
        if (!dgramSendMsg(q, sock, m, &wake))
            ok = false;
    }

    if (wake && q && q->sendPump)
        q->sendPump(q->sendCtx, sock);

    return ok;
}

_Use_decl_annotations_
bool NetFlow__filterStreamSend(NetFlow* self, NetQueue* q, NetSocket* sock, const uint8* data,
                                size_t len)
{
    bool ok    = true;
    bool fatal = false;
    bool ready = false;
    bool wake  = false;

    NetErrorCode ferr = NERR_None;
    NetAddr feaddr    = { 0 };

    withMutex (&self->filterLock) {
        withMutex (&sock->sendLock) {
            // Over the high watermark the payload is refused outright rather than staged: staging it
            // would report success for data the socket has already said it cannot take. What is
            // already staged counts toward the limit too -- a stage that is still negotiating
            // consumes nothing, so the send chain stays near empty while encIn grows, and a
            // watermark that only watched the chain would never fire during a handshake.
            size_t staged = self->encIn ? self->encIn->total : 0;
            if (data && len > 0 && sock->sendHigh &&
                sock->bufs.stream.send.total + staged >= sock->sendHigh) {
                sock->sendBlocked = true;
                ok                = false;
            } else {
                if (data && len > 0)
                    bufringWrite(self->encIn, data, len);

                fatal = !streamEncodeChain(sock, self);
                ok    = !fatal;
                ready = flushLocked(q, sock, &ferr, &feaddr);
                wake  = sock->bufs.stream.send.total > 0;
            }
        }
    }

    if (ferr != NERR_None)
        netsocket_sendError(sock, q, ferr, &feaddr);
    if (ready)
        fireSendReady(q, sock);
    if (wake && q && q->sendPump)
        q->sendPump(q->sendCtx, sock);

    // A stage failing fatally means the byte stream can no longer be reconstructed; there is nothing
    // to do but tear the flow down, which delivers NET_FlowClosed through the ordinary path. A
    // refusal at the watermark is not that -- the caller simply backs off and retries.
    if (fatal)
        netflow_close(self, NCR_Error);

    return ok;
}

_Use_decl_annotations_
void NetFlow__filterStreamRecv(NetFlow* self, NetQueue* q, NetSocket* sock)
{
    bool ok      = true;
    bool ready   = false;
    bool wake    = false;
    size_t avail = 0;

    NetErrorCode ferr = NERR_None;
    NetAddr feaddr    = { 0 };

    withMutex (&self->filterLock) {
        withMutex (&sock->recvLock)
            ok = streamDecodeChain(sock, self);

        // A decode routinely produces wire-bound output as a side effect -- a handshake reply, an
        // alert, a renegotiation record -- so the encode side runs with nothing staged on the same
        // pass to get it out.
        withMutex (&sock->sendLock) {
            if (ok)
                ok = streamEncodeChain(sock, self);
            ready = flushLocked(q, sock, &ferr, &feaddr);
            wake  = sock->bufs.stream.send.total > 0;
        }

        if (ok) {
            NetStreamFilter* head = saSize(self->filters) > 0 ? streamStage(self, 0) : NULL;
            avail                 = head ? head->decOut.total : 0;
        }
    }

    if (ferr != NERR_None)
        netsocket_sendError(sock, q, ferr, &feaddr);
    if (ready)
        fireSendReady(q, sock);
    if (wake && q && q->sendPump)
        q->sendPump(q->sendCtx, sock);

    // Already on the worker, so notifications are delivered inline -- which is what puts NFN_Secured
    // ahead of the data event produced by the very pass that completed the handshake.
    netflow_filterNotify(self, q, sock, true);

    if (!ok) {
        netflow_close(self, NCR_Error);
        return;
    }

    // Deliver only if the decode actually produced application bytes -- a wire read carrying nothing
    // but a partial record or a handshake flight yields nothing this pass.
    if (avail > 0) {
        NetEvent ev   = { .event = NET_DataReceived };
        ev.recv.msg   = NULL;
        ev.recv.bytes = avail;
        ev.recv.total = avail;
        netqueue_deliver(q, sock, self, &ev);
    }
}

_Use_decl_annotations_
bool NetFlow__filterDatagramEncode(NetFlow* self, NetQueue* q, const uint8* data, size_t len,
                                   NetMsgQueue* out, bool* fatalp)
{
    bool ok    = true;
    bool fatal = false;

    withMutex (&self->filterLock) {
        if (data && len > 0) {
            // The staging queue is what lets a stage decline application messages while it
            // negotiates without them being lost. It is bounded, and a full one is the datagram
            // equivalent of the send watermark: refuse now rather than buffer without limit.
            if (msgqCount(&self->encInMsgs) >= NET_FLOW_ENCQ_MAX) {
                ok = false;
            } else {
                NetMessage* m = dgramWrapPayload(q, data, len, &self->peer);
                if (!m)
                    ok = false;
                else
                    netMsgQueuePush(&self->encInMsgs, m);
            }
        }

        if (ok) {
            fatal = !dgramEncodeChain(q, self);
            ok    = !fatal;
        }

        if (ok)
            dgramCollectWire(self, out);
    }

    if (fatalp)
        *fatalp = fatal;

    return ok;
}

_Use_decl_annotations_
bool NetFlow__filterDatagramSend(NetFlow* self, NetQueue* q, NetSocket* sock, const uint8* data,
                                  size_t len)
{
    NetMsgQueue wire = { 0 };
    bool fatal       = false;

    bool ok = netflow_filterDatagramEncode(self, q, data, len, &wire, &fatal);

    if (!dgramFlushWire(q, sock, &wire))
        ok = false;

    if (fatal)
        netflow_close(self, NCR_Error);

    return ok;
}

_Use_decl_annotations_
bool NetFlow__filterDatagramRecv(NetFlow* self, NetQueue* q, NetSocket* sock, NetMessage* msg,
                                  NetMsgQueue* out)
{
    NetMsgQueue src  = { 0 };
    NetMsgQueue wire = { 0 };
    bool ok          = true;

    netMsgQueuePush(&src, msg);

    withMutex (&self->filterLock) {
        int32 n = saSize(self->filters);

        // Decode chain, wire -> app: the wire-end stage reads the packet, every stage above it
        // reads the decOut of the stage one hop closer to the wire; repeats until no stage has
        // more to produce.
        bool progress = true;
        while (ok && progress) {
            progress = false;

            for (int32 i = n - 1; i >= 0; i--) {
                NetDatagramFilter* f = dgramStage(self, i);
                if (!f) {
                    ok = false;
                    break;
                }

                NetMsgQueue* s = &src;
                if (i < n - 1) {
                    NetDatagramFilter* below = dgramStage(self, i + 1);
                    if (!below) {
                        ok = false;
                        break;
                    }
                    s = &below->decOut;
                }

                size_t before   = msgqCount(s);
                intptr produced = netdatagramfilterDecodeMsg(f, s);
                if (produced < 0) {
                    ok = false;
                    break;
                }
                if (produced > 0 || msgqCount(s) != before)
                    progress = true;
            }
        }

        if (ok && n > 0) {
            NetDatagramFilter* head = dgramStage(self, 0);
            NetMessage* m;
            while (head && (m = netMsgQueuePop(&head->decOut))) netMsgQueuePush(out, m);
        }

        // Decoding a handshake record is exactly the case that produces a reply, so the encode side
        // runs on the same pass to send it.
        if (ok) {
            ok = dgramEncodeChain(q, self);
            if (ok)
                dgramCollectWire(self, &wire);
        }
    }

    // A packet the wire-end stage chose not to consume has nowhere to wait -- unlike the send side
    // there is no staging queue on the way in, because a stage that wants a datagram later is
    // expected to take it into its own reassembly state now.
    netpoolFreeMsgQueue(self->pool, &src);

    dgramFlushWire(q, sock, &wire);

    return ok;
}

_Use_decl_annotations_
void NetFlow__primeFilters(NetFlow* self, NetQueue* q, NetSocket* sock)
{
    if (saSize(self->filters) == 0)
        return;

    if (sock->type == NST_Stream) {
        // Nothing to write to until the transport is up. The connect path primes the chain when it
        // gets there, and an accepted socket is already connected when its filter is attached.
        if (atomicLoad(uint32, &sock->state, Relaxed) != NS_Connected)
            return;

        netflow_filterStreamSend(self, q, sock, NULL, 0);
    } else {
        netflow_filterDatagramSend(self, q, sock, NULL, 0);
    }

    // Priming can run off a worker (a flow opened by an ingest thread or by a send), so the
    // notifications it raised are queued on the flow rather than delivered here.
    netflow_filterNotify(self, q, sock, false);
}

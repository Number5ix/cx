// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "quicsocket.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include <cx/format.h>

// Where QUIC meets netqueue.
//
// Everything below this file is transport with no idea a socket exists: a QuicConn is fed
// datagrams and hands finished ones back, and a QuicStreams is fed frames and hands back bytes.
// This file is the only place that knows about both, and it is deliberately the only place --
// which is why the whole engine can be tested without a network.
//
// Three ideas carry it.
//
// A connection is a socket and a stream is a flow, so the ordering guarantees netqueue already
// makes are the ones QUIC needs. Connection work -- decrypt, parse, acknowledge, retransmit --
// is ordered on the connection's control flow, and each stream is a flow of its own that runs on
// another worker.
//
// A packet is routed by the connection ID it carries, never by the address it came from. That is
// what the route hook on the endpoint socket is for: the flow table underneath it is keyed on
// peer addresses, which is exactly the wrong key for a protocol whose peers are allowed to move.
//
// One mutex guards the whole engine. Datagrams arrive on a worker, the application sends from
// wherever it likes, and a timer fires on a third thread; all three end up inside the same
// connection state. Nothing inside that lock ever calls application code -- what it does instead
// is put a message on a flow, which is how every other part of netqueue defers to a worker too.

#include "conn_private.h"
#include "qlog_private.h"
#include "stream_private.h"

#include <cxquic/quicnet.h>

#include <cx/net/net_private.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// Length of the connection IDs this layer issues. Fixed, because a short header does not carry
// the length and the receiver has to know it up front -- and because eight bytes is exactly what
// the routing table's key holds.
#define QUIC_SOCK_CID_LEN 8

// The flow key the datagram channel occupies. The flow table of a connection is keyed on stream
// id, and a stream id is a 62-bit varint, so the top of the range can never collide with one.
#define QUIC_DGRAM_FLOW_KEY UINT64_MAX

// The shortest a deadline is ever armed for. A connection that reports a deadline already in the
// past wants attention it could not get this pass; arming for the past instead of for a moment
// from now would turn the timer into a spin.
#define QUIC_SOCK_MIN_TIMER timeMS(1)

// Defaults for a QuicConfig left zeroed. The windows are what this endpoint advertises, so they
// bound what the peer may send.
#define QUIC_SOCK_DEF_MAX_DATA     (1 << 20)
#define QUIC_SOCK_DEF_MAX_SD       (1 << 18)
#define QUIC_SOCK_DEF_MAX_STREAMS  100
#define QUIC_SOCK_DEF_IDLE         timeS(30)

// Everything a QUIC socket needs that has no business in a public header.
//
// A listener uses only the transport parameters, the retry setting and the validation key -- the
// first as the template every connection it accepts is built from, the other two to answer a new
// client with. A connection uses all of it.
typedef struct QuicEngine {
    Mutex lock;

    QuicConn* conn;
    QuicStreams streams;
    bool streamsInit;
    bool streamsUp;      // the peer's transport parameters have been applied
    bool connected;      // the handshake finished and the layer above has been told
    bool torndown;       // the connection ended and its flows have been closed
    bool appClosing;     // the application asked for the close, rather than the peer or a timeout

    bool retry;          // listener: answer a new client with a Retry before making any state
    QuicTransportParams tp;

    // The deadline currently armed on the control flow, and the timer arming it. Kept so a flush
    // that does not move the deadline does not cancel and re-arm for nothing.
    NetTimerId timer;
    int64 timerAt;

    // Listener: the key address validation tokens are sealed with. Random per listener, so a
    // token from one server process is meaningless to another.
    uint8 tokenKey[32];
} QuicEngine;

// ---------------------------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------------------------

_meta_inline QuicEngine* engOf(_In_ NetSocketQuic* self)
{
    return (QuicEngine*)self->eng;
}

// The routing key for a connection ID: its bytes read as a 64-bit value. Exact for the eight-byte
// IDs this layer issues, and the reason routing costs one hashtable lookup rather than a scan.
_Pure static uint64 cidKey(_In_ const QuicCid* cid)
{
    uint64 k = 0;
    for (uint8 i = 0; i < cid->len && i < 8; i++)
        k = (k << 8) | cid->id[i];
    return k;
}

// Fills in whatever the caller left zeroed.
static void configDefaults(_Out_ QuicConfig* out, _In_ const QuicConfig* in)
{
    *out = *in;

    if (out->maxData == 0)
        out->maxData = QUIC_SOCK_DEF_MAX_DATA;
    if (out->maxStreamData == 0)
        out->maxStreamData = QUIC_SOCK_DEF_MAX_SD;
    if (out->maxStreamsBidi == 0)
        out->maxStreamsBidi = QUIC_SOCK_DEF_MAX_STREAMS;
    if (out->maxStreamsUni == 0)
        out->maxStreamsUni = QUIC_SOCK_DEF_MAX_STREAMS;
    if (out->idleTimeout == 0)
        out->idleTimeout = QUIC_SOCK_DEF_IDLE;
}

// The transport parameters that follow from a configuration. Only the limits differ from the
// defaults RFC 9000 section 18.2 lays down.
//
// `q` is the queue the endpoint runs on, which is what bounds the datagram size this endpoint can
// promise to accept: an arriving datagram is copied into one pooled receive buffer, so advertising
// more than one of those holds would be promising something the receive path cannot deliver.
static void tpFromConfig(_Out_ QuicTransportParams* tp, _In_ const QuicConfig* cfg,
                         _In_opt_ NetQueue* q)
{
    _quicTpDefaults(tp);

    tp->maxIdleTimeout      = (uint64)timeToMsec(cfg->idleTimeout);
    tp->initMaxData         = cfg->maxData;
    tp->initMaxSdBidiLocal  = cfg->maxStreamData;
    tp->initMaxSdBidiRemote = cfg->maxStreamData;
    tp->initMaxSdUni        = cfg->maxStreamData;
    tp->initMaxStreamsBidi  = cfg->maxStreamsBidi;
    tp->initMaxStreamsUni   = cfg->maxStreamsUni;

    if (cfg->datagrams) {
        size_t cap = QUIC_MAX_DATAGRAM;
        if (q && q->conf.recvBufSize > 0 && q->conf.recvBufSize < cap)
            cap = q->conf.recvBufSize;
        tp->maxDatagramFrame = cap;
    }
}

// The flow with this key -- a stream id, or QUIC_DGRAM_FLOW_KEY -- or NULL if there is none.
//
// A reference comes back with it. The table is not the only thing holding a flow up -- a worker
// finishing that flow's terminal event takes it out of the table and lets go of it -- so a bare
// pointer read under the lock could be freed the moment the lock is dropped.
_Ret_maybenull_ static NetFlow* findFlow(_In_ NetSocketQuic* self, uint64 key)
{
    NetFlow* flow = NULL;

    withReadLock (&self->flowLock) {
        htelem e = htFind(self->flows, uint64, key, none, NULL);
        if (e)
            flow = objAcquire((NetFlow*)hteVal(self->flows, object, e));
    }

    return flow;
}

// The connection's datagram flow, created on first use, with a reference held.
//
// Either end can be the first to want it: the application asking for it with
// netquicOpenDatagram(), or a datagram arriving from the peer. Both land here, and admitting an
// already-admitted key keeps whichever flow got in first, so a race between the two produces one
// flow rather than two.
_Ret_maybenull_ static NetFlow* datagramFlow(_In_ NetSocketQuic* self, _In_ NetQueue* q)
{
    NetFlow* flow = findFlow(self, QUIC_DGRAM_FLOW_KEY);
    if (flow)
        return flow;

    return netqueue_admitFlowObj(q, NetSocket(self), NetFlow(quicdatagramCreate(NetSocket(self))));
}

// Puts a message on a flow. The one place the engine reaches netqueue from inside its own lock,
// and safe there precisely because it runs no application code: the message is queued and a
// worker picks it up afterwards.
static void postMsg(_In_ NetSocketQuic* self, _In_opt_ NetFlow* flow, uint8 kind, size_t bytes)
{
    if (!flow)
        return;

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q)
        return;

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = kind;
    msg->bytes      = bytes;
    netqueue_submit(q, flow, msg);

    objRelease(&q);
}

// ---------------------------------------------------------------------------------------------
// The connection's clock
//
// A QUIC connection has one deadline at a time -- the earliest of an acknowledgement it owes, a
// packet it may have lost, a keep-alive, and the idle timeout. It is armed as an ordinary timer on
// the connection's control flow, so it is delivered on a worker in order behind the packets
// already queued for that connection rather than inline on whichever thread noticed it.
// ---------------------------------------------------------------------------------------------

// Brings the armed timer in line with what the connection now wants. Called after anything that
// could have moved the deadline, with the engine lock held.
static void armDeadline(_In_ NetSocketQuic* self, int64 now)
{
    QuicEngine* eng = engOf(self);
    if (!eng->conn || eng->torndown)
        return;

    int64 want = _quicConnDeadline(eng->conn);

    if (want == timeForever) {
        if (eng->timer != 0) {
            netflowCancelTimer(self->flow, eng->timer);
            eng->timer   = 0;
            eng->timerAt = 0;
        }
        return;
    }

    // A deadline already in the past means there is something the connection wants to do that the
    // flush just could not finish -- most often the congestion window is full. Waiting a moment is
    // the honest answer; arming for the past would spin.
    int64 delay = want > now ? want - now : QUIC_SOCK_MIN_TIMER;

    if (eng->timer != 0) {
        if (eng->timerAt == want)
            return;   // already armed for exactly this moment
        if (netflowRearmTimer(self->flow, eng->timer, delay)) {
            eng->timerAt = want;
            return;
        }
        eng->timer = 0;   // it fired while we were deciding; its delivery will re-arm
    }

    eng->timer = netflowAddTimer(self->flow, delay, NTF_None);
    if (eng->timer != 0)
        eng->timerAt = want;
}

// Sends whatever the connection has waiting and re-arms its deadline. Every path into the engine
// ends here, which is why none of them has to think about when a packet actually goes out.
// The queue's send watermarks mean the same thing on a QUIC stream as on a TCP socket, measured
// against one stream rather than the socket's whole backlog: sendHigh is how much a stream will
// hold for the application, and sendLow how much room it takes for waking a refused sender to be
// worth doing. Zero for either leaves the stream layer's own default in place.
static void applySendWatermarks(_Inout_ QuicEngine* eng, _In_ const NetSocket* sock)
{
    if (sock->sendHigh > 0)
        eng->streams.sendBufMax = sock->sendHigh;

    eng->streams.sendLow = sock->sendLow;
}

static void enginePump(_In_ NetSocketQuic* self, int64 now)
{
    QuicEngine* eng = engOf(self);
    if (!eng->conn || eng->torndown)
        return;

    _quicConnFlush(eng->conn, now);
    armDeadline(self, now);
}

// ---------------------------------------------------------------------------------------------
// Teardown
//
// A connection ends in one of three ways -- the peer closed, the application closed, or it timed
// out -- and all three arrive here. The socket stays alive afterwards so the application can still
// read why it ended; what goes away is the engine's claim on the queue.
// ---------------------------------------------------------------------------------------------

// Drops this connection's routing entries from its listener, so packets for it stop being routed
// and the listener stops holding it alive.
//
// The removal releases the listener's references to this socket, which are usually the last ones,
// so every caller has to be holding one of its own -- otherwise the socket is freed underneath the
// call that asked for the removal.
static void unregisterCids(_In_ NetSocketQuic* self)
{
    NetSocketQuic* lsn = objAcquireFromWeak(NetSocketQuic, self->listener);
    if (!lsn)
        return;

    // A connection is in the table once per ID it has issued. Collect them before removing any,
    // since removing while iterating would walk a table that is moving underneath.
    sa_uint64 keys;
    saInit(&keys, uint64, 4);

    withWriteLock (&lsn->cidLock) {
        foreach (hashtable, hti, lsn->cids) {
            if ((NetSocketQuic*)htiVal(object, hti) == self)
                saPush(&keys, uint64, htiKey(uint64, hti));
        }

        for (int32 i = 0; i < saSize(keys); i++)
            htRemove(&lsn->cids, uint64, keys.a[i]);
    }

    saDestroy(&keys);
    objRelease(&lsn);
}

// The connection is over. Close every flow with the reason it ended for, take the socket off the
// queue, and let go of whatever was holding it up. Runs with the engine lock held, on a thread
// that holds a reference to the socket -- which is what makes dropping the queue's reference here
// safe.
static void connTeardown(_In_ NetSocketQuic* self, NetCloseReason reason)
{
    QuicEngine* eng = engOf(self);
    if (eng->torndown)
        return;
    eng->torndown = true;

    if (eng->timer != 0) {
        netflowCancelTimer(self->flow, eng->timer);
        eng->timer   = 0;
        eng->timerAt = 0;
    }

    // Closes every stream flow and the control flow, each with the reason the connection actually
    // ended for. They are separate ordering domains, so nothing is promised about which handler
    // runs first -- what matters is that each one fires exactly once and says why.
    netsocket_closeFlows(self, reason);

    unregisterCids(self);

    // A client owns the endpoint it dialled out of; a connection a listener accepted shares the
    // listener's and must leave it alone. Which one this is is settled by who installed the route
    // hook on it.
    if (self->endpoint && self->endpoint->routeCtx == self) {
        self->endpoint->route    = NULL;
        self->endpoint->routeCtx = NULL;
        netsocketClose(self->endpoint);
    }

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (q) {
        netqueueRemoveSocket(q, NetSocket(self));
        objRelease(&q);
    }

    atomicStore(uint32, &self->state, NS_Closed, Relaxed);
}

// ---------------------------------------------------------------------------------------------
// What the connection tells the socket
//
// Every one of these runs with the engine lock held, which is why none of them calls application
// code: what they do instead is put a message on a flow.
// ---------------------------------------------------------------------------------------------

static bool onConnSend(_In_opt_ void* ctx, _In_ const NetAddr* peer, _In_ const NetPktInfo* info,
                       _In_reads_bytes_(len) const uint8* data, size_t len)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    if (!self->endpoint)
        return false;

    return netsocketSendEx(self->endpoint, data, len, peer, info, NSO_None);
}

// Hands an accepted connection to the application. It already carries the listener's handlers,
// which is what stops an event of its own from arriving first.
static void postAccept(_In_ NetSocketQuic* self)
{
    NetSocketQuic* lsn = objAcquireFromWeak(NetSocketQuic, self->listener);
    NetQueue* q        = objAcquireFromWeak(NetQueue, self->queue);

    if (lsn && lsn->flow && q) {
        NetMessage* msg = netpoolAllocHeader(q->pool);
        msg->kind       = NMSG_Accept;
        msg->asock      = objAcquire(NetSocket(self));   // the message carries this reference
        msg->addr       = self->remote;
        netqueue_submit(q, lsn->flow, msg);
    }

    objRelease(&q);
    objRelease(&lsn);
}

// 0-RTT is armed: a client may send now, and a server is about to read what the client already
// sent. Either way the connection becomes usable a round trip before the handshake finishes, so
// this is where the socket is handed over -- the same events as an ordinary connection, only
// earlier, and with netquicEarlyData() saying that what crosses it can still be a replay.
static bool onConnEarlyOpen(_In_opt_ void* ctx, _In_opt_ const QuicTransportParams* tp)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    if (!eng->streamsInit || eng->connected)
        return false;

    // A client has nothing to go by but the limits the resumed session ran under; a server already
    // has the peer's real ones, because they came with the ClientHello.
    _quicStreamsPeerParams(&eng->streams, tp ? tp : _quicConnPeerParams(eng->conn));
    eng->streamsUp = true;
    eng->connected = true;

    atomicStore(uint32, &self->state, NS_Connected, Relaxed);

    if (self->server)
        postAccept(self);
    else
        postMsg(self, self->flow, NMSG_Connect, (size_t)NERR_None);

    return true;
}

static void onConnEarlyReject(_In_opt_ void* ctx)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    // Nothing to undo: the transport has already queued everything that went out early to go
    // again under the real keys, and the application was never told which it was.
    logFmt(Verbose, _SL("QUIC server refused early data from ${string}; it will be sent again"),
           stvar(strref, self->hostname));
}

static void onConnConnected(_In_opt_ void* ctx)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    // The peer's limits arrive with the handshake, and until they do the stream layer may not send
    // anything at all -- every one of its windows starts at zero. A connection that started early
    // has been running on the remembered ones and is moved onto the real ones here.
    if (eng->streamsInit) {
        _quicStreamsPeerParams(&eng->streams, _quicConnPeerParams(eng->conn));
        eng->streamsUp = true;
    }

    // Already handed over, because 0-RTT made it usable a round trip ago. What is left to say is
    // that the handshake has now vouched for it: nothing that crossed it was a replay after all,
    // and nothing that crosses it from here could be.
    if (eng->connected) {
        postMsg(self, self->flow, NMSG_FilterNotify, (size_t)NFN_Secured);
        return;
    }

    eng->connected = true;
    atomicStore(uint32, &self->state, NS_Connected, Relaxed);

    if (!self->server) {
        postMsg(self, self->flow, NMSG_Connect, (size_t)NERR_None);
        return;
    }

    // A server hands the connection over only now, so the socket the application is given has a
    // finished handshake behind it and an authenticated peer in front of it.
    postAccept(self);
}

static void onConnClosed(_In_opt_ void* ctx, uint64 error, bool app, bool local,
                         _In_opt_ strref reason)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    self->closeError = error;
    self->closeApp   = app;
    strDup(&self->closeReason, reason);

    // A client whose handshake never finished has a connect still outstanding, and an event is the
    // only way it can be told the answer is no.
    if (!self->server && !eng->connected)
        postMsg(self, self->flow, NMSG_Connect, (size_t)NERR_ConnectionRefused);

    // An application close reports itself the moment it is asked for, before the frame that
    // carries it has been anywhere near the wire. Tearing down here would close the endpoint out
    // from under it, so that path finishes the job itself once the frame has gone out.
    if (eng->appClosing)
        return;

    // Otherwise the connection ended on its own: the peer said so, or this endpoint gave up on a
    // protocol error or the idle timeout.
    connTeardown(self, local ? NCR_Error : NCR_PeerClosed);
}

static bool onConnFrame(_In_opt_ void* ctx, _In_ const QuicFrame* f)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    if (!eng->streamsInit)
        return true;

    if (_quicStreamsFrame(&eng->streams, f))
        return true;

    _quicConnAbort(eng->conn, eng->streams.error, eng->streams.errorFrame);
    return false;
}

static size_t onConnFill(_In_opt_ void* ctx, _Out_writes_(bufsz) uint8* buf, size_t bufsz,
                         uint64 pn, _Out_ bool* eliciting)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    *eliciting = false;
    if (!eng->streamsInit)
        return 0;

    return _quicStreamsFill(&eng->streams, buf, bufsz, pn, eliciting);
}

static void onConnPktAcked(_In_opt_ void* ctx, uint64 pn)
{
    QuicEngine* eng = engOf((NetSocketQuic*)ctx);
    if (eng->streamsInit)
        _quicStreamsAcked(&eng->streams, pn);
}

static void onConnPktLost(_In_opt_ void* ctx, uint64 pn)
{
    QuicEngine* eng = engOf((NetSocketQuic*)ctx);
    if (eng->streamsInit)
        _quicStreamsLost(&eng->streams, pn);
}

static bool onConnWantsToSend(_In_opt_ void* ctx)
{
    QuicEngine* eng = engOf((NetSocketQuic*)ctx);
    return eng->streamsInit && _quicStreamsWantsToSend(&eng->streams);
}

static void onConnCidIssued(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq,
                            _In_reads_bytes_(QUIC_RESET_TOKEN_LEN) const uint8* resetToken)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    unused_noeval(seq);
    unused_noeval(resetToken);

    NetSocketQuic* lsn = objAcquireFromWeak(NetSocketQuic, self->listener);
    if (!lsn)
        return;   // a client's endpoint carries one connection, so there is nothing to route by

    withWriteLock (&lsn->cidLock)
        htInsert(&lsn->cids, uint64, cidKey(cid), object, self);

    objRelease(&lsn);
}

static void onConnCidRetired(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    unused_noeval(seq);

    NetSocketQuic* lsn = objAcquireFromWeak(NetSocketQuic, self->listener);
    if (!lsn)
        return;

    // The removal can drop the listener's last reference to this connection, so it happens with the
    // caller's reference still standing and never with the table locked for anything else.
    withWriteLock (&lsn->cidLock)
        htRemove(&lsn->cids, uint64, cidKey(cid));

    objRelease(&lsn);
}

// An unreliable datagram arrived. It is copied out of the packet buffer, which is reused the
// moment this returns, and delivered whole on the datagram flow -- the same shape a datagram
// socket's receive takes, which is what makes this flow usable exactly like one.
static void onConnDatagramRecv(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data,
                               size_t len)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q)
        return;

    // The channel is only reachable through a flow, and the peer may be the first to use it. A
    // peer-opened one is admitted here the way onStreamOpened() admits a peer-opened stream, so the
    // application sees NET_FlowOpen before the NET_DataReceived that follows.
    NetFlow* flow = datagramFlow(self, q);

    // A pooled buffer is what the whole receive path is capped by, and running out is a normal
    // condition rather than a reason to allocate around the ceiling. Dropping is also exactly what
    // the protocol says may happen to a datagram.
    NetMessage* msg = flow ? netpoolAllocMsg(q->pool) : NULL;
    if (msg && msg->buf->sz >= len) {
        memcpy(msg->buf->data, data, len);
        msg->buf->len = len;
        msg->kind     = NMSG_Data;
        msg->bytes    = len;
        netqueue_submit(q, flow, msg);
    } else if (msg) {
        netpoolFreeMsg(q->pool, &msg);
    }

    objRelease(&flow);
    objRelease(&q);
}

// The slot the pending datagram was in is free again, so a send that was refused can be retried.
static void onConnDatagramWritable(_In_opt_ void* ctx)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    NetFlow* flow = findFlow(self, QUIC_DGRAM_FLOW_KEY);
    postMsg(self, flow, NMSG_SendReady, 0);
    objRelease(&flow);
}

static const QuicConnHandlers quicConnHandlers = {
    .earlyOpen   = onConnEarlyOpen,
    .earlyReject = onConnEarlyReject,
    .send        = onConnSend,
    .connected   = onConnConnected,
    .closed      = onConnClosed,
    .frame       = onConnFrame,
    .fill        = onConnFill,
    .pktAcked    = onConnPktAcked,
    .pktLost     = onConnPktLost,
    .wantsToSend = onConnWantsToSend,
    .cidIssued   = onConnCidIssued,
    .cidRetired  = onConnCidRetired,
    .datagramRecv     = onConnDatagramRecv,
    .datagramWritable = onConnDatagramWritable,
};

// ---------------------------------------------------------------------------------------------
// What the streams tell the socket
//
// Also all under the engine lock, and for the same reason. A stream's whole application-facing
// life is four messages on its flow: it opened, there is something to read, there is room to
// write, and it is over.
// ---------------------------------------------------------------------------------------------

static bool onStreamOpened(_In_opt_ void* ctx, uint64 id)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q)
        return true;   // no queue to announce it on; the stream itself is still perfectly valid

    NetFlow* flow = netqueue_admitFlowObj(q, NetSocket(self),
                                          NetFlow(quicstreamCreate(NetSocket(self), id)));
    objRelease(&flow);
    objRelease(&q);
    return true;
}

static void onStreamReadable(_In_opt_ void* ctx, uint64 id)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    QuicEngine* eng     = engOf(self);

    NetFlow* flow = findFlow(self, id);
    postMsg(self, flow, NMSG_Data, _quicStreamReadable(&eng->streams, id));
    objRelease(&flow);
}

static void onStreamWritable(_In_opt_ void* ctx, uint64 id)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    NetFlow* flow = findFlow(self, id);
    postMsg(self, flow, NMSG_SendReady, 0);
    objRelease(&flow);
}

// The peer abandoned one direction of the stream. Which direction it was does not change what the
// application has to do about it, so both arrive as one NET_Error carrying one code.
static void streamFailed(_In_ NetSocketQuic* self, uint64 id, uint64 error)
{
    NetFlow* flow = findFlow(self, id);
    if (!flow)
        return;

    QuicStream* st = objDynCast(QuicStream, flow);
    if (st && !st->errored) {
        st->error   = error;
        st->errored = true;

        postMsg(self, flow, NMSG_Error, (size_t)NERR_ConnectionReset);
    }

    objRelease(&flow);
}

static void onStreamReset(_In_opt_ void* ctx, uint64 id, uint64 error)
{
    streamFailed((NetSocketQuic*)ctx, id, error);
}

static void onStreamStopped(_In_opt_ void* ctx, uint64 id, uint64 error)
{
    streamFailed((NetSocketQuic*)ctx, id, error);
}

static void onStreamClosed(_In_opt_ void* ctx, uint64 id)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    NetFlow* flow = findFlow(self, id);
    if (flow)
        netflow_close(flow, NCR_PeerClosed);

    objRelease(&flow);
}

static const QuicStreamHandlers quicStreamHandlers = {
    .opened   = onStreamOpened,
    .readable = onStreamReadable,
    .writable = onStreamWritable,
    .reset    = onStreamReset,
    .stopped  = onStreamStopped,
    .closed   = onStreamClosed,
};

// ---------------------------------------------------------------------------------------------
// The control flow
//
// Two events belong to cxquic rather than to the application: an arriving datagram, and the
// connection's own deadline. Both are registered on the connection's control flow, which leaves
// every other event of that flow -- NET_Connection, NET_Error, NET_FlowClosed -- falling through
// to whatever the application registered, since handlers resolve field by field.
// ---------------------------------------------------------------------------------------------

static void onCtlDatagram(_Inout_ NetEvent* ev)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, ev->socket);
    NetMessage* msg     = ev->recv.msg;

    if (!self || !msg || !msg->buf || msg->buf->len == 0)
        return;

    QuicEngine* eng = engOf(self);
    int64 now       = clockTimer();

    withMutex (&eng->lock) {
        if (eng->conn && !eng->torndown) {
            // Decryption happens in place, which the pooled buffer is fine with -- it belongs to
            // this message and goes back to the pool when the handler returns.
            _quicConnRecv(eng->conn, &msg->addr, &msg->info, msg->buf->data, msg->buf->len, now);
            enginePump(self, now);
        }
    }
}

static void onCtlTimer(_Inout_ NetEvent* ev)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, ev->socket);
    if (!self)
        return;

    QuicEngine* eng = engOf(self);
    int64 now       = clockTimer();

    withMutex (&eng->lock) {
        if (eng->timer == ev->timer.id) {
            eng->timer   = 0;
            eng->timerAt = 0;
        }

        if (eng->conn && !eng->torndown) {
            _quicConnTick(eng->conn, now);
            enginePump(self, now);
        }
    }
}

static const NetHandlers quicCtlHandlers = {
    .recv  = onCtlDatagram,
    .timer = onCtlTimer,
};

// ---------------------------------------------------------------------------------------------
// Routing
//
// Everything a QUIC endpoint receives lands here, on whichever thread the backend ingests with.
// The work is deliberately small: read the connection ID, find the connection it names, and drop
// the datagram in that connection's inbox. Anything that needs the engine happens later, on the
// worker that picks the message up.
// ---------------------------------------------------------------------------------------------

// Hands a datagram to a connection's control flow, taking the buffer.
static bool routeTo(_In_ NetSocketQuic* target, _In_ NetQueue* q, _In_ NetAddr* peer,
                    _In_opt_ const NetPktInfo* info, _Inout_ Buffer* buf)
{
    if (!target->flow)
        return false;

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_Data;
    msg->addr       = *peer;
    if (info)
        msg->info = *info;
    msg->buf        = *buf;
    msg->flags      = NMF_PoolBuf;   // the backend took this buffer from the queue's pool
    *buf            = NULL;

    netqueue_submit(q, target->flow, msg);
    return true;
}

// The connection a packet's Destination Connection ID names, with a reference held.
_Ret_maybenull_ static NetSocketQuic* routeLookup(_In_ NetSocketQuic* lsn, _In_ const QuicCid* dcid)
{
    NetSocketQuic* found = NULL;

    withReadLock (&lsn->cidLock) {
        htelem e = htFind(lsn->cids, uint64, cidKey(dcid), none, NULL);
        if (e)
            found = objAcquire((NetSocketQuic*)hteVal(lsn->cids, object, e));
    }

    return found;
}

// Sends a packet that belongs to no connection: a Retry or a Version Negotiation, both of which a
// listener answers with before any state exists for the peer.
static void sendStateless(_In_ NetSocketQuic* lsn, _In_ const NetAddr* peer,
                          _In_reads_(len) const uint8* pkt, size_t len)
{
    if (len > 0 && lsn->endpoint)
        netsocketSend(lsn->endpoint, pkt, len, peer, NSO_None);
}

static bool acceptInitial(_In_ NetSocketQuic* lsn, _In_ NetQueue* q, _In_ NetAddr* peer,
                          _In_opt_ const NetPktInfo* info, _Inout_ Buffer* buf,
                          _In_ const QuicPktHdr* h);

static bool quicRoute(void* ctx, NetSocket* ep, NetAddr* peer, const NetPktInfo* info,
                      Buffer* buf)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;
    bool taken          = false;

    NetQueue* q = objAcquireFromWeak(NetQueue, ep->queue);
    if (!q || !*buf || (*buf)->len == 0)
        goto out;

    QuicPktHdr h;
    if (!_quicHdrDecode(&h, (*buf)->data, (*buf)->len, QUIC_SOCK_CID_LEN))
        goto out;

    // A client's endpoint carries exactly one connection, so there is nothing to demultiplex: it
    // is the connection, and anything that arrives is either for it or for nobody.
    if (!self->server) {
        taken = routeTo(self, q, peer, info, buf);
        goto out;
    }

    NetSocketQuic* target = routeLookup(self, &h.dcid);
    if (target) {
        taken = routeTo(target, q, peer, info, buf);
        objRelease(&target);
        goto out;
    }

    // No connection owns this ID. Only an Initial packet can bring one into existence; anything
    // else is for a connection that is already gone, and is dropped.
    if (h.type == QUIC_PKT_INITIAL || h.type == QUIC_PKT_UNKNOWN)
        taken = acceptInitial(self, q, peer, info, buf, &h);

out:
    // Whatever was not passed on has to go back to the pool: the hook owns the buffer either way,
    // and destroying a pooled buffer costs the queue that much of its ceiling for good.
    if (*buf && q)
        bufpoolPut(&q->pool->msgbuf, buf);

    objRelease(&q);
    return taken;
}

// ---------------------------------------------------------------------------------------------
// Accepting
//
// A packet that names no connection and is an Initial is a new client. Before any state is made
// for it there are two things a listener can answer with on its own -- a Version Negotiation, and
// a Retry that proves the client is really at the address it claims -- and both are sent from
// here, on the ingest thread, with nothing allocated.
// ---------------------------------------------------------------------------------------------

// Answers an Initial with a Retry, so the client comes back with a token proving it can receive at
// the address it gave. Nothing is remembered: the token carries everything checking it needs.
static void sendRetry(_In_ NetSocketQuic* lsn, _In_ const NetAddr* peer,
                      _In_ const QuicPktHdr* h)
{
    QuicEngine* eng = engOf(lsn);

    uint8 token[QUIC_MAX_TOKEN];
    size_t tokenLen = 0;
    if (!_quicTokenSeal(token, &tokenLen, eng->tokenKey, true, peer, &h->dcid, clockWall()))
        return;

    // The Retry introduces a connection ID of the server's own, which the client uses as the
    // destination of its next Initial -- and which it also has to see echoed in the transport
    // parameters, or it has been handed a Retry an attacker made up.
    QuicCid retryScid;
    retryScid.len = QUIC_SOCK_CID_LEN;
    if (psa_generate_random(retryScid.id, retryScid.len) != PSA_SUCCESS)
        return;

    uint8 pkt[QUIC_MAX_TOKEN + 128];
    size_t n = _quicRetryBuild(pkt, sizeof(pkt), QUIC_VERSION_1, &h->dcid, &h->scid, &retryScid,
                               token, tokenLen);
    sendStateless(lsn, peer, pkt, n);
}

static bool acceptInitial(_In_ NetSocketQuic* lsn, _In_ NetQueue* q, _In_ NetAddr* peer,
                          _In_opt_ const NetPktInfo* info, _Inout_ Buffer* buf,
                          _In_ const QuicPktHdr* h)
{
    QuicEngine* leng = engOf(lsn);

    if (atomicLoad(uint32, &lsn->state, Relaxed) != NS_Listening)
        return false;

    // A version this endpoint does not speak gets the list of ones it does, and nothing else.
    if (h->version != QUIC_VERSION_1) {
        uint32 versions[] = { QUIC_VERSION_1 };
        uint8 pkt[64];
        sendStateless(lsn, peer, pkt,
                      _quicVersionNegBuild(pkt, sizeof(pkt), &h->dcid, &h->scid, versions, 1));
        return false;
    }

    // RFC 9000 section 14.1: a conforming client pads every datagram carrying an Initial out to at
    // least this, so that a server always has room to answer. One that is short did not come from
    // a client that wanted an answer.
    if ((*buf)->len < QUIC_MIN_INITIAL_LEN)
        return false;

    QuicCid odcid;
    bool haveOdcid = false;

    if (leng->retry) {
        if (h->tokenLen == 0) {
            sendRetry(lsn, peer, h);
            return false;
        }

        bool wasRetry = false;
        if (!_quicTokenOpen(h->token, h->tokenLen, leng->tokenKey, peer, clockWall(), &wasRetry,
                            &odcid))
            return false;   // forged, expired, or issued to somebody else

        haveOdcid = wasRetry;
    }

    NetSocketQuic* conn = netsocketquicCreate(true, lsn->endpoint, lsn->tls);
    if (!conn)
        return false;

    QuicEngine* eng = engOf(conn);

    conn->listener = objGetWeak(NetSocketQuic, lsn);
    conn->remote   = *peer;
    conn->local    = lsn->local;

    // Every connection a listener accepts advertises what the listener was configured to.
    eng->tp = leng->tp;

    QuicConnConfig cc;
    memset(&cc, 0, sizeof(cc));
    cc.tls          = lsn->tls;
    cc.localCidLen  = QUIC_SOCK_CID_LEN;
    cc.clientDcid   = h->dcid;
    cc.peerCid      = h->scid;
    cc.tp           = eng->tp;
    cc.origDcid     = odcid;
    cc.haveOrigDcid = haveOdcid;

    eng->conn = _quicConnCreate(true, &cc, peer);
    if (!eng->conn) {
        objRelease(&conn);
        return false;
    }

    _quicConnSetHandlers(eng->conn, &quicConnHandlers, conn);

    _quicStreamsInit(&eng->streams, true, &eng->tp);
    _quicStreamsSetHandlers(&eng->streams, &quicStreamHandlers, conn);
    eng->streamsInit = true;

    // The listener's handlers come across before the socket is reachable, so the first event of the
    // new connection has somewhere to go. NET_Accepted is where the application swaps in a set of
    // its own.
    withReadLock (&lsn->handlerLock) {
        ObjInst* hobj = lsn->handlerWeak ? objAcquireFromWeak(ObjInst, lsn->handlerWeak) : NULL;
        if (hobj) {
            netsocketSetHandlersObj(conn, lsn->handlers, hobj);
            objRelease(&hobj);
        } else {
            netsocketSetHandlers(conn, lsn->handlers, lsn->handlerCtx);
        }
    }

    // Held back until NET_Accepted has been delivered. A QUIC connection opens its peer's streams
    // on flows of their own as soon as the handshake finishes, which can be well before a worker
    // gets to the accept sitting on the listener's flow.
    atomicStore(uint32, &NetSocket(conn)->awaitingAccept, 1, Release);

    netqueueAddSocket(q, NetSocket(conn));

    // After the queue has it, not before: an accepted socket has no watermarks of its own until it
    // joins a queue and inherits that queue's. Reading them any earlier gets zero on every
    // connection a listener accepts, which is the half of a connection that sends the most.
    applySendWatermarks(eng, NetSocket(conn));

    netflowSetHandlersObj(conn->flow, &quicCtlHandlers, ObjInst(conn));
    atomicStore(uint32, &conn->state, NS_Connecting, Relaxed);

    // Two routing entries, because for one round trip the client is still addressing packets to
    // the connection ID it invented: that one, and the one this endpoint just chose. The rest
    // arrive through cidIssued as the connection hands out more.
    QuicCid localCid;
    _quicConnLocalCid(eng->conn, &localCid);

    withWriteLock (&lsn->cidLock) {
        htInsert(&lsn->cids, uint64, cidKey(&localCid), object, conn);
        htInsert(&lsn->cids, uint64, cidKey(&h->dcid), object, conn);
    }

    // Only now, with the lock dropped: handing the datagram over reaches the new connection's own
    // engine, and holding a listener-wide lock across another connection's engine is exactly the
    // shape a deadlock needs.
    bool taken = routeTo(conn, q, peer, info, buf);

    objRelease(&conn);
    return taken;
}

// ---------------------------------------------------------------------------------------------
// Starting a client
// ---------------------------------------------------------------------------------------------

// Builds the connection and sends its first Initial. Everything a client needs is known by now:
// the address to talk to, the name to authenticate, and the limits to advertise.
static bool startClient(_In_ NetSocketQuic* self, _In_ const NetAddr* addr)
{
    QuicEngine* eng = engOf(self);
    self->remote    = *addr;

    QuicConnConfig cc;
    memset(&cc, 0, sizeof(cc));
    cc.tls         = self->tls;
    cc.hostname    = self->hostname;
    cc.localCidLen = QUIC_SOCK_CID_LEN;
    cc.tp          = eng->tp;

    int64 now = clockTimer();
    bool ok   = false;

    withMutex (&eng->lock) {
        eng->conn = _quicConnCreate(false, &cc, addr);
        if (eng->conn) {
            _quicConnSetHandlers(eng->conn, &quicConnHandlers, self);

            _quicStreamsInit(&eng->streams, false, &eng->tp);
            applySendWatermarks(eng, NetSocket(self));
            _quicStreamsSetHandlers(&eng->streams, &quicStreamHandlers, self);
            eng->streamsInit = true;

            atomicStore(uint32, &self->state, NS_Connecting, Release);

            ok = _quicConnStart(eng->conn, now);
            if (ok)
                armDeadline(self, now);
        }
    }

    return ok;
}

// The resolver answered. Runs on a resolver worker, so it starts the handshake or reports the
// failure through the flow and touches nothing the application can see.
static void onResolved(_In_opt_ sa_NetAddr* addrs, NetErrorCode err, _In_opt_ void* ctx)
{
    NetSocketQuic* self = (NetSocketQuic*)ctx;

    // The endpoint socket is IPv4, so an address of any other family is one this connection cannot
    // use however good it is.
    const NetAddr* pick = NULL;
    if (err == NERR_None && addrs) {
        for (int32 i = 0; i < saSize(*addrs) && !pick; i++) {
            if (addrs->a[i].type == NA_IPv4)
                pick = &addrs->a[i];
        }
    }

    if (pick && atomicLoad(uint32, &self->state, Relaxed) == NS_Resolving && startClient(self, pick)) {
        objRelease(&self);
        return;
    }

    atomicStore(uint32, &self->state, NS_Init, Release);
    postMsg(self, self->flow, NMSG_Connect,
            (size_t)(err != NERR_None ? err : NERR_HostUnreachable));

    objRelease(&self);
}

// ---------------------------------------------------------------------------------------------
// NetSocketQuic
// ---------------------------------------------------------------------------------------------

_objfactory_guaranteed NetSocketQuic* NetSocketQuic_create(bool server, _In_ NetSocket* endpoint,
                                                           _In_ TlsConfig* tls)
{
    NetSocketQuic* self;
    self = objInstCreate(NetSocketQuic);

    // Read by NetSocket_init, which is what gives this socket a control flow and a stream table
    // instead of the buffers a stream or datagram socket gets.
    self->type = NST_Quic;

    self->server   = server;
    self->endpoint = objAcquire(endpoint);
    self->tls      = objAcquire(tls);

    QuicEngine* eng = xaAlloc(sizeof(QuicEngine), XA_Zero);
    mutexInit(&eng->lock);
    self->eng = eng;

    objInstInit(self);

    return self;
}

_objinit_guaranteed bool NetSocketQuic_init(_In_ NetSocketQuic* self)
{
    htInit(&self->cids, uint64, object, 16);

    // Autogen begins -----
    rwlockInit(&self->cidLock);
    return true;
    // Autogen ends -------
}

extern bool NetSocket_send(_In_ NetSocket* self, _In_ const uint8* data, size_t len, _In_opt_ const NetAddr* dest, flags_t flags);   // parent
#define parent_send(data, len, dest, flags) NetSocket_send((NetSocket*)(self), data, len, dest, flags)
bool NetSocketQuic_send(_In_ NetSocketQuic* self, _In_ const uint8* data, size_t len, _In_opt_ const NetAddr* dest, flags_t flags)
{
    unused_noeval(self);
    unused_noeval(data);
    unused_noeval(len);
    unused_noeval(dest);
    unused_noeval(flags);

    // A QUIC connection carries no bytes of its own: everything an application sends belongs to
    // one stream or another, so netflowSend() on a stream is the only way in.
    return false;
}

extern bool NetSocket_close(_In_ NetSocket* self);   // parent
#define parent_close() NetSocket_close((NetSocket*)(self))
bool NetSocketQuic_close(_In_ NetSocketQuic* self)
{
    if (atomicLoad(uint32, &self->state, Relaxed) == NS_Closed)
        return false;

    QuicEngine* eng = engOf(self);

    if (eng->conn) {
        withMutex (&eng->lock) {
            eng->appClosing = true;

            if (!eng->torndown) {
                // One CONNECTION_CLOSE goes out and the socket is done. RFC 9000 would have an
                // endpoint linger to repeat it for anything still in flight, but a socket the
                // application has closed has nothing left to repeat it with.
                _quicConnClose(eng->conn, 0, true, NULL);
                _quicConnFlush(eng->conn, clockTimer());
                connTeardown(self, NCR_AppClosed);
            }
        }

        return true;   // the teardown already took it off the queue
    }

    // A listener takes its connections with it. Snapshot them first: dropping the table's
    // references is itself what can destroy them.
    sa_NetSocket kids;
    saInit(&kids, NetSocket, 8);

    withWriteLock (&self->cidLock) {
        foreach (hashtable, hti, self->cids) {
            NetSocketQuic* c = (NetSocketQuic*)htiVal(object, hti);
            if (c)
                saPush(&kids, NetSocket, NetSocket(c));
        }
        htClear(&self->cids);
    }

    for (int32 i = 0; i < saSize(kids); i++)
        netsocketClose(kids.a[i]);   // a connection reached twice answers the second call with false

    saDestroy(&kids);

    if (self->endpoint && self->endpoint->routeCtx == self) {
        self->endpoint->route    = NULL;
        self->endpoint->routeCtx = NULL;
        netsocketClose(self->endpoint);
    }

    return parent_close();
}

extern bool NetSocket_connect(_In_ NetSocket* self, _In_ strref host, uint16 port);   // parent
#define parent_connect(host, port) NetSocket_connect((NetSocket*)(self), host, port)
bool NetSocketQuic_connect(_In_ NetSocketQuic* self, _In_ strref host, uint16 port)
{
    QuicEngine* eng = engOf(self);

    if (self->server || eng->conn)
        return false;

    // The queue owns the resolver, the pool, and the flow the answer is delivered on.
    NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
    if (!q)
        return false;

    uint32 expected = NS_Init;
    if (!atomicCompareExchange(uint32, strong, &self->state, &expected, NS_Resolving, AcqRel,
                               Relaxed)) {
        objRelease(&q);
        return false;
    }

    objRelease(&q);

    // A literal address needs no lookup, and resolving it inline means a program that only ever
    // dials addresses never starts the resolver at all.
    NetAddr lit;
    if (netAddrFromStr(&lit, host)) {
        lit.port = port;
        if (startClient(self, &lit))
            return true;

        atomicStore(uint32, &self->state, NS_Init, Release);
        return false;
    }

    objAcquire(self);   // the resolution holds this until it answers

    if (!_netResolveSubmit(host, port, onResolved, self)) {
        atomicStore(uint32, &self->state, NS_Init, Release);
        objRelease(&self);
        return false;
    }

    return true;
}

// Asks the endpoint for everything QUIC wants from the IP layer. All of it is optional and none of
// it is checked: a platform that cannot report which local address a datagram arrived on leaves the
// connection using one address, a platform that cannot carry ECN makes validation turn marking off
// after the first acknowledgement, and one that cannot set the don't-fragment bit makes an
// oversized probe succeed by being fragmented, which stops the search where it is.
static void endpointOptions(_In_ NetSocket* ep)
{
    netsocketSetRecvInfo(ep, true);
    netsocketSetDontFragment(ep, true);
}

bool NetSocketQuic_bind(_In_ NetSocketQuic* self, const NetAddr* addr)
{
    if (!self->endpoint || !netsocketBind(self->endpoint, addr))
        return false;

    endpointOptions(self->endpoint);

    // Read back from the endpoint rather than copied from what was asked for, so a listener bound
    // to port 0 reports the port the OS chose.
    self->local = self->endpoint->local;
    return true;
}

bool NetSocketQuic_listen(_In_ NetSocketQuic* self, int backlog)
{
    unused_noeval(backlog);   // QUIC has no accept backlog: a connection exists once its Initial arrives

    if (!self->server || !self->endpoint)
        return false;

    // From here on the endpoint's packets are demultiplexed by connection ID instead of reaching
    // its own address-keyed flow table.
    self->endpoint->route    = quicRoute;
    self->endpoint->routeCtx = self;

    atomicStore(uint32, &self->state, NS_Listening, Relaxed);
    return true;
}

void NetSocketQuic_destroy(_In_ NetSocketQuic* self)
{
    // A socket released without being closed still has to stop being reachable from the endpoint,
    // or the next arriving packet is handed a pointer to freed memory.
    if (self->endpoint && self->endpoint->routeCtx == self) {
        self->endpoint->route    = NULL;
        self->endpoint->routeCtx = NULL;
        netsocketClose(self->endpoint);
    }

    QuicEngine* eng = engOf(self);
    if (eng) {
        if (eng->streamsInit)
            _quicStreamsDestroy(&eng->streams);
        if (eng->conn)
            _quicConnDestroy(&eng->conn);
        mutexDestroy(&eng->lock);
        xaFree(eng);
        self->eng = NULL;
    }

    // Autogen begins -----
    objRelease(&self->endpoint);
    objDestroyWeak(&self->listener);
    objRelease(&self->tls);
    strDestroy(&self->hostname);
    htDestroy(&self->cids);
    rwlockDestroy(&self->cidLock);
    strDestroy(&self->closeReason);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// QuicStream
// ---------------------------------------------------------------------------------------------

_objfactory_guaranteed QuicStream* QuicStream_create(_In_ NetSocket* socket, uint64 id)
{
    QuicStream* self;
    self = objInstCreate(QuicStream);

    self->socket = objGetWeak(NetSocket, socket);
    self->key    = id;

    // Same as NetFlow_create: hold the pool rather than resolving it through the socket later,
    // when either weak arm may already be broken.
    NetQueue* q = objAcquireFromWeak(NetQueue, socket->queue);
    if (q) {
        self->pool = objAcquire(q->pool);
        objRelease(&q);
    }

    objInstInit(self);

    return self;
}

// Resolves a flow back to the connection it belongs to, with a reference held.
_Ret_maybenull_ static NetSocketQuic* flowSock(_In_ NetFlow* flow)
{
    NetSocket* s        = objAcquireFromWeak(NetSocket, flow->socket);
    NetSocketQuic* self = objDynCast(NetSocketQuic, s);

    if (!self)
        objRelease(&s);

    return self;
}

extern bool NetFlow_send(_In_ NetFlow* self, _In_ const uint8* data, size_t len, flags_t flags);   // parent
#undef parent_send
#define parent_send(data, len, flags) NetFlow_send((NetFlow*)(self), data, len, flags)
bool QuicStream_send(_In_ QuicStream* self, _In_ const uint8* data, size_t len, flags_t flags)
{
    unused_noeval(flags);

    NetSocketQuic* sock = flowSock(NetFlow(self));
    if (!sock)
        return false;

    QuicEngine* eng = engOf(sock);
    bool ok         = false;

    withMutex (&eng->lock) {
        // All or nothing, the way every other send in netqueue is: a partial write would leave the
        // application holding a remainder it has no way to name. netquicWritable() is how it finds
        // out how much would fit.
        //
        // The refusal goes through the stream layer rather than being decided here, because a
        // refusal is state: it is what arms NET_SendReady, and what tells the peer we are up
        // against a limit it set.
        //
        // A refusal is worth a flush only when it had something new to say, which is at most once
        // per limit -- otherwise an application that keeps offering a write it cannot make would
        // drive a full flush on every attempt.
        if (eng->streamsInit && !eng->torndown) {
            bool blocked = false;
            ok           = _quicStreamSendAll(&eng->streams, self->key, data, len, &blocked);
            if (ok || blocked)
                enginePump(sock, clockTimer());
        }
    }

    objRelease(&sock);
    return ok;
}

extern bool NetFlow_close(_In_ NetFlow* self);   // parent
#undef parent_close
#define parent_close() NetFlow_close((NetFlow*)(self))
bool QuicStream_close(_In_ QuicStream* self)
{
    NetSocketQuic* sock = flowSock(NetFlow(self));

    if (sock) {
        QuicEngine* eng = engOf(sock);

        withMutex (&eng->lock) {
            if (eng->streamsInit && !eng->torndown) {
                // Closing a flow is immediate by contract, so both directions end here and now.
                // netquicFinish() is the graceful half of it, for an application that wants the
                // peer to receive what is already queued.
                _quicStreamReset(&eng->streams, self->key, 0);
                _quicStreamStopSending(&eng->streams, self->key, 0);
                enginePump(sock, clockTimer());
            }
        }

        objRelease(&sock);
    }

    return parent_close();
}

// ---------------------------------------------------------------------------------------------
// The datagram flow
// ---------------------------------------------------------------------------------------------

_objfactory_guaranteed QuicDatagram* QuicDatagram_create(_In_ NetSocket* socket)
{
    QuicDatagram* self;
    self = objInstCreate(QuicDatagram);

    self->socket = objGetWeak(NetSocket, socket);
    self->key    = QUIC_DGRAM_FLOW_KEY;

    // Same as QuicStream_create: hold the pool rather than resolving it through the socket later,
    // when either weak arm may already be broken.
    NetQueue* q = objAcquireFromWeak(NetQueue, socket->queue);
    if (q) {
        self->pool = objAcquire(q->pool);
        objRelease(&q);
    }

    objInstInit(self);

    return self;
}

extern bool NetFlow_send(_In_ NetFlow* self, _In_ const uint8* data, size_t len, flags_t flags);   // parent
#undef parent_send
#define parent_send(data, len, flags) NetFlow_send((NetFlow*)(self), data, len, flags)
bool QuicDatagram_send(_In_ QuicDatagram* self, _In_ const uint8* data, size_t len, flags_t flags)
{
    unused_noeval(flags);

    NetSocketQuic* sock = flowSock(NetFlow(self));
    if (!sock)
        return false;

    QuicEngine* eng = engOf(sock);
    bool ok         = false;

    withMutex (&eng->lock) {
        // All or nothing, and there is no partial case to consider: a datagram is one unit. A
        // refusal means either the payload is over netquicMaxDatagram() or the previous datagram
        // is still waiting on the congestion window, and NET_SendReady answers the second.
        if (eng->conn && !eng->torndown && _quicConnDatagramSend(eng->conn, data, len)) {
            ok = true;
            enginePump(sock, clockTimer());
        }
    }

    objRelease(&sock);
    return ok;
}

extern bool NetFlow_close(_In_ NetFlow* self);   // parent
#undef parent_close
#define parent_close() NetFlow_close((NetFlow*)(self))
bool QuicDatagram_close(_In_ QuicDatagram* self)
{
    // Nothing goes on the wire: RFC 9221 gives an endpoint no way to say it will send no more
    // datagrams, so closing the flow is purely local.
    return parent_close();
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void netquicQlog(strref dir)
{
    _quicQlogDir(dir);
}

_Use_decl_annotations_
NetSocket* netquicListen(NetQueue* q, const NetAddr* addr, const QuicConfig* cfg,
                         const NetHandlers* handlers, void* ctx)
{
    if (!q || !addr || !cfg || !cfg->tls || !_quicInit())
        return NULL;

    NetSocket* ep = netqueueSocket(q, NST_Datagram);
    if (!ep)
        return NULL;

    NetSocketQuic* lsn = netsocketquicCreate(true, ep, cfg->tls);
    QuicEngine* eng    = engOf(lsn);

    QuicConfig full;
    configDefaults(&full, cfg);
    tpFromConfig(&eng->tp, &full, q);
    eng->retry = full.retry;

    bool ok = psa_generate_random(eng->tokenKey, sizeof(eng->tokenKey)) == PSA_SUCCESS;

    // Bound before it joins the queue, so the backend never watches a socket with no address.
    ok = ok && netsocketBind(lsn, addr);
    ok = ok && netqueueAddSocket(q, ep);
    ok = ok && netqueueAddSocket(q, NetSocket(lsn));

    if (ok && handlers)
        netsocketSetHandlers(lsn, handlers, ctx);

    ok = ok && netsocketListen(lsn, 0);

    if (!ok) {
        // The listener only takes the endpoint down for itself once listen() has pointed the route
        // hook at it, so a failure before that has to close it here.
        netsocketClose(ep);
        netsocketClose(lsn);
        objRelease(&ep);
        objRelease(&lsn);
        return NULL;
    }

    objRelease(&ep);   // the listener holds its own reference now
    return NetSocket(lsn);
}

_Use_decl_annotations_
NetSocket* netquicConnect(NetQueue* q, strref host, uint16 port, strref hostname,
                          const QuicConfig* cfg, const NetHandlers* handlers, void* ctx)
{
    return netquicConnectPrep(q, host, port, hostname, cfg, handlers, ctx, NULL, NULL);
}

_Use_decl_annotations_
NetSocket* netquicConnectPrep(NetQueue* q, strref host, uint16 port, strref hostname,
                              const QuicConfig* cfg, const NetHandlers* handlers, void* ctx,
                              NetConnectPrepCB prep, void* prepctx)
{
    if (!q || !cfg || !cfg->tls || !_quicInit())
        return NULL;

    NetSocket* ep = netqueueSocket(q, NST_Datagram);
    if (!ep)
        return NULL;

    NetSocketQuic* sock = netsocketquicCreate(false, ep, cfg->tls);
    QuicEngine* eng     = engOf(sock);

    QuicConfig full;
    configDefaults(&full, cfg);
    tpFromConfig(&eng->tp, &full, q);

    // The name to authenticate defaults to the name dialled, which is what it is almost always.
    strDup(&sock->hostname, strEmpty(hostname) ? host : hostname);

    // Any port the OS likes, on the only family the platform's datagram sockets are made in.
    NetAddr any;
    memset(&any, 0, sizeof(any));
    any.type = NA_IPv4;

    bool ok = netsocketBind(sock, &any);
    ok      = ok && netqueueAddSocket(q, ep);
    ok      = ok && netqueueAddSocket(q, NetSocket(sock));

    if (ok && handlers)
        netsocketSetHandlers(sock, handlers, ctx);

    if (ok) {
        netflowSetHandlersObj(sock->flow, &quicCtlHandlers, ObjInst(sock));

        // Everything arriving on this endpoint belongs to this one connection, so the hook has
        // nothing to demultiplex -- it is here to keep the packets out of the endpoint's own
        // address-keyed flow table.
        ep->route    = quicRoute;
        ep->routeCtx = sock;

        // The last moment at which this connection is known to nobody but this call: it is
        // finished, and the handshake below is what makes it start raising events.
        if (prep)
            prep(NetSocket(sock), prepctx);

        ok = netsocketConnect(sock, host, port);
    }

    if (!ok) {
        netsocketClose(ep);
        netsocketClose(sock);
        objRelease(&ep);
        objRelease(&sock);
        return NULL;
    }

    objRelease(&ep);   // the connection holds its own reference now
    return NetSocket(sock);
}

_Use_decl_annotations_
void netquicClose(NetSocket* sock, uint64 error, strref reason)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return;

    QuicEngine* eng = engOf(self);

    withMutex (&eng->lock) {
        eng->appClosing = true;

        if (eng->conn && !eng->torndown) {
            _quicConnClose(eng->conn, error, true, reason);
            _quicConnFlush(eng->conn, clockTimer());
            connTeardown(self, NCR_AppClosed);
        }
    }
}

_Use_decl_annotations_
bool netquicALPN(NetSocket* sock, strhandle out)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return false;

    QuicEngine* eng = engOf(self);
    bool ok         = false;

    withMutex (&eng->lock) {
        TlsQuic* tq = eng->conn ? _quicConnTls(eng->conn) : NULL;
        if (tq)
            ok = tlsquicGetALPN(tq, out);
    }

    return ok;
}

_Use_decl_annotations_
bool netquicMigrate(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self || self->server)
        return false;

    NetQueue* q = objAcquireFromWeak(NetQueue, sock->queue);
    if (!q)
        return false;

    // The new socket is built and bound before the connection is asked to move, so a failure here
    // leaves the connection exactly where it was rather than halfway between two addresses.
    NetSocket* ep = netqueueSocket(q, NST_Datagram);
    NetSocket* old = NULL;
    bool ok        = ep != NULL;

    if (ok) {
        NetAddr any;
        memset(&any, 0, sizeof(any));
        any.type = NA_IPv4;
        ok       = netsocketBind(ep, &any);
    }
    if (ok) {
        endpointOptions(ep);
        ok = netqueueAddSocket(q, ep);
    }

    if (ok) {
        QuicEngine* eng = engOf(self);
        int64 now       = clockTimer();

        withMutex (&eng->lock) {
            ok = eng->conn && !eng->torndown && _quicConnMigrate(eng->conn, now);

            if (ok) {
                // Both halves of the swap happen under the engine lock, which is the only thing
                // that reads self->endpoint to send on: a datagram is built for one path or the
                // other, never for a mixture of the two.
                ep->route    = quicRoute;
                ep->routeCtx = self;

                old            = self->endpoint;
                self->endpoint = objAcquire(ep);
                self->local    = ep->local;

                // The challenge that proves the new path goes out from the new socket, because the
                // socket is what changed.
                enginePump(self, now);
            }
        }
    }

    if (old) {
        // Outside the lock: closing a socket delivers events, and the old endpoint must stop
        // routing to this connection before it goes.
        if (old->routeCtx == self) {
            old->route    = NULL;
            old->routeCtx = NULL;
        }
        netsocketClose(old);
        objRelease(&old);
    } else if (ep) {
        netsocketClose(ep);
    }

    objRelease(&ep);
    objRelease(&q);
    return ok;
}

_Use_decl_annotations_
size_t netquicPathMtu(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return 0;

    QuicEngine* eng = engOf(self);
    size_t mtu      = 0;

    withMutex (&eng->lock) {
        if (eng->conn)
            mtu = _quicConnPathMtu(eng->conn);
    }

    return mtu;
}

_Use_decl_annotations_
bool netquicEcn(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return false;

    QuicEngine* eng = engOf(self);
    bool on         = false;

    withMutex (&eng->lock) {
        if (eng->conn)
            on = _quicConnEcnActive(eng->conn);
    }

    return on;
}

_Use_decl_annotations_
QuicEarlyData netquicEarlyData(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return QUIC_EARLY_None;

    QuicEngine* eng = engOf(self);
    QuicEarlyState st = QUIC_ES_NONE;

    withMutex (&eng->lock) {
        if (eng->conn)
            st = _quicConnEarlyState(eng->conn);
    }

    switch (st) {
    case QUIC_ES_LIVE:    return QUIC_EARLY_Pending;
    case QUIC_ES_DONE:    return QUIC_EARLY_Accepted;
    case QUIC_ES_REFUSED: return QUIC_EARLY_Rejected;
    default:                 return QUIC_EARLY_None;
    }
}

_Use_decl_annotations_
uint32 netquicMigrations(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return 0;

    QuicEngine* eng = engOf(self);
    uint32 n        = 0;

    withMutex (&eng->lock) {
        if (eng->conn)
            n = _quicConnMigrations(eng->conn);
    }

    return n;
}

_Use_decl_annotations_
NetFlow* netquicOpen(NetSocket* sock, bool uni)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return NULL;

    QuicEngine* eng = engOf(self);
    NetFlow* flow   = NULL;

    withMutex (&eng->lock) {
        uint64 id;

        // Nothing may be opened before the peer's transport parameters arrive: until then every
        // limit it granted is zero, including how many streams may exist.
        if (eng->streamsInit && eng->streamsUp && !eng->torndown &&
            _quicStreamOpen(&eng->streams, uni, &id)) {
            NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
            if (q) {
                flow = netqueue_admitFlowObj(q, sock, NetFlow(quicstreamCreate(sock, id)));
                objRelease(&q);
            }
        }

        // Either a stream was opened, or a STREAMS_BLOCKED is owed saying it could not be.
        enginePump(self, clockTimer());
    }

    return flow;
}

_Use_decl_annotations_
NetFlow* netquicOpenDatagram(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return NULL;

    QuicEngine* eng = engOf(self);
    NetFlow* flow   = NULL;

    withMutex (&eng->lock) {
        // Zero means the two ends did not agree on the extension, which is settled by the
        // handshake and cannot change afterwards.
        if (eng->conn && !eng->torndown && _quicConnMaxDatagram(eng->conn) > 0) {
            NetQueue* q = objAcquireFromWeak(NetQueue, self->queue);
            if (q) {
                flow = datagramFlow(self, q);
                objRelease(&q);
            }
        }
    }

    return flow;
}

_Use_decl_annotations_
size_t netquicMaxDatagram(NetSocket* sock)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return 0;

    QuicEngine* eng = engOf(self);
    size_t n        = 0;

    withMutex (&eng->lock) {
        if (eng->conn && !eng->torndown)
            n = _quicConnMaxDatagram(eng->conn);
    }

    return n;
}

_Use_decl_annotations_
size_t netquicRecv(NetFlow* flow, uint8* buf, size_t bufsz, bool* fin)
{
    size_t n   = 0;
    bool ended = false;

    QuicStream* st = objDynCast(QuicStream, flow);
    if (st) {
        NetSocketQuic* sock = flowSock(flow);
        if (sock) {
            QuicEngine* eng = engOf(sock);

            withMutex (&eng->lock) {
                if (eng->streamsInit && !eng->torndown) {
                    n = _quicStreamRecv(&eng->streams, st->key, buf, bufsz, &ended);

                    // Taking bytes is what moves the receive window forward, and the peer only
                    // learns it moved when the MAX_STREAM_DATA that says so goes out.
                    if (n > 0)
                        enginePump(sock, clockTimer());
                }
            }

            objRelease(&sock);
        }
    }

    if (fin)
        *fin = ended;

    return n;
}

_Use_decl_annotations_
size_t netquicReadable(NetFlow* flow)
{
    size_t n = 0;

    QuicStream* st = objDynCast(QuicStream, flow);
    if (st) {
        NetSocketQuic* sock = flowSock(flow);
        if (sock) {
            QuicEngine* eng = engOf(sock);

            withMutex (&eng->lock) {
                if (eng->streamsInit && !eng->torndown)
                    n = _quicStreamReadable(&eng->streams, st->key);
            }

            objRelease(&sock);
        }
    }

    return n;
}

_Use_decl_annotations_
size_t netquicWritable(NetFlow* flow)
{
    size_t n = 0;

    QuicStream* st = objDynCast(QuicStream, flow);
    if (st) {
        NetSocketQuic* sock = flowSock(flow);
        if (sock) {
            QuicEngine* eng = engOf(sock);

            withMutex (&eng->lock) {
                if (eng->streamsInit && !eng->torndown)
                    n = _quicStreamWritable(&eng->streams, st->key);
            }

            objRelease(&sock);
        }
    }

    return n;
}

// The three ways an application ends one direction of a stream. They differ only in which stream
// call they make, so they share everything else.
typedef enum QuicStreamEnd {
    QSE_Finish,
    QSE_Reset,
    QSE_Stop,
} QuicStreamEnd;

static void streamEnd(_In_ NetFlow* flow, QuicStreamEnd how, uint64 error)
{
    QuicStream* st = objDynCast(QuicStream, flow);
    if (!st)
        return;

    NetSocketQuic* sock = flowSock(flow);
    if (!sock)
        return;

    QuicEngine* eng = engOf(sock);

    withMutex (&eng->lock) {
        if (eng->streamsInit && !eng->torndown) {
            switch (how) {
            case QSE_Finish:
                _quicStreamFinish(&eng->streams, st->key);
                break;
            case QSE_Reset:
                _quicStreamReset(&eng->streams, st->key, error);
                break;
            case QSE_Stop:
                _quicStreamStopSending(&eng->streams, st->key, error);
                break;
            }

            enginePump(sock, clockTimer());
        }
    }

    objRelease(&sock);
}

_Use_decl_annotations_
void netquicFinish(NetFlow* flow)
{
    streamEnd(flow, QSE_Finish, 0);
}

_Use_decl_annotations_
void netquicReset(NetFlow* flow, uint64 error)
{
    streamEnd(flow, QSE_Reset, error);
}

_Use_decl_annotations_
void netquicStopSending(NetFlow* flow, uint64 error)
{
    streamEnd(flow, QSE_Stop, error);
}


// ---------------------------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void netquicDebugState(NetSocket* sock, strhandle out)
{
    NetSocketQuic* self = objDynCast(NetSocketQuic, sock);
    if (!self)
        return;

    QuicEngine* eng = engOf(self);

    withMutex (&eng->lock) {
        string line = 0;

        strFormat(&line,
                  _SLL("sock: streamsInit=${uint} streamsUp=${uint} connected=${uint} "
                      "torndown=${uint} timerIn=${int}us\n"),
                  stvar(uint32, eng->streamsInit ? 1u : 0u),
                  stvar(uint32, eng->streamsUp ? 1u : 0u),
                  stvar(uint32, eng->connected ? 1u : 0u),
                  stvar(uint32, eng->torndown ? 1u : 0u),
                  stvar(int64, eng->timerAt == 0 ? -1 : eng->timerAt - clockTimer()));
        strAppend(out, line);

        // The UDP socket underneath: a datagram queue that never drains looks exactly like a
        // connection that has stopped sending, and only these three numbers tell them apart.
        if (self->endpoint) {
            strFormat(&line,
                      _SL("endpoint: sendQueued=${uint} canSend=${uint} sendBlocked=${uint}\n"),
                      stvar(uint64, (uint64)self->endpoint->sendQueued),
                      stvar(uint32, atomicLoad(bool, &self->endpoint->canSend, Relaxed) ? 1u : 0u),
                      stvar(uint32, self->endpoint->sendBlocked ? 1u : 0u));
            strAppend(out, line);
        }

        strDestroy(&line);

        if (eng->conn)
            _quicConnDebug(eng->conn, clockTimer(), out);
        if (eng->streamsInit)
            _quicStreamsDebug(&eng->streams, out);
    }
}
// Autogen begins -----
// clang-format off
#include "quicsocket.auto.inc"
// clang-format on
// Autogen ends -------

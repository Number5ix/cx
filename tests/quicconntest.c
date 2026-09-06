// The QUIC connection state machine, driven end to end between a cx client and a cx server with
// no sockets involved.
//
// The harness wires two QuicConn objects to each other: each one's send handler drops the finished
// datagram into a queue, and the test shuttles the queues until both ends go quiet. The clock is
// the test's own, so timeouts and closing periods are reached by moving it rather than by waiting.
//
// What that buys is that every packet in these tests is a real one -- header protected, sealed
// under keys derived from a real TLS 1.3 handshake, and parsed back out the same way. A test that
// says the handshake completed has established the whole stack below it at once.

#include <cxquic.h>

#include "../cxquic/stream_private.h"

#include "tlstestcert.h"

#define TEST_FILE  quicconntest
#define TEST_FUNCS quicconntest_funcs
#include "common.h"

// bool CHECK(const char *what, bool cond);
#define CHECK(what, cond)                                                 \
    do {                                                                  \
        if (!(cond)) {                                                    \
            TEST_FAILV(ret, 1, _SL("${string}"), stvar(strref, _S what));  \
            goto out;                                                     \
        }                                                                 \
    } while (0)

// bool CHECK_U(const char *what, int64 got, int64 want);
#define CHECK_U(what, got, want)                                              \
    do {                                                                      \
        if ((int64)(got) != (int64)(want)) {                                  \
            TEST_FAILV(ret, 1, _SL("${string}: got ${int}, expected ${int}"),  \
                       stvar(strref, _S what), stvar(int64, (int64)(got)),     \
                       stvar(int64, (int64)(want)));                          \
            goto out;                                                         \
        }                                                                     \
    } while (0)

// bool CHECK_UGE(const char *what, int64 got, int64 least);
#define CHECK_UGE(what, got, least)                                            \
    do {                                                                       \
        if ((int64)(got) < (int64)(least)) {                                   \
            TEST_FAILV(ret, 1, _SL("${string}: got ${int}, expected at least ${int}"),  \
                       stvar(strref, _S what), stvar(int64, (int64)(got)),      \
                       stvar(int64, (int64)(least)));                          \
            goto out;                                                          \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Two connections wired to each other
// ---------------------------------------------------------------------------------------------

#define QMAX_QUEUE 32

typedef struct QSide {
    QuicConn* conn;
    struct QFix* f;

    Buffer out[QMAX_QUEUE];   // datagrams waiting to be handed to the other side
    uint8 outEcn[QMAX_QUEUE]; // the ECN codepoint each one asked for, or 0xff for none
    uint32 nout;
    uint32 dropped;           // datagrams the queue had no room for
    uint64 bytesSent;

    // What the network does to this side's datagrams: let `dropAfter` of them through, then throw
    // the next `dropNext` away as if the path had.
    uint32 dropAfter;
    uint32 dropNext;
    uint32 ndropped;

    // The path this side's datagrams travel, as far as anything below QUIC is concerned. Nothing
    // here is QUIC state -- it is what a router or a link would do to a packet in transit, and it
    // is the only way to test behaviour that exists to cope with exactly that.
    size_t pathMtu;           // datagrams larger than this never arrive; 0 for no limit
    NetAddr blackhole;        // datagrams addressed here never arrive
    bool blackholeSet;
    uint32 ecnStripEvery;     // strip the mark from one datagram in this many; 0 for none
    uint32 ecnSeq;            // datagrams sent, for ecnStripEvery
    bool ecnStrip;            // the path does not carry ECN marks
    bool ecnRemark;           // a congested router rewrites every mark to CE
    bool ecnLie;              // the receiving end reports counts that never go up
    uint32 nTooBig;           // datagrams the path was too small for
    uint32 nMarked;           // datagrams this side asked to have marked ECT(0)
    size_t largestSent;       // the largest datagram this side has put on the path
    NetAddr lastDest;         // where the most recent datagram was addressed

    bool connected;
    bool closed;
    bool closeLocal;
    bool closeApp;
    uint64 closeError;
    string closeReason;

    // Set by tests that want to see 1-RTT frames move in a particular direction.
    bool sendMaxData;
    bool sendBadFrame;
    bool sendStream;
    uint64 sendStreamId;
    bool sendBadPathResponse;
    uint64 sendRoleFrame;   // a frame type only the other role may send, or 0
    bool sendBadAck;

    // How the next 1-RTT packet abuses NEW_CONNECTION_ID: 0 not at all, 1 more IDs than the peer
    // agreed to hold, 2 one sequence number naming two different IDs, 3 a zero-length ID.
    int sendCidMode;

    // Fill every packet to the brim with PING frames, so the connection has more to send than it
    // is allowed to have unanswered. Nothing else in this stage produces bulk traffic.
    bool fillBulk;

    // An acknowledgement built by hand, so a test can say how long the peer held on to it. Two cx
    // endpoints acknowledge on receipt, so the delay a real one reports is always zero.
    bool sendAck;
    uint64 ackLargest;
    uint64 ackDelayRaw;

    // ECN counts to put in that hand-built acknowledgement. Two cx endpoints always report counts
    // that rise, so a peer that claims otherwise has to be written by hand.
    bool sendAckEcn;
    uint64 ackEcn[3];

    uint64 gotMaxData;
    uint32 nMaxData;

    QuicCid issuedCid;
    uint64 issuedSeq;
    uint32 nissued;
    uint32 nretired;

    Buffer newToken;

    // The stream layer, wired into the connection's frame, fill and packet handlers. Off unless
    // the test asks for it, so the tests that use those handlers to inject frames still can.
    QuicStreams streams;
    bool streamsInit;
    bool streamsUp;      // the handshake finished, so the peer's limits are known
    uint32 nStreamOpen;
    uint32 nStreamClosed;
    uint32 nStreamReset;
    uint64 streamResetError;

    // 0-RTT, as the layer above sees it.
    bool earlyOpened;
    bool earlyRejected;

    // A DATAGRAM frame written by hand, for the checks a peer that follows the rules cannot
    // produce: one sent to an endpoint that never asked for them, and one larger than the size an
    // endpoint said it would accept.
    bool sendDatagram;
    size_t sendDatagramLen;

    // Unreliable datagrams, as the layer above sees them.
    uint32 ndgramIn;
    uint32 ndgramWritable;
    size_t lastDgramLen;
    Buffer lastDgram;
} QSide;

typedef struct QFix {
    TlsTestPKI pki;
    TlsCAStore* ca;
    TlsCreds* serverCreds;
    TlsConfig* ccfg;
    TlsConfig* scfg;

    NetAddr cliAddr;
    NetAddr srvAddr;

    // The ack_delay_exponent the client advertises, which is the scale the server's reported
    // acknowledgement delays arrive in. Zero leaves it at the default.
    uint64 cliAckDelayExp;

    QSide cli;
    QSide srv;

    int64 now;

    // The listener's address validation key, which in a real server lives on the listening socket.
    uint8 tokenKey[32];

    // Copies of the CA certificate to pad the server's chain with, for the tests that need its
    // handshake flight to be larger than one datagram allows.
    int extraChain;

    // Give both ends a stream layer.
    bool streamMode;

    // Have both ends advertise max_datagram_frame_size, which is what turns RFC 9221 on, and the
    // size they advertise. Zero means the largest a datagram could hold.
    bool datagrams;
    uint64 datagramMax;

    // Override the per-stream and connection windows both ends advertise, for a test that needs a
    // transfer to run without ever being flow control blocked.
    uint64 streamWindow;

    // Have the server advertise a smaller connection window than it did last time, which is the
    // one thing a resuming client's early data can be measured against and found not to fit.
    bool srvShrink;
} QFix;

// Whether two addresses name the same endpoint. Only the fields that mean anything for the family
// are compared, the way NetAddr's own stype does.
static bool qAddrEq(_In_ const NetAddr* a, _In_ const NetAddr* b)
{
    if (a->type != b->type || a->port != b->port)
        return false;
    if (a->type == NA_IPv6)
        return memcmp(a->ipv6, b->ipv6, sizeof(a->ipv6)) == 0;
    return memcmp(a->ipv4, b->ipv4, sizeof(a->ipv4)) == 0;
}

static bool qSend(_In_opt_ void* ctx, _In_ const NetAddr* peer, _In_ const NetPktInfo* info,
                  _In_reads_bytes_(len) const uint8* data, size_t len)
{
    QSide* s   = ctx;
    s->lastDest = *peer;

    // An address nothing answers from. This is what an endpoint that follows a forged migration
    // runs into, and the only way to reach the code that gives up on a path and goes back.
    if (s->blackholeSet && qAddrEq(peer, &s->blackhole)) {
        s->ndropped++;
        return true;
    }

    if (info && info->haveEcn)
        s->nMarked++;
    if (len > s->largestSent)
        s->largestSent = len;

    if (s->dropAfter > 0) {
        s->dropAfter--;
    } else if (s->dropNext > 0) {
        s->dropNext--;
        s->ndropped++;
        return true;
    }

    s->bytesSent += len;

    // A datagram larger than the path carries is dropped by the link, not refused by the socket.
    // That is exactly the signal path MTU discovery is built to read, so from here it is
    // indistinguishable from a loss -- which is the point.
    if (s->pathMtu > 0 && len > s->pathMtu) {
        s->nTooBig++;
        return true;
    }

    if (s->nout >= QMAX_QUEUE) {
        s->dropped++;
        return true;
    }

    uint8 ecn = 0xff;
    if (info && info->haveEcn && !s->ecnStrip)
        ecn = s->ecnRemark ? (uint8)NET_ECN_CE : info->ecn;

    // A path that carries the marks on most packets and drops them on the rest. The peer's counts
    // then rise, but by fewer than were marked, which no single-packet observation would reveal.
    if (s->ecnStripEvery > 0 && (++s->ecnSeq % s->ecnStripEvery) == 0)
        ecn = 0xff;

    Buffer b = bufCreate(len);
    memcpy(b->data, data, len);
    b->len              = len;
    s->outEcn[s->nout]  = ecn;
    s->out[s->nout++]   = b;
    return true;
}

static void qConnected(_In_opt_ void* ctx)
{
    QSide* s     = ctx;
    s->connected = true;

    // The peer's transport parameters are what say how much this endpoint may send, so the stream
    // layer cannot send anything until the handshake has produced them.
    if (s->streamsInit) {
        _quicStreamsPeerParams(&s->streams, _quicConnPeerParams(s->conn));
        s->streamsUp = true;
    }
}

// 0-RTT is armed. A client is handed the limits the resumed session ran under and a server NULL,
// because it already has the client's real ones; either way this is where the stream layer learns
// how much it may send, a round trip before the handshake would have said.
static bool qEarlyOpen(_In_opt_ void* ctx, _In_opt_ const QuicTransportParams* tp)
{
    QSide* s = ctx;

    if (!s->streamsInit)
        return false;

    _quicStreamsPeerParams(&s->streams, tp ? tp : _quicConnPeerParams(s->conn));
    s->streamsUp   = true;
    s->earlyOpened = true;
    return true;
}

static void qEarlyReject(_In_opt_ void* ctx)
{
    QSide* s         = ctx;
    s->earlyRejected = true;
}

static void qClosed(_In_opt_ void* ctx, uint64 error, bool app, bool local, _In_opt_ strref reason)
{
    QSide* s      = ctx;
    s->closed     = true;
    s->closeError = error;
    s->closeApp   = app;
    s->closeLocal = local;
    strDup(&s->closeReason, reason);
}

static bool qFrame(_In_opt_ void* ctx, _In_ const QuicFrame* f)
{
    QSide* s = ctx;

    if (s->streamsInit) {
        if (_quicStreamsFrame(&s->streams, f))
            return true;

        _quicConnAbort(s->conn, s->streams.error, s->streams.errorFrame);
        return false;
    }

    if (f->type == QUIC_FRAME_MAX_DATA) {
        s->gotMaxData = f->maxData.max;
        s->nMaxData++;
    }
    return true;
}

static size_t qFill(_In_opt_ void* ctx, _Out_writes_(bufsz) uint8* buf, size_t bufsz, uint64 pn,
                    _Out_ bool* ackEliciting)
{
    QSide* s      = ctx;
    *ackEliciting = false;

    if (s->sendAck) {
        QuicAckRange r;
        r.largest  = s->ackLargest;
        r.smallest = s->ackLargest;

        uint8 rb[64];
        QuicFrame af;
        if (!_quicAckBuild(&af, s->ackDelayRaw, &r, 1, s->sendAckEcn ? s->ackEcn : NULL, rb,
                           sizeof(rb)) ||
            _quicFrameSize(&af) > bufsz)
            return 0;

        QuicWr aw;
        _quicWrInit(&aw, buf, bufsz);
        if (!_quicFrameEncode(&aw, &af))
            return 0;

        s->sendAck = false;
        return _quicWrLen(&aw);
    }

    if (s->fillBulk) {
        QuicFrame pf;
        memset(&pf, 0, sizeof(pf));
        pf.type = QUIC_FRAME_PING;

        QuicWr pw;
        _quicWrInit(&pw, buf, bufsz);
        while (_quicFrameSize(&pf) <= _quicWrLeft(&pw)) {
            if (!_quicFrameEncode(&pw, &pf))
                break;
        }

        *ackEliciting = _quicWrLen(&pw) > 0;
        return _quicWrLen(&pw);
    }

    // A frame type QUIC version 1 does not define. Nothing says how long such a frame is, so it
    // cannot be skipped over -- RFC 9000 section 12.4 makes it a FRAME_ENCODING_ERROR.
    if (s->sendBadFrame && bufsz >= 1) {
        s->sendBadFrame = false;
        *ackEliciting   = true;
        buf[0]          = 0x3f;
        return 1;
    }

    if (s->sendBadAck) {
        QuicAckRange r;
        r.largest  = 1000000;
        r.smallest = 1000000;

        uint8 rb[64];
        QuicFrame af;
        if (!_quicAckBuild(&af, 0, &r, 1, NULL, rb, sizeof(rb)) || _quicFrameSize(&af) > bufsz)
            return 0;

        QuicWr aw;
        _quicWrInit(&aw, buf, bufsz);
        if (!_quicFrameEncode(&aw, &af))
            return 0;

        s->sendBadAck = false;
        return _quicWrLen(&aw);
    }

    if (s->sendCidMode != 0) {
        int mode = s->sendCidMode;
        uint64 count = mode == 1 ? 8 : 2;

        QuicWr cw;
        _quicWrInit(&cw, buf, bufsz);

        for (uint64 i = 0; i < count; i++) {
            QuicFrame nf;
            memset(&nf, 0, sizeof(nf));
            nf.type              = QUIC_FRAME_NEW_CONNECTION_ID;
            nf.newConnId.seq     = mode == 2 ? 10 : 10 + i;
            nf.newConnId.cid.len = mode == 3 ? 0 : 8;
            memset(nf.newConnId.cid.id, (uint8)(0x40 + i), 8);
            memset(nf.newConnId.token, (uint8)i, QUIC_RESET_TOKEN_LEN);

            if (_quicFrameSize(&nf) > _quicWrLeft(&cw) || !_quicFrameEncode(&cw, &nf))
                break;
        }

        s->sendCidMode = 0;
        *ackEliciting  = true;
        return _quicWrLen(&cw);
    }

    if (s->sendRoleFrame != 0) {
        QuicFrame rf;
        memset(&rf, 0, sizeof(rf));
        rf.type = s->sendRoleFrame;

        // NEW_TOKEN needs a body; HANDSHAKE_DONE has none.
        if (rf.type == QUIC_FRAME_NEW_TOKEN) {
            rf.newToken.len   = 4;
            rf.newToken.token = (const uint8*)"tokn";
        }

        if (_quicFrameSize(&rf) > bufsz)
            return 0;

        QuicWr rw;
        _quicWrInit(&rw, buf, bufsz);
        if (!_quicFrameEncode(&rw, &rf))
            return 0;

        s->sendRoleFrame = 0;
        *ackEliciting    = true;
        return _quicWrLen(&rw);
    }

    if (s->sendDatagram) {
        static const uint8 dgpay[512] = { 0 };

        QuicFrame df;
        memset(&df, 0, sizeof(df));
        df.type          = QUIC_FRAME_DATAGRAM_LEN;
        df.datagram.len  = s->sendDatagramLen ? s->sendDatagramLen : 4;
        df.datagram.data = dgpay;

        if (_quicFrameSize(&df) > bufsz)
            return 0;

        QuicWr dw;
        _quicWrInit(&dw, buf, bufsz);
        if (!_quicFrameEncode(&dw, &df))
            return 0;

        s->sendDatagram = false;
        *ackEliciting   = true;
        return _quicWrLen(&dw);
    }

    if (s->sendBadPathResponse) {
        QuicFrame pf;
        memset(&pf, 0, sizeof(pf));
        pf.type = QUIC_FRAME_PATH_RESPONSE;
        memset(pf.path.data, 0xaa, QUIC_PATH_DATA_LEN);

        if (_quicFrameSize(&pf) > bufsz)
            return 0;

        QuicWr pw;
        _quicWrInit(&pw, buf, bufsz);
        if (!_quicFrameEncode(&pw, &pf))
            return 0;

        s->sendBadPathResponse = false;
        *ackEliciting          = true;
        return _quicWrLen(&pw);
    }

    if (s->sendStream) {
        QuicFrame sf;
        memset(&sf, 0, sizeof(sf));
        sf.type      = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
        sf.stream.id = s->sendStreamId;

        if (_quicFrameSize(&sf) > bufsz)
            return 0;

        QuicWr sw;
        _quicWrInit(&sw, buf, bufsz);
        if (!_quicFrameEncode(&sw, &sf))
            return 0;

        s->sendStream = false;
        *ackEliciting = true;
        return _quicWrLen(&sw);
    }

    // Nothing was injected, so the stream layer gets the packet. A test that injects a frame takes
    // priority for that one packet only.
    if (!s->sendMaxData)
        return s->streamsUp ? _quicStreamsFill(&s->streams, buf, bufsz, pn, ackEliciting) : 0;

    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type        = QUIC_FRAME_MAX_DATA;
    f.maxData.max = 4096;

    if (_quicFrameSize(&f) > bufsz)
        return 0;

    QuicWr wr;
    _quicWrInit(&wr, buf, bufsz);
    if (!_quicFrameEncode(&wr, &f))
        return 0;

    s->sendMaxData = false;
    *ackEliciting  = true;
    return _quicWrLen(&wr);
}

static bool qWantsToSend(_In_opt_ void* ctx)
{
    const QSide* s = ctx;
    if (s->streamsUp && _quicStreamsWantsToSend(&s->streams))
        return true;
    return s->fillBulk || s->sendAck;
}

static void qPktAcked(_In_opt_ void* ctx, uint64 pn)
{
    QSide* s = ctx;
    if (s->streamsInit)
        _quicStreamsAcked(&s->streams, pn);
}

static void qPktLost(_In_opt_ void* ctx, uint64 pn)
{
    QSide* s = ctx;
    if (s->streamsInit)
        _quicStreamsLost(&s->streams, pn);
}

static bool qStreamOpened(_In_opt_ void* ctx, uint64 id)
{
    QSide* s = ctx;
    unused_noeval(id);
    s->nStreamOpen++;
    return true;
}

static void qStreamReset(_In_opt_ void* ctx, uint64 id, uint64 error)
{
    QSide* s           = ctx;
    unused_noeval(id);
    s->nStreamReset++;
    s->streamResetError = error;
}

static void qStreamClosed(_In_opt_ void* ctx, uint64 id)
{
    QSide* s = ctx;
    unused_noeval(id);
    s->nStreamClosed++;
}

static const QuicStreamHandlers qStreamHandlers = {
    .opened = qStreamOpened,
    .reset  = qStreamReset,
    .closed = qStreamClosed,
};

static void qDatagramRecv(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    QSide* s = ctx;

    s->ndgramIn++;
    s->lastDgramLen = len;

    // Copied, because the frame points into the packet buffer the caller is about to reuse -- the
    // same thing the socket layer has to do with it.
    bufDestroy(&s->lastDgram);
    bufAppendBytes(&s->lastDgram, data, len);
}

static void qDatagramWritable(_In_opt_ void* ctx)
{
    QSide* s = ctx;
    s->ndgramWritable++;
}

static void qCidIssued(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq,
                       _In_reads_bytes_(QUIC_RESET_TOKEN_LEN) const uint8* resetToken)
{
    QSide* s = ctx;
    unused_noeval(resetToken);

    s->issuedCid = *cid;
    s->issuedSeq = seq;
    s->nissued++;
}

static void qCidRetired(_In_opt_ void* ctx, _In_ const QuicCid* cid, uint64 seq)
{
    QSide* s = ctx;
    unused_noeval(cid);
    unused_noeval(seq);
    s->nretired++;
}

static void qToken(_In_opt_ void* ctx, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    QSide* s = ctx;
    bufDestroy(&s->newToken);
    bufAppendBytes(&s->newToken, data, len);
}

static const QuicConnHandlers qHandlers = {
    .earlyOpen   = qEarlyOpen,
    .earlyReject = qEarlyReject,
    .send        = qSend,
    .connected   = qConnected,
    .closed      = qClosed,
    .frame       = qFrame,
    .fill        = qFill,
    .wantsToSend = qWantsToSend,
    .pktAcked    = qPktAcked,
    .pktLost     = qPktLost,
    .cidIssued   = qCidIssued,
    .cidRetired  = qCidRetired,
    .token       = qToken,
    .datagramRecv     = qDatagramRecv,
    .datagramWritable = qDatagramWritable,
};

static void qSideDestroy(_Inout_ QSide* s)
{
    if (s->streamsInit) {
        _quicStreamsDestroy(&s->streams);
        s->streamsInit = false;
    }

    for (uint32 i = 0; i < s->nout; i++)
        bufDestroy(&s->out[i]);
    s->nout = 0;

    _quicConnDestroy(&s->conn);
    strDestroy(&s->closeReason);
    bufDestroy(&s->newToken);
    bufDestroy(&s->lastDgram);
}

// Hands everything one side has produced to the other. Datagrams are copies, so the receiver can
// unprotect headers in place the way it would with one off a socket.
static void qDeliver(_Inout_ QFix* f, _Inout_ QSide* from, _Inout_ QSide* to,
                     _In_ const NetAddr* fromAddr, _In_ const NetAddr* toAddr)
{
    uint32 n = from->nout;
    from->nout = 0;

    for (uint32 i = 0; i < n; i++) {
        if (to->conn) {
            NetPktInfo info;
            memset(&info, 0, sizeof(info));
            info.local     = *toAddr;
            info.haveLocal = true;
            if (from->outEcn[i] != 0xff) {
                info.ecn     = from->outEcn[i];
                info.haveEcn = true;
            }
            _quicConnRecv(to->conn, fromAddr, &info, from->out[i]->data, from->out[i]->len,
                          f->now);
        }
        bufDestroy(&from->out[i]);
    }
}

// Shuttles datagrams until neither end has anything left to say.
static void qRun(_Inout_ QFix* f)
{
    for (int i = 0; i < 32 && (f->cli.nout > 0 || f->srv.nout > 0); i++) {
        qDeliver(f, &f->cli, &f->srv, &f->cliAddr, &f->srvAddr);
        qDeliver(f, &f->srv, &f->cli, &f->srvAddr, &f->cliAddr);
    }
}

// Shuttles datagrams the way qRun does, but also moves the clock to whichever deadline comes
// first and runs the timers, so retransmissions actually happen.
//
// `limit` bounds how far the clock may move, which is what keeps a test waiting for a
// retransmission from silently running into the idle timeout instead.
static void qRunTimed(_Inout_ QFix* f, int rounds, int64 limit)
{
    int64 stop = f->now + limit;

    for (int i = 0; i < rounds; i++) {
        if (f->cli.nout > 0 || f->srv.nout > 0) {
            qDeliver(f, &f->cli, &f->srv, &f->cliAddr, &f->srvAddr);
            qDeliver(f, &f->srv, &f->cli, &f->srvAddr, &f->cliAddr);
            continue;
        }

        int64 d = timeForever;
        if (f->cli.conn) {
            int64 t = _quicConnDeadline(f->cli.conn);
            if (t < d)
                d = t;
        }
        if (f->srv.conn) {
            int64 t = _quicConnDeadline(f->srv.conn);
            if (t < d)
                d = t;
        }

        if (d == timeForever || d > stop)
            return;
        if (d > f->now)
            f->now = d;

        if (f->cli.conn)
            _quicConnTick(f->cli.conn, f->now);
        if (f->srv.conn)
            _quicConnTick(f->srv.conn, f->now);
    }
}

static void qTpDefaults(_Out_ QuicTransportParams* tp)
{
    _quicTpDefaults(tp);
    tp->maxIdleTimeout       = 30000;
    tp->initMaxData          = 1 << 20;
    tp->initMaxSdBidiLocal   = 1 << 16;
    tp->initMaxSdBidiRemote  = 1 << 16;
    tp->initMaxSdUni         = 1 << 16;
    tp->initMaxStreamsBidi   = 8;
    tp->initMaxStreamsUni    = 8;
    tp->activeCidLimit       = 4;
}

static bool qFixInit2(_Out_ QFix* f, int extraChain)
{
    memset(f, 0, sizeof(*f));
    f->extraChain = extraChain;

    // Nothing in cxquic works before PSA does, and the fixture reaches for randomness before it
    // has built anything that would have run the module's own initializer.
    if (!_quicInit())
        return false;

    f->now = timeS(1000);

    f->cliAddr.type = NA_IPv4;
    f->cliAddr.port = 40000;
    f->cliAddr.ipv4[0] = 192; f->cliAddr.ipv4[1] = 0;
    f->cliAddr.ipv4[2] = 2;   f->cliAddr.ipv4[3] = 10;

    f->srvAddr.type = NA_IPv4;
    f->srvAddr.port = 443;
    f->srvAddr.ipv4[0] = 192; f->srvAddr.ipv4[1] = 0;
    f->srvAddr.ipv4[2] = 2;   f->srvAddr.ipv4[3] = 1;

    if (psa_generate_random(f->tokenKey, sizeof(f->tokenKey)) != PSA_SUCCESS)
        return false;

    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    string chain = 0;
    strDup(&chain, f->pki.serverCert);
    for (int i = 0; i < f->extraChain; i++)
        strAppend(&chain, f->pki.caCert);

    f->serverCreds = tlscredsCreatePEM(chain, f->pki.serverKey, NULL);
    strDestroy(&chain);
    if (!f->serverCreds)
        return false;

    f->ccfg = tlsconfigCreateClient();
    f->scfg = tlsconfigCreateServer(f->serverCreds);
    if (!f->ccfg || !f->scfg)
        return false;

    tlsconfigSetCA(f->ccfg, f->ca);

    f->cli.f = f;
    f->srv.f = f;
    return true;
}

static bool qFixInit(_Out_ QFix* f)
{
    return qFixInit2(f, 0);
}

static void qFixDestroy(_Inout_ QFix* f)
{
    qSideDestroy(&f->cli);
    qSideDestroy(&f->srv);

    objRelease(&f->ccfg);
    objRelease(&f->scfg);
    objRelease(&f->serverCreds);
    objRelease(&f->ca);
    tlsTestPKIDestroy(&f->pki);
}

// Creates the client and lets it emit its first Initial.
static bool qStartClient(_Inout_ QFix* f)
{
    QuicConnConfig cc;
    memset(&cc, 0, sizeof(cc));
    cc.tls         = f->ccfg;
    cc.hostname    = _S TLS_TEST_HOSTNAME;
    cc.localCidLen = 8;
    qTpDefaults(&cc.tp);
    if (f->streamWindow != 0) {
        cc.tp.initMaxData         = f->streamWindow;
        cc.tp.initMaxSdBidiLocal  = f->streamWindow;
        cc.tp.initMaxSdBidiRemote = f->streamWindow;
        cc.tp.initMaxSdUni        = f->streamWindow;
    }
    if (f->cliAckDelayExp != 0)
        cc.tp.ackDelayExponent = f->cliAckDelayExp;
    if (f->datagrams)
        cc.tp.maxDatagramFrame = f->datagramMax ? f->datagramMax : QUIC_MAX_DATAGRAM;

    f->cli.conn = _quicConnCreate(false, &cc, &f->srvAddr);
    if (!f->cli.conn)
        return false;

    if (f->streamMode) {
        _quicStreamsInit(&f->cli.streams, false, &cc.tp);
        _quicStreamsSetHandlers(&f->cli.streams, &qStreamHandlers, &f->cli);
        f->cli.streamsInit = true;
    }

    _quicConnSetHandlers(f->cli.conn, &qHandlers, &f->cli);
    return _quicConnStart(f->cli.conn, f->now);
}

// What a listener does with a datagram for a connection it does not have: read the connection IDs
// off the first Initial packet and build a connection around them.
static bool qAccept(_Inout_ QFix* f, _In_opt_ const QuicCid* origDcid)
{
    if (f->cli.nout == 0)
        return false;

    QuicPktHdr h;
    if (!_quicHdrDecode(&h, f->cli.out[0]->data, f->cli.out[0]->len, 0))
        return false;
    if (h.type != QUIC_PKT_INITIAL)
        return false;

    QuicConnConfig sc;
    memset(&sc, 0, sizeof(sc));
    sc.tls         = f->scfg;
    sc.localCidLen = 8;
    sc.clientDcid  = h.dcid;
    sc.peerCid     = h.scid;
    qTpDefaults(&sc.tp);
    if (f->streamWindow != 0) {
        sc.tp.initMaxData         = f->streamWindow;
        sc.tp.initMaxSdBidiLocal  = f->streamWindow;
        sc.tp.initMaxSdBidiRemote = f->streamWindow;
        sc.tp.initMaxSdUni        = f->streamWindow;
    }

    // Deliberately far larger than the client's, so that a connection using the wrong one of the
    // two idle timeouts is visible rather than coincidentally right.
    sc.tp.maxIdleTimeout = 120000;

    if (f->datagrams)
        sc.tp.maxDatagramFrame = f->datagramMax ? f->datagramMax : QUIC_MAX_DATAGRAM;

    // RFC 9001 section 4.5: a resuming client sends its early data under the limits the session it
    // is resuming was given, so a server offering less than it did cannot take that data.
    if (f->srvShrink)
        sc.tp.initMaxData /= 2;

    if (origDcid) {
        sc.origDcid     = *origDcid;
        sc.haveOrigDcid = true;
    }

    f->srv.conn = _quicConnCreate(true, &sc, &f->cliAddr);
    if (!f->srv.conn)
        return false;

    if (f->streamMode) {
        _quicStreamsInit(&f->srv.streams, true, &sc.tp);
        _quicStreamsSetHandlers(&f->srv.streams, &qStreamHandlers, &f->srv);
        f->srv.streamsInit = true;
    }

    _quicConnSetHandlers(f->srv.conn, &qHandlers, &f->srv);
    return true;
}

// The whole ordinary path: client starts, listener accepts, both ends talk until quiet.
static bool qHandshake(_Inout_ QFix* f)
{
    return qFixInit(f) && qStartClient(f) && qAccept(f, NULL) && (qRun(f), true);
}

// The same, with a stream layer wired into both ends.
static bool qHandshakeStreams2(_Inout_ QFix* f, uint64 window)
{
    if (!qFixInit(f))
        return false;

    f->streamMode   = true;
    f->streamWindow = window;
    return qStartClient(f) && qAccept(f, NULL) && (qRun(f), true);
}

static bool qHandshakeStreams(_Inout_ QFix* f)
{
    return qHandshakeStreams2(f, 0);
}

// Both ends advertising max_datagram_frame_size, with a stream layer alongside so the tests that
// need datagrams and stream data competing for the same packet have both.
static bool qHandshakeDatagrams(_Inout_ QFix* f)
{
    if (!qFixInit(f))
        return false;

    f->streamMode = true;
    f->datagrams  = true;
    return qStartClient(f) && qAccept(f, NULL) && (qRun(f), true);
}

// A fixture whose two ends will resume, and optionally use 0-RTT when they do. Both settings have
// to be in place before the first handshake: what the second one can do is decided by the ticket
// the first one issues.
static bool qFixInitResume(_Out_ QFix* f, bool early)
{
    if (!qFixInit(f))
        return false;

    f->streamMode = true;

    return tlsconfigSetResumption(f->ccfg, true, timeS(600)) &&
           tlsconfigSetResumption(f->scfg, true, timeS(600)) &&
           (!early || (tlsconfigSetEarlyData(f->ccfg, true) &&
                       tlsconfigSetEarlyData(f->scfg, true)));
}

// Throws away one end's connection without touching the fixture's TLS configurations, which is
// what lets a second connection be run against the same server and offered the same ticket.
static void qSideReset(_Inout_ QSide* s)
{
    QFix* f = s->f;
    qSideDestroy(s);
    memset(s, 0, sizeof(*s));
    s->f = f;
}

// A second connection between the same two configurations. Stops with the client's first flight
// built but not delivered, so a test can queue 0-RTT data before the server has seen anything.
static bool qResumeStart(_Inout_ QFix* f)
{
    qSideReset(&f->cli);
    qSideReset(&f->srv);
    return qStartClient(f);
}

// One turn of a stream transfer: hand the connections whatever the application has, move the
// datagrams, and let time pass if that is what everything is waiting on.
static void qStreamRound(_Inout_ QFix* f)
{
    _quicConnFlush(f->cli.conn, f->now);
    _quicConnFlush(f->srv.conn, f->now);
    qRunTimed(f, 16, timeMS(200));
}

// Reads everything readable on a stream into `out`.
static bool qStreamDrain(_Inout_ QSide* s, uint64 id, _Inout_ Buffer* out)
{
    uint8 tmp[8192];
    bool fin = false;

    for (;;) {
        bool done = false;
        size_t n  = _quicStreamRecv(&s->streams, id, tmp, sizeof(tmp), &done);
        if (done)
            fin = true;
        if (n > 0)
            bufAppendBytes(out, tmp, n);
        if (n == 0)
            break;
    }

    return fin;
}

// A server with no stream layer attached, for the tests that check what happens to frames it
// cannot handle.
static const QuicConnHandlers qHandlersNoStreams = {
    .send      = qSend,
    .connected = qConnected,
    .closed    = qClosed,
};

// ---------------------------------------------------------------------------------------------
// Transport parameters
// ---------------------------------------------------------------------------------------------

int test_quicconntest_params(void)
{
    int ret = 0;

    QuicTransportParams tp;
    _quicTpDefaults(&tp);

    CHECK_U("default max_udp_payload_size", tp.maxUdpPayload, 65527);
    CHECK_U("default ack_delay_exponent", tp.ackDelayExponent, 3);
    CHECK_U("default max_ack_delay", tp.maxAckDelay, 25);
    CHECK_U("default active_connection_id_limit", tp.activeCidLimit, 2);
    CHECK_U("default max_idle_timeout", tp.maxIdleTimeout, 0);

    // A server's full set, including the three connection ID parameters only it may send.
    tp.initScid.len = 4;
    memcpy(tp.initScid.id, "\x01\x02\x03\x04", 4);
    tp.haveInitScid = true;

    tp.origDcid.len = 8;
    memcpy(tp.origDcid.id, "abcdefgh", 8);
    tp.haveOrigDcid = true;

    tp.retryScid.len = 5;
    memcpy(tp.retryScid.id, "retry", 5);
    tp.haveRetryScid = true;

    memset(tp.resetToken, 0x5a, QUIC_RESET_TOKEN_LEN);
    tp.haveResetToken = true;

    tp.maxIdleTimeout      = 30000;
    tp.initMaxData         = 1048576;
    tp.initMaxSdBidiLocal  = 65536;
    tp.initMaxSdBidiRemote = 32768;
    tp.initMaxSdUni        = 16384;
    tp.initMaxStreamsBidi  = 100;
    tp.initMaxStreamsUni   = 3;
    tp.ackDelayExponent    = 10;
    tp.maxAckDelay         = 50;
    tp.activeCidLimit      = 6;
    tp.disableMigration    = true;
    tp.maxUdpPayload       = 1452;

    uint8 buf[512];
    QuicWr wr;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("encode", _quicTpEncode(&wr, &tp, true));

    QuicTransportParams got;
    CHECK("decode", _quicTpDecode(&got, buf, _quicWrLen(&wr), true));

    CHECK_U("max_idle_timeout", got.maxIdleTimeout, tp.maxIdleTimeout);
    CHECK_U("max_udp_payload_size", got.maxUdpPayload, tp.maxUdpPayload);
    CHECK_U("initial_max_data", got.initMaxData, tp.initMaxData);
    CHECK_U("initial_max_stream_data_bidi_local", got.initMaxSdBidiLocal, tp.initMaxSdBidiLocal);
    CHECK_U("initial_max_stream_data_bidi_remote", got.initMaxSdBidiRemote, tp.initMaxSdBidiRemote);
    CHECK_U("initial_max_stream_data_uni", got.initMaxSdUni, tp.initMaxSdUni);
    CHECK_U("initial_max_streams_bidi", got.initMaxStreamsBidi, tp.initMaxStreamsBidi);
    CHECK_U("initial_max_streams_uni", got.initMaxStreamsUni, tp.initMaxStreamsUni);
    CHECK_U("ack_delay_exponent", got.ackDelayExponent, tp.ackDelayExponent);
    CHECK_U("max_ack_delay", got.maxAckDelay, tp.maxAckDelay);
    CHECK_U("active_connection_id_limit", got.activeCidLimit, tp.activeCidLimit);
    CHECK("disable_active_migration", got.disableMigration);

    CHECK("initial_source_connection_id", got.haveInitScid && got.initScid.len == 4 &&
                                              memcmp(got.initScid.id, "\x01\x02\x03\x04", 4) == 0);
    CHECK("original_destination_connection_id",
          got.haveOrigDcid && got.origDcid.len == 8 && memcmp(got.origDcid.id, "abcdefgh", 8) == 0);
    CHECK("retry_source_connection_id",
          got.haveRetryScid && got.retryScid.len == 5 && memcmp(got.retryScid.id, "retry", 5) == 0);
    CHECK("stateless_reset_token", got.haveResetToken && got.resetToken[0] == 0x5a);

    // A client's set drops the three parameters it has nothing to say in, and is accepted by a
    // decoder that knows it came from a client.
    QuicTransportParams cli;
    _quicTpDefaults(&cli);
    cli.initScid.len = 2;
    cli.haveInitScid = true;
    cli.haveOrigDcid = true;   // ignored on encode

    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("client encode", _quicTpEncode(&wr, &cli, false));
    CHECK("client decode", _quicTpDecode(&got, buf, _quicWrLen(&wr), false));
    CHECK("a client's set named an original connection ID", !got.haveOrigDcid);

out:
    return ret;
}

int test_quicconntest_params_malformed(void)
{
    int ret = 0;
    QuicTransportParams got;

    // A parameter set with no initial_source_connection_id names nothing, so nothing can be
    // checked against the connection IDs already in use.
    static const uint8 noScid[] = { 0x01, 0x02, 0x75, 0x30 };
    CHECK("a set with no initial_source_connection_id was accepted",
          !_quicTpDecode(&got, noScid, sizeof(noScid), false));

    // initial_source_connection_id, twice.
    static const uint8 dup[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x0f, 0x02, 0xaa, 0xbb };
    CHECK("a duplicate parameter was accepted", !_quicTpDecode(&got, dup, sizeof(dup), false));

    // A server's parameters coming from a client.
    static const uint8 fromCli[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x00, 0x02, 0xcc, 0xdd };
    CHECK("a client's original_destination_connection_id was accepted",
          !_quicTpDecode(&got, fromCli, sizeof(fromCli), false));

    // max_udp_payload_size below the 1200 floor.
    static const uint8 small[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x03, 0x02, 0x44, 0x00 };
    CHECK("max_udp_payload_size under 1200 was accepted",
          !_quicTpDecode(&got, small, sizeof(small), false));

    // ack_delay_exponent above 20.
    static const uint8 bigExp[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x0a, 0x01, 0x15 };
    CHECK("ack_delay_exponent of 21 was accepted",
          !_quicTpDecode(&got, bigExp, sizeof(bigExp), false));

    // active_connection_id_limit below 2.
    static const uint8 lowCid[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x0e, 0x01, 0x01 };
    CHECK("active_connection_id_limit of 1 was accepted",
          !_quicTpDecode(&got, lowCid, sizeof(lowCid), false));

    // A length that runs past the end of the set.
    static const uint8 overrun[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x04, 0x08, 0x00 };
    CHECK("a parameter running past the end was accepted",
          !_quicTpDecode(&got, overrun, sizeof(overrun), false));

    // A connection ID longer than QUIC allows.
    uint8 longCid[3 + 21];
    longCid[0] = 0x0f;
    longCid[1] = 21;
    memset(longCid + 2, 0xcc, 21);
    CHECK("a 21-byte connection ID was accepted",
          !_quicTpDecode(&got, longCid, 2 + 21, false));

    // A parameter this version does not define is skipped, not rejected.
    static const uint8 unknown[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x7f, 0x02, 0x01, 0x02 };
    CHECK("an unknown parameter was rejected",
          _quicTpDecode(&got, unknown, sizeof(unknown), false));

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Reassembly and received packet numbers
// ---------------------------------------------------------------------------------------------

int test_quicconntest_reasm(void)
{
    int ret = 0;
    QuicReasm r;
    _quicReasmInit(&r, 1024);

    CHECK_U("empty", _quicReasmReadable(&r), 0);

    // Out of order: the tail arrives first and is held back until the head fills the gap.
    CHECK("add tail", _quicReasmAdd(&r, 4, (const uint8*)"defg", 4));
    CHECK_U("readable with a hole at the front", _quicReasmReadable(&r), 0);

    CHECK("add head", _quicReasmAdd(&r, 0, (const uint8*)"abcd", 4));
    CHECK_U("readable after the hole closed", _quicReasmReadable(&r), 8);
    CHECK("contents", memcmp(_quicReasmData(&r), "abcddefg", 8) == 0);

    _quicReasmConsume(&r, 4);
    CHECK_U("readable after consuming half", _quicReasmReadable(&r), 4);
    CHECK("contents after consuming", memcmp(_quicReasmData(&r), "defg", 4) == 0);

    // A retransmission of bytes already handed to the reader is ignored rather than rewound, and
    // that includes one that ends before the window even starts.
    CHECK("re-add consumed", _quicReasmAdd(&r, 0, (const uint8*)"ABCD", 4));
    CHECK_U("readable after a stale retransmission", _quicReasmReadable(&r), 4);
    CHECK("contents unchanged", memcmp(_quicReasmData(&r), "defg", 4) == 0);

    CHECK("re-add fully consumed", _quicReasmAdd(&r, 0, (const uint8*)"AB", 2));
    CHECK_U("readable after a wholly stale retransmission", _quicReasmReadable(&r), 4);
    CHECK("contents unchanged after a wholly stale retransmission",
          memcmp(_quicReasmData(&r), "defg", 4) == 0);

    // Overlapping ranges merge into one.
    CHECK("add overlap", _quicReasmAdd(&r, 6, (const uint8*)"ghij", 4));
    CHECK_U("readable after an overlapping add", _quicReasmReadable(&r), 6);
    CHECK("merged contents", memcmp(_quicReasmData(&r), "deghij", 6) == 0);

    _quicReasmConsume(&r, 6);
    CHECK_U("readable after draining", _quicReasmReadable(&r), 0);

    // Past the limit is a flow control violation, which is the one thing that fails.
    CHECK("data past the limit was accepted", !_quicReasmAdd(&r, 1020, (const uint8*)"12345", 5));

    _quicReasmDestroy(&r);

    // Holes are capped: a peer that fragments beyond what is tracked has its extra pieces dropped,
    // and the stream still completes once the gaps are filled in order.
    _quicReasmInit(&r, 4096);
    for (uint32 i = 0; i < QUIC_REASM_MAX_RANGES + 8; i++) {
        uint8 b = (uint8)('A' + (i % 26));
        CHECK("sparse add", _quicReasmAdd(&r, 2 + i * 2, &b, 1));
    }
    CHECK_U("readable with the head missing", _quicReasmReadable(&r), 0);
    CHECK("add the head", _quicReasmAdd(&r, 0, (const uint8*)"\x01\x41", 2));
    CHECK("readable after the head arrived", _quicReasmReadable(&r) >= 3);

    _quicReasmDestroy(&r);

out:
    return ret;
}

int test_quicconntest_ackset(void)
{
    int ret = 0;

    QuicAckState as;
    memset(&as, 0, sizeof(as));
    as.largest = QUIC_PN_NONE;

    CHECK("add 5", _quicAckStateAdd(&as, 5, 100, true));
    CHECK_U("largest", as.largest, 5);
    CHECK_U("ranges", as.nranges, 1);
    CHECK("pending", as.pending);

    CHECK("duplicate 5 was accepted", !_quicAckStateAdd(&as, 5, 100, true));

    // Adjacent numbers extend the range instead of making a new one.
    CHECK("add 6", _quicAckStateAdd(&as, 6, 101, true));
    CHECK_U("ranges after extending upward", as.nranges, 1);
    CHECK("add 4", _quicAckStateAdd(&as, 4, 102, true));
    CHECK_U("ranges after extending downward", as.nranges, 1);
    CHECK_U("range top", as.ranges[0].largest, 6);
    CHECK_U("range bottom", as.ranges[0].smallest, 4);

    // A gap makes a second range, and filling it merges the two back together.
    CHECK("add 9", _quicAckStateAdd(&as, 9, 103, true));
    CHECK_U("ranges with a gap", as.nranges, 2);
    CHECK_U("largest after 9", as.largest, 9);
    CHECK("add 8", _quicAckStateAdd(&as, 8, 104, true));
    CHECK("add 7", _quicAckStateAdd(&as, 7, 105, true));
    CHECK_U("ranges after the gap closed", as.nranges, 1);
    CHECK_U("merged top", as.ranges[0].largest, 9);
    CHECK_U("merged bottom", as.ranges[0].smallest, 4);

    // Ranges are capped. Every other packet number makes one range each, and once the list is full
    // the oldest information is what falls off.
    memset(&as, 0, sizeof(as));
    as.largest = QUIC_PN_NONE;
    for (uint64 pn = 0; pn < QUIC_MAX_ACK_RANGES * 2 + 20; pn += 2)
        CHECK("sparse add", _quicAckStateAdd(&as, pn, 200, true));

    CHECK_U("ranges are capped", as.nranges, QUIC_MAX_ACK_RANGES);
    CHECK_U("largest survives", as.largest, (QUIC_MAX_ACK_RANGES * 2 + 18));
    CHECK("ranges stay sorted largest first",
          as.ranges[0].largest > as.ranges[QUIC_MAX_ACK_RANGES - 1].largest);

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Address validation tokens
// ---------------------------------------------------------------------------------------------

int test_quicconntest_token(void)
{
    int ret = 0;

    CHECK("init", _quicInit());

    uint8 key[32];
    CHECK("key", psa_generate_random(key, sizeof(key)) == PSA_SUCCESS);

    NetAddr a = { 0 }, b = { 0 };
    a.type = NA_IPv4;
    a.port = 1234;
    a.ipv4[0] = 10; a.ipv4[3] = 1;
    b = a;
    b.ipv4[3] = 2;

    QuicCid odcid = { 0 };
    odcid.len = 8;
    memcpy(odcid.id, "12345678", 8);

    int64 now = clockWall();

    uint8 tok[QUIC_MAX_TOKEN];
    size_t toklen;
    CHECK("seal", _quicTokenSeal(tok, &toklen, key, true, &a, &odcid, now));
    CHECK("token length", toklen > 0 && toklen <= QUIC_MAX_TOKEN);

    bool retry = false;
    QuicCid got;
    CHECK("open", _quicTokenOpen(tok, toklen, key, &a, now, &retry, &got));
    CHECK("kind", retry);
    CHECK("connection id", got.len == 8 && memcmp(got.id, "12345678", 8) == 0);

    // The address is authenticated, not merely carried, so a token replayed from elsewhere fails.
    CHECK("a token opened for the wrong address", !_quicTokenOpen(tok, toklen, key, &b, now, &retry, &got));

    // Only the address matters, not the port: a NAT rebinding must not invalidate a token.
    NetAddr moved = a;
    moved.port    = 9999;
    CHECK("a port change invalidated the token",
          _quicTokenOpen(tok, toklen, key, &moved, now, &retry, &got));

    // A Retry token is short lived.
    CHECK("an expired Retry token opened",
          !_quicTokenOpen(tok, toklen, key, &a, now + QUIC_RETRY_TOKEN_LIFETIME + timeS(1), &retry,
                          &got));

    // Any change to the sealed bytes breaks it.
    uint8 bad[QUIC_MAX_TOKEN];
    memcpy(bad, tok, toklen);
    bad[toklen / 2] ^= 0x40;
    CHECK("a tampered token opened", !_quicTokenOpen(bad, toklen, key, &a, now, &retry, &got));

    // A different key cannot open it either.
    uint8 key2[32];
    CHECK("key2", psa_generate_random(key2, sizeof(key2)) == PSA_SUCCESS);
    CHECK("a token opened under the wrong key",
          !_quicTokenOpen(tok, toklen, key2, &a, now, &retry, &got));

    // A NEW_TOKEN token names no connection and lives far longer.
    CHECK("seal new_token", _quicTokenSeal(tok, &toklen, key, false, &a, NULL, now));
    CHECK("open new_token", _quicTokenOpen(tok, toklen, key, &a, now, &retry, &got));
    CHECK("new_token kind", !retry);
    CHECK_U("new_token connection id length", got.len, 0);
    CHECK("a new_token expired within the Retry lifetime",
          _quicTokenOpen(tok, toklen, key, &a, now + QUIC_RETRY_TOKEN_LIFETIME + timeS(1), &retry,
                         &got));

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Handshakes
// ---------------------------------------------------------------------------------------------

int test_quicconntest_handshake(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    CHECK("client never connected", f.cli.connected);
    CHECK("server never connected", f.srv.connected);
    CHECK_U("client state", _quicConnGetState(f.cli.conn), QUIC_CS_CONNECTED);
    CHECK_U("server state", _quicConnGetState(f.srv.conn), QUIC_CS_CONNECTED);
    CHECK("client reported a close", !f.cli.closed);
    CHECK("server reported a close", !f.srv.closed);

    // The TLS engine underneath agrees, and so does the certificate check it did on the way.
    CHECK("client TLS incomplete", tlsquicComplete(_quicConnTls(f.cli.conn)));
    CHECK("server TLS incomplete", tlsquicComplete(_quicConnTls(f.srv.conn)));

    TlsInfo info;
    CHECK("client info", tlsquicGetInfo(_quicConnTls(f.cli.conn), &info));
    CHECK("server certificate not verified", info.peerVerified);
    nettlsInfoDestroy(&info);

    // Receiving a Handshake packet is what proves the client is really at that address.
    CHECK("server never validated the peer address", _quicConnValidated(f.srv.conn));

    // Confirmation is a later moment than completion, and the two ends reach it differently: a
    // server has checked the client's Finished, while a client has to be told with HANDSHAKE_DONE.
    CHECK("server handshake not confirmed", _quicConnHandshakeConfirmed(f.srv.conn));
    CHECK("client handshake not confirmed", _quicConnHandshakeConfirmed(f.cli.conn));

    // Each end read the other's transport parameters.
    CHECK_U("peer initial_max_data seen by the client",
            _quicConnPeerParams(f.cli.conn)->initMaxData, 1 << 20);
    CHECK_U("peer initial_max_streams_bidi seen by the server",
            _quicConnPeerParams(f.srv.conn)->initMaxStreamsBidi, 8);
    CHECK("the server's stateless reset token did not arrive",
          _quicConnPeerParams(f.cli.conn)->haveResetToken);
    CHECK("a client sent a stateless reset token",
          !_quicConnPeerParams(f.srv.conn)->haveResetToken);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_onertt(void)
{
    int ret = 0;
    QFix f;

    // Stop after the client has processed the server's flight but before the server's answer to
    // its Finished has been delivered: the client is complete but has not been told so.
    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);

    CHECK("client did not complete its handshake", f.cli.connected);
    CHECK("a client confirmed its own handshake", !_quicConnHandshakeConfirmed(f.cli.conn));

    qRun(&f);
    CHECK("client never confirmed", _quicConnHandshakeConfirmed(f.cli.conn));

    // A frame that only exists once 1-RTT keys do, in both directions.
    f.srv.sendMaxData = true;
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);
    CHECK_U("client did not receive MAX_DATA", f.cli.gotMaxData, 4096);

    f.cli.sendMaxData = true;
    CHECK("client flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);
    CHECK_U("server did not receive MAX_DATA", f.srv.gotMaxData, 4096);

    CHECK("client closed", !f.cli.closed);
    CHECK("server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// A key update, started by one end and followed by the other.
//
// The whole exchange is one bit in each packet's header, so what proves it happened is that both
// ends can still read each other afterwards -- packets sealed with keys derived from the old ones
// open only if the peer followed. RFC 9001 section 6.1 also allows a second update only once the
// first has been acknowledged, which is what stops a peer being asked to keep an unbounded number
// of generations.
int test_quicconntest_keyupdate(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRun(&f);
    CHECK("handshake confirmed", _quicConnHandshakeConfirmed(f.cli.conn));

    CHECK_U("no updates yet", _quicConnKeyUpdates(f.cli.conn), 0);
    CHECK_U("nor on the server", _quicConnKeyUpdates(f.srv.conn), 0);

    CHECK("the client starts one", _quicConnUpdateKeys(f.cli.conn));
    CHECK_U("the client moved on", _quicConnKeyUpdates(f.cli.conn), 1);

    // The server has heard nothing yet; it follows when a packet under the new keys arrives.
    f.cli.sendMaxData = true;
    CHECK("client flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK_U("the server followed", _quicConnKeyUpdates(f.srv.conn), 1);
    CHECK_U("and read what was in it", f.srv.gotMaxData, 4096);

    // And back the other way, which only works if the server is now sending under the new keys.
    f.srv.sendMaxData = true;
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);
    CHECK_U("the client read the answer", f.cli.gotMaxData, 4096);

    // A second update is refused until the first has been acknowledged, and allowed once it has.
    // The exchange above is what acknowledged it.
    CHECK("a second update is allowed now", _quicConnUpdateKeys(f.cli.conn));
    CHECK_U("two updates", _quicConnKeyUpdates(f.cli.conn), 2);
    CHECK("but not a third straight away", !_quicConnUpdateKeys(f.cli.conn));

    f.cli.sendMaxData = true;
    CHECK("client flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);
    CHECK_U("the server followed again", _quicConnKeyUpdates(f.srv.conn), 2);

    CHECK("client closed", !f.cli.closed);
    CHECK("server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_padding(void)
{
    int ret = 0;
    QFix f;
    QuicKeys ck, sk;
    bool haveKeys = false;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    CHECK_U("datagrams from a client start", f.cli.nout, 1);
    CHECK("a client's Initial datagram was not padded",
          f.cli.out[0]->len >= QUIC_INITIAL_MTU);

    // A server discards an Initial that arrives in a datagram too small to have come from a
    // conforming client, so a small request can never be turned into a large reply.
    CHECK("accept", qAccept(&f, NULL));

    QuicPktHdr h;
    CHECK("decode", _quicHdrDecode(&h, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK("initial keys", _quicKeysInitial(&ck, &sk, h.dcid.id, h.dcid.len));
    haveKeys = true;

    // A complete, correctly sealed Initial packet in a datagram far under the floor. Everything
    // about it is valid except its size, so only the size rule can reject it.
    static const uint8 ping[4] = { QUIC_FRAME_PING, 0, 0, 0 };

    QuicPktHdr sh;
    memset(&sh, 0, sizeof(sh));
    sh.type    = QUIC_PKT_INITIAL;
    sh.version = QUIC_VERSION_1;
    sh.dcid    = h.dcid;
    sh.scid    = h.scid;
    sh.pnLen   = 1;
    sh.pnEnc   = 1;
    sh.lenSize = 2;
    sh.len     = 1 + sizeof(ping) + QUIC_TAG_LEN;

    uint8 small[256];
    QuicWr w;
    _quicWrInit(&w, small, sizeof(small));
    CHECK("header encode", _quicHdrEncode(&w, &sh));

    size_t hdrsz = _quicHdrSize(&sh);
    size_t outLen;
    CHECK("seal", _quicPktSeal(&ck, small, sizeof(small), hdrsz - 1, 1, 1, ping, sizeof(ping),
                               &outLen));
    CHECK("the hand-built Initial datagram was not small", outLen < QUIC_INITIAL_MTU);

    _quicConnRecv(f.srv.conn, &f.cliAddr, NULL, small, outLen, f.now);

    CHECK_U("a server answered an undersized Initial datagram", f.srv.nout, 0);
    CHECK_U("a server left its initial state on an undersized datagram",
            _quicConnGetState(f.srv.conn), QUIC_CS_NEW);

out:
    if (haveKeys) {
        _quicKeysDestroy(&ck);
        _quicKeysDestroy(&sk);
    }
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_srvpad(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    // Hand the client's flight over. What comes back carries the ServerHello, so its Initial has
    // to be acknowledged, and RFC 9000 section 14.1 requires that datagram to be expanded to the
    // size every path is guaranteed to carry. A peer that enforces the rule discards anything
    // smaller, and the handshake dies with no diagnosis at all.
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK_U("datagrams from a server answering an Initial", f.srv.nout, 1);

    QuicPktHdr h;
    CHECK("decode", _quicHdrDecode(&h, f.srv.out[0]->data, f.srv.out[0]->len, 0));
    CHECK_U("the server's first packet was not an Initial", h.type, QUIC_PKT_INITIAL);

    CHECK_UGE("a server's ack-eliciting Initial datagram was not padded", f.srv.out[0]->len,
              QUIC_INITIAL_MTU);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_dup_packet(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    f.srv.sendMaxData = true;
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    CHECK_U("datagrams from the server", f.srv.nout, 1);

    // The same datagram twice. A packet number already seen is dropped before its frames are
    // looked at, so the second copy changes nothing.
    Buffer copy = bufCreate(f.srv.out[0]->len);
    memcpy(copy->data, f.srv.out[0]->data, f.srv.out[0]->len);
    copy->len = f.srv.out[0]->len;

    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);
    CHECK_U("MAX_DATA frames seen after one delivery", f.cli.nMaxData, 1);

    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, copy->data, copy->len, f.now);
    bufDestroy(&copy);

    CHECK_U("MAX_DATA frames seen after a replay", f.cli.nMaxData, 1);
    CHECK("client closed on a replay", !f.cli.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Retry and version negotiation
// ---------------------------------------------------------------------------------------------

// What a listener does when it wants a client's address validated first: answer the Initial with a
// Retry naming a fresh connection ID and a sealed token, and keep no state of its own.
// How a Retry is built, for the tests that check what the client refuses.
typedef enum QRetryMode {
    QRETRY_GOOD,        // what a real listener sends
    QRETRY_BADTAG,      // forged by something that never saw the client's Initial
    QRETRY_SAMECID      // names the connection ID the client already chose
} QRetryMode;

static bool qSendRetry(_Inout_ QFix* f, _Out_ QuicCid* odcidOut, _Out_ QuicCid* retryScidOut,
                       QRetryMode mode)
{
    if (f->cli.nout == 0)
        return false;

    QuicPktHdr h;
    if (!_quicHdrDecode(&h, f->cli.out[0]->data, f->cli.out[0]->len, 0))
        return false;

    *odcidOut = h.dcid;

    QuicCid rscid;
    memset(&rscid, 0, sizeof(rscid));
    rscid.len = 8;
    if (psa_generate_random(rscid.id, rscid.len) != PSA_SUCCESS)
        return false;

    // A Retry naming the connection ID the client already used would leave the Initial keys
    // exactly as they were, which is the one thing a Retry has to change.
    if (mode == QRETRY_SAMECID)
        rscid = h.dcid;

    *retryScidOut = rscid;

    uint8 token[QUIC_MAX_TOKEN];
    size_t tokenLen;
    if (!_quicTokenSeal(token, &tokenLen, f->tokenKey, true, &f->cliAddr, &h.dcid, clockWall()))
        return false;

    uint8 pkt[256];
    size_t n = _quicRetryBuild(pkt, sizeof(pkt), QUIC_VERSION_1, &h.dcid, &h.scid, &rscid, token,
                               tokenLen);
    if (n == 0)
        return false;

    if (mode == QRETRY_BADTAG)
        pkt[n - 1] ^= 0x01;

    // A server that answers with a Retry has not processed the Initial at all. One the client is
    // going to refuse never came from the server, so the real Initial is still on its way.
    if (mode == QRETRY_GOOD) {
        for (uint32 i = 0; i < f->cli.nout; i++)
            bufDestroy(&f->cli.out[i]);
        f->cli.nout = 0;
    }

    _quicConnRecv(f->cli.conn, &f->srvAddr, NULL, pkt, n, f->now);
    return true;
}

int test_quicconntest_retry(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    QuicCid odcid, retryScid;
    CHECK("retry", qSendRetry(&f, &odcid, &retryScid, QRETRY_GOOD));

    // The client answers with a second Initial: addressed to the connection ID the Retry named,
    // carrying the token, and still padded.
    CHECK_U("datagrams after a Retry", f.cli.nout, 1);
    CHECK("the client's second Initial was not padded", f.cli.out[0]->len >= QUIC_INITIAL_MTU);

    QuicPktHdr h2;
    CHECK("decode", _quicHdrDecode(&h2, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK_U("packet type after a Retry", h2.type, QUIC_PKT_INITIAL);
    CHECK("the client did not switch to the Retry's connection ID",
          h2.dcid.len == retryScid.len && memcmp(h2.dcid.id, retryScid.id, h2.dcid.len) == 0);
    CHECK("the client's second Initial carried no token", h2.tokenLen > 0);

    // The listener opens the token to recover the connection ID from before the Retry, which is
    // the only state it kept -- inside the token itself.
    bool isRetry = false;
    QuicCid fromToken;
    CHECK("token did not validate",
          _quicTokenOpen(h2.token, h2.tokenLen, f.tokenKey, &f.cliAddr, clockWall(), &isRetry,
                         &fromToken));
    CHECK("token is not a Retry token", isRetry);
    CHECK("token names the wrong connection",
          fromToken.len == odcid.len && memcmp(fromToken.id, odcid.id, odcid.len) == 0);

    CHECK("accept", qAccept(&f, &fromToken));
    qRun(&f);

    CHECK("client never connected after a Retry", f.cli.connected);
    CHECK("server never connected after a Retry", f.srv.connected);

    // The client checked the server's account of the Retry against what it saw.
    const QuicTransportParams* tp = _quicConnPeerParams(f.cli.conn);
    CHECK("no retry_source_connection_id", tp->haveRetryScid);
    CHECK("retry_source_connection_id mismatch",
          tp->retryScid.len == retryScid.len &&
              memcmp(tp->retryScid.id, retryScid.id, retryScid.len) == 0);
    CHECK("original_destination_connection_id mismatch",
          tp->haveOrigDcid && tp->origDcid.len == odcid.len &&
              memcmp(tp->origDcid.id, odcid.id, odcid.len) == 0);

    // A Retry validates the address before a connection exists, so the server starts out with the
    // amplification limit already lifted.
    CHECK("the server did not treat a Retry token as address validation",
          _quicConnValidated(f.srv.conn));

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_retry_forged(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    QuicPktHdr first;
    CHECK("decode", _quicHdrDecode(&first, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    QuicCid origDcid = first.dcid;

    QuicCid odcid, retryScid;
    CHECK("retry", qSendRetry(&f, &odcid, &retryScid, QRETRY_BADTAG));

    // The integrity tag is keyed by the connection ID the client chose, so only something that saw
    // the Initial can produce one. A forged Retry changes nothing: the client is still working on
    // the connection IDs it picked itself, and its original Initial is still on its way.
    CHECK_U("datagrams after a forged Retry", f.cli.nout, 1);

    QuicPktHdr again;
    CHECK("decode", _quicHdrDecode(&again, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK("a forged Retry moved the client to a new connection ID",
          again.dcid.len == origDcid.len && memcmp(again.dcid.id, origDcid.id, origDcid.len) == 0);
    CHECK("a forged Retry gave the client a token", again.tokenLen == 0);

    // A Retry that names the client's own connection ID is refused for a different reason: the
    // Initial keys come from that value, so such a Retry would change nothing at all.
    QuicCid odcid2, retryScid2;
    CHECK("retry", qSendRetry(&f, &odcid2, &retryScid2, QRETRY_SAMECID));
    CHECK_U("datagrams after a Retry naming the client's own connection ID", f.cli.nout, 1);

    CHECK("decode", _quicHdrDecode(&again, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK("a Retry naming the client's own connection ID was acted on",
          again.tokenLen == 0 && again.dcid.len == origDcid.len &&
              memcmp(again.dcid.id, origDcid.id, origDcid.len) == 0);

    CHECK("accept", qAccept(&f, NULL));
    qRun(&f);

    CHECK("a refused Retry broke the handshake", f.cli.connected && f.srv.connected);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_tp_mismatch(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    // A server that claims a Retry the client never saw. The connection IDs in the transport
    // parameters are the only record of what happened before either end had authenticated keys,
    // so a client that does not check them can be steered through a Retry by anything on the path.
    QuicCid bogus;
    memset(&bogus, 0, sizeof(bogus));
    bogus.len = 8;
    CHECK("bogus cid", psa_generate_random(bogus.id, bogus.len) == PSA_SUCCESS);

    CHECK("accept", qAccept(&f, &bogus));
    qRun(&f);

    CHECK("the client accepted a Retry it never saw", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_TRANSPORT_PARAMETER_ERROR);
    CHECK("the close was not local", f.cli.closeLocal);
    CHECK("the client completed a handshake it should have refused", !f.cli.connected);

    // A connection that fails part way through the handshake has no 1-RTT keys, so its
    // CONNECTION_CLOSE has to go out at a level the peer can still read or the server is left
    // waiting on a client that has already given up.
    CHECK("the server was never told why the handshake stopped", f.srv.closed);
    CHECK_U("error the server was told", f.srv.closeError, QUIC_ERR_TRANSPORT_PARAMETER_ERROR);

out:
    qFixDestroy(&f);
    return ret;
}

// A packet whose payload decrypts to nothing at all. It cannot arise from an endpoint that is
// following the protocol, so accepting it means accepting a packet number with no content behind
// it, which is exactly what an attacker probing for a decryption oracle would send.
int test_quicconntest_no_frames(void)
{
    int ret = 0;
    QFix f;
    QuicKeys ck, sk;
    bool haveKeys = false;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    QuicPktHdr h;
    CHECK("decode", _quicHdrDecode(&h, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK("initial keys", _quicKeysInitial(&ck, &sk, h.dcid.id, h.dcid.len));
    haveKeys = true;

    QuicCid scid;
    memset(&scid, 0, sizeof(scid));
    scid.len = 8;
    CHECK("scid", psa_generate_random(scid.id, scid.len) == PSA_SUCCESS);

    // A four-byte packet number leaves exactly enough room for the header protection sample with
    // no payload behind it, which is the only way to build such a packet at all.
    QuicPktHdr sh;
    memset(&sh, 0, sizeof(sh));
    sh.type    = QUIC_PKT_INITIAL;
    sh.version = QUIC_VERSION_1;
    sh.dcid    = h.scid;
    sh.scid    = scid;
    sh.pnLen   = 4;
    sh.lenSize = 2;
    sh.len     = 4 + QUIC_TAG_LEN;

    uint8 pkt[1500];
    QuicWr w;
    _quicWrInit(&w, pkt, sizeof(pkt));
    CHECK("header encode", _quicHdrEncode(&w, &sh));

    size_t hdrsz = _quicHdrSize(&sh);
    size_t outLen;
    CHECK("seal", _quicPktSeal(&sk, pkt, sizeof(pkt), hdrsz - 4, 4, 0, NULL, 0, &outLen));

    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, outLen, f.now);

    CHECK("a packet carrying no frames was accepted", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_PROTOCOL_VIOLATION);

out:
    if (haveKeys) {
        _quicKeysDestroy(&ck);
        _quicKeysDestroy(&sk);
    }
    qFixDestroy(&f);
    return ret;
}

// An acknowledgement of a packet number that was never sent. Believing one would move the largest
// acknowledged value past anything real, and every packet number decoded after it resolves against
// that value.
int test_quicconntest_bad_ack(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    f.srv.sendBadAck = true;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("an acknowledgement for an unsent packet was accepted", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_PROTOCOL_VIOLATION);
    CHECK("the close was not local", f.cli.closeLocal);

out:
    qFixDestroy(&f);
    return ret;
}

// More connection IDs than this endpoint said it would hold. The limit is what bounds the table,
// so a peer that ignores it is asking for unbounded storage.
int test_quicconntest_cid_limit(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    f.srv.sendCidMode = 1;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("a flood of connection IDs was accepted", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_CONNECTION_ID_LIMIT_ERROR);
    CHECK("the close was not local", f.cli.closeLocal);

out:
    qFixDestroy(&f);
    return ret;
}

// Two ways a NEW_CONNECTION_ID frame can be wrong regardless of how many arrive: one sequence
// number naming two different IDs, which makes the mapping untrustworthy, and a zero-length ID,
// which an endpoint that routes by connection ID cannot use for anything. RFC 9000 section 19.15
// gives them different error codes, and the second is caught while the frame is still being read.
int test_quicconntest_cid_bad(void)
{
    int ret = 0;
    QFix f;
    QFix g;
    bool haveG = false;

    CHECK("handshake", qHandshake(&f));

    f.srv.sendCidMode = 2;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("one sequence number naming two connection IDs was accepted", f.cli.closed);
    CHECK_U("close error for a repeated sequence number", f.cli.closeError,
            QUIC_ERR_PROTOCOL_VIOLATION);

    CHECK("second handshake", qHandshake(&g));
    haveG = true;

    g.srv.sendCidMode = 3;
    CHECK("flush", _quicConnFlush(g.srv.conn, g.now));
    qRun(&g);

    CHECK("a zero-length connection ID was accepted", g.cli.closed);
    CHECK_U("close error for a zero-length connection ID", g.cli.closeError,
            QUIC_ERR_FRAME_ENCODING_ERROR);

out:
    if (haveG)
        qFixDestroy(&g);
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_versionneg(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    QuicPktHdr h;
    CHECK("decode", _quicHdrDecode(&h, f.cli.out[0]->data, f.cli.out[0]->len, 0));

    // A Version Negotiation packet that lists the version already in use is invalid, and RFC 9000
    // section 6.2 says to ignore it rather than act on it.
    uint32 withV1[] = { QUIC_VERSION_1, 0x0a0a0a0a };
    uint8 pkt[256];
    size_t n = _quicVersionNegBuild(pkt, sizeof(pkt), &h.dcid, &h.scid, withV1, 2);
    CHECK("version negotiation build", n > 0);

    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, n, f.now);
    CHECK("a Version Negotiation offering the current version was acted on", !f.cli.closed);

    // One that does not echo this connection's own connection IDs belongs to some other attempt,
    // or to nobody, and is ignored on that basis alone.
    QuicCid wrong;
    memset(&wrong, 0, sizeof(wrong));
    wrong.len = 8;
    CHECK("wrong cid", psa_generate_random(wrong.id, wrong.len) == PSA_SUCCESS);

    uint32 others[] = { 0x0a0a0a0a, 0xff00001d };
    n = _quicVersionNegBuild(pkt, sizeof(pkt), &h.dcid, &wrong, others, 2);
    CHECK("version negotiation build", n > 0);
    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, n, f.now);
    CHECK("a Version Negotiation for another connection was acted on", !f.cli.closed);

    // One with nothing in common ends the attempt, without anything going on the wire.
    n = _quicVersionNegBuild(pkt, sizeof(pkt), &h.dcid, &h.scid, others, 2);
    CHECK("version negotiation build", n > 0);

    uint64 before = f.cli.bytesSent;
    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, n, f.now);

    CHECK("the client did not give up", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_CONNECTION_REFUSED);
    CHECK("the close was not reported as local", f.cli.closeLocal);
    CHECK_U("state", _quicConnGetState(f.cli.conn), QUIC_CS_CLOSED);
    CHECK_U("bytes sent in answer to a Version Negotiation", f.cli.bytesSent, before);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Errors, closing, and timeouts
// ---------------------------------------------------------------------------------------------

int test_quicconntest_close(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    _quicConnClose(f.cli.conn, 0x1234, true, _S"all done");
    CHECK("client state after closing", _quicConnGetState(f.cli.conn) == QUIC_CS_CLOSING);
    CHECK("the closing end was not told", f.cli.closed && f.cli.closeLocal);

    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("the server never saw the close", f.srv.closed);
    CHECK_U("close error", f.srv.closeError, 0x1234);
    CHECK("close was not reported as an application error", f.srv.closeApp);
    CHECK("close was reported as local at the receiving end", !f.srv.closeLocal);
    CHECK("close reason", strEq(f.srv.closeReason, _S"all done"));
    CHECK_U("server state", _quicConnGetState(f.srv.conn), QUIC_CS_DRAINING);

    // Nothing more goes out from a draining connection, however much arrives.
    uint64 before = f.srv.bytesSent;
    f.srv.sendMaxData = true;
    CHECK("flush while draining", _quicConnFlush(f.srv.conn, f.now));
    CHECK_U("a draining connection sent something", f.srv.bytesSent, before);

    // Both ends sit out a closing period so a late packet still finds someone, then go away.
    f.now += timeS(2);
    _quicConnTick(f.srv.conn, f.now);
    _quicConnTick(f.cli.conn, f.now);
    CHECK("server did not finish draining", _quicConnIsClosed(f.srv.conn));
    CHECK("client did not finish closing", _quicConnIsClosed(f.cli.conn));

out:
    qFixDestroy(&f);
    return ret;
}

// A closing connection repeats its CONNECTION_CLOSE at every encryption level it still has keys
// for, so that a peer part way through the handshake can read it. Once the handshake is confirmed
// there is only one level left, and a server that kept its Handshake keys would announce its close
// twice -- to a client that threw those keys away and cannot read the first copy.
int test_quicconntest_close_level(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    _quicConnClose(f.srv.conn, 0x99, true, _S"server done");
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));

    CHECK_U("datagrams carrying the close", f.srv.nout, 1);
    CHECK("the close went out at an encryption level the handshake had already retired",
          (f.srv.out[0]->data[0] & 0x80) == 0);

    qRun(&f);
    CHECK("the client never saw the close", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, 0x99);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_bad_frame(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    f.srv.sendBadFrame = true;
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("an unknown frame type was accepted", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_FRAME_ENCODING_ERROR);
    CHECK("the close was not local", f.cli.closeLocal);
    CHECK("close was reported as an application error", !f.cli.closeApp);

    // The error reaches the other end as a CONNECTION_CLOSE rather than silence.
    CHECK("the server was never told why", f.srv.closed);
    CHECK_U("error the server was told", f.srv.closeError, QUIC_ERR_FRAME_ENCODING_ERROR);
    CHECK_U("server state", _quicConnGetState(f.srv.conn), QUIC_CS_DRAINING);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_stream_refused(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    // A server with nothing above it to own streams. It advertised limits in its transport
    // parameters, but there is no stream layer to hand a frame to.
    _quicConnSetHandlers(f.srv.conn, &qHandlersNoStreams, &f.srv);
    qRun(&f);
    CHECK("handshake", f.cli.connected && f.srv.connected);

    f.cli.sendStream = true;
    CHECK("client flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("a STREAM frame was accepted with no stream layer", f.srv.closed);
    CHECK_U("close error", f.srv.closeError, QUIC_ERR_STREAM_LIMIT_ERROR);
    CHECK("the client was never told", f.cli.closed);
    CHECK_U("error the client was told", f.cli.closeError, QUIC_ERR_STREAM_LIMIT_ERROR);

out:
    qFixDestroy(&f);
    return ret;
}

// Two frames only a server may send. A client that sends one is not confused about the protocol,
// it is claiming an authority it does not have -- HANDSHAKE_DONE would make a client's peer
// believe its own handshake was confirmed by someone who never checked anything.
int test_quicconntest_role_frames(void)
{
    int ret = 0;
    QFix f;
    QFix g;
    bool haveG = false;

    CHECK("handshake", qHandshake(&f));

    f.cli.sendRoleFrame = QUIC_FRAME_HANDSHAKE_DONE;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("a server accepted HANDSHAKE_DONE from a client", f.srv.closed);
    CHECK_U("close error for HANDSHAKE_DONE", f.srv.closeError, QUIC_ERR_PROTOCOL_VIOLATION);

    CHECK("second handshake", qHandshake(&g));
    haveG = true;

    g.cli.sendRoleFrame = QUIC_FRAME_NEW_TOKEN;
    CHECK("flush", _quicConnFlush(g.cli.conn, g.now));
    qRun(&g);

    CHECK("a server accepted NEW_TOKEN from a client", g.srv.closed);
    CHECK_U("close error for NEW_TOKEN", g.srv.closeError, QUIC_ERR_PROTOCOL_VIOLATION);

out:
    if (haveG)
        qFixDestroy(&g);
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_frame_level(void)
{
    int ret = 0;
    QFix f;
    QuicKeys ck, sk;
    bool haveKeys = false;

    CHECK("fixture", qFixInit(&f));
    CHECK("client start", qStartClient(&f));

    // Anything that sees the client's first Initial can derive the Initial keys from it, since they
    // come from the connection ID and nothing else. That is what makes this packet forgeable, and
    // exactly why an Initial packet may only carry the frames the handshake itself needs.
    QuicPktHdr h;
    CHECK("decode", _quicHdrDecode(&h, f.cli.out[0]->data, f.cli.out[0]->len, 0));
    CHECK("initial keys", _quicKeysInitial(&ck, &sk, h.dcid.id, h.dcid.len));
    haveKeys = true;

    uint8 payload[64];
    QuicWr pw;
    _quicWrInit(&pw, payload, sizeof(payload));

    QuicFrame sf;
    memset(&sf, 0, sizeof(sf));
    sf.type = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    CHECK("frame encode", _quicFrameEncode(&pw, &sf));

    QuicCid scid;
    memset(&scid, 0, sizeof(scid));
    scid.len = 8;
    CHECK("scid", psa_generate_random(scid.id, scid.len) == PSA_SUCCESS);

    QuicPktHdr sh;
    memset(&sh, 0, sizeof(sh));
    sh.type    = QUIC_PKT_INITIAL;
    sh.version = QUIC_VERSION_1;
    sh.dcid    = h.scid;
    sh.scid    = scid;
    sh.pnLen   = 1;
    sh.lenSize = 2;
    sh.len     = 1 + _quicWrLen(&pw) + QUIC_TAG_LEN;

    uint8 pkt[1500];
    QuicWr w;
    _quicWrInit(&w, pkt, sizeof(pkt));
    CHECK("header encode", _quicHdrEncode(&w, &sh));

    size_t hdrsz = _quicHdrSize(&sh);
    size_t outLen;
    CHECK("seal", _quicPktSeal(&sk, pkt, sizeof(pkt), hdrsz - 1, 1, 0, payload, _quicWrLen(&pw),
                               &outLen));

    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, outLen, f.now);

    CHECK("a STREAM frame in an Initial packet was accepted", f.cli.closed);
    CHECK_U("close error", f.cli.closeError, QUIC_ERR_PROTOCOL_VIOLATION);
    CHECK("the close was not local", f.cli.closeLocal);

out:
    if (haveKeys) {
        _quicKeysDestroy(&ck);
        _quicKeysDestroy(&sk);
    }
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_idle(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // Well inside the timeout, nothing happens.
    f.now += timeS(10);
    _quicConnTick(f.cli.conn, f.now);
    CHECK("the connection timed out early", !f.cli.closed);

    // Past it, the connection ends without sending anything: there is nobody there to tell.
    uint64 before = f.cli.bytesSent;
    f.now += timeS(31);
    _quicConnTick(f.cli.conn, f.now);

    CHECK("the connection did not time out", f.cli.closed);
    CHECK("timeout was not reported as local", f.cli.closeLocal);
    CHECK("timeout reason", strEq(f.cli.closeReason, _S"idle timeout"));
    CHECK_U("state", _quicConnGetState(f.cli.conn), QUIC_CS_CLOSED);
    CHECK_U("bytes sent on an idle timeout", f.cli.bytesSent, before);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_statelessreset(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // The server's stateless reset token reached the client inside the transport parameters, which
    // is the only place it is ever sent in the clear to the right party.
    const QuicTransportParams* tp = _quicConnPeerParams(f.cli.conn);
    CHECK("no stateless reset token arrived", tp->haveResetToken);

    uint8 pkt[128];
    size_t n = _quicStatelessResetBuild(pkt, sizeof(pkt), sizeof(pkt), tp->resetToken);
    CHECK("reset build", n > 0);
    CHECK("a reset packet does not look like a short header", (pkt[0] & 0xc0) == 0x40);

    uint64 before = f.cli.bytesSent;
    _quicConnRecv(f.cli.conn, &f.srvAddr, NULL, pkt, n, f.now);

    CHECK("the client ignored a stateless reset", f.cli.closed);
    CHECK_U("state after a reset", _quicConnGetState(f.cli.conn), QUIC_CS_CLOSED);
    CHECK("reset reason", strEq(f.cli.closeReason, _S"stateless reset"));
    CHECK_U("bytes sent in answer to a reset", f.cli.bytesSent, before);

    // A packet ending in something else is just an undecryptable packet, and is dropped.
    QFix g;
    CHECK("second handshake", qHandshake(&g));
    uint8 other[128];
    n = _quicStatelessResetBuild(other, sizeof(other), sizeof(other),
                                 (const uint8*)"0123456789abcdef");
    CHECK("reset build", n > 0);
    _quicConnRecv(g.cli.conn, &g.srvAddr, NULL, other, n, g.now);
    bool bogusClosed = g.cli.closed;
    qFixDestroy(&g);
    CHECK("an unrecognized token reset the connection", !bogusClosed);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Connection IDs, paths, and the amplification limit
// ---------------------------------------------------------------------------------------------

int test_quicconntest_cids(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // Both ends advertised an active_connection_id_limit of 4 and already used sequence number 0
    // in the handshake, so three more go out in NEW_CONNECTION_ID frames.
    CHECK_U("connection IDs the client issued", f.cli.nissued, 3);
    CHECK_U("connection IDs the server issued", f.srv.nissued, 3);
    CHECK_U("sequence number of the last one", f.cli.issuedSeq, 3);
    CHECK_U("length of an issued connection ID", f.cli.issuedCid.len, 8);

    // Neither end objected to what the other sent, and the connection still works.
    CHECK("client closed after exchanging connection IDs", !f.cli.closed);
    CHECK("server closed after exchanging connection IDs", !f.srv.closed);

    f.srv.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);
    CHECK_U("the connection stopped working", f.cli.gotMaxData, 4096);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_path(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // Finishing a handshake proves the peer receives what is sent to its address, which is the
    // same thing a challenge proves, so the path a connection starts on needs no challenge.
    CHECK("the handshake did not validate the path it ran over",
          _quicConnPathValid(f.cli.conn));

    // Asking for one anyway un-validates the path until the answer comes back.
    _quicConnValidatePath(f.cli.conn);
    CHECK("a path stayed validated while it was being probed", !_quicConnPathValid(f.cli.conn));

    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("the path was never validated", _quicConnPathValid(f.cli.conn));
    CHECK("client closed", !f.cli.closed);
    CHECK("server closed", !f.srv.closed);

    // A PATH_RESPONSE that answers no challenge proves nothing, so the data in it is compared
    // rather than merely counted. The server is put back to waiting for an answer, and the only
    // one it is given is the wrong one -- its own challenge goes out with this datagram, so the
    // exchange stops before the client can produce a real answer to it.
    _quicConnValidatePath(f.srv.conn);
    CHECK("a path stayed validated while it was being probed", !_quicConnPathValid(f.srv.conn));

    f.cli.sendBadPathResponse = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);
    CHECK("an unsolicited PATH_RESPONSE validated a path", !_quicConnPathValid(f.srv.conn));

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_amplification(void)
{
    int ret = 0;
    QFix f;

    // The server's certificate chain is padded out so its first flight is larger than three times
    // the one datagram it has received, which is the only way the limit is reachable at all.
    CHECK("fixture", qFixInit2(&f, 10));
    CHECK("client start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    CHECK_U("datagrams from a client start", f.cli.nout, 1);
    size_t recvd = f.cli.out[0]->len;

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK("a server answered nothing", f.srv.bytesSent > 0);
    CHECK("a server sent more than three times what it received",
          f.srv.bytesSent <= 3 * (uint64)recvd);

    // Guards the test against becoming vacuous. The server's chain is padded so its flight does
    // not fit in the budget; if a change to the test certificates made it fit again, everything
    // below would pass without the limit ever having been reached.
    CHECK("the amplification limit was never reached, so nothing was tested",
          f.srv.bytesSent > 2 * (uint64)recvd);

    // Nothing more comes out while the budget is spent and nothing new has arrived.
    uint64 before = f.srv.bytesSent;
    for (int i = 0; i < 4; i++)
        CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    CHECK_U("a server kept sending without receiving anything", f.srv.bytesSent, before);

    // Once the client answers, the budget grows and the handshake finishes.
    qRun(&f);
    CHECK("the handshake did not complete", f.cli.connected && f.srv.connected);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Loss
// ---------------------------------------------------------------------------------------------

// A client whose first packet vanishes has nothing to go on: no reply is coming, and nothing will
// arrive to prompt it. RFC 9002 section 6.2.4 is what breaks that -- the client gives up waiting
// and says the same thing again.
int test_quicconntest_loss_initial(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));

    f.cli.dropNext = 1;
    CHECK("start", qStartClient(&f));
    CHECK_U("the client's Initial was dropped", f.cli.nout, 0);
    CHECK_U("one datagram was dropped", f.cli.ndropped, 1);

    // Nothing is going to arrive, so the only thing that can move is the clock.
    int64 t0 = f.now;
    for (int i = 0; i < 8 && f.cli.nout == 0; i++) {
        int64 d = _quicConnDeadline(f.cli.conn);
        CHECK("the client stopped waiting for anything", d != timeForever);
        if (d > f.now)
            f.now = d;
        _quicConnTick(f.cli.conn, f.now);
    }

    CHECK("the client never repeated itself", f.cli.nout > 0);
    CHECK("a probe timeout fired", _quicConnRecovery(f.cli.conn)->ptoCount > 0);

    // Twice the assumed round trip is the wait when none has been measured, and the repeat is a
    // full sized datagram like the one it replaces.
    CHECK_U("the wait before repeating", f.now - t0, 2 * QUIC_INITIAL_RTT);
    CHECK("the repeat was not padded", f.cli.out[0]->len >= QUIC_INITIAL_MTU);

    CHECK("accept", qAccept(&f, NULL));

    // What went out has to be the handshake again, not merely something ack-eliciting: a server
    // given a bare probe has nothing to answer with but an acknowledgement, and the handshake
    // would only start on the round trip after that.
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    size_t reply = 0;
    for (uint32 i = 0; i < f.srv.nout; i++)
        reply += f.srv.out[i]->len;
    CHECK("the repeat did not carry the handshake", reply > 500);

    qRun(&f);

    CHECK("the handshake did not complete", f.cli.connected && f.srv.connected);
    CHECK_U("the backoff was not cleared", _quicConnRecovery(f.cli.conn)->ptoCount, 0);

out:
    qFixDestroy(&f);
    return ret;
}

// The server's whole first flight goes missing. Both ends have to work their way out of it: the
// client repeats its Initial to give an amplification limited server room to answer, and the
// server repeats a flight nothing has acknowledged.
int test_quicconntest_loss_flight(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    f.srv.dropNext = 3;
    qRunTimed(&f, 200, timeS(25));

    CHECK_U("datagrams dropped", f.srv.ndropped, 3);
    CHECK("the handshake did not survive a lost flight", f.cli.connected && f.srv.connected);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

    // The connection still works afterwards.
    f.srv.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);
    CHECK_U("the connection stopped working", f.cli.gotMaxData, 4096);

out:
    qFixDestroy(&f);
    return ret;
}

// One datagram out of the middle of a flight that spans several. What is around the hole arrives
// and is held; only the missing bytes are sent again, and the handshake finishes on them.
int test_quicconntest_loss_crypto(void)
{
    int ret = 0;
    QFix f;

    // A certificate chain big enough that the server's flight needs more than one datagram, so
    // there is a middle to lose.
    CHECK("fixture", qFixInit2(&f, 10));
    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    f.srv.dropAfter = 1;
    f.srv.dropNext  = 1;

    qRunTimed(&f, 200, timeS(25));

    CHECK_U("datagrams dropped", f.srv.ndropped, 1);
    CHECK("the handshake did not complete", f.cli.connected && f.srv.connected);

    // The server noticed rather than waiting out a timeout for everything it had sent.
    const QuicRecovery* r = _quicConnRecovery(f.srv.conn);
    CHECK("nothing was declared lost", r->nlost > 0);
    CHECK("the window was never cut", r->ncongestion > 0);

out:
    qFixDestroy(&f);
    return ret;
}

// A control frame is not bytes in a stream, so losing one is a different problem: there is nothing
// to reassemble, only a thing that has to be said again. The connection remembers which packet
// carried it and puts it back in the queue when that packet is declared lost.
int test_quicconntest_loss_control(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));
    CHECK("the handshake did not complete", f.cli.connected && f.srv.connected);

    _quicConnValidatePath(f.cli.conn);
    CHECK("the path is not validated yet", !_quicConnPathValid(f.cli.conn));

    // The datagram carrying the challenge never arrives.
    f.cli.dropNext = 1;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK_U("the challenge was dropped", f.cli.ndropped, 1);
    CHECK_U("nothing reached the server", f.cli.nout, 0);

    // Something has to be sent again for the loss to be noticed at all, so the exchange carries
    // on until the challenge is answered.
    for (int i = 0; i < 6 && !_quicConnPathValid(f.cli.conn); i++) {
        f.srv.sendMaxData = true;
        _quicConnFlush(f.srv.conn, f.now);
        qRunTimed(&f, 40, timeS(20));
    }

    CHECK("the lost path challenge was never sent again", _quicConnPathValid(f.cli.conn));
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// The congestion window is what a connection is allowed to have unanswered at any moment. A
// handshake never comes close to filling it, and that is the point: the acknowledgements it
// produces measure how quickly the handshake ran out of things to say, not how much the path will
// carry, so they must not grow the window either.
int test_quicconntest_congestion(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);

    CHECK_U("the handshake was treated as a loss", r->ncongestion, 0);
    CHECK_U("packets were declared lost", r->nlost, 0);
    CHECK("nothing is left unanswered", r->inFlight == 0);
    CHECK("the handshake was not recognised as having nothing more to send", r->appLimited);
    CHECK_U("a handshake inflated the window", r->window, 10 * (uint64)QUIC_INITIAL_MTU);
    CHECK("no round trip was measured", r->rtt.have);

    // Both ends measured the same instant twice, so the estimate is zero rather than absent --
    // which is the floor the probe timeout has to cope with.
    CHECK_U("the round trip estimate", r->rtt.smoothed, 0);
    CHECK("the probe timer is armed with a measurement of zero",
          _quicConnDeadline(f.cli.conn) != timeForever);

out:
    qFixDestroy(&f);
    return ret;
}

// The congestion window is a ceiling on what may be unanswered at once, and it has to be the
// connection that respects it -- there is nothing below this that could.
int test_quicconntest_congestion_limit(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    uint64 window         = r->window;

    // More to send than the window allows, and nothing to stop it but the window. Long enough
    // has passed for the pacer to have made back what the handshake spent, so what stops the
    // burst is the window rather than the pacing of it.
    f.cli.fillBulk  = true;
    f.cli.bytesSent = 0;
    f.now += timeMS(50);

    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK("nothing went out", f.cli.bytesSent > 0);
    CHECK("more than a window went out at once", f.cli.bytesSent <= window);
    CHECK("the window was not filled", r->inFlight > window - QUIC_INITIAL_MTU);

    // Time passing lets the pacer refill, but nothing has been answered, so the window has not
    // moved and neither does anything else.
    f.now += timeMS(500);
    uint64 before = f.cli.bytesSent;
    CHECK("flush again", _quicConnFlush(f.cli.conn, f.now));
    CHECK_U("a full window let more out", f.cli.bytesSent, before);

    // A probe is the exception: when a timeout says everything in flight may be gone, something
    // has to go out or the connection has no way to find out otherwise.
    uint32 nbefore = f.cli.nout;
    int64 d        = _quicConnDeadline(f.cli.conn);
    CHECK("no timer was armed", d != timeForever);
    f.now = d > f.now ? d : f.now;
    _quicConnTick(f.cli.conn, f.now);

    CHECK("a probe could not get past a full window", f.cli.nout > nbefore);
    CHECK("a probe timeout did not fire", _quicConnRecovery(f.cli.conn)->ptoCount > 0);

    // It was all still on its way, so nothing was really lost. The window opens again and grows,
    // because the whole time there was more waiting than it allowed.
    uint64 wbefore = r->window;
    qRun(&f);

    CHECK_U("packets were declared lost", r->nlost, 0);
    CHECK("the window did not grow while there was more to send", r->window > wbefore);

    f.cli.fillBulk = false;
    qRun(&f);
    CHECK("the window never reopened", _quicConnRecovery(f.cli.conn)->inFlight == 0);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// A congestion window with a few bytes free is full, not nearly full. Those few bytes cannot
// carry a packet of any kind, and a connection that trims its next datagram down to them can no
// longer acknowledge -- which does not count against the window -- nor probe, which the window is
// not allowed to hold back. Nothing it could still send would bring back the acknowledgement that
// reopens the window, so both ends wait forever.
int test_quicconntest_congestion_sliver(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // Fill the client's window, and leave what went out undelivered so nothing acknowledges it.
    // Long enough passes for the pacer to refill, so the window is the only thing that could hold
    // anything back -- which is what the test needs it to be.
    f.cli.fillBulk = true;
    f.now += timeMS(50);
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    f.cli.fillBulk = false;
    f.now += timeMS(500);

    QuicRecovery* r = (QuicRecovery*)_quicConnRecovery(f.cli.conn);
    CHECK("nothing was in flight to squeeze the window around", r->inFlight > 0);
    r->window = r->inFlight + 8;

    CHECK("the window already counted as full", _quicRecovCanSend(r));
    CHECK("the pacer was still holding packets back", _quicRecovPacerReady(r));
    CHECK_U("the sliver was not a sliver", _quicRecovWindowRoom(r), 8);

    // The server sends the client something it has to acknowledge. An acknowledgement does not
    // count against the window, so the sliver must not be able to stop it.
    f.srv.fillBulk = true;
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    f.srv.fillBulk = false;
    CHECK("the server sent nothing to acknowledge", f.srv.nout > 0);

    uint32 nbefore   = f.cli.nout;
    uint64 flightWas = r->inFlight;
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);

    CHECK("an acknowledgement could not get out past a sliver of a window", f.cli.nout > nbefore);
    CHECK_U("the acknowledgement was counted against the window", r->inFlight, flightWas);

    // And a probe must get past it, for the same reason it must get past a window that is full.
    nbefore = f.cli.nout;
    int64 d = _quicConnDeadline(f.cli.conn);
    CHECK("no timer was armed", d != timeForever);
    f.now = d > f.now ? d : f.now;
    _quicConnTick(f.cli.conn, f.now);

    CHECK("a probe could not get out past a sliver of a window", f.cli.nout > nbefore);

    // With both ends able to speak again the connection carries on rather than waiting forever.
    r->window = QUIC_INITIAL_MTU * 10;
    qRun(&f);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// Pacing spreads a window's worth of packets across the round trip instead of letting them leave
// in one burst. It only ever delays what the congestion window would have allowed, so what it
// holds back has to come out on a timer of its own.
int test_quicconntest_pacing(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    uint64 window         = r->window;

    f.cli.fillBulk = true;
    for (uint32 i = 0; i < f.cli.nout; i++)
        bufDestroy(&f.cli.out[i]);
    f.cli.nout      = 0;
    f.cli.bytesSent = 0;

    // No time has passed since the handshake, so the pacer has not made back what the handshake
    // spent and runs out before the window does.
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK("nothing went out", f.cli.bytesSent > 0);
    CHECK("the pacer let a whole window out at once", f.cli.bytesSent < window);
    CHECK("the window was what stopped it", r->inFlight < window);

    // What it is waiting for is the pacer, not a timeout, and it says so.
    int64 d = _quicConnDeadline(f.cli.conn);
    CHECK("nothing was waiting", d != timeForever);
    CHECK("the connection waits for a probe timeout rather than for the pacer",
          d < _quicRecovTimer(r));

    f.now           = d > f.now ? d : f.now;
    f.cli.bytesSent = 0;
    for (uint32 i = 0; i < f.cli.nout; i++)
        bufDestroy(&f.cli.out[i]);
    f.cli.nout = 0;

    CHECK("flush again", _quicConnFlush(f.cli.conn, f.now));
    CHECK("the pacer never refilled", f.cli.bytesSent > 0);

    f.cli.fillBulk = false;
    qRun(&f);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// The delay a peer reports on an acknowledgement is in units of two to the power of the exponent
// this endpoint advertised, and that much of the round trip was the peer thinking rather than the
// path carrying. Two cx endpoints acknowledge the moment a packet arrives, so this needs an
// acknowledgement built by hand to say otherwise.
int test_quicconntest_ack_delay(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    f.cliAckDelayExp = 10;
    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRun(&f);
    CHECK("the handshake did not complete", f.cli.connected && f.srv.connected);

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    int64 before          = r->rtt.smoothed;

    // Something for the server to acknowledge.
    f.cli.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));

    const QuicSentRing* ring = &r->sent[QUIC_PNS_APP];
    CHECK("nothing was sent", ring->count > 0);
    uint64 pn = ring->basePn + ring->count - 1;

    // The server's own answer never arrives, so its packet is still outstanding when the hand
    // built acknowledgement for it turns up two hundred milliseconds later.
    f.srv.dropNext = 1;
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);
    CHECK_U("the answer was not dropped", f.srv.ndropped, 1);

    f.now += timeMS(200);
    f.srv.sendAck     = true;
    f.srv.ackLargest  = pn;
    f.srv.ackDelayRaw = 20;   // twenty units of 2^10 microseconds, just over 20ms
    CHECK("server flush", _quicConnFlush(f.srv.conn, f.now));
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);

    CHECK_U("the round trip", r->rtt.latest, timeMS(200));
    CHECK_U("the peer's delay was not taken off in the units it was sent in", r->rtt.smoothed,
            (7 * before + (timeMS(200) - (20 << 10))) / 8);

out:
    qFixDestroy(&f);
    return ret;
}

// A path challenge is a question with an answer that only fits it. Asking again with fresh data
// while the first is still unanswered has to put the new question on the wire now -- an answer to
// the old one would not answer the new one, and neither would waiting for the old one to be given
// up on.
int test_quicconntest_path_replace(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // The server asks, and the client's answer never arrives.
    _quicConnValidatePath(f.srv.conn);
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));

    f.cli.dropNext = 1;
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);
    CHECK_U("the answer was not dropped", f.cli.ndropped, 1);
    CHECK("the path validated on an answer that was lost", !_quicConnPathValid(f.srv.conn));

    // It asks again, with different data, while the first question is still outstanding. No time
    // passes, so nothing has been given up on: the new question has to go out on its own account.
    _quicConnValidatePath(f.srv.conn);
    CHECK("second flush", _quicConnFlush(f.srv.conn, f.now));
    CHECK("the replacement challenge never went out", f.srv.nout > 0);

    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK("the replacement challenge was never answered", _quicConnPathValid(f.srv.conn));
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Streams over a real connection
// ---------------------------------------------------------------------------------------------

int test_quicconntest_stream_echo(void)
{
    int ret    = 0;
    Buffer got = 0, back = 0;
    QFix f;

    CHECK("handshake", qHandshakeStreams(&f));
    CHECK("both ends are up", f.cli.streamsUp && f.srv.streamsUp);

    uint64 id = 0;
    CHECK("the client opened a stream", _quicStreamOpen(&f.cli.streams, false, &id));
    CHECK_U("its id", id, 0);

    static const char msg[] = "a message carried by real packets";
    CHECK_U("bytes queued", _quicStreamSend(&f.cli.streams, id, (const uint8*)msg,
                                            sizeof(msg) - 1), sizeof(msg) - 1);
    _quicStreamFinish(&f.cli.streams, id);

    for (int i = 0; i < 8; i++)
        qStreamRound(&f);

    CHECK_U("the server saw the stream open", f.srv.nStreamOpen, 1);
    CHECK("the server read to the end", qStreamDrain(&f.srv, id, &got));
    CHECK_U("bytes read", got ? got->len : 0, sizeof(msg) - 1);
    CHECK("the bytes match", got && memcmp(got->data, msg, sizeof(msg) - 1) == 0);

    // The other direction of the same stream, back the way it came.
    CHECK_U("the echo was queued",
            _quicStreamSend(&f.srv.streams, id, got->data, got->len), got->len);
    _quicStreamFinish(&f.srv.streams, id);

    for (int i = 0; i < 8; i++)
        qStreamRound(&f);

    CHECK("the client read to the end", qStreamDrain(&f.cli, id, &back));
    CHECK("the echo matches", back && back->len == got->len &&
                              memcmp(back->data, got->data, got->len) == 0);

    // Both directions are over, so neither end is holding the stream any more.
    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    CHECK_U("the client let it go", f.cli.nStreamClosed, 1);
    CHECK_U("the server let it go", f.srv.nStreamClosed, 1);
    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

out:
    bufDestroy(&got);
    bufDestroy(&back);
    qFixDestroy(&f);
    return ret;
}

// Moves `total` bytes on one stream and checks they arrive unchanged. The transfer is larger than
// the window either end starts with, so it only finishes if the flow control updates, the
// congestion window and the pacer all keep it moving.
static int qStreamBulk(size_t total, uint32 dropAfter, uint32 dropNext, bool dropSender,
                       uint64 window)
{
    int ret    = 0;
    Buffer got = 0;
    uint8* src = NULL;
    QFix f;

    CHECK("handshake", qHandshakeStreams2(&f, window));

    QSide* lossy    = dropSender ? &f.cli : &f.srv;
    lossy->dropAfter = dropAfter;
    lossy->dropNext  = dropNext;

    src = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8)(i * 131 + 17);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.cli.streams, false, &id));

    size_t off = 0;
    bool done  = false;
    bool fin   = false;

    for (int i = 0; i < 4000 && !fin; i++) {
        if (off < total)
            off += _quicStreamSend(&f.cli.streams, id, src + off, total - off);
        if (off == total && !done) {
            _quicStreamFinish(&f.cli.streams, id);
            done = true;
        }

        qStreamRound(&f);

        if (qStreamDrain(&f.srv, id, &got))
            fin = true;

        if (f.cli.closed || f.srv.closed)
            break;
    }

    CHECK_U("the datagrams that were meant to be lost were", lossy->ndropped, dropNext);
    CHECK("the connection stayed up", !f.cli.closed && !f.srv.closed);
    CHECK("everything was queued", off == total);
    CHECK("the stream ended", fin);
    CHECK_U("bytes read", got ? got->len : 0, total);
    CHECK("the bytes match", got && memcmp(got->data, src, total) == 0);

out:
    xaFree(src);
    bufDestroy(&got);
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_stream_bulk(void)
{
    // Three times the window either end advertises, so the transfer cannot finish without the
    // receiver moving its limit and the sender hearing about it.
    return qStreamBulk(200000, 0, 0, false, 0);
}

int test_quicconntest_stream_loss(void)
{
    // The same transfer with a run of the server's datagrams thrown away, which takes out
    // acknowledgements and window updates rather than the data itself.
    return qStreamBulk(120000, 4, 3, false, 0);
}

int test_quicconntest_stream_loss_data(void)
{
    // Losing the sender's datagrams instead, with windows wide enough that the packets hold
    // nothing but stream data. Nothing recovers those unless a packet carrying stream data counts
    // as needing acknowledgement, which is what makes loss recovery see it go missing at all.
    return qStreamBulk(120000, 6, 4, true, 1 << 20);
}

int test_quicconntest_stream_reset(void)
{
    int ret    = 0;
    Buffer got = 0;
    QFix f;

    CHECK("handshake", qHandshakeStreams(&f));

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.cli.streams, true, &id));
    CHECK_U("bytes queued", _quicStreamSend(&f.cli.streams, id, (const uint8*)"half a", 6), 6);

    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    _quicStreamReset(&f.cli.streams, id, 77);

    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    CHECK_U("the server heard about it", f.srv.nStreamReset, 1);
    CHECK_U("with the code it was given", f.srv.streamResetError, 77);
    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

    // A reset ends one stream, not the connection, so another one still works afterwards.
    uint64 id2 = 0;
    CHECK("opened another", _quicStreamOpen(&f.cli.streams, true, &id2));
    CHECK_U("bytes queued", _quicStreamSend(&f.cli.streams, id2, (const uint8*)"whole", 5), 5);
    _quicStreamFinish(&f.cli.streams, id2);

    for (int i = 0; i < 6; i++)
        qStreamRound(&f);

    CHECK("the second stream ended", qStreamDrain(&f.srv, id2, &got));
    CHECK("its bytes arrived", got && got->len == 5 && memcmp(got->data, "whole", 5) == 0);

out:
    bufDestroy(&got);
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_stream_tail_loss(void)
{
    int ret    = 0;
    Buffer got = 0;
    QFix f;

    CHECK("handshake", qHandshakeStreams(&f));

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.cli.streams, true, &id));

    // The last thing a connection sends is the hardest to recover: there is nothing behind it to
    // arrive and reveal it went missing, so only the probe timer finds it -- and that timer is
    // only armed for packets counted as needing acknowledgement.
    f.cli.dropNext = 1;
    CHECK_U("bytes queued", _quicStreamSend(&f.cli.streams, id, (const uint8*)"tail", 4), 4);
    _quicStreamFinish(&f.cli.streams, id);

    for (int i = 0; i < 12; i++)
        qStreamRound(&f);

    CHECK_U("the datagram was lost", f.cli.ndropped, 1);
    CHECK("the stream still ended", qStreamDrain(&f.srv, id, &got));
    CHECK("and its bytes arrived", got && got->len == 4 && memcmp(got->data, "tail", 4) == 0);
    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

out:
    bufDestroy(&got);
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_stream_reset_loss(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshakeStreams(&f));

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.cli.streams, true, &id));
    CHECK_U("bytes queued", _quicStreamSend(&f.cli.streams, id, (const uint8*)"half a", 6), 6);

    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    // The datagram carrying the reset never arrives. It is the only thing the client has to say,
    // so the packet holds nothing else -- and where a stream ended is not something a later frame
    // repeats, so it has to be found missing and sent again.
    f.cli.dropNext = 1;
    _quicStreamReset(&f.cli.streams, id, 77);

    for (int i = 0; i < 12; i++)
        qStreamRound(&f);

    CHECK_U("the datagram was lost", f.cli.ndropped, 1);
    CHECK_U("but the server heard about it in the end", f.srv.nStreamReset, 1);
    CHECK_U("with the code it was given", f.srv.streamResetError, 77);
    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

int test_quicconntest_stream_violation(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshakeStreams(&f));

    // A STREAM frame naming a stream the sender cannot send on: stream 3 runs one way and the
    // server is the end that opens it, so data arriving from the client on it is a state error.
    f.cli.sendStream   = true;
    f.cli.sendStreamId = 3;

    _quicConnFlush(f.cli.conn, f.now);
    qRun(&f);

    CHECK("the server closed", f.srv.closed);
    CHECK("with a transport error", !f.srv.closeApp);
    CHECK_U("the code", f.srv.closeError, QUIC_ERR_STREAM_STATE_ERROR);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Explicit congestion notification (RFC 9000 section 13.4)
// ---------------------------------------------------------------------------------------------

// A path that carries the marks: both ends mark, both ends count what arrives, and neither turns
// marking off.
int test_quicconntest_ecn(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    CHECK("the client marked nothing", f.cli.nMarked > 0);
    CHECK("the server marked nothing", f.srv.nMarked > 0);

    CHECK("the client stopped marking on a path that carries marks",
          _quicConnEcnActive(f.cli.conn));
    CHECK("the server stopped marking on a path that carries marks",
          _quicConnEcnActive(f.srv.conn));

    // Both ends have to have counted what arrived, or there would be nothing for the other to
    // check its own marks against.
    uint64 cli[3], srv[3];
    _quicConnEcnCounts(f.cli.conn, QUIC_PNS_APP, cli);
    _quicConnEcnCounts(f.srv.conn, QUIC_PNS_APP, srv);

    CHECK("the client counted no marked packets", cli[0] > 0);
    CHECK("the server counted no marked packets", srv[0] > 0);
    CHECK_U("packets the client saw marked congested", cli[2], 0);
    CHECK_U("packets the server saw marked congested", srv[2], 0);

out:
    qFixDestroy(&f);
    return ret;
}

// A path that quietly drops the marks. The client's acknowledgements come back saying nothing was
// marked, which is the only evidence there is, and it stops marking for good.
int test_quicconntest_ecn_stripped(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    f.cli.ecnStrip = true;

    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRun(&f);

    CHECK("the connection did not come up", f.cli.connected && f.srv.connected);
    CHECK("the client asked for no marks at all", f.cli.nMarked > 0);
    CHECK("the client kept marking a path that strips marks", !_quicConnEcnActive(f.cli.conn));

    // The server's own direction is untouched, so it has no reason to stop.
    CHECK("the server stopped marking a path that carries marks",
          _quicConnEcnActive(f.srv.conn));

    uint64 srv[3];
    _quicConnEcnCounts(f.srv.conn, QUIC_PNS_APP, srv);
    CHECK_U("marks the server counted on a stripped path", srv[0], 0);

    // And the connection is otherwise entirely well.
    f.cli.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);
    CHECK_U("MAX_DATA the server received", f.srv.gotMaxData, 4096);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// A congested path rewrites every mark to CE. The peer reports the rising count, and the sender
// answers it the way it would answer a loss -- but a round trip earlier, and without having lost
// anything.
int test_quicconntest_ecn_congestion(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    f.cli.ecnRemark = true;

    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRun(&f);

    CHECK("the connection did not come up", f.cli.connected && f.srv.connected);

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    CHECK("the client never answered the congestion the peer reported", r->ncongestion > 0);
    CHECK_U("packets declared lost on a path that dropped none", r->nlost, 0);

    // Marks that come back as CE are still marks, so nothing about this says the path cannot carry
    // them -- the client goes on marking.
    CHECK("the client stopped marking after seeing congestion", _quicConnEcnActive(f.cli.conn));

    uint64 srv[3];
    _quicConnEcnCounts(f.srv.conn, QUIC_PNS_APP, srv);
    CHECK("the server counted no congestion marks", srv[2] > 0);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Path MTU discovery (RFC 8899)
// ---------------------------------------------------------------------------------------------

// A path that carries more than the base size but less than the ceiling. The search has to find it
// by losing probes, and losing a probe must not look like congestion.
int test_quicconntest_pmtu(void)
{
    int ret = 0;
    QFix f;
    const size_t limit = 1350;

    CHECK("fixture", qFixInit(&f));
    f.cli.pathMtu = limit;
    f.srv.pathMtu = limit;

    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRunTimed(&f, 600, timeS(10));

    CHECK("the connection did not come up", f.cli.connected && f.srv.connected);
    CHECK("the search never finished", _quicConnPathMtuDone(f.cli.conn));

    size_t mtu = _quicConnPathMtu(f.cli.conn);
    CHECK("the path was never measured past the base size", mtu > QUIC_INITIAL_MTU);
    CHECK("a size the path cannot carry was settled on", mtu <= limit);

    // Probes that were too large were dropped by the path, which is the answer the search wanted.
    CHECK("no probe was ever too large for the path", f.cli.nTooBig > 0);

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    CHECK_U("a lost probe was treated as congestion", r->ncongestion, 0);

    // Probes above the path's size were meant to be too large, so what the search sent says
    // nothing. What matters is what the connection sends now that it has an answer: bulk traffic
    // fills every datagram to the size discovery settled on, and all of it has to arrive.
    f.cli.largestSent = 0;
    f.cli.nTooBig     = 0;
    f.cli.fillBulk    = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRunTimed(&f, 64, timeS(2));
    f.cli.fillBulk = false;

    CHECK("bulk traffic went out at the base size", f.cli.largestSent > QUIC_INITIAL_MTU);
    CHECK("a datagram larger than the path went out", f.cli.largestSent <= limit);
    CHECK_U("datagrams the path was too small for", f.cli.nTooBig, 0);

out:
    qFixDestroy(&f);
    return ret;
}

// A path that carries nothing above the base size. Every probe fails, the search gives up where it
// started, and the connection is no worse for having asked.
int test_quicconntest_pmtu_floor(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    f.cli.pathMtu = QUIC_INITIAL_MTU;
    f.srv.pathMtu = QUIC_INITIAL_MTU;

    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));
    qRunTimed(&f, 600, timeS(10));

    CHECK("the connection did not come up", f.cli.connected && f.srv.connected);
    CHECK("the search never finished", _quicConnPathMtuDone(f.cli.conn));
    CHECK_U("the path MTU moved on a path that carries nothing more",
            (uint64)_quicConnPathMtu(f.cli.conn), (uint64)QUIC_INITIAL_MTU);

    const QuicRecovery* r = _quicConnRecovery(f.cli.conn);
    CHECK_U("failed probes were treated as congestion", r->ncongestion, 0);

    f.cli.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRunTimed(&f, 32, timeS(1));
    CHECK_U("MAX_DATA the server received", f.srv.gotMaxData, 4096);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Connection migration (RFC 9000 section 9)
// ---------------------------------------------------------------------------------------------

// Throws away everything a side has queued, as a path that carries nothing would.
static void qDropOut(_Inout_ QSide* s)
{
    for (uint32 i = 0; i < s->nout; i++)
        bufDestroy(&s->out[i]);
    s->nout = 0;
}

// Moves the client to `addr` and produces one ack-eliciting packet from there.
static void qMoveClient(_Inout_ QFix* f, _In_ const NetAddr* addr)
{
    f->cliAddr         = *addr;
    f->cli.sendMaxData = true;
    _quicConnFlush(f->cli.conn, f->now);
}

// The client's address changes, which is what a NAT rebinding or a laptop changing network looks
// like from the server. The server follows it, proves the new path works, and carries on.
int test_quicconntest_migrate(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));
    CHECK("the handshake was never confirmed", _quicConnHandshakeConfirmed(f.srv.conn));

    NetAddr moved = f.cliAddr;
    moved.port    = (uint16)(moved.port + 1);

    qMoveClient(&f, &moved);
    qRunTimed(&f, 64, timeS(2));

    CHECK_U("the server did not follow the peer's move", _quicConnMigrations(f.srv.conn), 1);

    NetAddr where;
    _quicConnPeerAddr(f.srv.conn, &where);
    CHECK("the server is still sending to the old address", qAddrEq(&where, &moved));
    CHECK("the server never validated the new path", _quicConnPathValid(f.srv.conn));

    CHECK_U("MAX_DATA the server received across the move", f.srv.gotMaxData, 4096);
    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// A packet from a new address containing nothing but probing frames is answered on that path, but
// says nothing about where the peer is: an attacker who copied a packet and replayed it from an
// address of their choosing must not be able to move the connection there.
int test_quicconntest_migrate_probing(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    // A client that migrates itself has exactly one thing to say and it is a PATH_CHALLENGE, so
    // this is the datagram in question -- with nothing else owed, nothing else goes in it.
    CHECK("the client could not migrate", _quicConnMigrate(f.cli.conn, f.now));
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK("the client sent nothing", f.cli.nout > 0);

    NetAddr moved = f.cliAddr;
    moved.port    = (uint16)(moved.port + 7);
    f.cliAddr     = moved;

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK_U("a probing packet moved the connection", _quicConnMigrations(f.srv.conn), 0);

    NetAddr where;
    _quicConnPeerAddr(f.srv.conn, &where);
    CHECK("the server left the path it was on", !qAddrEq(&where, &moved));

    // It is answered all the same, and on the path it arrived on -- RFC 9000 section 8.2.2. That is
    // what lets a peer finish validating a path before moving to it, and it is only observable in
    // where the answer was addressed.
    CHECK("the server sent nothing back", f.srv.nout > 0);
    CHECK("the server answered a challenge somewhere other than where it came from",
          qAddrEq(&f.srv.lastDest, &moved));

    qRunTimed(&f, 64, timeS(2));
    CHECK("the client's new path was never validated", _quicConnPathValid(f.cli.conn));

out:
    qFixDestroy(&f);
    return ret;
}

// A packet that arrives from a new address but is older than one already seen is a reordered copy
// of where the peer used to be, not news about where it is now.
int test_quicconntest_migrate_reorder(void)
{
    int ret = 0;
    QFix f;
    Buffer first = NULL;

    CHECK("handshake", qHandshake(&f));

    // Two ack-eliciting datagrams, held rather than delivered, so their order can be chosen.
    f.cli.sendMaxData = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK_U("datagrams the client sent", f.cli.nout, 1);

    first        = f.cli.out[0];
    f.cli.out[0] = NULL;
    f.cli.nout   = 0;

    f.cli.sendMaxData = true;
    CHECK("flush again", _quicConnFlush(f.cli.conn, f.now));
    CHECK_U("datagrams the client sent the second time", f.cli.nout, 1);

    // The newer one arrives from where the client has always been.
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    // The older one turns up afterwards from somewhere else entirely.
    NetAddr moved = f.cliAddr;
    moved.port    = (uint16)(moved.port + 11);

    NetPktInfo info;
    memset(&info, 0, sizeof(info));
    info.local     = f.srvAddr;
    info.haveLocal = true;
    _quicConnRecv(f.srv.conn, &moved, &info, first->data, first->len, f.now);

    CHECK_U("a reordered packet moved the connection", _quicConnMigrations(f.srv.conn), 0);

    NetAddr where;
    _quicConnPeerAddr(f.srv.conn, &where);
    CHECK("the server left the path it was on", !qAddrEq(&where, &moved));

out:
    bufDestroy(&first);
    qFixDestroy(&f);
    return ret;
}

// The connection follows the peer to an address that turns out to answer nothing. This is what a
// forged source address does, and it is why the path being left is kept: when the challenge is
// never answered, the connection goes back rather than stopping.
int test_quicconntest_migrate_fallback(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    NetAddr real   = f.cliAddr;
    NetAddr forged = real;
    forged.port    = (uint16)(forged.port + 3);

    // Nothing the server sends to the forged address ever arrives, which is exactly the position
    // it is in when the address was never the peer's.
    f.srv.blackhole    = forged;
    f.srv.blackholeSet = true;

    qMoveClient(&f, &forged);
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK_U("the server did not follow the forged address", _quicConnMigrations(f.srv.conn), 1);
    CHECK("the server validated a path that answers nothing", !_quicConnPathValid(f.srv.conn));

    // Time enough for the challenge to give up.
    f.cliAddr = real;
    for (int i = 0; i < 64 && !_quicConnPathValid(f.srv.conn) && !f.srv.closed; i++) {
        f.now += timeMS(200);
        _quicConnTick(f.srv.conn, f.now);
        qDropOut(&f.srv);   // still addressed to the forged path, and still going nowhere
    }

    NetAddr where;
    _quicConnPeerAddr(f.srv.conn, &where);
    CHECK("the server never went back to the path that worked", qAddrEq(&where, &real));
    CHECK("the server closed instead of falling back", !f.srv.closed);

    // And the connection works from the real address again.
    f.srv.blackholeSet = false;
    f.cli.sendMaxData  = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRunTimed(&f, 64, timeS(2));
    CHECK_U("MAX_DATA the server received after falling back", f.srv.gotMaxData, 4096);

out:
    qFixDestroy(&f);
    return ret;
}

// A path that carries most of the marks and drops the rest. Nothing about any one packet looks
// wrong; the only evidence is that the peer's counts rise by fewer than were marked, which is
// exactly what the arithmetic in RFC 9000 section 13.4.2 is for.
int test_quicconntest_ecn_partial(void)
{
    int ret = 0;
    QFix f;

    // The handshake runs on an unblemished path, so ECN is established before anything goes wrong
    // and everything the test looks at afterwards is in the application space, where the counts stay
    // readable. (A number space's counts go when its keys do.)
    CHECK("handshake", qHandshake(&f));
    CHECK("the client was not marking to begin with", _quicConnEcnActive(f.cli.conn));

    f.cli.ecnStripEvery = 2;

    f.cli.fillBulk = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRunTimed(&f, 64, timeS(2));
    f.cli.fillBulk = false;

    // The server did see marks, so its acknowledgements carry counts. This is not the case where
    // the peer reports nothing at all -- that one is ecn_stripped, and it takes a different branch.
    uint64 srv[3];
    _quicConnEcnCounts(f.srv.conn, QUIC_PNS_APP, srv);
    CHECK("the server counted no marks at all, which is a different failure", srv[0] > 0);

    CHECK("the client kept marking a path that carries only some marks",
          !_quicConnEcnActive(f.cli.conn));

    // The other direction is untouched and has no reason to stop.
    CHECK("the server stopped marking a path that carries marks", _quicConnEcnActive(f.srv.conn));

    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// 0-RTT (RFC 9001 section 4.6)
// ---------------------------------------------------------------------------------------------

// A resumed connection carrying a request in the client's very first flight. What makes this
// 0-RTT rather than an early send is that the server can read it before it has said anything at
// all -- the assertion below is taken with the server's own first flight still sitting in its
// queue, undelivered.
int test_quicconntest_early(void)
{
    int ret = 0;
    QFix f;
    STR_CONST(payload, "GET / early");

    CHECK("fixture", qFixInitResume(&f, true));
    CHECK("first handshake", qStartClient(&f) && qAccept(&f, NULL) && (qRun(&f), true));
    CHECK("first connection came up", f.cli.connected && f.srv.connected);
    CHECK_U("nothing to resume yet, so nothing early", _quicConnEarlyState(f.cli.conn),
            QUIC_ES_NONE);

    CHECK("resumed client started", qResumeStart(&f));
    CHECK("0-RTT was armed", f.cli.earlyOpened);
    CHECK_U("client is sending early", _quicConnEarlyState(f.cli.conn), QUIC_ES_LIVE);

    uint64 sid = 0;
    CHECK("stream opened before the handshake", _quicStreamOpen(&f.cli.streams, false, &sid));
    CHECK_U("request queued", _quicStreamSend(&f.cli.streams, sid,
                                              (const uint8*)strC(payload), strLen(payload)),
            strLen(payload));

    _quicConnFlush(f.cli.conn, f.now);
    CHECK("client produced a first flight", f.cli.nout >= 2);

    // The request left in a 0-RTT packet and nothing else could have carried it: no other packet
    // type this client can build yet is allowed to hold a STREAM frame.
    bool sawZeroRtt = false;
    for (uint32 i = 0; i < f.cli.nout; i++) {
        QuicPktHdr h;
        if (_quicHdrDecode(&h, f.cli.out[i]->data, f.cli.out[i]->len, 0) &&
            h.type == QUIC_PKT_0RTT)
            sawZeroRtt = true;
    }
    CHECK("a 0-RTT packet went out", sawZeroRtt);

    CHECK("server accepted", qAccept(&f, NULL));

    // Only the client's flight moves. Whatever the server has after this, it has without having
    // answered -- which is the whole of what 0-RTT is worth.
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK("server has not finished its handshake", !f.srv.connected);
    CHECK_U("server read the early data", _quicConnEarlyState(f.srv.conn), QUIC_ES_LIVE);
    CHECK_U("server saw the stream open", f.srv.nStreamOpen, 1);


    Buffer got = NULL;
    qStreamDrain(&f.srv, sid, &got);
    CHECK("server got the request before answering", got && got->len == strLen(payload) &&
          memcmp(got->data, strC(payload), got->len) == 0);
    bufDestroy(&got);

    qRun(&f);

    CHECK("both ends came up", f.cli.connected && f.srv.connected);
    CHECK_U("client's early data was vouched for", _quicConnEarlyState(f.cli.conn), QUIC_ES_DONE);
    CHECK_U("server's early data was vouched for", _quicConnEarlyState(f.srv.conn), QUIC_ES_DONE);
    CHECK("the client was never told it was refused", !f.cli.earlyRejected);

out:
    qFixDestroy(&f);
    return ret;
}

// A server offering less than it did when the ticket was issued cannot read the early data, since
// the client sent it under the old limits. The refusal has to cost the application nothing: the
// request goes again under the real keys and arrives all the same.
int test_quicconntest_early_rejected(void)
{
    int ret = 0;
    QFix f;
    STR_CONST(payload, "GET / early");

    CHECK("fixture", qFixInitResume(&f, true));
    CHECK("first handshake", qStartClient(&f) && qAccept(&f, NULL) && (qRun(&f), true));
    CHECK("first connection came up", f.cli.connected && f.srv.connected);

    CHECK("resumed client started", qResumeStart(&f));
    CHECK("0-RTT was armed", f.cli.earlyOpened);

    uint64 sid = 0;
    CHECK("stream opened before the handshake", _quicStreamOpen(&f.cli.streams, false, &sid));
    CHECK_U("request queued", _quicStreamSend(&f.cli.streams, sid,
                                              (const uint8*)strC(payload), strLen(payload)),
            strLen(payload));
    _quicConnFlush(f.cli.conn, f.now);

    f.srvShrink = true;
    CHECK("server accepted", qAccept(&f, NULL));

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);
    CHECK_U("server would not read it", _quicConnEarlyState(f.srv.conn), QUIC_ES_NONE);
    CHECK_U("server saw no stream", f.srv.nStreamOpen, 0);

    // Stepped by hand rather than run to quiet, because how quickly the request arrives is the
    // whole question. The client has heard exactly one flight from the server when it sends
    // again, and nothing in that flight acknowledged an application space packet -- so no loss
    // detection can have run, and what reaches the server is what the rejection itself put back.
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);

    CHECK("the client was told", f.cli.earlyRejected);
    CHECK_U("and knows what happened", _quicConnEarlyState(f.cli.conn), QUIC_ES_REFUSED);

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);
    CHECK_U("the request was not sent again straight away", f.srv.nStreamOpen, 1);

    qRun(&f);
    CHECK("both ends came up anyway", f.cli.connected && f.srv.connected);

    Buffer got = NULL;
    qStreamDrain(&f.srv, sid, &got);
    CHECK("and so did the request", got && got->len == strLen(payload) &&
          memcmp(got->data, strC(payload), got->len) == 0);
    bufDestroy(&got);

    // Refused packets stop counting against the congestion window. They were never lost -- the
    // peer simply would not read them -- so nothing releases them if the rejection does not.
    qStreamRound(&f);
    qStreamRound(&f);
    CHECK_U("bytes still counted as in flight", _quicConnRecovery(f.cli.conn)->inFlight, 0);

out:
    qFixDestroy(&f);
    return ret;
}

// RFC 9000 section 12.4 lists what a 0-RTT packet may carry, and PATH_RESPONSE is not on it: a
// packet written before anything has been received cannot be answering a challenge. The frame is
// chosen because an unsolicited one is harmless in a 1-RTT packet -- `path_replace` establishes
// that it is ignored there -- so nothing but the 0-RTT rule can be what closes this connection.
int test_quicconntest_early_badframe(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInitResume(&f, true));
    CHECK("first handshake", qStartClient(&f) && qAccept(&f, NULL) && (qRun(&f), true));
    CHECK("first connection came up", f.cli.connected && f.srv.connected);

    CHECK("resumed client started", qResumeStart(&f));
    CHECK("0-RTT was armed", f.cli.earlyOpened);

    f.cli.sendBadPathResponse = true;
    _quicConnFlush(f.cli.conn, f.now);

    CHECK("server accepted", qAccept(&f, NULL));
    qRun(&f);

    CHECK("the server closed the connection", f.srv.closed);
    CHECK_U("with a protocol violation", f.srv.closeError, QUIC_ERR_PROTOCOL_VIOLATION);

out:
    qFixDestroy(&f);
    return ret;
}

// Resumption without 0-RTT. The ticket is offered and taken, the handshake is abbreviated, and
// nothing is sent early -- which is what an application that has not said its requests are safe to
// repeat gets.
int test_quicconntest_early_off(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInitResume(&f, false));
    CHECK("first handshake", qStartClient(&f) && qAccept(&f, NULL) && (qRun(&f), true));
    CHECK("first connection came up", f.cli.connected && f.srv.connected);

    CHECK("resumed client started", qResumeStart(&f));
    CHECK("nothing was armed", !f.cli.earlyOpened);
    CHECK_U("and nothing went early", _quicConnEarlyState(f.cli.conn), QUIC_ES_NONE);

    CHECK("server accepted", qAccept(&f, NULL));
    qRun(&f);

    CHECK("the connection came up", f.cli.connected && f.srv.connected);

    // A resumed handshake carries the identity forward instead of sending a certificate, which is
    // what says the ticket was taken rather than quietly ignored.
    TlsInfo info;
    memset(&info, 0, sizeof(info));
    CHECK("client has handshake info", tlsquicGetInfo(_quicConnTls(f.cli.conn), &info));
    bool resumed = !info.peerVerified;
    nettlsInfoDestroy(&info);

    CHECK("the ticket was not used", resumed);

out:
    qFixDestroy(&f);
    return ret;
}

// A peer whose reported counts go down. No honest endpoint does this, so it is either a broken one
// or someone injecting frames, and either way nothing it says about congestion can be believed.
int test_quicconntest_ecn_lying(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));
    CHECK("the client stopped marking a path that carries marks",
          _quicConnEcnActive(f.cli.conn));

    // A first acknowledgement with counts far above anything real, so the next one is certain to be
    // lower whatever the connection did in between.
    f.srv.sendAck    = true;
    f.srv.sendAckEcn = true;
    f.srv.ackLargest = 0;
    f.srv.ackEcn[0]  = 1000;
    f.srv.ackEcn[1]  = 0;
    f.srv.ackEcn[2]  = 0;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("an acknowledgement with plausible counts stopped the client marking",
          _quicConnEcnActive(f.cli.conn));

    f.srv.sendAck    = true;
    f.srv.sendAckEcn = true;
    f.srv.ackLargest = 0;
    f.srv.ackEcn[0]  = 1;
    CHECK("flush again", _quicConnFlush(f.srv.conn, f.now));
    qRun(&f);

    CHECK("the client believed counts that went backwards", !_quicConnEcnActive(f.cli.conn));
    CHECK("the client closed", !f.cli.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// A move throws away everything that was measured about the path being left, and puts a connection
// ID the peer has not seen from the new address in front of it. None of this is observable from the
// data still flowing, which is why it needs a test of its own.
int test_quicconntest_migrate_resets(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    const QuicRecovery* r = _quicConnRecovery(f.srv.conn);

    // Put the server's congestion controller somewhere other than where it starts, by making every
    // datagram it sends arrive marked as congested.
    f.srv.ecnRemark = true;
    f.srv.fillBulk  = true;
    CHECK("flush", _quicConnFlush(f.srv.conn, f.now));
    qRunTimed(&f, 64, timeS(2));
    f.srv.fillBulk = false;

    CHECK("the server's window was never cut, so there is nothing to reset", r->ncongestion > 0);
    CHECK("the server's window was never cut, so there is nothing to reset",
          r->ssthresh != UINT64_MAX);
    CHECK("the path was never measured, so there is nothing to reset",
          _quicConnPathMtu(f.srv.conn) > QUIC_INITIAL_MTU);

    QuicCid before;
    _quicConnRemoteCid(f.srv.conn, &before);

    NetAddr moved = f.cliAddr;
    moved.port    = (uint16)(moved.port + 5);

    f.srv.ecnRemark = false;
    qMoveClient(&f, &moved);
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK_U("the server did not follow the peer's move", _quicConnMigrations(f.srv.conn), 1);

    // Everything below described the path that was.
    CHECK_U("the congestion window carried over from the old path", r->ssthresh, UINT64_MAX);
    CHECK("the server stayed in recovery across a move", !r->inRecovery);
    CHECK_U("the path MTU carried over from the old path",
            (uint64)_quicConnPathMtu(f.srv.conn), (uint64)QUIC_INITIAL_MTU);
    CHECK_U("the congestion window did not go back to where a new path starts", r->window,
            (uint64)10 * QUIC_INITIAL_MTU);

    // RFC 9000 section 9.5: addressing the peer by the same connection ID from a new address is
    // what lets someone watching both paths tie them together.
    QuicCid after;
    _quicConnRemoteCid(f.srv.conn, &after);
    CHECK("the server kept using the same connection ID across a move",
          after.len != before.len || memcmp(after.id, before.id, after.len) != 0);

out:
    qFixDestroy(&f);
    return ret;
}

// Nothing may migrate before the handshake is confirmed. A packet from a new address that early is
// not a peer that moved -- an endpoint that acted on it could be steered by anyone who reflected a
// handshake packet from an address of their choosing.
int test_quicconntest_migrate_early(void)
{
    int ret = 0;
    QFix f;

    CHECK("fixture", qFixInit(&f));
    CHECK("start", qStartClient(&f));
    CHECK("accept", qAccept(&f, NULL));

    // One round, so the client has Handshake keys and something ack-eliciting to say, but neither
    // end has finished.
    qDeliver(&f, &f.srv, &f.cli, &f.srvAddr, &f.cliAddr);
    CHECK("the handshake finished before the move could be tried",
          !_quicConnHandshakeConfirmed(f.srv.conn));
    CHECK("the client had nothing to send", f.cli.nout > 0);

    NetAddr moved = f.cliAddr;
    moved.port    = (uint16)(moved.port + 13);
    f.cliAddr     = moved;

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK_U("an address change during the handshake moved the connection",
            _quicConnMigrations(f.srv.conn), 0);

    NetAddr where;
    _quicConnPeerAddr(f.srv.conn, &where);
    CHECK("the server left the path the handshake was running on", !qAddrEq(&where, &moved));

out:
    qFixDestroy(&f);
    return ret;
}

// A move this endpoint made itself has nowhere to go back to: the local address it left is being
// given up, and the socket that was bound there with it. So a challenge that is never answered ends
// the connection rather than quietly returning to an address nothing is listening on.
int test_quicconntest_migrate_self_fails(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));
    CHECK("the client could not migrate", _quicConnMigrate(f.cli.conn, f.now));
    CHECK("the client did not start over on the new path", !_quicConnPathValid(f.cli.conn));

    // Nothing the client sends from the new address arrives, so nothing answers the challenge.
    for (int i = 0; i < 64 && !f.cli.closed; i++) {
        _quicConnTick(f.cli.conn, f.now);
        qDropOut(&f.cli);
        f.now += timeMS(200);
    }

    CHECK("the client neither validated the new path nor gave up on it", f.cli.closed);
    CHECK_U("the error the client closed with", f.cli.closeError, QUIC_ERR_NO_VIABLE_PATH);
    CHECK("the client blamed the peer for its own path failing", f.cli.closeLocal);

out:
    qFixDestroy(&f);
    return ret;
}

// Answering a challenge on the path it came from is per-challenge, not a mode the connection stays
// in. Once a peer that probed from elsewhere goes back to challenging on the path in use, the
// answers have to go back there too -- otherwise every later answer is addressed to somewhere the
// peer used once.
int test_quicconntest_path_response_addr(void)
{
    int ret = 0;
    QFix f;

    CHECK("handshake", qHandshake(&f));

    NetAddr real = f.cliAddr;
    NetAddr away = real;
    away.port    = (uint16)(away.port + 17);

    // A probing-only packet from an address the connection is not using.
    CHECK("the client could not migrate", _quicConnMigrate(f.cli.conn, f.now));
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK("the client sent nothing", f.cli.nout > 0);

    f.cliAddr = away;
    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK("the server sent nothing back", f.srv.nout > 0);
    CHECK("the server answered somewhere other than where the challenge came from",
          qAddrEq(&f.srv.lastDest, &away));
    qDropOut(&f.srv);

    // Now a challenge on the path the connection is actually using.
    f.cliAddr = real;
    _quicConnValidatePath(f.cli.conn);
    CHECK("flush again", _quicConnFlush(f.cli.conn, f.now));
    CHECK("the client sent nothing the second time", f.cli.nout > 0);

    qDeliver(&f, &f.cli, &f.srv, &f.cliAddr, &f.srvAddr);

    CHECK("the server sent nothing back the second time", f.srv.nout > 0);
    CHECK("the server was still answering to the address the peer probed from once",
          qAddrEq(&f.srv.lastDest, &real));

    CHECK("the client closed", !f.cli.closed);
    CHECK("the server closed", !f.srv.closed);

out:
    qFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Unreliable datagrams (RFC 9221)
// ---------------------------------------------------------------------------------------------

int test_quicconntest_dgram_params(void)
{
    int ret = 0;

    QuicTransportParams tp;
    _quicTpDefaults(&tp);
    CHECK_U("max_datagram_frame_size defaults to off", tp.maxDatagramFrame, 0);

    tp.initScid.len     = 2;
    tp.haveInitScid     = true;
    tp.maxDatagramFrame = 1200;

    uint8 buf[128];
    QuicWr wr;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("encode", _quicTpEncode(&wr, &tp, false));

    QuicTransportParams got;
    CHECK("decode", _quicTpDecode(&got, buf, _quicWrLen(&wr), false));
    CHECK_U("max_datagram_frame_size", got.maxDatagramFrame, 1200);

    // A set that leaves it alone says nothing about it, which the peer reads as no datagrams.
    tp.maxDatagramFrame = 0;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("encode without it", _quicTpEncode(&wr, &tp, false));
    CHECK("decode without it", _quicTpDecode(&got, buf, _quicWrLen(&wr), false));
    CHECK_U("an absent parameter", got.maxDatagramFrame, 0);

    // Twice, which is a malformed set like any other duplicate. The mask that catches this is
    // indexed by parameter id, and 0x20 is past the width the ids RFC 9000 defines need.
    static const uint8 dup[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x20, 0x02, 0x44, 0xb0,
                                 0x20, 0x02, 0x44, 0xb0 };
    CHECK("a duplicate max_datagram_frame_size was accepted",
          !_quicTpDecode(&got, dup, sizeof(dup), false));

    // The ids between the block RFC 9000 defines and this one are still unknown, and an unknown
    // parameter has to be skipped whatever its value looks like. This one's value is not a varint,
    // so a decoder that reached the integer reader for it would reject the whole set -- which RFC
    // 9000 section 7.4.2 forbids.
    static const uint8 gap[] = { 0x0f, 0x02, 0xaa, 0xbb, 0x1f, 0x03, 0xff, 0xff, 0xff };
    CHECK("an unknown parameter between 0x10 and 0x20 was rejected",
          _quicTpDecode(&got, gap, sizeof(gap), false));
    CHECK_U("it was read as a datagram parameter", got.maxDatagramFrame, 0);

out:
    return ret;
}

int test_quicconntest_dgram_frame(void)
{
    int ret = 0;

    static const uint8 payload[] = "an unreliable datagram";
    const size_t plen = sizeof(payload) - 1;
    uint8 buf[128];

    // Only the form with a length field is ever written, since the builder cannot promise a frame
    // will be the last one in its packet.
    QuicFrame f;
    memset(&f, 0, sizeof(f));
    f.type          = QUIC_FRAME_DATAGRAM_LEN;
    f.datagram.len  = plen;
    f.datagram.data = payload;

    size_t sz = _quicFrameSize(&f);
    CHECK_U("size of a DATAGRAM frame", sz, 1 + 1 + plen);

    QuicWr wr;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("encode", _quicFrameEncode(&wr, &f));
    CHECK_U("bytes written", _quicWrLen(&wr), sz);

    QuicFrame got;
    QuicRd rd;
    _quicRdInit(&rd, buf, _quicWrLen(&wr));
    CHECK("decode", _quicFrameDecode(&got, &rd));
    CHECK_U("type", got.type, QUIC_FRAME_DATAGRAM_LEN);
    CHECK_U("length", got.datagram.len, plen);
    CHECK("payload", memcmp(got.datagram.data, payload, plen) == 0);
    CHECK_U("nothing left over", _quicRdLeft(&rd), 0);

    // The form without a length runs to the end of the packet, so everything after the type byte
    // is the payload. cx never writes one, but a peer may.
    uint8 raw[64];
    raw[0] = QUIC_FRAME_DATAGRAM;
    memcpy(raw + 1, payload, plen);

    _quicRdInit(&rd, raw, 1 + plen);
    CHECK("decode the lengthless form", _quicFrameDecode(&got, &rd));
    CHECK_U("its type", got.type, QUIC_FRAME_DATAGRAM);
    CHECK_U("its length", got.datagram.len, plen);
    CHECK("its payload", memcmp(got.datagram.data, payload, plen) == 0);

    // An empty datagram is a legal one.
    memset(&f, 0, sizeof(f));
    f.type = QUIC_FRAME_DATAGRAM_LEN;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK("encode an empty datagram", _quicFrameEncode(&wr, &f));

    _quicRdInit(&rd, buf, _quicWrLen(&wr));
    CHECK("decode an empty datagram", _quicFrameDecode(&got, &rd));
    CHECK_U("its length", got.datagram.len, 0);

    // A length that runs past the end of what is there.
    static const uint8 overrun[] = { QUIC_FRAME_DATAGRAM_LEN, 0x20, 0x01, 0x02 };
    _quicRdInit(&rd, overrun, sizeof(overrun));
    CHECK("a truncated DATAGRAM frame was accepted", !_quicFrameDecode(&got, &rd));

out:
    return ret;
}

// One datagram each way across a real handshake, under real packet protection.
int test_quicconntest_dgram_cross(void)
{
    int ret     = 0;
    uint8* big  = NULL;
    QFix f;

    CHECK("handshake", qHandshakeDatagrams(&f));
    CHECK("both ends are up", f.cli.connected && f.srv.connected);

    size_t max = _quicConnMaxDatagram(f.cli.conn);
    CHECK("the client has no datagram channel", max > 0);
    CHECK("the server has no datagram channel", _quicConnMaxDatagram(f.srv.conn) > 0);

    big = xaAlloc(max + 1);
    for (size_t i = 0; i < max + 1; i++)
        big[i] = (uint8)(i * 7 + 3);

    CHECK("a datagram larger than the limit was taken",
          !_quicConnDatagramSend(f.cli.conn, big, max + 1));

    static const uint8 msg[] = "carried unreliably";
    CHECK("the client queued a datagram",
          _quicConnDatagramSend(f.cli.conn, msg, sizeof(msg) - 1));

    // The slot holds exactly one, so a second send before the first has gone out is refused.
    CHECK("a second datagram was taken while the first was still waiting",
          !_quicConnDatagramSend(f.cli.conn, msg, sizeof(msg) - 1));

    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    CHECK_U("datagrams the server received", f.srv.ndgramIn, 1);
    CHECK_U("its length", f.srv.lastDgramLen, sizeof(msg) - 1);
    CHECK("its payload",
          f.srv.lastDgram && memcmp(f.srv.lastDgram->data, msg, sizeof(msg) - 1) == 0);
    CHECK("the client was never told the slot drained", f.cli.ndgramWritable > 0);

    // And back the other way, at exactly the size the limit allows.
    size_t srvMax = _quicConnMaxDatagram(f.srv.conn);
    CHECK("a datagram of exactly the limit was refused",
          _quicConnDatagramSend(f.srv.conn, big, srvMax));

    for (int i = 0; i < 4; i++)
        qStreamRound(&f);

    CHECK_U("datagrams the client received", f.cli.ndgramIn, 1);
    CHECK_U("the largest allowed datagram arrived truncated", f.cli.lastDgramLen, srvMax);
    CHECK("its payload changed on the way",
          f.cli.lastDgram && memcmp(f.cli.lastDgram->data, big, srvMax) == 0);

    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

out:
    if (big)
        xaFree(big);
    qFixDestroy(&f);
    return ret;
}

// A datagram small enough to share a packet rides out with the stream data it was queued alongside
// rather than costing a datagram of its own.
int test_quicconntest_dgram_coalesce(void)
{
    int ret    = 0;
    Buffer got = 0;
    QFix f;

    CHECK("handshake", qHandshakeDatagrams(&f));

    uint64 id = 0;
    CHECK("the client opened a stream", _quicStreamOpen(&f.cli.streams, false, &id));

    static const uint8 msg[]  = "small enough to share";
    static const uint8 body[] = "stream bytes going the same way";

    CHECK_U("stream bytes queued",
            _quicStreamSend(&f.cli.streams, id, body, sizeof(body) - 1), sizeof(body) - 1);
    CHECK("the datagram was queued",
          _quicConnDatagramSend(f.cli.conn, msg, sizeof(msg) - 1));

    uint32 before = f.cli.nout;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    CHECK_U("datagrams the client put on the wire", f.cli.nout - before, 1);

    qRun(&f);

    CHECK_U("datagrams the server received", f.srv.ndgramIn, 1);
    CHECK_U("its length", f.srv.lastDgramLen, sizeof(msg) - 1);

    qStreamDrain(&f.srv, id, &got);
    CHECK("the stream bytes in the same packet did not arrive",
          got && got->len == sizeof(body) - 1 && memcmp(got->data, body, sizeof(body) - 1) == 0);

out:
    bufDestroy(&got);
    qFixDestroy(&f);
    return ret;
}

// A full-size datagram cannot share a packet with anything, and must not be given up on because of
// it: the packet that had no room goes without it, and the next one carries it whole.
int test_quicconntest_dgram_solo(void)
{
    int ret    = 0;
    uint8* big = NULL;
    Buffer got = 0;
    QFix f;

    CHECK("handshake", qHandshakeDatagrams(&f));

    size_t max = _quicConnMaxDatagram(f.cli.conn);
    CHECK("no datagram channel", max > 0);

    big = xaAlloc(max);
    for (size_t i = 0; i < max; i++)
        big[i] = (uint8)(i * 29 + 11);

    // Two other things wanting the same packet: a frame the connection owes, which is written
    // before the datagram is considered, and stream data, which is written after. A full-size
    // datagram plus either of them is more than one packet holds, so the first packet has to go
    // without it and a second one has to be built for it to travel in.
    uint64 id = 0;
    CHECK("the client opened a stream", _quicStreamOpen(&f.cli.streams, false, &id));

    uint8 body[2048];
    for (size_t i = 0; i < sizeof(body); i++)
        body[i] = (uint8)(i * 3 + 1);
    CHECK_U("stream bytes queued", _quicStreamSend(&f.cli.streams, id, body, sizeof(body)),
            sizeof(body));
    _quicStreamFinish(&f.cli.streams, id);

    _quicConnValidatePath(f.cli.conn);
    CHECK("the datagram was queued", _quicConnDatagramSend(f.cli.conn, big, max));

    uint32 before = f.cli.nout;
    CHECK("flush the client", _quicConnFlush(f.cli.conn, f.now));

    // At least two: one that the other frames left no room in, and one built empty for the
    // datagram to claim whole.
    CHECK_UGE("datagrams the client put on the wire", f.cli.nout - before, 2);

    for (int i = 0; i < 16; i++)
        qStreamRound(&f);

    CHECK_U("datagrams the server received", f.srv.ndgramIn, 1);
    CHECK_U("it arrived truncated", f.srv.lastDgramLen, max);
    CHECK("its payload changed on the way",
          f.srv.lastDgram && memcmp(f.srv.lastDgram->data, big, max) == 0);

    CHECK("the stream beside it did not finish", qStreamDrain(&f.srv, id, &got));
    CHECK("the stream data beside it did not get through", got && got->len == sizeof(body));

out:
    bufDestroy(&got);
    if (big)
        xaFree(big);
    qFixDestroy(&f);
    return ret;
}

// A datagram lost on the way is gone. Nothing puts it back, and the stream sharing the connection
// is repaired as usual -- which is the whole difference between the two.
int test_quicconntest_dgram_lost(void)
{
    int ret    = 0;
    Buffer got = 0;
    QFix f;

    CHECK("handshake", qHandshakeDatagrams(&f));

    static const uint8 msg[] = "this one never arrives";

    // The path throws away the next few datagrams this side produces, and the frame exists nowhere
    // else -- there is no send buffer behind it and no packet number recorded for it.
    f.cli.dropNext = 4;
    CHECK("the datagram was queued",
          _quicConnDatagramSend(f.cli.conn, msg, sizeof(msg) - 1));
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));

    CHECK("the datagram never reached a packet", f.cli.ndgramWritable > 0);
    CHECK_U("something got past a path that was dropping everything", f.cli.nout, 0);

    uint64 id = 0;
    CHECK("the client opened a stream", _quicStreamOpen(&f.cli.streams, false, &id));

    static const uint8 body[] = "the stream beside it still gets through";
    CHECK_U("stream bytes queued",
            _quicStreamSend(&f.cli.streams, id, body, sizeof(body) - 1), sizeof(body) - 1);
    _quicStreamFinish(&f.cli.streams, id);

    // Long enough for several retransmissions, so a datagram that was ever going to come back has
    // had every chance to.
    for (int i = 0; i < 32; i++)
        qStreamRound(&f);

    CHECK_U("a lost datagram was retransmitted", f.srv.ndgramIn, 0);

    CHECK("the stream did not finish beside a datagram that was dropped",
          qStreamDrain(&f.srv, id, &got));
    CHECK("the stream bytes did not arrive",
          got && got->len == sizeof(body) - 1 && memcmp(got->data, body, sizeof(body) - 1) == 0);
    CHECK("the connection is still up", !f.cli.closed && !f.srv.closed);

out:
    bufDestroy(&got);
    qFixDestroy(&f);
    return ret;
}

// The two ways a peer can send a DATAGRAM frame that was never asked for. Both need a frame
// written by hand, because an endpoint following the rules produces neither.
int test_quicconntest_dgram_refused(void)
{
    int ret = 0;
    QFix f;

    // Neither end advertised the parameter, so the frame arriving at all is the peer ignoring what
    // this endpoint said it would read.
    CHECK("handshake", qHandshakeStreams(&f));
    CHECK("a connection that never advertised the parameter offers a channel",
          _quicConnMaxDatagram(f.cli.conn) == 0);
    CHECK("it queued a datagram anyway",
          !_quicConnDatagramSend(f.cli.conn, (const uint8*)"x", 1));

    f.cli.sendDatagram = true;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("the server accepted a DATAGRAM it never asked for", f.srv.closed);
    CHECK_U("the error it closed with", f.srv.closeError, QUIC_ERR_PROTOCOL_VIOLATION);
    qFixDestroy(&f);

    // Advertised, but the frame is larger than the size that was advertised. The size counts the
    // type and length fields as well as the payload, so 64 leaves room for 61 bytes.
    CHECK("handshake", qFixInit(&f));
    f.streamMode   = true;
    f.datagrams    = true;
    f.datagramMax  = 64;
    CHECK("start", qStartClient(&f) && qAccept(&f, NULL) && (qRun(&f), true));

    CHECK_U("the size a 64-byte limit leaves for a payload", _quicConnMaxDatagram(f.cli.conn), 61);

    f.cli.sendDatagram    = true;
    f.cli.sendDatagramLen = 200;
    CHECK("flush", _quicConnFlush(f.cli.conn, f.now));
    qRun(&f);

    CHECK("the server accepted an oversized DATAGRAM", f.srv.closed);
    CHECK_U("the error it closed with", f.srv.closeError, QUIC_ERR_PROTOCOL_VIOLATION);

out:
    qFixDestroy(&f);
    return ret;
}

testfunc quicconntest_funcs[] = {
    { "stream_echo", test_quicconntest_stream_echo },
    { "stream_bulk", test_quicconntest_stream_bulk },
    { "stream_loss", test_quicconntest_stream_loss },
    { "stream_loss_data", test_quicconntest_stream_loss_data },
    { "stream_reset", test_quicconntest_stream_reset },
    { "stream_reset_loss", test_quicconntest_stream_reset_loss },
    { "stream_tail_loss", test_quicconntest_stream_tail_loss },
    { "stream_violation", test_quicconntest_stream_violation },
    { "params", test_quicconntest_params },
    { "params_malformed", test_quicconntest_params_malformed },
    { "reasm", test_quicconntest_reasm },
    { "ackset", test_quicconntest_ackset },
    { "token", test_quicconntest_token },
    { "handshake", test_quicconntest_handshake },
    { "onertt", test_quicconntest_onertt },
    { "keyupdate", test_quicconntest_keyupdate },
    { "padding", test_quicconntest_padding },
    { "srvpad", test_quicconntest_srvpad },
    { "dup_packet", test_quicconntest_dup_packet },
    { "retry", test_quicconntest_retry },
    { "retry_forged", test_quicconntest_retry_forged },
    { "tp_mismatch", test_quicconntest_tp_mismatch },
    { "no_frames", test_quicconntest_no_frames },
    { "bad_ack", test_quicconntest_bad_ack },
    { "cid_limit", test_quicconntest_cid_limit },
    { "cid_bad", test_quicconntest_cid_bad },
    { "versionneg", test_quicconntest_versionneg },
    { "close", test_quicconntest_close },
    { "close_level", test_quicconntest_close_level },
    { "bad_frame", test_quicconntest_bad_frame },
    { "stream_refused", test_quicconntest_stream_refused },
    { "frame_level", test_quicconntest_frame_level },
    { "role_frames", test_quicconntest_role_frames },
    { "idle", test_quicconntest_idle },
    { "statelessreset", test_quicconntest_statelessreset },
    { "cids", test_quicconntest_cids },
    { "path", test_quicconntest_path },
    { "path_replace", test_quicconntest_path_replace },
    { "amplification", test_quicconntest_amplification },
    { "loss_initial", test_quicconntest_loss_initial },
    { "loss_flight", test_quicconntest_loss_flight },
    { "loss_crypto", test_quicconntest_loss_crypto },
    { "loss_control", test_quicconntest_loss_control },
    { "congestion", test_quicconntest_congestion },
    { "congestion_limit", test_quicconntest_congestion_limit },
    { "congestion_sliver", test_quicconntest_congestion_sliver },
    { "ecn", test_quicconntest_ecn },
    { "ecn_stripped", test_quicconntest_ecn_stripped },
    { "ecn_congestion", test_quicconntest_ecn_congestion },
    { "pmtu", test_quicconntest_pmtu },
    { "pmtu_floor", test_quicconntest_pmtu_floor },
    { "migrate", test_quicconntest_migrate },
    { "migrate_probing", test_quicconntest_migrate_probing },
    { "migrate_reorder", test_quicconntest_migrate_reorder },
    { "migrate_fallback", test_quicconntest_migrate_fallback },
    { "migrate_resets", test_quicconntest_migrate_resets },
    { "migrate_early", test_quicconntest_migrate_early },
    { "migrate_self_fails", test_quicconntest_migrate_self_fails },
    { "path_response_addr", test_quicconntest_path_response_addr },
    { "ecn_partial", test_quicconntest_ecn_partial },
    { "ecn_lying", test_quicconntest_ecn_lying },
    { "pacing", test_quicconntest_pacing },
    { "ack_delay", test_quicconntest_ack_delay },
    { "early", test_quicconntest_early },
    { "early_rejected", test_quicconntest_early_rejected },
    { "early_badframe", test_quicconntest_early_badframe },
    { "early_off", test_quicconntest_early_off },
    { "dgram_params", test_quicconntest_dgram_params },
    { "dgram_frame", test_quicconntest_dgram_frame },
    { "dgram_cross", test_quicconntest_dgram_cross },
    { "dgram_coalesce", test_quicconntest_dgram_coalesce },
    { "dgram_solo", test_quicconntest_dgram_solo },
    { "dgram_lost", test_quicconntest_dgram_lost },
    { "dgram_refused", test_quicconntest_dgram_refused },
    { NULL, NULL },
};

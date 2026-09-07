// QUIC through netqueue, over real loopback UDP sockets.
//
// Everything below this level is already covered without a network: the wire codec, the packet
// protection, the handshake, loss recovery and the streams each have their own suite driven by
// fixtures and a clock the test controls. What is left to prove here is only what those cannot
// reach -- that a connection is a socket, a stream is a flow, and the events arrive where the
// netqueue API says they do.
//
// The queue is polled, so the test drives it: netqueueTick() runs ingest, dispatch and the timer
// sweep on the calling thread, which makes an assertion failure a stack rather than a race.

#include <cxquic.h>

#include "tlstestcert.h"

#include <cx/container.h>
#include <cx/platform/os.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

#define TEST_FILE  quicnettest
#define TEST_FUNCS quicnettest_funcs
#include "common.h"

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)

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

// Runs the queue until `cond` holds or the budget runs out. Loopback is immediate, so a passing
// test spends almost none of this; the budget is there so a failing one reports rather than hangs.
#define QN_WAIT(f, cond, ms)                                     \
    do {                                                         \
        int64 _end = clockTimer() + timeMS(ms);                  \
        while (!(cond) && clockTimer() < _end)                   \
            qnTick(f, timeMS(2));                                \
    } while (0)

#define QN_BUDGET 4000

// ---------------------------------------------------------------------------------------------
// One end of the conversation
// ---------------------------------------------------------------------------------------------

#define QN_MAXSTREAMS 8

typedef struct QNStream {
    NetFlow* flow;
    uint64 id;
    Buffer data;      // everything read from the stream, in order
    uint64 error;
    uint64 echoed;    // bytes handed back by the echo pump
    bool fin;
    bool finSent;     // the echo pump ended its own sending half
    bool closed;
    bool errored;
} QNStream;

typedef struct QNSide {
    struct QNFix* f;
    NetSocket* sock;        // the connection: dialled for a client, accepted for a server

    uint32 nAccepted;
    uint32 nConnected;
    uint32 nConnFailed;
    NetErrorCode lastErr;

    uint32 nOpened;         // streams the peer or this end opened
    uint32 nStreamClosed;
    uint32 nSendReady;
    uint32 nErrors;
    uint32 nSecured;        // NFN_Secured: the handshake vouched for a connection that used 0-RTT
    bool connClosed;        // the connection's control flow reached its terminal event
    NetCloseReason connCloseReason;
    bool lsnClosed;         // the listener's own control flow did
    NetCloseReason streamCloseReason;

    QNStream streams[QN_MAXSTREAMS];
    uint32 nstreams;

    bool echo;              // send whatever arrives straight back on the same stream
    bool drain;             // read what arrives and throw it away, counting the bytes
    bool hold;              // leave the bytes where they are, so the peer's window stays shut
    uint64 drained;
    uint64 announced;       // what NET_DataReceived said was readable, summed over every event

    // A send driven entirely by NET_SendReady: the first push happens when the test starts it,
    // and every one after that is the event saying there is room again.
    NetFlow* pumpFlow;
    const uint8* pumpData;
    size_t pumpTotal;
    size_t pumpSent;
    bool pumpRefused;       // a send was refused with netquicWritable() reporting room

    // Non-zero makes the pump write whole units of this size, the way a framed protocol has to:
    // it offers the frame and lets the send be refused rather than measuring the window first,
    // since a window with bytes left in it but not enough for a frame is one it cannot spend.
    size_t pumpFrame;
    uint32 pumpRefusals;    // times a whole-frame write was turned away

    // The least room any wake-up on the pump's stream arrived with. What the watermark decides is
    // how full the window has to be for waking the sender to be worth doing, so this is the one
    // place its effect is visible from outside.
    size_t pumpWakeRoomMin;

    // Echo what arrives, reading only as much as there is room to send straight back, and end the
    // sending half once the peer has ended its own. Leaving the rest unread is what holds the
    // peer's window shut, which is what an application that echoes actually does.
    bool echoPump;

    // The unreliable datagram channel. Unlike a stream there is at most one per connection, and
    // what arrives on it is a whole payload in the event rather than bytes to be drained.
    NetFlow* dgFlow;
    uint32 ndgram;          // datagrams delivered here
    uint32 nDgSendReady;    // NET_SendReady on the datagram flow
    size_t dgLast;          // length of the most recent one
    Buffer dgData;          // its payload
    bool dgEcho;            // send whatever arrives straight back
} QNSide;

typedef struct QNFix {
    NetQueue* q;

    /// A queue of the client's own, for the one test that needs to run the two ends at different
    /// times. NULL everywhere else, where both ends share `q`.
    NetQueue* q2;

    TlsTestPKI pki;
    TlsCAStore* ca;
    TlsCreds* creds;
    TlsConfig* ccfg;
    TlsConfig* scfg;

    NetSocket* lsn;
    QNSide cli;
    QNSide srv;
} QNFix;

// The record kept for one stream, created on its NET_FlowOpen and found by flow afterwards.
_Ret_maybenull_ static QNStream* qnStream(_Inout_ QNSide* s, _In_ NetFlow* flow)
{
    for (uint32 i = 0; i < s->nstreams; i++) {
        if (s->streams[i].flow == flow)
            return &s->streams[i];
    }

    if (s->nstreams >= QN_MAXSTREAMS)
        return NULL;

    QNStream* st = &s->streams[s->nstreams++];
    st->flow     = flow;
    st->id       = flow->key;
    return st;
}

// The same, found by stream id: an end that has streams in both directions cannot assume which
// one arrived first.
_Ret_maybenull_ static QNStream* qnStreamById(_Inout_ QNSide* s, uint64 id)
{
    for (uint32 i = 0; i < s->nstreams; i++) {
        if (s->streams[i].id == id)
            return &s->streams[i];
    }
    return NULL;
}

// Whether the first `n` streams this end knows about have each received exactly `len` bytes.
static bool qnAllGot(_In_ const QNSide* s, uint32 n, size_t len)
{
    if (s->nstreams < n)
        return false;

    for (uint32 i = 0; i < n; i++) {
        if (bufLen(s->streams[i].data) != len)
            return false;
    }

    return true;
}

static void qnOnAccepted(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;

    s->nAccepted++;
    if (!s->sock)
        s->sock = objAcquire(ev->accept.newSocket);
}

static void qnOnConnection(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;

    if (ev->conn.err == NERR_None) {
        s->nConnected++;
    } else {
        s->nConnFailed++;
        s->lastErr = ev->conn.err;
    }
}

static void qnOnFlowOpen(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;

    // The peer's first datagram is what admits this flow on the receiving end, so this is where
    // an end that never asked for the channel itself finds out it has one.
    if (objDynCast(QuicDatagram, ev->flow)) {
        if (!s->dgFlow)
            s->dgFlow = objAcquire(ev->flow);
        return;
    }

    s->nOpened++;
    qnStream(s, ev->flow);
}

// Reads only what there is room to echo, and ends the sending half once the peer's end has been
// read. A window that is shut leaves the rest where it is, which is what stops the peer.
static void qnEcho(_Inout_ QNSide* s, _Inout_ QNStream* st)
{
    uint8 buf[4096];

    for (;;) {
        size_t room = netquicWritable(st->flow);
        if (room == 0)
            break;
        if (room > sizeof(buf))
            room = sizeof(buf);

        bool fin = false;
        size_t n = netquicRecv(st->flow, buf, room, &fin);
        if (fin)
            st->fin = true;
        if (n == 0)
            break;

        if (!netflowSend(st->flow, buf, n, 0)) {
            s->pumpRefused = true;
            return;
        }
        st->echoed += n;
    }

    if (st->fin && !st->finSent) {
        netquicFinish(st->flow);
        st->finSent = true;
    }
}

// A datagram flow delivers the whole payload in the event, the way a datagram socket does, so
// there is nothing to drain afterwards and nothing to accumulate across events.
static void qnOnDatagram(_Inout_ QNSide* s, _Inout_ NetEvent* ev)
{
    Buffer buf = ev->recv.msg ? ev->recv.msg->buf : NULL;

    s->ndgram++;
    s->dgLast = buf ? buf->len : 0;

    bufDestroy(&s->dgData);
    if (buf)
        bufAppendBytes(&s->dgData, buf->data, buf->len);

    if (s->dgEcho && buf)
        netflowSend(ev->flow, buf->data, buf->len, 0);
}

static void qnOnRecv(_Inout_ NetEvent* ev)
{
    QNSide* s   = (QNSide*)ev->ctx;

    if (objDynCast(QuicDatagram, ev->flow)) {
        qnOnDatagram(s, ev);
        return;
    }

    QNStream* st = qnStream(s, ev->flow);

    s->announced += ev->recv.bytes;

    if (s->echoPump) {
        if (st)
            qnEcho(s, st);
        return;
    }

    if (s->hold)
        return;

    uint8 buf[4096];
    bool fin   = false;
    bool ended = false;
    size_t n;

    // The event says there is something to read; the bytes themselves are taken here, which is
    // also what tells the peer its window has moved. The end of the stream is reported by whichever
    // call reaches it and not by the ones after, so it has to be accumulated rather than read off
    // the last one.
    while ((n = netquicRecv(ev->flow, buf, sizeof(buf), &fin)) > 0) {
        ended = ended || fin;

        if (s->drain) {
            s->drained += n;
        } else if (st) {
            if (!st->data)
                st->data = bufCreate(n);
            bufAppendBytes(&st->data, buf, n);
        }

        if (s->echo)
            netflowSend(ev->flow, buf, n, 0);
    }

    if ((ended || fin) && st)
        st->fin = true;
}

// Pushes as much as netquicWritable() says will fit. A send that is refused anyway means the two
// disagree, which is the one thing this must not do: an application has no other way to find out
// how much it may hand over.
static void qnPump(_Inout_ QNSide* s)
{
    while (s->pumpSent < s->pumpTotal) {
        size_t want = s->pumpTotal - s->pumpSent;

        if (s->pumpFrame) {
            if (want > s->pumpFrame)
                want = s->pumpFrame;

            // Being refused is the whole point: it is what says this sender is waiting, and there
            // is nothing else for it to wait on.
            if (!netflowSend(s->pumpFlow, s->pumpData + s->pumpSent, want, 0)) {
                s->pumpRefusals++;
                return;
            }

            s->pumpSent += want;
            continue;
        }

        size_t room = netquicWritable(s->pumpFlow);
        size_t n    = room < want ? room : want;

        if (n == 0)
            return;

        if (!netflowSend(s->pumpFlow, s->pumpData + s->pumpSent, n, 0)) {
            s->pumpRefused = true;
            return;
        }

        s->pumpSent += n;
    }
}

static void qnOnSendReady(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;

    if (objDynCast(QuicDatagram, ev->flow)) {
        s->nDgSendReady++;
        return;
    }

    s->nSendReady++;

    if (s->pumpFlow && ev->flow == s->pumpFlow) {
        size_t room = netquicWritable(ev->flow);
        if (s->nSendReady == 1 || room < s->pumpWakeRoomMin)
            s->pumpWakeRoomMin = room;
        qnPump(s);
    }

    if (s->echoPump) {
        QNStream* st = qnStream(s, ev->flow);
        if (st)
            qnEcho(s, st);
    }
}

static void qnOnFilterNotify(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;
    if (ev->filter.notify == NFN_Secured)
        s->nSecured++;
}

static void qnOnError(_Inout_ NetEvent* ev)
{
    QNSide* s    = (QNSide*)ev->ctx;
    QNStream* st = qnStream(s, ev->flow);

    s->nErrors++;

    QuicStream* qs = objDynCast(QuicStream, ev->flow);
    if (st && qs) {
        st->error   = qs->error;
        st->errored = true;
    }
}

static void qnOnFlowClosed(_Inout_ NetEvent* ev)
{
    QNSide* s = (QNSide*)ev->ctx;

    // A QUIC socket has two kinds of flow, and the terminal event is the same for both: the one
    // that is the socket's own control flow says that socket ended, any other says a stream did.
    // A listener and everything it accepts share this handler set, so which socket it came from is
    // what separates the connection ending from the listener ending.
    if (ev->socket && ev->flow == ev->socket->flow) {
        if (ev->socket == s->sock) {
            s->connClosed      = true;
            s->connCloseReason = ev->closed.reason;
        } else {
            s->lsnClosed = true;
        }
        return;
    }

    if (objDynCast(QuicDatagram, ev->flow))
        return;

    s->nStreamClosed++;
    s->streamCloseReason = ev->closed.reason;

    QNStream* st = qnStream(s, ev->flow);
    if (st)
        st->closed = true;
}

static const NetHandlers qnHandlers = {
    .accepted   = qnOnAccepted,
    .connection = qnOnConnection,
    .filterNotify = qnOnFilterNotify,
    .flowOpen   = qnOnFlowOpen,
    .recv       = qnOnRecv,
    .sendReady  = qnOnSendReady,
    .error      = qnOnError,
    .flowClosed = qnOnFlowClosed,
};

// ---------------------------------------------------------------------------------------------
// The fixture
// ---------------------------------------------------------------------------------------------

// Runs both ends. They normally share one queue; the split-queue test is the exception, and it is
// the reason this is a function rather than a bare netqueueTick().
static void qnTick(_Inout_ QNFix* f, int64 wait)
{
    netqueueTick(f->q, wait);
    if (f->q2)
        netqueueTick(f->q2, 0);
}

// Loopback in NetAddr's host-order storage, where ipv4[0] is the least significant octet.
static NetAddr qnLoopback(uint16 port)
{
    NetAddr a = { .type = NA_IPv4, .port = port };
    a.ipv4[3] = 127;
    a.ipv4[0] = 1;
    return a;
}

// Distinct connections in a listener's routing table. A connection appears once per ID it has
// issued, so counting rows would count the same socket several times.
static uint32 qnListenerConns(_In_ NetSocket* lsn)
{
    NetSocketQuic* q = objDynCast(NetSocketQuic, lsn);
    if (!q)
        return 0;

    NetSocket* seen[QN_MAXSTREAMS];
    uint32 n = 0;

    withReadLock (&q->cidLock) {
        foreach (hashtable, hti, q->cids) {
            NetSocket* c = (NetSocket*)htiVal(object, hti);
            bool dup     = false;
            for (uint32 i = 0; i < n; i++)
                dup = dup || seen[i] == c;
            if (!dup && n < QN_MAXSTREAMS)
                seen[n++] = c;
        }
    }

    return n;
}

static void qnSideDestroy(_Inout_ QNSide* s)
{
    for (uint32 i = 0; i < s->nstreams; i++)
        bufDestroy(&s->streams[i].data);

    bufDestroy(&s->dgData);
    objRelease(&s->dgFlow);
    objRelease(&s->sock);
}

static void qnFixDestroy(_Inout_ QNFix* f)
{
    if (f->q) {
        // Give whatever was still in flight a chance to land, so a leaked flow or an undelivered
        // terminal event shows up here rather than as a mystery in the next test.
        for (int i = 0; i < 8; i++)
            qnTick(f, 0);
    }

    qnSideDestroy(&f->cli);
    qnSideDestroy(&f->srv);

    objRelease(&f->lsn);

    if (f->q2) {
        netqueueShutdown(f->q2, timeS(2));
        objRelease(&f->q2);
    }

    if (f->q) {
        netqueueShutdown(f->q, timeS(2));
        objRelease(&f->q);
    }

    objRelease(&f->ccfg);
    objRelease(&f->scfg);
    objRelease(&f->creds);
    objRelease(&f->ca);
    tlsTestPKIDestroy(&f->pki);
}

// recvBufMax caps the receive buffer pool; 0 leaves the preset's own cap alone.
static bool qnFixInitPool(_Out_ QNFix* f, uint32 recvBufMax)
{
    memset(f, 0, sizeof(*f));

    f->cli.f = f;
    f->srv.f = f;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);   // polled: the test drives netqueueTick() itself
    if (recvBufMax > 0)
        conf.recvBufMax = recvBufMax;
    f->q = netqueueCreate(&conf);
    if (!f->q)
        return false;

    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    f->creds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    if (!f->creds)
        return false;

    f->ccfg = tlsconfigCreateClient();
    f->scfg = tlsconfigCreateServer(f->creds);
    if (!f->ccfg || !f->scfg)
        return false;

    tlsconfigSetCA(f->ccfg, f->ca);
    return true;
}

static bool qnFixInit(_Out_ QNFix* f)
{
    return qnFixInitPool(f, 0);
}

// Starts a listener on an ephemeral loopback port and reports which one it got.
static bool qnListen(_Inout_ QNFix* f, _In_ const QuicConfig* base, uint16* port)
{
    QuicConfig cfg = *base;
    cfg.tls        = f->scfg;

    NetAddr addr = qnLoopback(0);
    f->lsn       = netquicListen(f->q, &addr, &cfg, &qnHandlers, &f->srv);
    if (!f->lsn)
        return false;

    *port = f->lsn->local.port;
    return *port != 0;
}

static bool qnDial(_Inout_ QNFix* f, _In_ const QuicConfig* base, uint16 port)
{
    QuicConfig cfg = *base;
    cfg.tls        = f->ccfg;

    f->cli.sock = netquicConnect(f->q2 ? f->q2 : f->q, _S "127.0.0.1", port, _S TLS_TEST_HOSTNAME,
                                 &cfg, &qnHandlers, &f->cli);
    return f->cli.sock != NULL;
}

// Listener up, client connected, connection accepted. Everything past the handshake starts here.
static bool qnConnectPool(_Inout_ QNFix* f, _In_opt_ const QuicConfig* base, uint32 recvBufMax)
{
    QuicConfig def = { 0 };
    if (!base)
        base = &def;

    uint16 port = 0;
    if (!qnFixInitPool(f, recvBufMax) || !qnListen(f, base, &port) || !qnDial(f, base, port))
        return false;

    QN_WAIT(f, f->cli.nConnected > 0 && f->srv.sock != NULL, QN_BUDGET);
    return f->cli.nConnected > 0 && f->srv.sock != NULL;
}

static bool qnConnect(_Inout_ QNFix* f, _In_opt_ const QuicConfig* base)
{
    return qnConnectPool(f, base, 0);
}

// Opens a stream, sends one payload on it, and lets both ends settle.
static _Ret_maybenull_ NetFlow* qnSend(_Inout_ QNFix* f, _In_ NetSocket* sock, bool uni,
                                       _In_reads_(len) const uint8* data, size_t len)
{
    NetFlow* flow = netquicOpen(sock, uni);
    if (!flow)
        return NULL;

    if (!netflowSend(flow, data, len, 0)) {
        objRelease(&flow);
        return NULL;
    }

    return flow;
}

// ---------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------

// The whole point of the stage in one test: netquicListen and netquicConnect produce two ordinary
// sockets, and the ordinary events say the connection is up.
static int test_quicnettest_handshake(void)
{
    int ret = 0;
    QNFix f;

    QuicConfig cfg = { 0 };
    uint16 port    = 0;

    CHECK("fixture", qnFixInit(&f));
    CHECK("listen", qnListen(&f, &cfg, &port));
    CHECK("dial", qnDial(&f, &cfg, port));

    // Nothing may be opened until the peer's transport parameters arrive, because until then every
    // limit it granted is zero -- including how many streams may exist.
    CHECK("a stream opened before the handshake finished", netquicOpen(f.cli.sock, false) == NULL);

    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);
    CHECK("connected", f.cli.nConnected > 0 && f.srv.sock != NULL);

    CHECK_U("client connections", f.cli.nConnected, 1);
    CHECK_U("client failures", f.cli.nConnFailed, 0);
    CHECK_U("accepts", f.srv.nAccepted, 1);

    // The accepted socket is a QUIC socket in its own right, already on the queue and already
    // carrying the listener's handlers.
    CHECK("the accepted socket is a QUIC socket", objDynCast(NetSocketQuic, f.srv.sock) != NULL);
    CHECK_U("accepted socket type", f.srv.sock->type, NST_Quic);
    CHECK("the accepted socket has no OS handle", f.srv.sock->handle == NET_INVALID_HANDLE);
    CHECK("the accepted socket has a control flow", f.srv.sock->flow != NULL);

    // Neither end opened a stream, so neither has a flow in its table.
    CHECK_U("client stream flows", htSize(f.cli.sock->flows), 0);
    CHECK_U("server stream flows", htSize(f.srv.sock->flows), 0);

out:
    qnFixDestroy(&f);
    return ret;
}

// A bidirectional stream is a flow at both ends: the peer's end appears through NET_FlowOpen, the
// bytes arrive as NET_DataReceived, and netflowSend sends back on the same stream.
static int test_quicnettest_echo(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    static const uint8 payload[] = "the quick brown fox";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.cli.nstreams > 0 && bufLen(f.cli.streams[0].data) == sizeof(payload) - 1,
            QN_BUDGET);

    CHECK_U("server saw a stream open", f.srv.nOpened, 1);
    CHECK_U("client stream flows", htSize(f.cli.sock->flows), 1);
    CHECK_U("stream id", flow->key, 0);   // the client's first bidirectional stream

    CHECK_U("echoed bytes", bufLen(f.cli.streams[0].data), sizeof(payload) - 1);
    CHECK("echoed content",
          memcmp(f.cli.streams[0].data->data, payload, sizeof(payload) - 1) == 0);

    // The event says how much is readable, so a handler can size a buffer before calling
    // netquicRecv(). One event carried the whole payload in each direction.
    CHECK_U("bytes the server was told about", f.srv.announced, sizeof(payload) - 1);
    CHECK_U("bytes the client was told about", f.cli.announced, sizeof(payload) - 1);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Streams are independent: four of them carry four different payloads and each one's bytes come
// back on its own flow, which is the property that made QUIC worth building.
static int test_quicnettest_streams(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flows[4] = { 0 };
    uint8 payload[4][32];

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    for (int i = 0; i < 4; i++) {
        memset(payload[i], 'a' + i, sizeof(payload[i]));
        flows[i] = qnSend(&f, f.cli.sock, false, payload[i], sizeof(payload[i]));
        CHECK("open and send", flows[i] != NULL);
    }

    QN_WAIT(&f, qnAllGot(&f.cli, 4, sizeof(payload[0])), QN_BUDGET);

    CHECK_U("streams the server saw open", f.srv.nOpened, 4);
    CHECK_U("client stream flows", htSize(f.cli.sock->flows), 4);

    for (int i = 0; i < 4; i++) {
        QNStream* st = qnStream(&f.cli, flows[i]);
        CHECK("the client kept a record of every stream", st != NULL);
        CHECK_U("echoed bytes", bufLen(st->data), sizeof(payload[i]));
        CHECK("echoed content", memcmp(st->data->data, payload[i], sizeof(payload[i])) == 0);
        CHECK_U("stream id", st->id, (uint64)(i * 4));   // client bidirectional: 0, 4, 8, 12
    }

out:
    for (int i = 0; i < 4; i++)
        objRelease(&flows[i]);
    qnFixDestroy(&f);
    return ret;
}

// A unidirectional stream only goes one way, and saying so is the receiving end's job: the flow it
// gets can be read but never written.
static int test_quicnettest_uni(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 payload[] = "one way";
    flow = qnSend(&f, f.cli.sock, true, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.srv.nstreams > 0 && bufLen(f.srv.streams[0].data) == sizeof(payload) - 1,
            QN_BUDGET);

    CHECK_U("stream id", flow->key, 2);   // the client's first unidirectional stream
    CHECK_U("received bytes", bufLen(f.srv.streams[0].data), sizeof(payload) - 1);

    // The receiving end of a unidirectional stream has nothing to send with.
    CHECK_U("the server can write on it", netquicWritable(f.srv.streams[0].flow), 0);
    CHECK("a send on it is refused", !netflowSend(f.srv.streams[0].flow, payload, 4, 0));

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Finishing both halves ends the stream, and the flow's terminal event is what says so.
static int test_quicnettest_finish(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 payload[] = "last words";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    netquicFinish(flow);

    QN_WAIT(&f, f.srv.nstreams > 0 && f.srv.streams[0].fin, QN_BUDGET);
    CHECK("the server reached the end of the stream", f.srv.streams[0].fin);
    CHECK_U("the stream closed before the peer finished too", f.srv.nStreamClosed, 0);

    // The other direction is still open until the server ends it as well.
    netquicFinish(f.srv.streams[0].flow);

    QN_WAIT(&f, f.cli.nStreamClosed > 0 && f.srv.nStreamClosed > 0, QN_BUDGET);
    CHECK_U("client stream closed", f.cli.nStreamClosed, 1);
    CHECK_U("server stream closed", f.srv.nStreamClosed, 1);
    CHECK_U("client stream flows left", htSize(f.cli.sock->flows), 0);
    CHECK_U("server stream flows left", htSize(f.srv.sock->flows), 0);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// A reset stream reaches the peer as NET_Error carrying the QUIC code, which a plain flow has
// nowhere to put and QuicStream does.
static int test_quicnettest_reset(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 payload[] = "never mind";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.srv.nOpened > 0, QN_BUDGET);
    netquicReset(flow, 0x1234);

    QN_WAIT(&f, f.srv.nErrors > 0, QN_BUDGET);
    CHECK_U("errors the server saw", f.srv.nErrors, 1);
    CHECK("the error carried the code", f.srv.streams[0].errored);
    CHECK_U("error code", f.srv.streams[0].error, 0x1234);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// STOP_SENDING travels the other way: the receiver says it wants no more, and the sender hears it
// as an error on the stream it was writing to.
static int test_quicnettest_stopsending(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 payload[] = "go on then";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.srv.nstreams > 0 && bufLen(f.srv.streams[0].data) > 0, QN_BUDGET);
    netquicStopSending(f.srv.streams[0].flow, 0x99);

    QN_WAIT(&f, f.cli.nErrors > 0, QN_BUDGET);
    CHECK_U("errors the client saw", f.cli.nErrors, 1);
    CHECK_U("error code", f.cli.streams[0].error, 0x99);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// A transfer far larger than one packet, or one flow control window: it only completes because
// acknowledgements keep moving the window and the loss recovery underneath keeps the bytes in
// order.
static int test_quicnettest_bulk(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    const size_t total = 256 * 1024;
    uint8* payload     = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.drain = true;

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    size_t sent  = 0;
    int64 giveUp = clockTimer() + timeMS(QN_BUDGET);

    while (sent < total && clockTimer() < giveUp) {
        size_t room = netquicWritable(flow);
        size_t want = total - sent;
        if (room > want)
            room = want;

        if (room == 0) {
            qnTick(&f, timeMS(2));
            continue;
        }

        CHECK("send", netflowSend(flow, payload + sent, room, 0));
        sent += room;
    }

    CHECK_U("bytes handed to the stream", sent, total);

    netquicFinish(flow);
    QN_WAIT(&f, f.srv.drained == total, QN_BUDGET);

    CHECK_U("bytes that arrived", f.srv.drained, total);
    CHECK_U("streams the server saw", f.srv.nOpened, 1);

out:
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// The same bulk transfer with a receive buffer pool too small to hold the flight, so the pool runs
// dry mid-transfer. Running dry is ordinary -- the datagrams that find no buffer are dropped and
// QUIC retransmits them -- but a completion backend has to keep a receive posted through it. One
// that answers a completion without posting another loses that receive for good, and the socket
// goes deaf as soon as the last one is retired. That is invisible to a readiness backend, which is
// told about the same socket again on the very next poll.
static int test_quicnettest_bulkstarved(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    const size_t total = 64 * 1024;
    uint8* payload     = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    CHECK("connect", qnConnectPool(&f, NULL, 8));
    f.srv.drain = true;

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    size_t sent  = 0;
    int64 giveUp = clockTimer() + timeMS(QN_BUDGET);

    while (sent < total && clockTimer() < giveUp) {
        size_t room = netquicWritable(flow);
        size_t want = total - sent;
        if (room > want)
            room = want;

        if (room == 0) {
            qnTick(&f, timeMS(2));
            continue;
        }

        CHECK("send", netflowSend(flow, payload + sent, room, 0));
        sent += room;
    }

    CHECK_U("bytes handed to the stream", sent, total);

    netquicFinish(flow);
    QN_WAIT(&f, f.srv.drained == total, QN_BUDGET);

    CHECK_U("bytes that arrived", f.srv.drained, total);

out:
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// More than one send buffer's worth on one stream, driven only by NET_SendReady.
//
// The buffer holding data until it is acknowledged is a limit of its own, on top of every flow
// control one: fill it and nothing more may be handed over until an acknowledgement releases some.
// Nothing else raises a limit at that point, so if the acknowledgement does not also say there is
// room again, a sender that waits to be told -- which is what NET_SendReady is for -- waits
// forever.
static int test_quicnettest_sendwakeup(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    // Comfortably more than the send buffer holds, so the transfer cannot finish without the
    // buffer being emptied and refilled several times over.
    const size_t total = 768 * 1024;
    uint8* payload     = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    QuicConfig cfg    = { 0 };
    cfg.maxData       = 4 * 1024 * 1024;
    cfg.maxStreamData = 2 * 1024 * 1024;

    CHECK("connect", qnConnect(&f, &cfg));
    f.srv.drain = true;

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    f.cli.pumpFlow  = flow;
    f.cli.pumpData  = payload;
    f.cli.pumpTotal = total;
    qnPump(&f.cli);

    CHECK("the first push filled the send buffer", f.cli.pumpSent < total);

    QN_WAIT(&f, f.cli.pumpSent == total || f.cli.pumpRefused, QN_BUDGET);

    CHECK("netquicWritable() and netflowSend() agree", !f.cli.pumpRefused);
    CHECK_U("bytes handed to the stream", f.cli.pumpSent, total);

    netquicFinish(flow);
    QN_WAIT(&f, f.srv.drained == total, QN_BUDGET);
    CHECK_U("bytes that arrived", f.srv.drained, total);

out:
    f.cli.pumpFlow = NULL;
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// A sender that writes whole frames stops with room to spare, and has to be woken anyway.
//
// This is the shape of every framed protocol on top of QUIC: the unit is a header plus a payload,
// and a window with a few bytes left in it holds no such thing. Spending the window to exactly
// zero is not something the sender can choose to do, so "you reached zero" is not a signal it can
// wait on. What it can produce is a refused send, and that is what has to arm the wake-up.
static int test_quicnettest_sendframed(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    const size_t total = 128 * 1024;
    uint8* payload     = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    // A window several times smaller than the transfer, and a frame size that does not divide it.
    // Whatever the window happens to be when the sender runs out, the remainder is unusable and
    // nothing but the refusal records that the sender stopped.
    QuicConfig cfg    = { 0 };
    cfg.maxData       = 1024 * 1024;
    cfg.maxStreamData = 16 * 1024;

    CHECK("connect", qnConnect(&f, &cfg));
    f.srv.drain = true;

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    f.cli.pumpFlow  = flow;
    f.cli.pumpData  = payload;
    f.cli.pumpTotal = total;
    f.cli.pumpFrame = 3000;
    qnPump(&f.cli);

    CHECK("the first run filled the window", f.cli.pumpSent < total);
    CHECK("and was turned away short of it", f.cli.pumpRefusals > 0);
    CHECK("with room left it could not use", netquicWritable(flow) > 0);

    QN_WAIT(&f, f.cli.pumpSent == total, QN_BUDGET);
    CHECK_U("bytes handed to the stream", f.cli.pumpSent, total);

    // Each of those refusals had to be answered by a NET_SendReady or the transfer would have
    // stopped where it stood, which is what this is really testing.
    CHECK("it was refused more than once", f.cli.pumpRefusals > 1);
    CHECK("and woken every time", f.cli.nSendReady >= f.cli.pumpRefusals);

    netquicFinish(flow);
    QN_WAIT(&f, f.srv.drained == total, QN_BUDGET);
    CHECK_U("bytes that arrived", f.srv.drained, total);

out:
    f.cli.pumpFlow = NULL;
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// A framed sender is woken when it can write a frame, not when a byte comes free.
//
// The windows here are wide enough that what holds the sender back is the send buffer, which
// acknowledgements release a packet at a time. Waking on the first byte of that would wake the
// sender ten-odd times per frame, and every one of those rounds ends in a refused send: the room
// that came free is real but useless, because it is smaller than the frame waiting to go into it.
static int test_quicnettest_sendwatermark(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    const size_t frame  = 16 * 1024;
    const size_t total  = 768 * 1024;
    const uint32 frames = (uint32)(total / frame);

    uint8* payload = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    QuicConfig cfg    = { 0 };
    cfg.maxData       = 8 * 1024 * 1024;
    cfg.maxStreamData = 8 * 1024 * 1024;

    CHECK("connect", qnConnect(&f, &cfg));
    f.srv.drain = true;

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    f.cli.pumpFlow  = flow;
    f.cli.pumpData  = payload;
    f.cli.pumpTotal = total;
    f.cli.pumpFrame = frame;
    qnPump(&f.cli);

    CHECK("the first run filled the buffer", f.cli.pumpSent < total);

    QN_WAIT(&f, f.cli.pumpSent == total, QN_BUDGET);
    CHECK_U("bytes handed to the stream", f.cli.pumpSent, total);

    // The real measure is how many rounds it took. Every wake-up here is answered by a refusal, so
    // an early one buys nothing: it wakes the sender to tell it something it already knew. With
    // the watermark each round carries several frames, and waking on any free byte instead runs to
    // three times as many rounds for the same transfer.
    TEST_INFO(_S"wakeups=${uint} refusals=${uint} frames=${uint} minroom=${uint}",
              stvar(uint32, f.cli.nSendReady), stvar(uint32, f.cli.pumpRefusals),
              stvar(uint32, frames), stvar(uint32, (uint32)f.cli.pumpWakeRoomMin));
    CHECK("it was woken at all", f.cli.nSendReady > 0);
    CHECK("and each wake-up carried several frames", f.cli.nSendReady <= frames / 4);
    CHECK("never for less room than the watermark asks for", f.cli.pumpWakeRoomMin >= 64 * 1024);
    CHECK("with nothing woken that was not refused", f.cli.pumpRefusals <= f.cli.nSendReady + 1);

    netquicFinish(flow);
    QN_WAIT(&f, f.srv.drained == total, QN_BUDGET);
    CHECK_U("bytes that arrived", f.srv.drained, total);

out:
    f.cli.pumpFlow = NULL;
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// The same measurement with the accepted end as the sender.
//
// A socket has no watermarks of its own until it joins a queue and inherits that queue's, and an
// accepted socket joins later than a dialled one does. Reading them before that leaves every
// connection a listener accepts with no watermark at all -- and that is the wrong half to lose,
// since the end that answers is the end that sends the bytes.
static int test_quicnettest_sendwatermarksrv(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    // Frames well under the watermark, so that what decides the number of wake-ups is the
    // watermark and not the size of what the sender happened to ask for.
    const size_t frame  = 1024;
    const size_t total  = 768 * 1024;
    const uint32 frames = (uint32)(total / frame);

    uint8* payload = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    QuicConfig cfg    = { 0 };
    cfg.maxData       = 8 * 1024 * 1024;
    cfg.maxStreamData = 8 * 1024 * 1024;

    CHECK("connect", qnConnect(&f, &cfg));
    f.cli.drain = true;

    // A stream the peer has never heard of is one it cannot answer on, so it takes a byte from
    // this end before the other end has anything to send back.
    flow = netquicOpen(f.cli.sock, false);
    CHECK("open", flow != NULL);
    CHECK("the stream reached the far end", netflowSend(flow, (const uint8*)"?", 1, 0));

    QN_WAIT(&f, f.srv.nstreams > 0, QN_BUDGET);
    CHECK("the server saw the stream", f.srv.nstreams > 0);

    f.srv.pumpFlow  = f.srv.streams[0].flow;
    f.srv.pumpData  = payload;
    f.srv.pumpTotal = total;
    f.srv.pumpFrame = frame;
    qnPump(&f.srv);

    CHECK("the first run filled the buffer", f.srv.pumpSent < total);

    QN_WAIT(&f, f.srv.pumpSent == total, QN_BUDGET);
    CHECK_U("bytes handed to the stream", f.srv.pumpSent, total);

    TEST_INFO(_S"wakeups=${uint} refusals=${uint} frames=${uint} minroom=${uint}",
              stvar(uint32, f.srv.nSendReady), stvar(uint32, f.srv.pumpRefusals),
              stvar(uint32, frames), stvar(uint32, (uint32)f.srv.pumpWakeRoomMin));
    CHECK("it was woken at all", f.srv.nSendReady > 0);
    CHECK("and never for less room than the watermark asks for",
          f.srv.pumpWakeRoomMin >= 64 * 1024);

    netquicFinish(f.srv.pumpFlow);
    QN_WAIT(&f, f.cli.drained == total, QN_BUDGET);
    CHECK_U("bytes that arrived", f.cli.drained, total);

out:
    f.srv.pumpFlow = NULL;
    xaFree(payload);
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Several streams at once, each larger than the connection's flow control window allows in one
// go, echoed back by a server that only reads what it has room to send.
//
// This is the shape a real application has: both ends are limited by the other's window, both
// have to be woken to continue, and the end of each stream has to arrive even though it is named
// long after the data it follows. Anything that loses a wake-up, or an end of stream, stalls here
// and nowhere else -- one stream on its own never runs the connection window down far enough for
// the two ends to have to take turns.
static int test_quicnettest_echostreams(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flows[QN_MAXSTREAMS] = { 0 };
    size_t sent[QN_MAXSTREAMS]    = { 0 };
    bool finished[QN_MAXSTREAMS]  = { 0 };
    const uint32 nstreams         = QN_MAXSTREAMS;
    const size_t total            = 200 * 1024;

    uint8* payload = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        payload[i] = (uint8)(i * 31 + (i >> 8));

    QuicConfig cfg = { 0 };
    uint16 port    = 0;

    CHECK("fixture", qnFixInit(&f));

    // Each end gets a queue of its own, so the two run independently rather than taking strict
    // turns on one. Which end is inside the engine when a window opens is the whole question here.
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    f.q2 = netqueueCreate(&conf);
    CHECK("second queue", f.q2 != NULL);

    CHECK("listen", qnListen(&f, &cfg, &port));
    CHECK("dial", qnDial(&f, &cfg, port));
    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);
    CHECK("connected", f.cli.nConnected > 0 && f.srv.sock != NULL);

    f.srv.echoPump = true;

    for (uint32 i = 0; i < nstreams; i++) {
        flows[i] = netquicOpen(f.cli.sock, false);
        CHECK("open", flows[i] != NULL);
    }

    int64 giveUp = clockTimer() + timeMS(QN_BUDGET * 4);
    bool done    = false;

    while (!done && clockTimer() < giveUp) {
        for (uint32 i = 0; i < nstreams; i++) {
            while (sent[i] < total) {
                size_t room = netquicWritable(flows[i]);
                size_t want = total - sent[i];
                size_t n    = room < want ? room : want;
                if (n == 0)
                    break;

                CHECK("send", netflowSend(flows[i], payload + sent[i], n, 0));
                sent[i] += n;
            }

            if (sent[i] == total && !finished[i]) {
                netquicFinish(flows[i]);
                finished[i] = true;
            }
        }

        qnTick(&f, timeMS(2));

        done = qnAllGot(&f.cli, nstreams, total);
        for (uint32 i = 0; done && i < nstreams; i++)
            done = f.cli.streams[i].fin;
    }

    CHECK("the echo pump and netquicWritable() agree", !f.srv.pumpRefused);

    for (uint32 i = 0; i < nstreams; i++) {
        CHECK_U("bytes handed to the stream", sent[i], total);
        CHECK_U("bytes echoed back", bufLen(f.cli.streams[i].data), total);
        CHECK("the end of the stream came back", f.cli.streams[i].fin);
    }

out:
    for (uint32 i = 0; i < nstreams; i++)
        objRelease(&flows[i]);
    xaFree(payload);
    qnFixDestroy(&f);
    return ret;
}

// Receive flow control reaches all the way up: an application that does not read stops the peer
// from sending, and reading is what lets it go again.
static int test_quicnettest_backpressure(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    // Small enough that a couple of sends fill it, so the test does not depend on how large a
    // default window happens to be -- and not a divisor of it, so a send that took only part of
    // its payload would show up as fewer bytes arriving than the accepted sends accounted for.
    QuicConfig cfg     = { 0 };
    cfg.maxStreamData  = 4096;
    cfg.maxData        = 8192;

    CHECK("connect", qnConnect(&f, &cfg));
    f.srv.hold = true;   // the bytes stay in the stream, so the window stays shut

    uint8 chunk[700];
    memset(chunk, 'z', sizeof(chunk));

    flow = netquicOpen(f.cli.sock, true);
    CHECK("open", flow != NULL);

    // Fill the window. The first sends are taken; once the peer's limit is reached nothing more is.
    uint32 accepted  = 0;
    size_t sentBytes = 0;
    for (int i = 0; i < 32; i++) {
        if (netflowSend(flow, chunk, sizeof(chunk), 0)) {
            accepted++;
            sentBytes += sizeof(chunk);
        }
        qnTick(&f, 0);
    }

    CHECK("something was sent", accepted > 0);
    CHECK("the window did not stay open forever", accepted < 32);

    // A send is all or nothing, so the refusals started while there was still room -- just not a
    // whole chunk of it. That is what netquicWritable() is for: it says how much would fit.
    size_t tail = netquicWritable(flow);
    CHECK("less room left than one chunk", tail < sizeof(chunk));

    if (tail > 0) {
        CHECK("the rest of the window", netflowSend(flow, chunk, tail, 0));
        sentBytes += tail;
        qnTick(&f, 0);
    }

    CHECK_U("no room left", netquicWritable(flow), 0);

    QN_WAIT(&f, f.srv.nOpened > 0, QN_BUDGET);
    CHECK("the server has the stream", f.srv.nstreams > 0);

    // Read it out. That moves the receive window, the peer is told, and the send side opens up.
    uint8 buf[4096];
    bool fin  = false;
    size_t got = 0, n;
    int64 giveUp = clockTimer() + timeMS(QN_BUDGET);

    while (got < sentBytes && clockTimer() < giveUp) {
        n = netquicRecv(f.srv.streams[0].flow, buf, sizeof(buf), &fin);
        got += n;
        qnTick(&f, timeMS(2));
    }

    // Every byte a send reported taking has to be a byte that arrives.
    CHECK_U("bytes read out", got, sentBytes);

    QN_WAIT(&f, f.cli.nSendReady > 0, QN_BUDGET);
    CHECK("the client was told there is room again", f.cli.nSendReady > 0);
    CHECK("and there is", netquicWritable(flow) > 0);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Closing a connection with an application code puts it on the wire, and the peer reads it back
// off its own socket once the control flow's terminal event has arrived.
static int test_quicnettest_close(void)
{
    int ret = 0;
    QNFix f;

    CHECK("connect", qnConnect(&f, NULL));

    netquicClose(f.cli.sock, 0x4242, _S "that will do");

    QN_WAIT(&f, f.srv.connClosed, QN_BUDGET);
    CHECK("the server's connection closed", f.srv.connClosed);

    NetSocketQuic* sq = objDynCast(NetSocketQuic, f.srv.sock);
    CHECK("the accepted socket is a QUIC socket", sq != NULL);
    CHECK_U("close code", sq->closeError, 0x4242);
    CHECK("the code was an application one", sq->closeApp);
    CHECK("the reason came through", strEq(sq->closeReason, _S "that will do"));

    // The peer ended it, and the terminal event has to say so rather than reporting the generic
    // "your socket went away" a plain close would.
    CHECK_U("close reason", f.srv.connCloseReason, NCR_PeerClosed);

    // The listener stopped holding it: nothing routes to a connection that is gone.
    NetSocketQuic* lsn = objDynCast(NetSocketQuic, f.lsn);
    CHECK_U("routing entries left", htSize(lsn->cids), 0);

out:
    qnFixDestroy(&f);
    return ret;
}

// Closing the listener takes its connections with it, since they have no endpoint of their own to
// carry on with.
static int test_quicnettest_listener_close(void)
{
    int ret = 0;
    QNFix f;

    CHECK("connect", qnConnect(&f, NULL));

    netsocketClose(f.lsn);
    QN_WAIT(&f, f.srv.connClosed, QN_BUDGET);

    CHECK("the accepted connection closed", f.srv.connClosed);
    CHECK_U("the accepted socket is closed",
            atomicLoad(uint32, &f.srv.sock->state, Relaxed), NS_Closed);
    CHECK_U("close reason", f.srv.connCloseReason, NCR_AppClosed);
    CHECK("the listener's own flow closed", f.srv.lsnClosed);

    // And the client hears about it, because the listener sends a CONNECTION_CLOSE on the way out
    // rather than just vanishing.
    QN_WAIT(&f, f.cli.connClosed, QN_BUDGET);
    CHECK("the client's connection closed", f.cli.connClosed);
    CHECK_U("the client's close reason", f.cli.connCloseReason, NCR_PeerClosed);

out:
    qnFixDestroy(&f);
    return ret;
}

// A Retry costs one extra round trip and proves the client is really where it says it is. From the
// application's side nothing about the connection looks any different.
static int test_quicnettest_retry(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    QuicConfig cfg = { 0 };
    cfg.retry      = true;

    CHECK("connect", qnConnect(&f, &cfg));
    f.srv.echo = true;

    static const uint8 payload[] = "after the detour";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.cli.nstreams > 0 && bufLen(f.cli.streams[0].data) == sizeof(payload) - 1,
            QN_BUDGET);

    CHECK_U("client connections", f.cli.nConnected, 1);
    CHECK_U("echoed bytes", bufLen(f.cli.streams[0].data), sizeof(payload) - 1);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Nobody is listening on the port. The connect does not hang: the idle timeout ends it and the
// failure arrives as NET_Connection, exactly as it would for a TCP socket that was refused.
static int test_quicnettest_nolistener(void)
{
    int ret = 0;
    QNFix f;

    CHECK("fixture", qnFixInit(&f));

    QuicConfig cfg   = { 0 };
    cfg.tls          = f.ccfg;
    cfg.idleTimeout  = timeMS(300);

    // A listener bound and immediately closed leaves a port nothing is on, without guessing at a
    // number that might belong to something else on the machine.
    uint16 port = 0;
    CHECK("listen", qnListen(&f, &cfg, &port));
    netsocketClose(f.lsn);
    objRelease(&f.lsn);

    CHECK("dial", qnDial(&f, &cfg, port));

    QN_WAIT(&f, f.cli.nConnFailed > 0, QN_BUDGET);

    CHECK_U("connections that succeeded", f.cli.nConnected, 0);
    CHECK_U("connections that failed", f.cli.nConnFailed, 1);

out:
    qnFixDestroy(&f);
    return ret;
}

// A stream opened before the peer's limits are known cannot be, and neither can one past them.
static int test_quicnettest_streamlimit(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flows[3] = { 0 };

    QuicConfig cfg      = { 0 };
    cfg.maxStreamsBidi  = 2;
    cfg.maxStreamsUni   = 2;

    CHECK("connect", qnConnect(&f, &cfg));

    for (int i = 0; i < 3; i++)
        flows[i] = netquicOpen(f.cli.sock, false);

    CHECK("the first two opened", flows[0] != NULL && flows[1] != NULL);
    CHECK("the third was refused", flows[2] == NULL);
    CHECK_U("stream flows", htSize(f.cli.sock->flows), 2);

out:
    for (int i = 0; i < 3; i++)
        objRelease(&flows[i]);
    qnFixDestroy(&f);
    return ret;
}

// The application protocol the handshake settled on, read back off the socket at both ends.
static int test_quicnettest_alpn(void)
{
    int ret = 0;
    QNFix f;
    string proto = 0;

    CHECK("fixture", qnFixInit(&f));

    sa_string protos;
    saInit(&protos, string, 2);
    saPush(&protos, string, _S "cxq/1");
    tlsconfigSetALPN(f.ccfg, &protos);
    tlsconfigSetALPN(f.scfg, &protos);
    saDestroy(&protos);

    QuicConfig cfg = { 0 };
    uint16 port    = 0;
    CHECK("listen", qnListen(&f, &cfg, &port));
    CHECK("dial", qnDial(&f, &cfg, port));

    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);
    CHECK("connected", f.cli.nConnected > 0 && f.srv.sock != NULL);

    CHECK("the client negotiated a protocol", netquicALPN(f.cli.sock, &proto));
    CHECK("client protocol", strEq(proto, _S "cxq/1"));

    CHECK("the server negotiated a protocol", netquicALPN(f.srv.sock, &proto));
    CHECK("server protocol", strEq(proto, _S "cxq/1"));

out:
    strDestroy(&proto);
    qnFixDestroy(&f);
    return ret;
}

// Both ends open streams at once. A server-initiated stream is numbered differently and travels
// the other way, but is otherwise the same flow the client's is.
static int test_quicnettest_bothways(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* cflow = NULL;
    NetFlow* sflow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 fromClient[] = "client says";
    static const uint8 fromServer[] = "server says";

    cflow = qnSend(&f, f.cli.sock, false, fromClient, sizeof(fromClient) - 1);
    sflow = qnSend(&f, f.srv.sock, false, fromServer, sizeof(fromServer) - 1);
    CHECK("both opened", cflow != NULL && sflow != NULL);

    QN_WAIT(&f,
            qnStreamById(&f.srv, 0) && bufLen(qnStreamById(&f.srv, 0)->data) == sizeof(fromClient) - 1 &&
                qnStreamById(&f.cli, 1) && bufLen(qnStreamById(&f.cli, 1)->data) == sizeof(fromServer) - 1,
            QN_BUDGET);

    CHECK_U("client stream id", cflow->key, 0);   // client-initiated bidirectional
    CHECK_U("server stream id", sflow->key, 1);   // server-initiated bidirectional

    // Each end has both streams open, so which one is first in its list depends on the order the
    // opens landed in. Find them by id.
    QNStream* atServer = qnStreamById(&f.srv, 0);
    QNStream* atClient = qnStreamById(&f.cli, 1);
    CHECK("both ends see both streams", atServer != NULL && atClient != NULL);

    CHECK_U("bytes the server got", bufLen(atServer->data), sizeof(fromClient) - 1);
    CHECK_U("bytes the client got", bufLen(atClient->data), sizeof(fromServer) - 1);

out:
    objRelease(&cflow);
    objRelease(&sflow);
    qnFixDestroy(&f);
    return ret;
}

// Two connections on one listener share a UDP port and nothing else: each is routed by its own
// connection ID, and neither sees the other's streams.
static int test_quicnettest_two_clients(void)
{
    int ret = 0;
    QNFix f;
    QNSide cli2   = { 0 };
    NetSocket* c2 = NULL;
    NetFlow* f1   = NULL;
    NetFlow* f2   = NULL;

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    QuicConfig cfg = { 0 };
    cfg.tls        = f.ccfg;
    cli2.f         = &f;
    c2 = netquicConnect(f.q, _S "127.0.0.1", f.lsn->local.port, _S TLS_TEST_HOSTNAME, &cfg,
                        &qnHandlers, &cli2);
    cli2.sock = c2;
    CHECK("second dial", c2 != NULL);

    QN_WAIT(&f, cli2.nConnected > 0 && f.srv.nAccepted == 2, QN_BUDGET);
    CHECK_U("accepts", f.srv.nAccepted, 2);

    static const uint8 one[] = "first";
    static const uint8 two[] = "second";

    f1 = qnSend(&f, f.cli.sock, false, one, sizeof(one) - 1);
    f2 = qnSend(&f, c2, false, two, sizeof(two) - 1);
    CHECK("both sent", f1 != NULL && f2 != NULL);

    QN_WAIT(&f, f.cli.nstreams > 0 && cli2.nstreams > 0 &&
                    bufLen(f.cli.streams[0].data) == sizeof(one) - 1 &&
                    bufLen(cli2.streams[0].data) == sizeof(two) - 1,
            QN_BUDGET);

    CHECK_U("first client's echo", bufLen(f.cli.streams[0].data), sizeof(one) - 1);
    CHECK("first client's content", memcmp(f.cli.streams[0].data->data, one, sizeof(one) - 1) == 0);
    CHECK_U("second client's echo", bufLen(cli2.streams[0].data), sizeof(two) - 1);
    CHECK("second client's content", memcmp(cli2.streams[0].data->data, two, sizeof(two) - 1) == 0);

    // Each client's stream is its own flow on its own socket; neither has the other's.
    CHECK_U("first client's stream flows", htSize(f.cli.sock->flows), 1);
    CHECK_U("second client's stream flows", htSize(c2->flows), 1);

out:
    objRelease(&f1);
    objRelease(&f2);
    qnSideDestroy(&cli2);
    qnFixDestroy(&f);
    return ret;
}

// A client whose first packet goes unanswered sends it again, still addressed to the connection ID
// it invented, because it has not been told one yet. That retransmission has to reach the
// connection the first copy created -- routed anywhere else, it builds a second connection for the
// same client, and a single lost server flight turns into a handshake that never completes.
static int test_quicnettest_dup_initial(void)
{
    int ret = 0;
    QNFix f;

    QuicConfig cfg = { 0 };
    uint16 port    = 0;

    CHECK("fixture", qnFixInit(&f));

    // The client gets a queue of its own so it can be run while the listener's is left standing
    // still. Its first Initial then sits unread in the listener's socket buffer, which is exactly
    // what a lost packet looks like from the client's side.
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    f.q2 = netqueueCreate(&conf);
    CHECK("second queue", f.q2 != NULL);

    CHECK("listen", qnListen(&f, &cfg, &port));
    CHECK("dial", qnDial(&f, &cfg, port));

    // Long enough for the client's first probe timeout to expire and its Initial to go out again.
    int64 end = clockTimer() + timeMS(1500);
    while (clockTimer() < end)
        netqueueTick(f.q2, timeMS(5));

    // Now let the listener read both copies.
    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);
    CHECK("connected", f.cli.nConnected > 0 && f.srv.sock != NULL);

    CHECK_U("connections accepted", f.srv.nAccepted, 1);
    CHECK_U("connections the listener holds", qnListenerConns(f.lsn), 1);

out:
    qnFixDestroy(&f);
    return ret;
}

// RFC 9000 section 14.1: a client pads every datagram carrying an Initial out to at least 1200
// bytes, so that a server always has room to answer. One that is short did not come from a client
// that wanted an answer, and a server that built a connection for it anyway would be doing far more
// work than the sender did -- which is the whole shape of an amplification attack.
static int test_quicnettest_short_initial(void)
{
    int ret = 0;
    QNFix f;
    NetSocket* raw = NULL;

    QuicConfig cfg = { 0 };
    uint16 port    = 0;

    CHECK("fixture", qnFixInit(&f));
    CHECK("listen", qnListen(&f, &cfg, &port));

    // A well-formed Initial header with a hundred bytes behind it. Everything up to the length is
    // real, so it decodes; what it is not is padded.
    uint8 pkt[100];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0xc3;                                  // long header, Initial, 4-byte packet number
    pkt[1] = 0x00; pkt[2] = 0x00;
    pkt[3] = 0x00; pkt[4] = 0x01;                   // version 1
    pkt[5] = 0x08;                                  // destination connection id, 8 bytes
    memset(&pkt[6], 0x11, 8);
    pkt[14] = 0x00;                                 // no source connection id
    pkt[15] = 0x00;                                 // no token
    pkt[16] = 0x40; pkt[17] = 0x52;                 // length: 82 = the rest of the datagram

    NetAddr any = { .type = NA_IPv4 };
    NetAddr dst = qnLoopback(port);

    raw = netqueueSocket(f.q, NST_Datagram);
    CHECK("raw socket", raw != NULL);
    CHECK("bind", netsocketBind(raw, &any));
    CHECK("register", netqueueAddSocket(f.q, raw));
    CHECK("send", netsocketSend(raw, pkt, sizeof(pkt), &dst, 0));

    for (int i = 0; i < 40; i++)
        qnTick(&f, timeMS(2));

    CHECK_U("connections the listener built", qnListenerConns(f.lsn), 0);
    CHECK_U("accepts", f.srv.nAccepted, 0);

    // And it is still a working listener afterwards.
    CHECK("dial", qnDial(&f, &cfg, port));
    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);

    CHECK_U("client connections", f.cli.nConnected, 1);
    CHECK_U("connections the listener built", qnListenerConns(f.lsn), 1);

out:
    if (raw) {
        netsocketClose(raw);
        objRelease(&raw);
    }
    qnFixDestroy(&f);
    return ret;
}

// Closing a stream flow is immediate by contract: both directions end, and both ends get the
// terminal event without waiting for anything to be finished gracefully.
static int test_quicnettest_flowclose(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));

    static const uint8 payload[] = "half said";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.srv.nOpened > 0, QN_BUDGET);

    netflowClose(flow);

    QN_WAIT(&f, f.cli.nStreamClosed > 0 && f.srv.nStreamClosed > 0, QN_BUDGET);
    CHECK_U("client stream closed", f.cli.nStreamClosed, 1);
    CHECK_U("server stream closed", f.srv.nStreamClosed, 1);
    CHECK("the connection is still up", !f.cli.connClosed && !f.srv.connClosed);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

#endif   // _PLATFORM_WIN || _PLATFORM_UNIX || _PLATFORM_WASM

// Whether this platform can report what the IP layer carried with a datagram. Everything ECN does
// rests on it: an endpoint that cannot read the marks off arriving packets has nothing to count,
// and its peer finds that out and stops marking.
static bool qnReportsPktInfo(_Inout_ QNFix* f)
{
    NetSocket* s = netqueueSocket(f->q, NST_Datagram);
    if (!s)
        return false;

    NetAddr any = { .type = NA_IPv4 };
    bool ok     = netsocketBind(s, &any) && netsocketSetRecvInfo(s, true);

    netsocketClose(s);
    objRelease(&s);
    return ok;
}

// ECN end to end over a real socket: the mark is asked for on the way out, read back off the
// arriving datagram, counted, echoed in an acknowledgement, and checked against what was sent. Any
// break in that chain shows up here as marking being switched off.
static int test_quicnettest_ecn(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    bool reports = qnReportsPktInfo(&f);

    // Enough traffic in both directions for each end to have acknowledged marked packets, which is
    // the only thing that ever settles the question.
    static const uint8 payload[] = "marked on the way out";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);
    QN_WAIT(&f, f.cli.nstreams > 0 && bufLen(f.cli.streams[0].data) == sizeof(payload) - 1,
            QN_BUDGET);
    CHECK_U("echoed bytes", bufLen(f.cli.streams[0].data), sizeof(payload) - 1);

    if (reports) {
        // Loopback does not strip the marks, so validation has no reason to give up on them.
        CHECK("the client stopped marking on loopback", netquicEcn(f.cli.sock));
        CHECK("the server stopped marking on loopback", netquicEcn(f.srv.sock));
    } else {
        // The peer has nothing to count, so its acknowledgements say so and both ends stop.
        CHECK("marking survived on a platform that cannot report marks",
              !netquicEcn(f.cli.sock));
    }

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Path MTU discovery over a real socket. Loopback carries far more than QUIC will ever send, so
// the search runs all the way to the ceiling -- which is the case that proves the probes are being
// acknowledged rather than merely not answered.
static int test_quicnettest_pathmtu(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    // The search needs a few round trips, and nothing else here would produce them.
    QN_WAIT(&f, netquicPathMtu(f.cli.sock) > QUIC_MIN_INITIAL_LEN, QN_BUDGET);

    size_t mtu = netquicPathMtu(f.cli.sock);
    CHECK("the path was never measured past the base size", mtu > QUIC_MIN_INITIAL_LEN);

    // And the connection still works at whatever it settled on.
    static const uint8 payload[] = "after the search";
    flow = qnSend(&f, f.cli.sock, false, payload, sizeof(payload) - 1);
    CHECK("open and send", flow != NULL);

    QN_WAIT(&f, f.cli.nstreams > 0 && bufLen(f.cli.streams[0].data) == sizeof(payload) - 1,
            QN_BUDGET);
    CHECK_U("echoed bytes", bufLen(f.cli.streams[0].data), sizeof(payload) - 1);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// A client moves itself to a fresh local port in the middle of a connection. The socket the
// application holds does not change, the streams on it do not change, and the bytes keep moving --
// which is the whole point of a transport identified by a connection ID rather than by an address.
static int test_quicnettest_migrate(void)
{
    int ret = 0;
    QNFix f;
    NetFlow* flow = NULL;

    CHECK("connect", qnConnect(&f, NULL));
    f.srv.echo = true;

    static const uint8 before[] = "before the move";
    flow = qnSend(&f, f.cli.sock, false, before, sizeof(before) - 1);
    CHECK("open and send", flow != NULL);
    QN_WAIT(&f, f.cli.nstreams > 0 && bufLen(f.cli.streams[0].data) == sizeof(before) - 1,
            QN_BUDGET);
    CHECK_U("echoed bytes before the move", bufLen(f.cli.streams[0].data), sizeof(before) - 1);

    NetAddr wasAt = f.cli.sock->local;

    CHECK("the client could not migrate", netquicMigrate(f.cli.sock));
    CHECK_U("moves the client counted", netquicMigrations(f.cli.sock), 1);
    CHECK("the client kept the port it was on",
          f.cli.sock->local.port != 0 && f.cli.sock->local.port != wasAt.port);

    // The server has to follow, which it only does once a packet that is not merely probing the
    // path arrives from the new address.
    static const uint8 after[] = "after the move";
    CHECK("send after the move", netflowSend(flow, after, sizeof(after) - 1, 0));

    QN_WAIT(&f, bufLen(f.cli.streams[0].data) == sizeof(before) + sizeof(after) - 2 &&
                    netquicMigrations(f.srv.sock) > 0,
            QN_BUDGET);

    CHECK_U("moves the server followed", netquicMigrations(f.srv.sock), 1);
    CHECK_U("total echoed bytes", bufLen(f.cli.streams[0].data),
            sizeof(before) + sizeof(after) - 2);
    CHECK("echoed content across the move",
          memcmp(f.cli.streams[0].data->data, before, sizeof(before) - 1) == 0 &&
              memcmp(f.cli.streams[0].data->data + sizeof(before) - 1, after,
                     sizeof(after) - 1) == 0);

    // The stream is the same flow it always was; nothing below it reopened anything.
    CHECK_U("streams the server saw open", f.srv.nOpened, 1);
    CHECK("the connection closed across the move", !f.cli.connClosed && !f.srv.connClosed);

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// Lets go of the current connection at both ends and forgets everything that happened on it,
// leaving the queue, the listener and the two TLS configurations standing -- so the ticket the
// first handshake produced is still there for a second connection to offer back.
static void qnResumeReset(_Inout_ QNFix* f)
{
    // Run the connection on for a moment before letting go of it. The server issues its session
    // ticket after the handshake rather than during it, so a client that closed the instant it
    // was told it had connected would never receive the thing this whole exercise is about.
    for (int i = 0; i < 16; i++)
        qnTick(f, timeMS(2));

    if (f->cli.sock)
        netsocketClose(f->cli.sock);
    if (f->srv.sock)
        netsocketClose(f->srv.sock);

    for (int i = 0; i < 32; i++)
        qnTick(f, timeMS(2));

    qnSideDestroy(&f->cli);
    memset(&f->cli, 0, sizeof(f->cli));
    f->cli.f = f;

    qnSideDestroy(&f->srv);
    memset(&f->srv, 0, sizeof(f->srv));
    f->srv.f = f;
}

// 0-RTT over a real socket, end to end. The first connection is there only to be given a ticket;
// everything asserted is about the second, which sends its request in the same flight as its
// handshake and is handed to both applications a round trip before that handshake finishes.
static int test_quicnettest_earlydata(void)
{
    int ret       = 0;
    NetFlow* flow = NULL;
    QNFix f;
    QuicConfig cfg = { 0 };

    CHECK("fixture", qnFixInit(&f));

    CHECK("client resumption", tlsconfigSetResumption(f.ccfg, true, timeS(600)));
    CHECK("server resumption", tlsconfigSetResumption(f.scfg, true, timeS(600)));
    CHECK("client early data", tlsconfigSetEarlyData(f.ccfg, true));
    CHECK("server early data", tlsconfigSetEarlyData(f.scfg, true));

    uint16 port = 0;
    CHECK("listener", qnListen(&f, &cfg, &port));
    CHECK("first dial", qnDial(&f, &cfg, port));
    QN_WAIT(&f, f.cli.nConnected > 0 && f.srv.sock != NULL, QN_BUDGET);
    CHECK("first connection", f.cli.nConnected > 0 && f.srv.sock != NULL);

    // Nothing to resume the first time, so nothing is early and there is no edge to report.
    CHECK_U("no early data on a fresh connection", netquicEarlyData(f.cli.sock),
            QUIC_EARLY_None);
    CHECK_U("and no secured notification", f.cli.nSecured, 0);

    qnResumeReset(&f);

    f.srv.echo = true;

    CHECK("second dial", qnDial(&f, &cfg, port));
    QN_WAIT(&f, f.cli.nConnected > 0, QN_BUDGET);
    CHECK("second connection reported", f.cli.nConnected > 0);

    // The application is handed the socket while the handshake is still running, which is the
    // only way the round trip 0-RTT saves can actually be saved.
    CHECK_U("client is sending early", netquicEarlyData(f.cli.sock), QUIC_EARLY_Pending);
    CHECK_U("nothing has vouched for it yet", f.cli.nSecured, 0);

    static const uint8 req[] = "GET / early";
    flow = netquicOpen(f.cli.sock, false);
    CHECK("stream opened before the handshake finished", flow != NULL);
    CHECK("request queued", netflowSend(flow, req, sizeof(req) - 1, 0));

    QN_WAIT(&f, f.srv.sock != NULL && qnAllGot(&f.srv, 1, sizeof(req) - 1), QN_BUDGET);
    CHECK("server was handed the connection", f.srv.sock != NULL);
    CHECK("server got the request", qnAllGot(&f.srv, 1, sizeof(req) - 1));
    CHECK("and it is the right one",
          memcmp(f.srv.streams[0].data->data, req, sizeof(req) - 1) == 0);

    QN_WAIT(&f, f.cli.nSecured > 0 && f.srv.nSecured > 0, QN_BUDGET);

    CHECK_U("the client was told the replay window closed", f.cli.nSecured, 1);
    CHECK_U("so was the server", f.srv.nSecured, 1);
    CHECK_U("client's early data is vouched for", netquicEarlyData(f.cli.sock),
            QUIC_EARLY_Accepted);
    CHECK_U("server's early data is vouched for", netquicEarlyData(f.srv.sock),
            QUIC_EARLY_Accepted);

    // The connection is an ordinary one from here, echo and all.
    QN_WAIT(&f, qnAllGot(&f.cli, 1, sizeof(req) - 1), QN_BUDGET);
    CHECK("the echo came back", qnAllGot(&f.cli, 1, sizeof(req) - 1));

out:
    objRelease(&flow);
    qnFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Unreliable datagrams (RFC 9221)
// ---------------------------------------------------------------------------------------------

// One flow, whole payloads in and out, at several sizes including the largest the connection will
// take. Boundaries are the property under test: a stream would deliver the same bytes without them.
static int test_quicnettest_datagram(void)
{
    int ret = 0;
    QNFix f;

    QuicConfig cfg = { .datagrams = true };
    CHECK("connect", qnConnect(&f, &cfg));

    f.srv.dgEcho = true;

    f.cli.dgFlow = netquicOpenDatagram(f.cli.sock);
    CHECK("the datagram channel was not offered", f.cli.dgFlow != NULL);

    size_t max = netquicMaxDatagram(f.cli.sock);
    CHECK("no datagram size limit", max > 0);

    uint8* payload = xaAlloc(max);
    for (size_t i = 0; i < max; i++)
        payload[i] = (uint8)(i * 37 + 5);

    const size_t sizes[] = { 1, 64, 1000, 0 };   // the last is filled in with the limit
    uint32 sent = 0;

    for (uint32 i = 0; i < 4; i++) {
        size_t len = sizes[i] ? sizes[i] : max;
        if (len > max)
            continue;

        // One at a time: the connection holds one datagram waiting to go out, so a second send
        // before the first has left is refused by design.
        f.cli.ndgram = 0;
        bool ok      = netflowSend(f.cli.dgFlow, payload, len, 0);
        if (!ok)
            break;
        sent++;

        QN_WAIT(&f, f.cli.ndgram > 0, QN_BUDGET);

        if (f.cli.ndgram != 1 || f.cli.dgLast != len || !f.cli.dgData ||
            memcmp(f.cli.dgData->data, payload, len) != 0) {
            TEST_FAILV(ret, 1, _SL("datagram of ${int} bytes came back as ${int}"),
                       stvar(int64, (int64)len), stvar(int64, (int64)f.cli.dgLast));
            break;
        }
    }

    xaFree(payload);
    if (ret != 0)
        goto out;

    CHECK_U("datagrams sent", sent, 4);
    CHECK_U("datagrams the server saw", f.srv.ndgram, 4);

    // The channel is one flow, not one per datagram, and the stream calls do not reach it.
    CHECK("it was counted as a stream", f.cli.nOpened == 0 && f.srv.nOpened == 0);

    uint8 scratch[16];
    CHECK_U("netquicRecv on a datagram flow returned bytes",
            netquicRecv(f.cli.dgFlow, scratch, sizeof(scratch), NULL), 0);
    CHECK_U("netquicReadable on a datagram flow", netquicReadable(f.cli.dgFlow), 0);

    NetFlow* again = netquicOpenDatagram(f.cli.sock);
    bool same      = again == f.cli.dgFlow;
    objRelease(&again);
    CHECK("opening the channel twice made two flows", same);

out:
    qnFixDestroy(&f);
    return ret;
}

// The size limit is a real refusal, not advice.
static int test_quicnettest_datagram_size(void)
{
    int ret = 0;
    QNFix f;

    QuicConfig cfg = { .datagrams = true };
    CHECK("connect", qnConnect(&f, &cfg));

    f.cli.dgFlow = netquicOpenDatagram(f.cli.sock);
    CHECK("the datagram channel was not offered", f.cli.dgFlow != NULL);

    size_t max = netquicMaxDatagram(f.cli.sock);
    CHECK("no datagram size limit", max > 0);

    uint8* payload = xaAlloc(max + 1);
    memset(payload, 0x5a, max + 1);

    bool tooBig = netflowSend(f.cli.dgFlow, payload, max + 1, 0);
    bool exact  = netflowSend(f.cli.dgFlow, payload, max, 0);

    xaFree(payload);

    CHECK("a datagram one byte over the limit was accepted", !tooBig);
    CHECK("a datagram of exactly the limit was refused", exact);

    QN_WAIT(&f, f.srv.ndgram > 0, QN_BUDGET);
    CHECK_U("datagrams the server received", f.srv.ndgram, 1);
    CHECK_U("its length", f.srv.dgLast, max);

out:
    qnFixDestroy(&f);
    return ret;
}

// Both ends have to ask for it, and there is no channel at all if either did not.
static int test_quicnettest_datagram_unsupported(void)
{
    int ret = 0;
    QNFix f;

    CHECK("connect", qnConnect(&f, NULL));

    CHECK("a client that never asked for datagrams was given a channel",
          netquicOpenDatagram(f.cli.sock) == NULL);
    CHECK("a server that never asked for datagrams was given a channel",
          netquicOpenDatagram(f.srv.sock) == NULL);
    CHECK_U("its datagram size limit", netquicMaxDatagram(f.cli.sock), 0);
    CHECK_U("the server's", netquicMaxDatagram(f.srv.sock), 0);

out:
    qnFixDestroy(&f);
    return ret;
}

// A send with the congestion window full is refused rather than queued, and NET_SendReady is the
// edge that says to try again. Nothing is delivered here between sends, so the window fills and
// stays full until the test lets acknowledgements through.
static int test_quicnettest_datagram_blocked(void)
{
    int ret = 0;
    QNFix f;

    QuicConfig cfg = { .datagrams = true };
    CHECK("connect", qnConnect(&f, &cfg));

    f.cli.dgFlow = netquicOpenDatagram(f.cli.sock);
    CHECK("the datagram channel was not offered", f.cli.dgFlow != NULL);

    size_t max = netquicMaxDatagram(f.cli.sock);
    CHECK("no datagram size limit", max > 0);

    uint8* payload = xaAlloc(max);
    memset(payload, 0xa5, max);

    uint32 accepted = 0;
    bool refused    = false;

    for (uint32 i = 0; i < 4000; i++) {
        if (!netflowSend(f.cli.dgFlow, payload, max, 0)) {
            refused = true;
            break;
        }
        accepted++;
    }

    xaFree(payload);

    CHECK("the congestion window never filled", refused);
    CHECK("nothing was accepted at all", accepted > 0);

    // The refusal is answered: once acknowledgements come back the window reopens, the datagram
    // that was waiting goes out, and the flow says so.
    QN_WAIT(&f, f.cli.nDgSendReady > 0, QN_BUDGET);
    CHECK("no NET_SendReady followed the refusal", f.cli.nDgSendReady > 0);

    CHECK("nothing arrived at all", f.srv.ndgram > 0);
    CHECK("the connection did not survive", f.cli.connClosed == false);

out:
    qnFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The accept gate
//
// A threaded queue, because that is the only place the ordering this proves can go wrong. Every
// other test here runs polled, where one thread does everything in turn and an accepted socket is
// always introduced before anything of its own is looked at.
// ---------------------------------------------------------------------------------------------

typedef struct QNGate {
    Semaphore done;

    atomic(uint32) seq;   // ticket dispenser: two different workers take from it
    uint32 acceptAt;     // the ticket NET_Accepted took
    uint32 flowOpenAt;   // the one the peer's first stream took
    uint32 nFlowOpen;
    size_t got;
} QNGate;

static void qgOnFlowOpen(_Inout_ NetEvent* ev)
{
    QNGate* g = (QNGate*)ev->ctx;

    if (g->nFlowOpen++ == 0)
        g->flowOpenAt = atomicFetchAdd(uint32, &g->seq, 1, AcqRel) + 1;
}

static void qgOnRecv(_Inout_ NetEvent* ev)
{
    QNGate* g = (QNGate*)ev->ctx;

    uint8 buf[1024];
    bool fin = false;

    for (;;) {
        // Each call writes the flag, so it has to be collected rather than read at the end: the
        // one that returns nothing would otherwise clear what the one before it reported.
        bool atEnd = false;
        size_t n   = netquicRecv(ev->flow, buf, sizeof(buf), &atEnd);
        fin        = fin || atEnd;
        if (n == 0)
            break;
        g->got += n;
    }

    if (fin)
        semaInc(&g->done, 1);
}

// What the application swaps in when it is handed the connection. The listener's own table has
// none of this, which is the whole point: an event that arrives before the accept has nowhere to
// go, and is gone for good.
static const NetHandlers qgConnHandlers = {
    .flowOpen = qgOnFlowOpen,
    .recv     = qgOnRecv,
};

static void qgOnAccepted(_Inout_ NetEvent* ev)
{
    QNGate* g = (QNGate*)ev->ctx;

    g->acceptAt = atomicFetchAdd(uint32, &g->seq, 1, AcqRel) + 1;

    // An application does something here -- looks up a policy, allocates its session state, writes
    // a line to a log -- and the peer's first stream does not wait for it. The pause only widens a
    // window that is there anyway: the accepted socket's flows run on other workers, and without
    // the gate the stream is announced to the handlers installed below before they are installed.
    osSleep(timeMS(100));

    netsocketSetHandlers(ev->accept.newSocket, &qgConnHandlers, g);
}

static const NetHandlers qgListenHandlers = {
    .accepted = qgOnAccepted,
};

static void qgOnConnect(_Inout_ NetEvent* ev)
{
    if (ev->conn.err != NERR_None)
        return;

    // Immediately, from the worker that delivered the connection: this is the moment that makes
    // the race, because the peer's accept is at that instant only queued.
    static const uint8 payload[64] = { 0 };
    NetFlow* flow                  = netquicOpen(ev->socket, false);
    if (flow) {
        netflowSend(flow, payload, sizeof(payload), 0);
        netquicFinish(flow);
        objRelease(&flow);
    }
}

static const NetHandlers qgDialHandlers = {
    .connection = qgOnConnect,
};

// A stream opened the instant the handshake finishes must still be announced to the handlers the
// application installs when it is given the connection -- not to the listener's, which it inherited
// and which knows nothing about streams.
static int test_quicnettest_acceptorder(void)
{
    int ret     = 0;
    QNGate g    = { 0 };
    NetQueue* q = NULL;
    NetSocket* lsn = NULL;
    NetSocket* cli = NULL;

    TlsTestPKI pki  = { 0 };
    TlsCAStore* ca  = NULL;
    TlsCreds* creds = NULL;
    TlsConfig* ccfg = NULL;
    TlsConfig* scfg = NULL;

    semaInit(&g.done, 0);

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;   // 1 ingest thread + 2 dispatch workers; nothing here calls netqueueTick()
    q             = netqueueCreate(&conf);
    CHECK("the queue would not start", q != NULL);

    CHECK("no test PKI", tlsTestPKIInit(&pki));
    ca = tlscastoreCreate();
    CHECK("no CA store", ca && tlscastoreAddPEM(ca, pki.caCert));
    creds = tlscredsCreatePEM(pki.serverCert, pki.serverKey, NULL);
    CHECK("no credentials", creds != NULL);

    ccfg = tlsconfigCreateClient();
    scfg = tlsconfigCreateServer(creds);
    CHECK("no TLS configuration", ccfg && scfg);
    tlsconfigSetCA(ccfg, ca);

    QuicConfig scfgq = { 0 };
    scfgq.tls        = scfg;

    NetAddr addr = qnLoopback(0);
    lsn          = netquicListen(q, &addr, &scfgq, &qgListenHandlers, &g);
    CHECK("the listener would not start", lsn != NULL);

    QuicConfig ccfgq = { 0 };
    ccfgq.tls        = ccfg;

    cli = netquicConnect(q, _S "127.0.0.1", lsn->local.port, _S TLS_TEST_HOSTNAME, &ccfgq,
                         &qgDialHandlers, &g);
    CHECK("the dial would not start", cli != NULL);

    if (!semaTryDecTimeout(&g.done, timeS(5))) {
        TEST_FAILV(ret, 1, _SL("stream never arrived: acceptAt=${int} flowOpen=${int} got=${int}"),
                   stvar(int64, (int64)g.acceptAt), stvar(int64, (int64)g.nFlowOpen),
                   stvar(int64, (int64)g.got));
        goto out;
    }
    CHECK_U("wrong number of bytes", g.got, 64);
    CHECK("the stream was never announced", g.nFlowOpen > 0);
    CHECK("the stream was announced before the connection it is on",
          g.acceptAt != 0 && g.acceptAt < g.flowOpenAt);

out:
    if (q)
        netqueueShutdown(q, timeS(2));

    objRelease(&cli);
    objRelease(&lsn);
    objRelease(&q);
    objRelease(&ccfg);
    objRelease(&scfg);
    objRelease(&creds);
    objRelease(&ca);
    tlsTestPKIDestroy(&pki);
    semaDestroy(&g.done);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The same rule, for the connection's own control flow
//
// The accept gate lets a QUIC connection's control flow run while the gate is shut, because that
// flow is the transport: it answers the handshake that eventually produces the accept, and holding
// it would stall the connection short of its own introduction. What it may not do is deliver an
// application event through that exemption.
//
// 0-RTT is where that matters. The server is handed the connection a round trip early, so the
// notification that says what arrived early was not a replay is raised afterwards -- on the control
// flow, while the accept for the same socket may still be sitting on the listener's.
// ---------------------------------------------------------------------------------------------

typedef struct QNSecured {
    Semaphore done;
    Semaphore connected;

    atomic(uint32) seq;      // ticket dispenser: two different workers take from it
    uint32 acceptAt;         // the ticket NET_Accepted took
    uint32 securedAt;        // the one NFN_Secured took
    uint32 nSecured;
    uint32 early;            // what the client made of 0-RTT on the second connection

    atomic(ptr) accepted;    // the server's side of the second connection
} QNSecured;

static void qsOnFilterNotify(_Inout_ NetEvent* ev)
{
    QNSecured* g = (QNSecured*)ev->ctx;
    if (ev->filter.notify != NFN_Secured)
        return;

    g->securedAt = atomicFetchAdd(uint32, &g->seq, 1, AcqRel) + 1;
    g->nSecured++;
    semaInc(&g->done, 1);
}

// What the application swaps in when it is handed the connection. The listener's own table has no
// filterNotify, which is the whole point: a notification that arrives before the accept has
// nowhere to go, and is gone for good.
static const NetHandlers qsConnHandlers = {
    .filterNotify = qsOnFilterNotify,
};

static void qsOnAccepted(_Inout_ NetEvent* ev)
{
    QNSecured* g = (QNSecured*)ev->ctx;

    g->acceptAt = atomicFetchAdd(uint32, &g->seq, 1, AcqRel) + 1;

    NetSocket* prev = (NetSocket*)atomicExchange(ptr, &g->accepted,
                                                 objAcquire(ev->accept.newSocket), AcqRel);
    objRelease(&prev);

    // The handshake finishes while this is still running, which is what makes the race: the
    // notification is raised on a flow of this socket's own, on another worker, before the
    // handlers below exist.
    osSleep(timeMS(100));

    netsocketSetHandlers(ev->accept.newSocket, &qsConnHandlers, g);
}

static const NetHandlers qsListenHandlers = {
    .accepted = qsOnAccepted,
};

static void qsOnConnect(_Inout_ NetEvent* ev)
{
    QNSecured* g = (QNSecured*)ev->ctx;

    if (ev->conn.err == NERR_None) {
        g->early = netquicEarlyData(ev->socket);
        semaInc(&g->connected, 1);
    }
}

static const NetHandlers qsDialHandlers = {
    .connection = qsOnConnect,
};

// A 0-RTT connection's NFN_Secured belongs to the application, so it waits for the accept that
// gives the application somewhere to put it -- even though it travels on the one flow the gate
// lets run.
static int test_quicnettest_securedorder(void)
{
    int ret        = 0;
    QNSecured g    = { 0 };
    NetQueue* q    = NULL;
    NetSocket* lsn = NULL;
    NetSocket* cli = NULL;

    TlsTestPKI pki  = { 0 };
    TlsCAStore* ca  = NULL;
    TlsCreds* creds = NULL;
    TlsConfig* ccfg = NULL;
    TlsConfig* scfg = NULL;

    semaInit(&g.done, 0);
    semaInit(&g.connected, 0);

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;   // nothing here calls netqueueTick(); the ordering needs real workers
    q             = netqueueCreate(&conf);
    CHECK("the queue would not start", q != NULL);

    CHECK("no test PKI", tlsTestPKIInit(&pki));
    ca = tlscastoreCreate();
    CHECK("no CA store", ca && tlscastoreAddPEM(ca, pki.caCert));
    creds = tlscredsCreatePEM(pki.serverCert, pki.serverKey, NULL);
    CHECK("no credentials", creds != NULL);

    ccfg = tlsconfigCreateClient();
    scfg = tlsconfigCreateServer(creds);
    CHECK("no TLS configuration", ccfg && scfg);
    tlsconfigSetCA(ccfg, ca);

    CHECK("client resumption", tlsconfigSetResumption(ccfg, true, timeS(600)));
    CHECK("server resumption", tlsconfigSetResumption(scfg, true, timeS(600)));
    CHECK("client early data", tlsconfigSetEarlyData(ccfg, true));
    CHECK("server early data", tlsconfigSetEarlyData(scfg, true));

    QuicConfig scfgq = { 0 };
    scfgq.tls        = scfg;

    NetAddr addr = qnLoopback(0);
    lsn          = netquicListen(q, &addr, &scfgq, &qsListenHandlers, &g);
    CHECK("the listener would not start", lsn != NULL);

    QuicConfig ccfgq = { 0 };
    ccfgq.tls        = ccfg;

    // The first connection exists only to be given a session ticket, which the server issues after
    // its handshake rather than during it -- so it has to be left running for a moment before it is
    // let go of.
    cli = netquicConnect(q, _S "127.0.0.1", lsn->local.port, _S TLS_TEST_HOSTNAME, &ccfgq,
                         &qsDialHandlers, &g);
    CHECK("the first dial would not start", cli != NULL);
    CHECK("the first connection never came up", semaTryDecTimeout(&g.connected, timeS(5)));
    osSleep(timeMS(200));

    {
        NetSocket* srv = (NetSocket*)atomicExchange(ptr, &g.accepted, NULL, AcqRel);
        if (srv) {
            netsocketClose(srv);
            objRelease(&srv);
        }
    }
    netsocketClose(cli);
    objRelease(&cli);
    osSleep(timeMS(200));

    g.acceptAt = 0;

    // The second one offers the ticket back and sends in the same flight, so both ends are handed
    // it a round trip before the handshake finishes.
    cli = netquicConnect(q, _S "127.0.0.1", lsn->local.port, _S TLS_TEST_HOSTNAME, &ccfgq,
                         &qsDialHandlers, &g);
    CHECK("the second dial would not start", cli != NULL);
    CHECK("the second connection never came up", semaTryDecTimeout(&g.connected, timeS(5)));

    // Without this the rest proves nothing: a resumption that did not take is an ordinary
    // connection, and an ordinary connection raises no NFN_Secured to be ordered against anything.
    CHECK_U("0-RTT did not happen", g.early, QUIC_EARLY_Pending);

    if (!semaTryDecTimeout(&g.done, timeS(10))) {
        TEST_FAILV(ret, 1,
                   _SL("the server was never told the replay window closed: acceptAt=${int}"),
                   stvar(int64, (int64)g.acceptAt));
        goto out;
    }

    CHECK_U("more than one notification", g.nSecured, 1);
    CHECK("the connection was never accepted", g.acceptAt != 0);
    CHECK("the notification arrived before the connection it is about",
          g.acceptAt < g.securedAt);

out:
    if (q)
        netqueueShutdown(q, timeS(2));

    {
        NetSocket* srv = (NetSocket*)atomicExchange(ptr, &g.accepted, NULL, AcqRel);
        objRelease(&srv);
    }
    objRelease(&cli);
    objRelease(&lsn);
    objRelease(&q);
    objRelease(&ccfg);
    objRelease(&scfg);
    objRelease(&creds);
    objRelease(&ca);
    tlsTestPKIDestroy(&pki);
    semaDestroy(&g.connected);
    semaDestroy(&g.done);
    return ret;
}

// Each group below runs several of the subtests above in one process, so ctest spends one
// process launch per feature area instead of one per subtest. The individual subtests stay
// registered under their own names too, for running or debugging one in isolation.
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)

int test_quicnettest_grp_handshake(void)
{
    TEST_CHAIN(test_quicnettest_handshake, test_quicnettest_echo, test_quicnettest_streams,
               test_quicnettest_uni, test_quicnettest_bothways, test_quicnettest_finish);
}

int test_quicnettest_grp_flow(void)
{
    TEST_CHAIN(test_quicnettest_flowclose, test_quicnettest_reset, test_quicnettest_stopsending,
               test_quicnettest_streamlimit);
}

int test_quicnettest_grp_bulk(void)
{
    TEST_CHAIN(test_quicnettest_bulk, test_quicnettest_bulkstarved, test_quicnettest_sendwakeup,
               test_quicnettest_sendframed, test_quicnettest_sendwatermark,
               test_quicnettest_sendwatermarksrv, test_quicnettest_echostreams,
               test_quicnettest_backpressure);
}

int test_quicnettest_grp_lifecycle(void)
{
    TEST_CHAIN(test_quicnettest_close, test_quicnettest_listener_close, test_quicnettest_retry,
               test_quicnettest_nolistener, test_quicnettest_alpn, test_quicnettest_two_clients,
               test_quicnettest_dup_initial, test_quicnettest_short_initial);
}

int test_quicnettest_grp_path(void)
{
    TEST_CHAIN(test_quicnettest_ecn, test_quicnettest_pathmtu, test_quicnettest_migrate,
               test_quicnettest_earlydata);
}

int test_quicnettest_grp_datagram(void)
{
    TEST_CHAIN(test_quicnettest_datagram, test_quicnettest_datagram_size,
               test_quicnettest_datagram_unsupported, test_quicnettest_datagram_blocked);
}

int test_quicnettest_grp_accept(void)
{
    TEST_CHAIN(test_quicnettest_acceptorder, test_quicnettest_securedorder);
}

#endif

testfunc quicnettest_funcs[] = {
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)
    { "handshake",      test_quicnettest_handshake      },
    { "echo",           test_quicnettest_echo           },
    { "streams",        test_quicnettest_streams        },
    { "uni",            test_quicnettest_uni            },
    { "bothways",       test_quicnettest_bothways       },
    { "finish",         test_quicnettest_finish         },
    { "flowclose",      test_quicnettest_flowclose      },
    { "reset",          test_quicnettest_reset          },
    { "stopsending",    test_quicnettest_stopsending    },
    { "bulk",           test_quicnettest_bulk           },
    { "bulkstarved",    test_quicnettest_bulkstarved    },
    { "sendwakeup",     test_quicnettest_sendwakeup     },
    { "sendframed",     test_quicnettest_sendframed     },
    { "sendwatermark",  test_quicnettest_sendwatermark  },
    { "sendwatermarksrv", test_quicnettest_sendwatermarksrv },
    { "echostreams",    test_quicnettest_echostreams    },
    { "backpressure",   test_quicnettest_backpressure   },
    { "streamlimit",    test_quicnettest_streamlimit    },
    { "close",          test_quicnettest_close          },
    { "listener_close", test_quicnettest_listener_close },
    { "retry",          test_quicnettest_retry          },
    { "nolistener",     test_quicnettest_nolistener     },
    { "alpn",           test_quicnettest_alpn           },
    { "two_clients",    test_quicnettest_two_clients    },
    { "dup_initial",    test_quicnettest_dup_initial    },
    { "short_initial",  test_quicnettest_short_initial  },
    { "ecn",            test_quicnettest_ecn             },
    { "pathmtu",        test_quicnettest_pathmtu         },
    { "migrate",        test_quicnettest_migrate         },
    { "earlydata",      test_quicnettest_earlydata       },
    { "datagram",             test_quicnettest_datagram             },
    { "datagram_size",        test_quicnettest_datagram_size        },
    { "datagram_unsupported", test_quicnettest_datagram_unsupported },
    { "datagram_blocked",     test_quicnettest_datagram_blocked     },
    { "acceptorder",          test_quicnettest_acceptorder          },
    { "securedorder",         test_quicnettest_securedorder         },
    { "grp_handshake",  test_quicnettest_grp_handshake   },
    { "grp_flow",       test_quicnettest_grp_flow        },
    { "grp_bulk",       test_quicnettest_grp_bulk        },
    { "grp_lifecycle",  test_quicnettest_grp_lifecycle   },
    { "grp_path",       test_quicnettest_grp_path        },
    { "grp_datagram",   test_quicnettest_grp_datagram    },
    { "grp_accept",     test_quicnettest_grp_accept      },
#endif
    { NULL,             NULL                            }
};

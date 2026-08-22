// End-to-end tests for the cxtls filters.
//
// Everything here runs real TLS over real loopback sockets, driven by a polled NetQueue so the
// whole conversation happens on this thread and each step can be asserted in order. The PKI is
// minted at startup (see tlstestcert.c), so there is nothing checked in to expire.

#include <cxtls.h>
#include <cxtls_mbed.h>

#include <cx/net.h>
#include <cx/net/net_private.h>
#include <cx/time/clock.h>

#include "netfilterobj.h"
#include "tlstestcert.h"

// The listener binds to port 0 and the OS picks one; nothing in the cx socket API reports it back,
// so it is read off the handle the same way tests/nettest.c does.
#if defined(_WIN32)
#include <cx/platform/win.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#define TEST_FILE  tlstest
#define TEST_FUNCS tlstest_funcs
#include "common.h"

// How many ticks a step gets before it is called a failure. Loopback TLS handshakes settle in a
// handful; this is generous enough that a loaded CI machine does not flake, and small enough that a
// genuinely stuck test still finishes.
#define MAX_TICKS 400
#define TICK_WAIT 20

// The client's receive buffer in these tests. Only the small transfers are compared byte for byte;
// the bulk test checks a running total and a checksum instead.
#define PEERBUF 8192

// ---------------------------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------------------------

// The port a socket bound to. NetSocket::local is not filled in by bind, so this reads the OS
// handle directly, as tests/nettest.c does for the same reason.
static uint16 boundPort(NetSocket* sock)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));

    if (getsockname((int)sock->handle, (struct sockaddr*)&sa, &len) != 0)
        return 0;

    return ntohs(sa.sin_port);
}

typedef struct Peer {
    int32 secured;      // NFN_Secured notifications seen
    int32 recvEvents;   // NET_DataReceived events seen
    int32 closed;       // NET_FlowClosed events seen
    int32 errors;       // NET_Error events seen
    NetCloseReason closeReason;

    size_t got;           // total decoded bytes received
    uint32 sum;           // running checksum of everything received
    uint8 buf[PEERBUF];   // the first PEERBUF bytes, for byte-exact comparison
    size_t buflen;

    TlsInfo info;   // snapshot taken at NFN_Secured
    bool haveInfo;
} Peer;

typedef struct TestCtx {
    Peer client;
    Peer server;

    NetSocket* clientSock;   // identifies which peer an event belongs to
    NetSocket* serverSock;   // the accepted connection, acquired in onAccepted
    NetQueue* q;
} TestCtx;

static Peer* peerFor(NetEvent* ev)
{
    TestCtx* t = (TestCtx*)ev->ctx;
    return ev->socket == t->clientSock ? &t->client : &t->server;
}

static void onNotify(NetEvent* ev)
{
    if (ev->filter.notify != NFN_Secured)
        return;

    Peer* p = peerFor(ev);
    p->secured++;

    // Taken here on purpose: NFN_Secured is the documented point at which the snapshot exists, and
    // a test that reads it anywhere else would not be testing the contract.
    if (!p->haveInfo)
        p->haveInfo = nettlsFlowInfo(ev->flow, &p->info);
}

static void onRecv(NetEvent* ev)
{
    Peer* p = peerFor(ev);
    p->recvEvents++;

    uint8 tmp[4096];
    size_t n;
    while ((n = netsocketRecv(ev->socket, tmp, sizeof(tmp), NULL, 0)) > 0) {
        for (size_t i = 0; i < n; i++) p->sum = p->sum * 31 + tmp[i];

        size_t room = PEERBUF - p->buflen;
        if (room > 0) {
            size_t take = min(room, n);
            memcpy(p->buf + p->buflen, tmp, take);
            p->buflen += take;
        }
        p->got += n;
    }
}

static void onClosed(NetEvent* ev)
{
    Peer* p = peerFor(ev);
    p->closed++;
    p->closeReason = ev->closed.reason;
}

static void onError(NetEvent* ev)
{
    peerFor(ev)->errors++;
}

static void onAccepted(NetEvent* ev)
{
    TestCtx* t = (TestCtx*)ev->ctx;

    // The queue runs with NQ_AutoAccept, so the socket is already registered and being serviced --
    // and already filtered, since it inherited the listener's chain before it became reachable.
    // All this does is keep a reference so the test can send on it.
    if (!t->serverSock)
        t->serverSock = objAcquire(ev->accept.newSocket);
}

// One handler set for everything, with the peer picked per event. Registering queue-wide rather
// than per socket avoids depending on the NET_Accepted handler having run before the accepted
// socket's first event -- which is exactly the ordering the accept-inheritance change stopped
// requiring, and a test should not quietly reintroduce the dependency.
static const NetHandlers testHandlers = {
    .filterNotify = onNotify,
    .accepted     = onAccepted,
    .recv         = onRecv,
    .flowClosed   = onClosed,
    .error        = onError,
};

static void peerDestroy(Peer* p)
{
    if (p->haveInfo)
        nettlsInfoDestroy(&p->info);
}

// Tear down a secondary context: the tests that open a second connection get their own TestCtx so
// its events do not land on the first one's counters, which means they also own the sockets that
// connection produced -- including the accepted one that arrived through onAccepted.
static void ctxDestroy(TestCtx* t)
{
    if (t->clientSock) {
        netsocketClose(t->clientSock);
        objRelease(&t->clientSock);
    }
    if (t->serverSock) {
        netsocketClose(t->serverSock);
        objRelease(&t->serverSock);
    }

    peerDestroy(&t->client);
    peerDestroy(&t->server);
}

// Tick the queue until `cond` holds, and fail the test if it never does. `ret` is expected in
// scope: every test here uses it as its accumulated result, and a step that times out has failed by
// definition, so there is nothing for a caller to decide.
#define pumpUntil(qq, cond)                                                              \
    do {                                                                                 \
        for (int _i = 0; _i < MAX_TICKS && !(cond); _i++) netqueueTick((qq), TICK_WAIT); \
        if (!(cond))                                                                     \
            ret = 1;                                                                     \
    } while (0)

// A few extra ticks with nothing to wait for, to let anything already in flight land.
static void pumpIdle(TestCtx* t, int n)
{
    for (int i = 0; i < n; i++) netqueueTick(t->q, TICK_WAIT);
}

// ---------------------------------------------------------------------------------------------
// Fixture: a polled queue, a TLS listener on loopback, and a TLS client connected to it
// ---------------------------------------------------------------------------------------------

typedef struct Fixture {
    TestCtx t;
    TlsTestPKI pki;

    TlsCAStore* ca;
    TlsCreds* creds;
    TlsConfig* clientCfg;
    TlsConfig* serverCfg;

    NetSocket* listener;
    uint16 port;
} Fixture;

// Build the PKI, a trust store holding its CA, and credentials for the server leaf. The two
// configs are left to the caller, which is what lets each test vary the policy under test.
static bool fixtureBase(Fixture* f)
{
    memset(f, 0, sizeof(*f));

    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert) || tlscastoreCount(f->ca) != 1)
        return false;

    f->creds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    return f->creds != NULL;
}

// Stand the listener up on an ephemeral loopback port and record which port that turned out to be.
static bool fixtureListen(Fixture* f)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);   // polled: nthreads 0, so every event lands on this thread
    conf.flags |= NQ_AutoAccept;

    f->t.q = netqueueCreate(&conf);
    if (!f->t.q)
        return false;

    netqueueSetHandlers(f->t.q, &testHandlers, &f->t);

    NetAddr la;
    netAddrFromStr(&la, _SL("127.0.0.1"));
    la.port = 0;

    f->listener = nettlsListen(f->t.q, &la, 4, f->serverCfg, NULL, NULL);
    if (!f->listener)
        return false;

    f->port = boundPort(f->listener);
    return f->port != 0;
}

static bool fixtureConnect(Fixture* f, strref hostname)
{
    f->t.clientSock =
        nettlsConnect(f->t.q, _SL("127.0.0.1"), f->port, hostname, f->clientCfg, NULL, NULL);
    return f->t.clientSock != NULL;
}

static void fixtureDestroy(Fixture* f)
{
    if (f->t.clientSock) {
        netsocketClose(f->t.clientSock);
        objRelease(&f->t.clientSock);
    }
    if (f->t.serverSock) {
        netsocketClose(f->t.serverSock);
        objRelease(&f->t.serverSock);
    }
    if (f->listener) {
        netsocketClose(f->listener);
        objRelease(&f->listener);
    }
    if (f->t.q) {
        netqueueShutdown(f->t.q, 0);
        objRelease(&f->t.q);
    }

    objRelease(&f->clientCfg);
    objRelease(&f->serverCfg);
    objRelease(&f->creds);
    objRelease(&f->ca);

    peerDestroy(&f->t.client);
    peerDestroy(&f->t.server);
    tlsTestPKIDestroy(&f->pki);
}

// ---------------------------------------------------------------------------------------------

// The whole point, end to end: a client and a server that have never met complete a handshake over
// loopback, each raises NFN_Secured exactly once, and application data survives the round trip in
// both directions.
static int test_tlstest_handshake(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // Exactly once on each side: the edge is a transition, not a repeated state report.
    if (f.t.client.secured != 1 || f.t.server.secured != 1)
        ret = 1;

    // Both ends know what they negotiated, and the client verified the server cleanly.
    if (!f.t.client.haveInfo || !f.t.client.info.secured || f.t.client.info.verifyFlags != 0)
        ret = 1;
    if (strEmpty(f.t.client.info.version) || strEmpty(f.t.client.info.ciphersuite))
        ret = 1;
    if (strFind(f.t.client.info.peerSubject, 0, _S TLS_TEST_HOSTNAME) < 0)
        ret = 1;

    // client -> server
    static const uint8 up[] = "hello from the client";
    if (!ret && !netsocketSend(f.t.clientSock, (uint8*)up, sizeof(up) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= sizeof(up) - 1);
    if (f.t.server.buflen != sizeof(up) - 1 || memcmp(f.t.server.buf, up, sizeof(up) - 1) != 0)
        ret = 1;

    // server -> client
    static const uint8 down[] = "and hello back from the server";
    if (!ret && f.t.serverSock &&
        !netsocketSend(f.t.serverSock, (uint8*)down, sizeof(down) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.client.got >= sizeof(down) - 1);
    if (f.t.client.buflen != sizeof(down) - 1 ||
        memcmp(f.t.client.buf, down, sizeof(down) - 1) != 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// What actually reaches the socket is TLS records, not the payload. A tap stage attached after the
// TLS filter sits closer to the wire and records everything the TLS filter produced, so this can
// assert the plaintext never appears there -- the one thing every other test takes on faith.
static int test_tlstest_wire(void)
{
    int ret = 0;
    Fixture f;
    BufRing wire;

    bufringInit(&wire, 16384);

    if (!fixtureBase(&f)) {
        bufringDestroy(&wire);
        return 1;
    }

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!fixtureListen(&f))
        ret = 1;

    // Built by hand rather than through nettlsConnect(), because the tap has to be attached after
    // the TLS filter (making it the wire-end stage) and before the connect starts.
    if (!ret) {
        f.t.clientSock = netqueueSocket(f.t.q, NST_Stream);
        if (!f.t.clientSock || !netqueueAddSocket(f.t.q, f.t.clientSock))
            ret = 1;
    }

    if (!ret) {
        TlsClientFilter* tls     = tlsclientfilterCreate(f.clientCfg, _S TLS_TEST_HOSTNAME);
        NetTapStreamFactory* tap = nettapstreamfactoryCreate(&wire);

        if (!tls || !netsocketAddFilter(f.t.clientSock, NetFilter(tls)) ||
            !netsocketAddFilter(f.t.clientSock, NetFilter(tap)))
            ret = 1;

        objRelease(&tls);
        objRelease(&tap);
    }

    if (!ret && !netsocketConnect(f.t.clientSock, _SL("127.0.0.1"), f.port))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    static const uint8 secret[] = "SUPER-SECRET-PLAINTEXT-MARKER";
    if (!ret && !netsocketSend(f.t.clientSock, (uint8*)secret, sizeof(secret) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= sizeof(secret) - 1);

    // The recorded wire bytes must start with a TLS record header -- content type 22 (handshake),
    // version major 3 -- and must not contain the marker anywhere.
    size_t wlen = wire.total;
    uint8* wbuf = xaAlloc(wlen + 1, XA_Zero);
    bufringRead(&wire, wbuf, wlen);

    if (wlen < 5 || wbuf[0] != 0x16 || wbuf[1] != 0x03)
        ret = 1;

    for (size_t i = 0; i + sizeof(secret) - 1 <= wlen; i++) {
        if (memcmp(wbuf + i, secret, sizeof(secret) - 1) == 0) {
            ret = 1;
            break;
        }
    }

    xaFree(wbuf);
    bufringDestroy(&wire);
    fixtureDestroy(&f);
    return ret;
}

// A client that does not trust the server's issuer must not complete a handshake, and must not be
// told it did. The failure arrives the way every fatal filter error does: the flow closes with
// NCR_Error.
static int test_tlstest_verify(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    // Trust an unrelated CA that issued nothing in this test.
    TlsCAStore* wrong = tlscastoreCreate();
    if (!wrong || !tlscastoreAddPEM(wrong, f.pki.otherCACert))
        ret = 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, wrong);
    objRelease(&wrong);

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.closed > 0);

    if (f.t.client.secured != 0)
        ret = 1;   // the channel must never be reported as secured
    if (f.t.client.closeReason != NCR_Error)
        ret = 1;
    if (f.t.client.got != 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// The certificate chains to a trusted CA but carries the wrong name. Verification has to fail on
// that alone -- the check mbedTLS will skip entirely if set_hostname() is never called.
static int test_tlstest_hostname(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!fixtureListen(&f))
        ret = 1;

    // The trust store is correct; only the expected name is wrong. The certificate still chains to
    // a trusted CA, so nothing but the name check can reject it.
    if (!ret) {
        f.t.clientSock = nettlsConnect(f.t.q,
                                       _SL("127.0.0.1"),
                                       f.port,
                                       _S"wrong.invalid",
                                       f.clientCfg,
                                       NULL,
                                       NULL);
        if (!f.t.clientSock)
            ret = 1;
    }

    if (!ret)
        pumpUntil(f.t.q, f.t.client.closed > 0);

    if (f.t.client.secured != 0 || f.t.client.closeReason != NCR_Error)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// Bulk transfer: more than fits in one record, one ring segment, or one pass, in both directions at
// once. Exercises fragmentation, partial reads that leave half a record buffered, and the send
// watermark.
static int test_tlstest_large(void)
{
    int ret = 0;
    Fixture f;

    const size_t total = 1 * 1024 * 1024;
    uint8* payload     = xaAlloc(total, XA_Zero);
    for (size_t i = 0; i < total; i++) payload[i] = (uint8)(i * 7 + (i >> 8));

    uint32 expect = 0;
    for (size_t i = 0; i < total; i++) expect = expect * 31 + payload[i];

    if (!fixtureBase(&f)) {
        xaFree(payload);
        return 1;
    }

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // Fed in chunks, backing off whenever the socket says it is full -- which is the contract a
    // real sender follows, and the only way this much data goes out without the send being refused.
    size_t sent = 0;
    int stalled = 0;
    while (!ret && sent < total && stalled < MAX_TICKS) {
        size_t chunk = min((size_t)32768, total - sent);
        if (netsocketSend(f.t.clientSock, payload + sent, chunk, NULL, 0)) {
            sent += chunk;
            stalled = 0;
        } else {
            stalled++;
        }
        netqueueTick(f.t.q, TICK_WAIT);
    }

    if (sent != total)
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= total);

    if (f.t.server.got != total || f.t.server.sum != expect)
        ret = 1;

    xaFree(payload);
    fixtureDestroy(&f);
    return ret;
}

// STARTTLS: talk in the clear first, then upgrade the same live sockets in place. Attaching a
// filter to an already-connected socket builds its chain and primes it immediately, so no separate
// mechanism is needed.
static int test_tlstest_starttls(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    // Listener with no filter: this connection starts in the clear.
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_AutoAccept;
    f.t.q = netqueueCreate(&conf);
    if (!f.t.q)
        ret = 1;

    if (!ret) {
        netqueueSetHandlers(f.t.q, &testHandlers, &f.t);

        NetAddr la;
        netAddrFromStr(&la, _SL("127.0.0.1"));
        la.port = 0;

        f.listener = netqueueListen(f.t.q, &la, 4, NULL, NULL);
        if (!f.listener)
            ret = 1;
        else
            f.port = boundPort(f.listener);
    }

    if (!ret) {
        f.t.clientSock = netqueueConnect(f.t.q, _SL("127.0.0.1"), f.port, NULL, NULL);
        if (!f.t.clientSock)
            ret = 1;
    }

    if (!ret)
        pumpUntil(f.t.q, f.t.serverSock != NULL);

    // Cleartext round trip, standing in for the protocol's own STARTTLS exchange.
    static const uint8 greeting[] = "PLAIN-HELLO";
    if (!ret && !netsocketSend(f.t.clientSock, (uint8*)greeting, sizeof(greeting) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= sizeof(greeting) - 1);
    if (f.t.server.buflen != sizeof(greeting) - 1 ||
        memcmp(f.t.server.buf, greeting, sizeof(greeting) - 1) != 0)
        ret = 1;

    // Upgrade. The server side goes on first so its stage exists before the client's ClientHello
    // can arrive, which is the same ordering a real STARTTLS gives you: the server answers "go
    // ahead" and only then does the client start the handshake.
    if (!ret) {
        TlsServerFilter* sf = tlsserverfilterCreate(f.serverCfg);
        TlsClientFilter* cf = tlsclientfilterCreate(f.clientCfg, _S TLS_TEST_HOSTNAME);

        if (!sf || !cf || !netsocketAddFilter(f.t.serverSock, NetFilter(sf)) ||
            !netsocketAddFilter(f.t.clientSock, NetFilter(cf)))
            ret = 1;

        objRelease(&sf);
        objRelease(&cf);
    }

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // And now the same sockets carry encrypted traffic.
    static const uint8 secret[] = "NOW-ENCRYPTED";
    size_t before               = f.t.server.got;
    if (!ret && !netsocketSend(f.t.clientSock, (uint8*)secret, sizeof(secret) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= before + sizeof(secret) - 1);
    if (memcmp(f.t.server.buf + before, secret, sizeof(secret) - 1) != 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// ALPN: both ends offer a list, and the negotiated result shows up in the snapshot on both sides.
static int test_tlstest_alpn(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    sa_string protos;
    saInit(&protos, string, 2);
    saPush(&protos, string, _S"h2");
    saPush(&protos, string, _S"http/1.1");

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!tlsconfigSetALPN(f.clientCfg, &protos) || !tlsconfigSetALPN(f.serverCfg, &protos))
        ret = 1;
    saDestroy(&protos);

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    if (!f.t.client.haveInfo || !strEq(f.t.client.info.alpn, _S"h2"))
        ret = 1;
    if (!f.t.server.haveInfo || !strEq(f.t.server.info.alpn, _S"h2"))
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// Mutual TLS: the server demands a client certificate and verifies it against the same CA. The
// client presents the alt identity, and the server's snapshot names it.
static int test_tlstest_mtls(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    TlsCreds* clientCreds = tlscredsCreatePEM(f.pki.altCert, f.pki.altKey, NULL);
    if (!clientCreds)
        ret = 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);

    tlsconfigSetCA(f.clientCfg, f.ca);

    // The server verifies clients against the same CA, and refuses one it cannot.
    tlsconfigSetCA(f.serverCfg, f.ca);
    tlsconfigSetAuthMode(f.serverCfg, TLSAUTH_Required);

    // The client's own certificate rides on its config, the same way the server's does.
    if (!ret)
        tlsconfigSetCreds(f.clientCfg, clientCreds);
    objRelease(&clientCreds);

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    if (!f.t.server.haveInfo || f.t.server.info.verifyFlags != 0)
        ret = 1;
    if (strFind(f.t.server.info.peerSubject, 0, _S TLS_TEST_ALT_HOSTNAME) < 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// Certificate rotation: swapping the server filter's configuration affects connections opened
// afterwards and leaves one already running alone.
static int test_tlstest_rotate(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    // Built by hand so the test keeps a handle on the server filter to rotate through.
    TlsServerFilter* sf = tlsserverfilterCreate(f.serverCfg);
    if (!sf)
        ret = 1;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_AutoAccept;
    f.t.q = netqueueCreate(&conf);
    if (!f.t.q)
        ret = 1;

    if (!ret) {
        netqueueSetHandlers(f.t.q, &testHandlers, &f.t);

        NetAddr la;
        netAddrFromStr(&la, _SL("127.0.0.1"));
        la.port = 0;

        f.listener = netqueueSocket(f.t.q, NST_Stream);
        if (!f.listener || !netqueueAddSocket(f.t.q, f.listener) ||
            !netsocketAddFilter(f.listener, NetFilter(sf)) || !netsocketBind(f.listener, &la) ||
            !netsocketListen(f.listener, 4))
            ret = 1;
        else
            f.port = boundPort(f.listener);
    }

    if (!ret && !fixtureConnect(&f, _S TLS_TEST_HOSTNAME))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // The first connection got the original identity.
    if (!f.t.client.haveInfo || strFind(f.t.client.info.peerSubject, 0, _S TLS_TEST_HOSTNAME) < 0)
        ret = 1;

    // Rotate to the alternate identity while that connection is live.
    TlsCreds* altCreds = tlscredsCreatePEM(f.pki.altCert, f.pki.altKey, NULL);
    TlsConfig* altCfg  = altCreds ? tlsconfigCreateServer(altCreds) : NULL;
    if (!altCfg)
        ret = 1;
    else
        tlsserverfilterSetConfig(sf, altCfg);

    // The live connection is undisturbed: it still holds the old configuration, and still works.
    static const uint8 ping[] = "still-here";
    if (!ret && !netsocketSend(f.t.clientSock, (uint8*)ping, sizeof(ping) - 1, NULL, 0))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.server.got >= sizeof(ping) - 1);

    // A second client, opened after the rotation, is presented the new certificate. Its expected
    // name is the alternate one, so this also proves verification followed the rotation.
    // A separate context, so the second connection's events do not land on the first's counters.
    TestCtx t2 = { 0 };
    t2.q       = f.t.q;

    if (!ret) {
        netqueueSetHandlers(f.t.q, &testHandlers, &t2);
        t2.clientSock = nettlsConnect(f.t.q,
                                      _SL("127.0.0.1"),
                                      f.port,
                                      _S TLS_TEST_ALT_HOSTNAME,
                                      f.clientCfg,
                                      NULL,
                                      NULL);
        if (!t2.clientSock)
            ret = 1;
    }

    if (!ret)
        pumpUntil(t2.q, t2.client.secured > 0);

    if (!t2.client.haveInfo || strFind(t2.client.info.peerSubject, 0, _S TLS_TEST_ALT_HOSTNAME) < 0)
        ret = 1;

    ctxDestroy(&t2);

    objRelease(&altCfg);
    objRelease(&altCreds);
    objRelease(&sf);

    netqueueSetHandlers(f.t.q, &testHandlers, &f.t);
    fixtureDestroy(&f);
    return ret;
}

// An orderly close: shutting one end down emits close_notify while the wire is still usable, and
// both ends see exactly one NET_FlowClosed.
static int test_tlstest_shutdown(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // The client closes. Its own flow closes for the application's reason; the server sees the
    // connection end rather than an error, since close_notify went out ahead of the FIN.
    if (!ret) {
        netsocketClose(f.t.clientSock);
        objRelease(&f.t.clientSock);
        f.t.clientSock = NULL;
    }

    if (!ret)
        pumpUntil(f.t.q, f.t.server.closed > 0);

    if (f.t.server.closed != 1)
        ret = 1;
    if (f.t.server.closeReason == NCR_Error)
        ret = 1;   // a clean close must not be reported as a failure

    fixtureDestroy(&f);
    return ret;
}

// Session resumption: a second connection to the same host reuses the ticket the first was given,
// which is visible as a handshake that completes without the server sending a certificate.
static int test_tlstest_resume(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    if (!tlsconfigSetResumption(f.clientCfg, true, 0) ||
        !tlsconfigSetResumption(f.serverCfg, true, 0))
        ret = 1;

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // The TLS 1.3 ticket arrives as a post-handshake message, so the client has to keep reading for
    // a moment before it has anything to resume with.
    pumpIdle(&f.t, 20);

    // Second connection on the same config, so it finds the cached session for this hostname.
    TestCtx t2 = { 0 };
    t2.q       = f.t.q;
    netqueueSetHandlers(f.t.q, &testHandlers, &t2);

    t2.clientSock =
        nettlsConnect(f.t.q, _SL("127.0.0.1"), f.port, _S TLS_TEST_HOSTNAME, f.clientCfg, NULL, NULL);
    if (!t2.clientSock)
        ret = 1;

    if (!ret)
        pumpUntil(t2.q, t2.client.secured > 0);

    // A resumed handshake sends no certificate -- the identity carries over from the session being
    // resumed -- so peerVerified being false on the second connection while it was true on the
    // first is the observable proof that the cached ticket was actually used.
    if (!t2.client.haveInfo || !t2.client.info.secured || t2.client.info.verifyFlags != 0)
        ret = 1;
    if (!f.t.client.haveInfo || !f.t.client.info.peerVerified)
        ret = 1;   // the first connection did a full handshake and checked the certificate
    if (t2.client.info.peerVerified)
        ret = 1;   // the second did not, because it resumed

    ctxDestroy(&t2);

    netqueueSetHandlers(f.t.q, &testHandlers, &f.t);
    fixtureDestroy(&f);
    return ret;
}

// A configuration that cannot work must fail closed. A client that verifies against an empty trust
// store cannot be sealed, so the stage it produces tears the flow down rather than dropping out of
// the chain and letting the socket run in the clear.
static int test_tlstest_failclosed(void)
{
    int ret = 0;
    Fixture f;

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();   // TLSAUTH_Required by default, and no CA store
    f.serverCfg = tlsconfigCreateServer(f.creds);

    if (tlsconfigSeal(f.clientCfg))
        ret = 1;   // sealing must refuse: verification is on with nothing to verify against

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;

    if (!ret)
        pumpUntil(f.t.q, f.t.client.closed > 0);

    if (f.t.client.secured != 0 || f.t.client.closeReason != NCR_Error)
        ret = 1;

    // Nothing may have reached the wire in the clear, and nothing may have been delivered.
    if (f.t.client.got != 0 || f.t.server.got != 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

// Post-handshake NewSessionTickets arriving immediately ahead of a payload and a close: the shape
// that exposed a decode() stall against real servers, where a pass could end having consumed
// nothing and produced nothing while mbedTLS still held an unprocessed record, stranding a fully
// received payload in the socket ring (see the WANT_READ handling in cxtls/tlsfilter.c).
//
// Read the coverage honestly: this exercises the multi-ticket path and the send-then-close race,
// but it does **not** fail without that fix. Reproducing the stall needs a peer that packs two
// handshake messages into a single TLS record -- OpenSSL-based servers do, which is how Cloudflare
// triggered it. mbedTLS puts each ticket in a record of its own, so every stalled pass here still
// consumes from `src` and the chain driver's progress check carries it through. Bumping the ticket
// count does not change that; only a coalescing peer does.
static int test_tlstest_tickets(void)
{
    int ret = 0;
    Fixture f;

    static const char payload[] = "the response that must not be lost";

    if (!fixtureBase(&f))
        return 1;

    f.clientCfg = tlsconfigCreateClient();
    f.serverCfg = tlsconfigCreateServer(f.creds);
    tlsconfigSetCA(f.clientCfg, f.ca);

    // Resumption is what makes the server issue tickets at all; the count is not something cxtls
    // exposes, so it is set on the sealed config directly. The client deliberately leaves
    // resumption off -- it still receives and parses the tickets, which is all this needs.
    tlsconfigSetResumption(f.serverCfg, true, 0);
    if (!tlsconfigSeal(f.serverCfg))
        ret = 1;
    else
        mbedtls_ssl_conf_new_session_tickets(tlsconfigMbedConf(f.serverCfg), 2);

    if (!ret && (!fixtureListen(&f) || !fixtureConnect(&f, _S TLS_TEST_HOSTNAME)))
        ret = 1;
    if (!ret)
        pumpUntil(f.t.q, f.t.client.secured && f.t.server.secured);

    // Send and close in the same breath, so the tickets and the payload reach the client together
    // and the FIN is right behind them: no later wire read exists to rescue a stalled decode.
    if (!ret && !netsocketSend(f.t.serverSock, (uint8*)payload, sizeof(payload) - 1, NULL, 0))
        ret = 1;
    if (!ret) {
        netsocketClose(f.t.serverSock);
        objRelease(&f.t.serverSock);
        f.t.serverSock = NULL;
    }

    if (!ret)
        pumpUntil(f.t.q, f.t.client.closed > 0);

    // The whole payload has to arrive, and it has to arrive intact.
    if (f.t.client.got != sizeof(payload) - 1)
        ret = 1;
    else if (memcmp(f.t.client.buf, payload, sizeof(payload) - 1) != 0)
        ret = 1;

    fixtureDestroy(&f);
    return ret;
}

testfunc tlstest_funcs[] = {
    { "handshake",  test_tlstest_handshake  },
    { "wire",       test_tlstest_wire       },
    { "verify",     test_tlstest_verify     },
    { "hostname",   test_tlstest_hostname   },
    { "large",      test_tlstest_large      },
    { "starttls",   test_tlstest_starttls   },
    { "alpn",       test_tlstest_alpn       },
    { "mtls",       test_tlstest_mtls       },
    { "rotate",     test_tlstest_rotate     },
    { "shutdown",   test_tlstest_shutdown   },
    { "resume",     test_tlstest_resume     },
    { "tickets",    test_tlstest_tickets    },
    { "failclosed", test_tlstest_failclosed },
    { 0,            0                       },
};

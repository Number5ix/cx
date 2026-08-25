#include <cx/net.h>
#include <cx/thread.h>
#include <cx/platform/os.h>
#include <cx/time/clock.h>

#include <cx/net/net_private.h>
// For netqueue_ingestDatagram, private backend constructors, etc.

// The backend integration tests below drive real loopback sockets, building a "raw peer" by hand
// with plain OS socket calls to test the NetQueue backends against. cx headers must precede
// windows.h, so winsock comes last -- cx/platform/win.h has already set WIN32_LEAN_AND_MEAN, which
// keeps the incompatible winsock v1 out of windows.h and lets winsock2 be included cleanly here.
#if defined(_WIN32)
#include <cx/platform/win.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/win_net_socket.h"   // netsocketwinWrap, to drive an accepted stream socket
#elif defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
// netsocketposixWrap, to drive an accepted stream socket -- same class name and generated header
// shape on both platforms (see wasm_net_socket.cxh), just a different platform subdirectory.
#if defined(_PLATFORM_UNIX)
#include "platform/unix/unix_net_socket.h"
#if defined(_PLATFORM_LINUX)
#include "platform/unix/unix_net_epoll.h"   // netqueueepollCreate, to exercise the epoll backend directly
#elif defined(_PLATFORM_FBSD)
#include "platform/unix/unix_net_kqueue.h"   // netqueuekqueueCreate, to exercise the kqueue backend directly
#endif
#elif defined(_PLATFORM_WASM)
#include "platform/wasm/wasm_net_socket.h"
// No performance backend to exercise directly here -- select is the only queue backend WASM has.
#endif

// The Windows-flavored raw-socket API the test bodies below are written against, aliased onto the
// POSIX equivalents rather than rewriting every call site: SOCKET is already just an OS handle
// (SOCKET_ERROR is only used the return-value-check position -- INVALID_SOCKET/-1 do that job on
// Unix), ioctlsocket(FIONBIO) is the one call whose signature genuinely differs, and
// netsocketwinWrap's role (wrap an accepted OS handle as a connected NetSocket) is exactly what
// netsocketposixWrap does.
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket    close
#define netsocketwinWrap netsocketposixWrap

typedef unsigned long u_long;   // matches Windows' u_long closely enough for these tests' use
#define FIONBIO 1
static int ioctlsocket(SOCKET s, int cmd, u_long* arg)
{
    unused_noeval(cmd);   // the only command these tests ever pass is FIONBIO
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0)
        return -1;
    fl = *arg ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK);
    return fcntl(s, F_SETFL, fl);
}

// Every test below declares its length out-param as a plain `int` (matching Windows' getsockname,
// whose length parameter is an int), but POSIX's is a socklen_t*. A thin wrapper is less invasive
// than retyping every local across every test body.
static int nettestGetsockname(SOCKET s, struct sockaddr* addr, int* len)
{
    socklen_t sl = (socklen_t)*len;
    int rc        = getsockname(s, addr, &sl);
    *len          = (int)sl;
    return rc;
}
#define getsockname nettestGetsockname

// Same reasoning as nettestGetsockname: Windows' recvfrom takes a plain int* for the address
// length, POSIX's takes a socklen_t*.
static int nettestRecvfrom(SOCKET s, void* buf, size_t len, int flags, struct sockaddr* addr, int* addrlen)
{
    socklen_t sl = (socklen_t)*addrlen;
    int       rc = (int)recvfrom(s, buf, len, flags, addr, &sl);
    *addrlen      = (int)sl;
    return rc;
}
#define recvfrom nettestRecvfrom
#endif

#define TEST_FILE  nettest
#define TEST_FUNCS nettest_funcs
#include "common.h"
#include "netfilterobj.h"
#include "nettestobj.h"

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

static NetAddr peerAddr(uint8 last, uint16 port)
{
    NetAddr a = { .type = NA_IPv4, .port = port };
    a.ipv4[0] = 10;
    a.ipv4[1] = 0;
    a.ipv4[2] = 0;
    a.ipv4[3] = last;
    return a;
}

// Inject a packet carrying a single byte payload, the way a backend would once a completion
// arrives. Returns what netqueue_ingestDatagram() reported.
static bool inject(NetQueue* q, NetSocket* sock, NetAddr* peer, uint8 val)
{
    // The buffer must come from the queue's own pool, exactly as it would on a real backend --
    // ingest hands it straight to the flow and the dispatcher returns it to the pool afterwards.
    Buffer buf = bufpoolGet(&q->pool->msgbuf);
    if (!buf)
        return false;

    buf->data[0] = val;
    buf->len     = 1;

    bool ret = netqueue_ingestDatagram(q, sock, peer, &buf);
    if (buf)
        bufpoolPut(&q->pool->msgbuf, &buf);   // only reached if ingest declined it
    return ret;
}

typedef struct Recorder {
    uint32 recvCount;
    uint32 closeCount;
    uint32 openCount;
    uint32 refusedCount;
    uint32 sendReadyCount;
    uint32 securedCount;
    uint32 connCount;
    NetConnectionState connState;
    NetErrorCode connErr;
    uint32 acceptCount;
    NetSocket* accepted;
    NetCloseReason lastReason;
    void* lastCtx;
    uint8 seq[256];
    uint32 seqlen;
    uint32 timerCount;
    NetTimerId lastTimerId;
} Recorder;

static void onConnect(NetEvent* ev)
{
    Recorder* r  = (Recorder*)ev->ctx;
    r->connCount++;
    r->connState = ev->conn.state;
    r->connErr   = ev->conn.err;
}

static void onRecv(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->recvCount++;
    r->lastCtx = ev->ctx;
}

static void onSecured(NetEvent* ev)
{
    // Only an NFN_Secured notification counts as "secured"; the filter raised it explicitly, and it
    // arrives on the dedicated NET_FilterNotify event carrying the code as its payload.
    Recorder* r = (Recorder*)ev->ctx;
    if (ev->filter.notify == NFN_Secured)
        r->securedCount++;
}

// Drain a filtered stream socket's decoded plaintext inside the recv handler, accumulating it so a
// filter test can verify the payload survived the encode/decode round trip.
static void onFilterRecv(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->recvCount++;

    uint8 buf[256];
    size_t n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0);
    for (size_t i = 0; i < n && r->seqlen < sizeof(r->seq); i++)
        r->seq[r->seqlen++] = buf[i];
}

// Record an accepted connection, holding a reference so it survives past the NET_Accepted message
// being retired (which releases the message's own reference). Mirrors what an application keeping
// the socket without NQ_AutoAccept must do.
static void onAccept(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->acceptCount++;
    if (!r->accepted && ev->accept.newSocket)
        r->accepted = objAcquire(ev->accept.newSocket);
}

static void onClosed(NetEvent* ev)
{
    Recorder* r     = (Recorder*)ev->ctx;
    r->closeCount++;
    r->lastReason = ev->closed.reason;
}

// Counts flow creations. NET_FlowOpen is delivered through the flow itself, so ev->flow is
// always set and no packet accompanies it.
static void onFlowOpen(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->openCount++;
}

// Counts refusals at the flow cap. The raw packet travels on ev->refused.msg with no flow.
static void onFlowRefused(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->refusedCount++;
}

static NetQueue* makeQueue(NetQueueConfig* conf)
{
    NetQueueConfig def;
    if (!conf) {
        netqueuePresetClient(&def);
        conf = &def;
    }
    return NetQueue(netqueuetestCreate(conf));
}

// Build a datagram socket already registered with the queue.
static NetSocket* makeSocket(NetQueue* q, NetSocketType type)
{
    NetSocket* s = netqueueSocket(q, type);
    netqueueAddSocket(q, s);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------

// Parse a literal and require the formatter to produce the expected canonical form.
#define ADDRRT(instr, outstr)                                                            \
    do {                                                                                 \
        NetAddr _a;                                                                      \
        string _s = 0;                                                                   \
        if (!netAddrFromStr(&_a, _SL(instr)) || !netAddrToStr(&_s, &_a) ||               \
            !strEq(_s, _SL(outstr)))                                                     \
            TEST_FAILV(ret, 1, _SL("round-trip of '${string}' expected '${string}', got '${string}'"), \
                       stvar(strref, _SL(instr)), stvar(strref, _SL(outstr)), stvar(strref, _s));       \
        strDestroy(&_s);                                                                 \
    } while (0)

// Require the parser to reject a malformed literal.
#define ADDRBAD(instr)                                \
    do {                                              \
        NetAddr _a;                                   \
        if (netAddrFromStr(&_a, _SL(instr)))          \
            TEST_FAILV(ret, 1, _SL("expected '${string}' to be rejected as malformed, but it parsed"), \
                       stvar(strref, _SL(instr)));    \
    } while (0)

// Address literal parsing and formatting.
static int test_nettest_addr(void)
{
    int ret = 0;
    NetAddr a;

    ADDRRT("127.0.0.1", "127.0.0.1");
    ADDRRT("192.168.1.4", "192.168.1.4");
    ADDRRT("::1", "::1");
    ADDRRT("::", "::");
    ADDRRT("1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8");
    ADDRRT("2001:db8::8a2e:370:7334", "2001:db8::8a2e:370:7334");
    ADDRRT("1::", "1::");
    ADDRRT("fe80::1%3", "fe80::1%3");
    ADDRRT("FE80::A", "fe80::a");                        // formatter output is lowercase
    ADDRRT("::ffff:192.168.1.1", "::ffff:c0a8:101");     // v4 tail parses, formats as hex
    ADDRRT("0:0:0:0:0:0:0:1", "::1");                    // longest zero run compresses
    ADDRRT("1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0");        // lone zero group does not compress

    // Byte-level checks: ipv4[0] is the least significant octet, ipv6 is network order.
    if (!netAddrFromStr(&a, _SL("10.0.0.99")) || a.type != NA_IPv4 || a.ipv4[3] != 10 ||
        a.ipv4[0] != 99 || a.port != 0)
        TEST_FAILV(ret, 1, _SL("10.0.0.99: expected type ${int}, ipv4[3]=10, ipv4[0]=99, port=0; got type ${int}, ipv4[3]=${int}, ipv4[0]=${int}, port=${int}"),
                   stvar(int32, NA_IPv4), stvar(int32, a.type), stvar(int32, a.ipv4[3]), stvar(int32, a.ipv4[0]), stvar(int32, a.port));
    if (!netAddrFromStr(&a, _SL("2001:db8::1")) || a.type != NA_IPv6 || a.ipv6[0] != 0x20 ||
        a.ipv6[1] != 0x01 || a.ipv6[2] != 0x0d || a.ipv6[3] != 0xb8 || a.ipv6[15] != 1 ||
        a.scope != 0)
        TEST_FAILV(ret, 1, _SL("2001:db8::1: expected type ${int}, bytes[0..3]=20:01:0d:b8, byte[15]=1, scope=0; got type ${int}, bytes[0..3]=${int}:${int}:${int}:${int}, byte[15]=${int}, scope=${int}"),
                   stvar(int32, NA_IPv6), stvar(int32, a.type), stvar(int32, a.ipv6[0]), stvar(int32, a.ipv6[1]), stvar(int32, a.ipv6[2]), stvar(int32, a.ipv6[3]), stvar(int32, a.ipv6[15]), stvar(int32, a.scope));
    if (!netAddrFromStr(&a, _SL("fe80::42%7")) || a.scope != 7)
        TEST_FAILV(ret, 1, _SL("fe80::42%7: expected scope 7, got ${int}"), stvar(int32, a.scope));

    ADDRBAD("");
    ADDRBAD("1.2.3");
    ADDRBAD("1.2.3.4.5");
    ADDRBAD("1.2.3.999");
    ADDRBAD("1.2.3.4:80");   // no port suffix
    ADDRBAD(":");
    ADDRBAD(":::");
    ADDRBAD("1:2:3");
    ADDRBAD("1::2::3");
    ADDRBAD("12345::");
    ADDRBAD("g::1");
    ADDRBAD("1:2:3:4:5:6:7:8:9");
    ADDRBAD("1:2:3:4:5:6:7:8::");   // :: must stand for at least one zero group
    ADDRBAD("fe80::1%");
    ADDRBAD("fe80::1%notarealiface0");

    return ret;
}

// Two peers on one socket get two flows, and each flow's packets arrive at the handler in order.
static int test_nettest_flow_basic(void)
{
    int ret = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueue* q  = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p1 = peerAddr(1, 5000);
    NetAddr p2 = peerAddr(2, 5000);

    for (uint8 i = 0; i < 4; i++) {
        if (!inject(q, s, &p1, i))
            TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p1, i)"), stvNone);
        if (!inject(q, s, &p2, i))
            TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p2, i)"), stvNone);
    }

    // Two distinct flows should exist, one per source address.
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 2"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    netqueueTick(q, 0);

    if (rec.recvCount != 8)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 8"), stvar(uint32, rec.recvCount));
    if (rec.lastCtx != &rec)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastCtx=${ptr} != &rec"), stvar(ptr, rec.lastCtx));

    // The same peer must resolve to the same flow rather than creating a second one.
    if (!inject(q, s, &p1, 99))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p1, 99)"), stvNone);
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 2"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    netqueueTick(q, 0);
    if (rec.recvCount != 9)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 9"), stvar(uint32, rec.recvCount));

    // A stream socket has exactly one flow, created during socket init, so a consumer handling a
    // single connection never constructs one. It still gets a terminal event on close like any
    // other flow.
    NetSocket* stream = makeSocket(q, NST_Stream);
    if (!stream->flow)
        TEST_FAILV(ret, 1, _SL("assertion failed: !stream->flow"), stvNone);

    static const NetHandlers streamHandlers = { .flowClosed = onClosed };
    netsocketSetHandlers(stream, &streamHandlers, &rec);

    netsocketClose(stream);
    netqueueTick(q, 0);
    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));
    if (rec.lastReason != NCR_SocketClosed)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastReason=${int} != NCR_SocketClosed"), stvar(int32, rec.lastReason));
    objRelease(&stream);

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// Handler resolution walks flow -> socket -> queue, and falls through per field so that a partial
// override inherits everything it did not fill in.
typedef struct Tally {
    uint32 n;
} Tally;

static void tallyRecv(NetEvent* ev)
{
    ((Tally*)ev->ctx)->n++;
}

static int test_nettest_flow_handlers(void)
{
    int ret = 0;
    Tally qt = { 0 }, st = { 0 }, ft = { 0 };
    Recorder qrec = { 0 };

    // The queue supplies both recv and flowClosed; the socket overrides only recv, so flowClosed
    // must still fall through to the queue's.
    static const NetHandlers qhandlers = { .recv = tallyRecv, .flowClosed = onClosed };
    static const NetHandlers shandlers = { .recv = tallyRecv };
    static const NetHandlers fhandlers = { .recv = tallyRecv };

    NetQueue* q = makeQueue(NULL);
    netqueueSetHandlers(q, &qhandlers, &qt);

    NetSocket* s = makeSocket(q, NST_Datagram);
    NetAddr p1   = peerAddr(1, 5000);

    // No socket or flow handlers yet: the queue-wide set wins.
    inject(q, s, &p1, 1);
    netqueueTick(q, 0);
    if (qt.n != 1 || st.n != 0 || ft.n != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: qt.n=${uint} != 1 || st.n=${uint} != 0 || ft.n=${uint} != 0"), stvar(uint32, qt.n), stvar(uint32, st.n), stvar(uint32, ft.n));

    // Socket override takes precedence, and brings its own ctx along.
    netsocketSetHandlers(s, &shandlers, &st);
    inject(q, s, &p1, 2);
    netqueueTick(q, 0);
    if (qt.n != 1 || st.n != 1 || ft.n != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: qt.n=${uint} != 1 || st.n=${uint} != 1 || ft.n=${uint} != 0"), stvar(uint32, qt.n), stvar(uint32, st.n), stvar(uint32, ft.n));

    // Flow override is the most specific and wins over both.
    NetFlow* flow = netqueuePromoteFlow(q, s, &p1);
    if (!flow)
        TEST_FAIL(1, _SL("assertion failed: !flow"), stvNone);
    netflowSetHandlers(flow, &fhandlers, &ft);

    inject(q, s, &p1, 3);
    netqueueTick(q, 0);
    if (qt.n != 1 || st.n != 1 || ft.n != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: qt.n=${uint} != 1 || st.n=${uint} != 1 || ft.n=${uint} != 1"), stvar(uint32, qt.n), stvar(uint32, st.n), stvar(uint32, ft.n));

    // flowClosed is set on neither the socket nor the flow, so it falls through to the queue --
    // per field, not per set. Point the queue's flowClosed ctx at a recorder we can read.
    netqueueSetHandlers(q, &qhandlers, &qrec);
    netflowClose(flow);
    netqueueTick(q, 0);

    if (qrec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: qrec.closeCount=${uint} != 1"), stvar(uint32, qrec.closeCount));
    if (qrec.lastReason != NCR_AppClosed)
        TEST_FAILV(ret, 1, _SL("assertion failed: qrec.lastReason=${int} != NCR_AppClosed"), stvar(int32, qrec.lastReason));

    objRelease(&flow);
    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// A terminal event is ordered behind everything already queued for the flow, fires exactly once,
// and takes the flow out of the table.
static int test_nettest_flow_close(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv, .flowClosed = onClosed };

    NetQueue* q  = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p1 = peerAddr(1, 5000);

    // Queue three packets, then close. Nothing has been dispatched yet, so the terminal event
    // lands behind all three rather than racing them.
    for (uint8 i = 0; i < 3; i++)
        inject(q, s, &p1, i);

    NetFlow* flow = netqueuePromoteFlow(q, s, &p1);
    if (!flow)
        TEST_FAIL(1, _SL("assertion failed: !flow"), stvNone);

    if (!netflowClose(flow))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowClose(flow)"), stvNone);
    // A second close is a no-op: the application sees exactly one NET_FlowClosed per flow.
    if (netflowClose(flow))
        TEST_FAILV(ret, 1, _SL("assertion failed: netflowClose(flow)"), stvNone);

    netqueueTick(q, 0);

    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));
    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));
    if (rec.lastReason != NCR_AppClosed)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastReason=${int} != NCR_AppClosed"), stvar(int32, rec.lastReason));

    // The flow is out of the table, so the same peer is now a genuinely new source.
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 0"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    objRelease(&flow);

    inject(q, s, &p1, 9);
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));
    netqueueTick(q, 0);
    if (rec.recvCount != 4)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 4"), stvar(uint32, rec.recvCount));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// At the cap, the least recently active flow is reclaimed to make room. With noReclaim set
// nothing is evicted and the new peer is refused through the flowRefused handler instead.
static int test_nettest_flow_reclaim(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv        = onRecv,
                                          .flowClosed  = onClosed,
                                          .flowOpen    = onFlowOpen,
                                          .flowRefused = onFlowRefused };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.maxflows     = 2;
    conf.reclaimBatch = 1;

    NetQueue* q = makeQueue(&conf);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p1 = peerAddr(1, 5000);
    NetAddr p2 = peerAddr(2, 5000);
    NetAddr p3 = peerAddr(3, 5000);

    inject(q, s, &p1, 1);
    inject(q, s, &p2, 1);
    netqueueTick(q, 0);

    if (atomicLoad(uint32, &q->nflows, Relaxed) != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 2"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));
    // Each auto-created flow announced itself, ordered ahead of its first packet.
    if (rec.openCount != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 2"), stvar(uint32, rec.openCount));

    // Touch p2 so p1 is the least recently active, then hit the cap with a third peer.
    inject(q, s, &p2, 2);
    netqueueTick(q, 0);

    // At the cap the third packet cannot get a flow yet -- reclaim only *marks* the victim, which
    // does not leave the table until its terminal event has been delivered.
    if (inject(q, s, &p3, 1))
        TEST_FAILV(ret, 1, _SL("assertion failed: inject(q, s, &p3, 1)"), stvNone);
    if (rec.refusedCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.refusedCount=${uint} != 1"), stvar(uint32, rec.refusedCount));

    netqueueTick(q, 0);

    // Now p1's teardown has run and there is room.
    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));
    if (rec.lastReason != NCR_Reclaimed)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastReason=${int} != NCR_Reclaimed"), stvar(int32, rec.lastReason));

    if (!inject(q, s, &p3, 2))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p3, 2)"), stvNone);
    netqueueTick(q, 0);

    // p3's admission opened a third flow; every open so far has a live flow or a close to pair
    // with.
    if (rec.openCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 3"), stvar(uint32, rec.openCount));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    // Same setup, but with reclamation disabled: under a debugger nothing may be evicted, and a
    // new peer must be refused loudly rather than costing the flow being debugged.
    Recorder rec2 = { 0 };
    netqueuePresetClient(&conf);
    conf.maxflows  = 2;
    conf.noReclaim = true;

    q = makeQueue(&conf);
    netqueueSetHandlers(q, &handlers, &rec2);
    s = makeSocket(q, NST_Datagram);

    inject(q, s, &p1, 1);
    inject(q, s, &p2, 1);
    netqueueTick(q, 0);

    if (inject(q, s, &p3, 1))
        TEST_FAILV(ret, 1, _SL("assertion failed: inject(q, s, &p3, 1)"), stvNone);
    netqueueTick(q, 0);

    if (rec2.openCount != 2)    // p1 and p2; refused p3 opened nothing
        TEST_FAILV(ret, 1, _SL("p1 and p2; refused p3 opened nothing: : assertion failed: rec2.openCount=${uint} != 2"), stvar(uint32, rec2.openCount));
    if (rec2.refusedCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec2.refusedCount=${uint} != 1"), stvar(uint32, rec2.refusedCount));
    if (rec2.closeCount != 0)   // nothing was reclaimed
        TEST_FAILV(ret, 1, _SL("nothing was reclaimed: : assertion failed: rec2.closeCount=${uint} != 0"), stvar(uint32, rec2.closeCount));

    // The application can still admit the peer explicitly once it has validated the packet.
    // Promotion fires NET_FlowOpen exactly like an auto-created flow.
    NetFlow* promoted = netqueuePromoteFlow(q, s, &p3);
    if (!promoted)
        TEST_FAILV(ret, 1, _SL("assertion failed: !promoted"), stvNone);
    objRelease(&promoted);

    if (!inject(q, s, &p3, 2))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p3, 2)"), stvNone);
    netqueueTick(q, 0);
    if (rec2.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec2.recvCount=${uint} != 3"), stvar(uint32, rec2.recvCount));
    if (rec2.openCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec2.openCount=${uint} != 3"), stvar(uint32, rec2.openCount));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// reclaimMinIdle keeps cap-pressure reclaim from churning flows that are still active: with a
// threshold nothing can meet, no flow is evicted at the cap and the new peer is refused, while
// netqueuePromoteFlow() remains the application's override.
static int test_nettest_flow_reclaim_idle(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv        = onRecv,
                                          .flowClosed  = onClosed,
                                          .flowOpen    = onFlowOpen,
                                          .flowRefused = onFlowRefused };

    // A threshold far beyond the test's runtime: every flow counts as recently active, so the
    // cap can never be relieved by eviction. (Idle time is measured in coarse ~1s ticks, which
    // makes an hour equivalent to "never" here without any waiting.)
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.maxflows       = 2;
    conf.reclaimBatch   = 1;
    conf.reclaimMinIdle = timeS(3600);

    NetQueue* q = makeQueue(&conf);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p1 = peerAddr(1, 5000);
    NetAddr p2 = peerAddr(2, 5000);
    NetAddr p3 = peerAddr(3, 5000);

    inject(q, s, &p1, 1);
    inject(q, s, &p2, 1);
    netqueueTick(q, 0);
    if (rec.openCount != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 2"), stvar(uint32, rec.openCount));

    // At the cap with nothing idle enough to evict: the newcomer is refused, and neither
    // existing flow is even marked for teardown.
    if (inject(q, s, &p3, 1))
        TEST_FAILV(ret, 1, _SL("assertion failed: inject(q, s, &p3, 1)"), stvNone);
    netqueueTick(q, 0);

    if (rec.refusedCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.refusedCount=${uint} != 1"), stvar(uint32, rec.refusedCount));
    if (rec.closeCount != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 0"), stvar(uint32, rec.closeCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 2"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    // Both original sessions are untouched and still deliver.
    inject(q, s, &p1, 2);
    inject(q, s, &p2, 2);
    netqueueTick(q, 0);
    if (rec.recvCount != 4)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 4"), stvar(uint32, rec.recvCount));

    // The application override still works: promotion may exceed the cap by one and fires the
    // usual NET_FlowOpen for the new session.
    NetFlow* promoted = netqueuePromoteFlow(q, s, &p3);
    if (!promoted)
        TEST_FAILV(ret, 1, _SL("assertion failed: !promoted"), stvNone);
    objRelease(&promoted);

    if (!inject(q, s, &p3, 2))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p3, 2)"), stvNone);
    netqueueTick(q, 0);
    if (rec.recvCount != 5 || rec.openCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 5 || rec.openCount=${uint} != 3"), stvar(uint32, rec.recvCount), stvar(uint32, rec.openCount));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    // Teardown paired every open with exactly one close.
    if (rec.closeCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 3"), stvar(uint32, rec.closeCount));

    return ret;
}

// A packet arriving for a reclaimed flow before its terminal event has been delivered cancels the
// teardown. An application close is a decision and is not undone the same way.
static int test_nettest_flow_resurrect(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv        = onRecv,
                                          .flowClosed  = onClosed,
                                          .flowOpen    = onFlowOpen,
                                          .flowRefused = onFlowRefused };

    // A cap of one means the second peer forces the first to be reclaimed, which is the only way
    // to reach the speculative-close state that resurrection applies to.
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.maxflows     = 1;
    conf.reclaimBatch = 1;

    NetQueue* q = makeQueue(&conf);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p1 = peerAddr(1, 5000);
    NetAddr p2 = peerAddr(2, 5000);

    inject(q, s, &p1, 1);
    netqueueTick(q, 0);
    if (rec.recvCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 1"), stvar(uint32, rec.recvCount));
    if (rec.openCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 1"), stvar(uint32, rec.openCount));

    // p2 hits the cap, so p1 is marked dying with NCR_Reclaimed. Nothing has been dispatched, so
    // the terminal event is still sitting in p1's inbox.
    inject(q, s, &p2, 1);
    if (rec.refusedCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.refusedCount=${uint} != 1"), stvar(uint32, rec.refusedCount));

    // p1 was not gone after all. The arriving packet un-dies it and cancels the terminal event.
    inject(q, s, &p1, 2);
    netqueueTick(q, 0);

    if (rec.closeCount != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 0"), stvar(uint32, rec.closeCount));
    if (rec.recvCount != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 2"), stvar(uint32, rec.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));
    // Resurrection continues the same session; it must not announce a second open.
    if (rec.openCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 1"), stvar(uint32, rec.openCount));

    // The flow is fully alive again and keeps working, with no session state rebuilt.
    inject(q, s, &p1, 3);
    netqueueTick(q, 0);
    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));

    // An application close is not speculative, so an arriving packet does not un-decide it.
    NetFlow* flow = netqueuePromoteFlow(q, s, &p1);
    if (!flow)
        TEST_FAIL(1, _SL("assertion failed: !flow"), stvNone);
    netflowClose(flow);

    if (inject(q, s, &p1, 4))
        ret = 1;   // refused: the flow is genuinely going away

    netqueueTick(q, 0);
    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));
    if (rec.lastReason != NCR_AppClosed)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastReason=${int} != NCR_AppClosed"), stvar(int32, rec.lastReason));
    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));
    // Promoting the already-live flow opened nothing new, and the post-close packet was a
    // refusal, not an open: exactly one NET_FlowOpen for the whole session.
    if (rec.openCount != 1 || rec.refusedCount != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 1 || rec.refusedCount=${uint} != 2"), stvar(uint32, rec.openCount), stvar(uint32, rec.refusedCount));

    objRelease(&flow);
    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// Closing a socket, and shutting the queue down, both deliver NET_FlowClosed for every live flow
// with the right cause. This is the guarantee an application relies on to free per-peer state.
static int test_nettest_flow_shutdown(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv, .flowClosed = onClosed };

    NetQueue* q = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    for (uint8 i = 1; i <= 5; i++) {
        NetAddr p = peerAddr(i, 5000);
        inject(q, s, &p, i);
    }
    netqueueTick(q, 0);
    if (rec.recvCount != 5)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 5"), stvar(uint32, rec.recvCount));

    // Shutdown must run the terminal events it queues, so handlers fire before the queue goes.
    netqueueShutdown(q, 0);

    if (rec.closeCount != 5)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 5"), stvar(uint32, rec.closeCount));
    if (rec.lastReason != NCR_Shutdown)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.lastReason=${int} != NCR_Shutdown"), stvar(int32, rec.lastReason));

    objRelease(&s);
    objRelease(&q);

    // Closing just the socket produces the same guarantee with a different cause.
    Recorder rec2 = { 0 };
    q             = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec2);
    s = makeSocket(q, NST_Datagram);

    for (uint8 i = 1; i <= 3; i++) {
        NetAddr p = peerAddr(i, 6000);
        inject(q, s, &p, i);
    }
    netqueueTick(q, 0);

    netsocketClose(s);
    netqueueTick(q, 0);

    if (rec2.closeCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec2.closeCount=${uint} != 3"), stvar(uint32, rec2.closeCount));
    if (rec2.lastReason != NCR_SocketClosed)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec2.lastReason=${int} != NCR_SocketClosed"), stvar(int32, rec2.lastReason));

    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);

    return ret;
}

// ---------------------------------------------------------------------------------------------
// Filter hooks
//
// These drive the real filter data-plane code -- chain construction, the encode/decode fixpoint
// drivers, and the send/recv seams -- using passthrough and rate-mismatched filters. No
// cxtls/mbedTLS is involved.
//
// Most of them run over the synthetic backend, which has no OS handle: the seams are called
// directly and the "wire" is whatever lands in the socket's send buffers. The synthetic socket also
// overrides send(), so the one thing that cannot be reached that way -- a send opening a flow for an
// unknown peer -- gets a real loopback socket of its own at the end.
// ---------------------------------------------------------------------------------------------

// Inject a chunk of stream data the way ingestStream() would: raw bytes into the receive ring, then
// a bufferless NMSG_Data on the socket's single flow recording how many arrived.
static void injectStream(NetQueue* q, NetSocket* sock, const uint8* data, size_t len)
{
    withMutex (&sock->recvLock)
        bufringWrite(&sock->bufs.stream.recv, data, len);

    NetMessage* msg = netpoolAllocHeader(q->pool);
    msg->kind       = NMSG_Data;
    msg->buf        = NULL;
    msg->bytes      = len;
    msg->addr       = sock->remote;
    netqueue_submit(q, sock->flow, msg);
}

// Bring a synthetic socket to the state a filtered flow expects. Binding is what the test socket
// uses to reach NS_Connected, which is the gate on priming a stream chain; sendPending then keeps
// the send path off the OS handle it does not have, exactly as it does for a completion backend
// with a write already in flight, leaving encoded bytes parked in the send chain to be inspected.
static void readySocket(NetSocket* sock)
{
    NetAddr a = peerAddr(1, 1);
    netsocketBind(sock, &a);
    sock->sendPending = true;
}

// A single passthrough filter attached to a stream socket: the flow gets a one-stage chain, the
// encode seam lands the app payload on the send chain unchanged, and the decode seam hands it back
// through netsocketRecv().
static int test_nettest_filter_stream(void)
{
    int ret = 0;

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeSocket(q, NST_Stream);
    readySocket(s);

    NetPassStreamFactory* f = netpassstreamfactoryCreate();
    if (!netsocketAddFilter(s, NetFilter(f)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketAddFilter(s, NetFilter(f))"), stvNone);
    objRelease(&f);   // the socket holds its own reference

    // The socket keeps factories; the flow is where the actual stages live, built when the filter
    // was attached because the flow already existed.
    if (saSize(s->filters) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: saSize(s->filters)=${int} != 1"), stvar(int32, saSize(s->filters)));
    if (!s->flow || saSize(s->flow->filters) != 1 || !s->flow->encIn)
        TEST_FAILV(ret, 1, _SL("assertion failed: !s->flow || saSize(s->flow->filters)=${int} != 1 || !s->flow->encIn"), stvar(int32, saSize(s->flow->filters)));

    // Encode: run the send seam, expect the payload verbatim on the send chain.
    const uint8 payload[] = "the quick brown fox";
    size_t plen           = sizeof(payload) - 1;
    if (!netflow_filterStreamSend(s->flow, q, s, payload, plen))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflow_filterStreamSend(s->flow, q, s, payload, plen)"), stvNone);

    if (s->bufs.stream.send.total != plen)
        TEST_FAILV(ret, 1, _SL("assertion failed: s->bufs.stream.send.total=${uint} != plen=${uint}"), stvar(uint64, s->bufs.stream.send.total), stvar(uint64, plen));
    uint8 wire[64];
    size_t wlen = bufchainRead(&s->bufs.stream.send, wire, sizeof(wire));
    if (wlen != plen || memcmp(wire, payload, plen) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: wlen=${uint} != plen=${uint} || memcmp(wire, payload, plen) != 0"), stvar(uint64, wlen), stvar(uint64, plen));

    // Decode: raw wire bytes into the receive ring, run the recv seam, read plaintext back out.
    withMutex (&s->recvLock)
        bufringWrite(&s->bufs.stream.recv, payload, plen);
    netflow_filterStreamRecv(s->flow, q, s);

    uint8 got[64];
    size_t glen = netsocketRecv(s, got, sizeof(got), NULL, 0);
    if (glen != plen || memcmp(got, payload, plen) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: glen=${uint} != plen=${uint} || memcmp(got, payload, plen) != 0"), stvar(uint64, glen), stvar(uint64, plen));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// The recv seam through drainFlow: the first filtered stream data delivers an NFN_Secured
// notification exactly once, ordered ahead of the data it unlocked, and the decoded payload arrives
// on NET_DataReceived. A second chunk raises no further notification.
static int test_nettest_filter_secured(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .filterNotify = onSecured, .recv = onFilterRecv };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeSocket(q, NST_Stream);
    readySocket(s);
    netsocketSetHandlers(s, &handlers, &rec);

    NetPassStreamFactory* f = netpassstreamfactoryCreate();
    netsocketAddFilter(s, NetFilter(f));
    objRelease(&f);

    const uint8 first[] = "alpha";
    injectStream(q, s, first, sizeof(first) - 1);
    netqueueTick(q, 0);

    if (rec.securedCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.securedCount=${uint} != 1"), stvar(uint32, rec.securedCount));
    if (rec.recvCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 1"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 5 || memcmp(rec.seq, first, 5) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 5 || memcmp(rec.seq, first, 5) != 0"), stvar(uint32, rec.seqlen));

    const uint8 second[] = "beta";
    injectStream(q, s, second, sizeof(second) - 1);
    netqueueTick(q, 0);

    if (rec.securedCount != 1)   // the edge already fired; no second notification
        TEST_FAILV(ret, 1, _SL("the edge already fired; no second notification: : assertion failed: rec.securedCount=${uint} != 1"), stvar(uint32, rec.securedCount));
    if (rec.recvCount != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 2"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 9 || memcmp(rec.seq + 5, second, 4) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 9 || memcmp(rec.seq + 5, second, 4) != 0"), stvar(uint32, rec.seqlen));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A length-2 chain (app -> pass -> frame -> wire) composes across a partial-frame boundary: encode
// length-prefixes the payload, and decode reassembles it even when the frame is split over two wire
// reads, proving the boundary rings and the fixpoint driver hold buffered remainders.
static int test_nettest_filter_chain(void)
{
    int ret = 0;

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeSocket(q, NST_Stream);
    readySocket(s);

    // Filters are appended, so the first attached is the stage closest to the application: head =
    // pass, then frame at the wire. Encode runs pass then frame; decode runs frame then pass.
    NetPassStreamFactory* pf = netpassstreamfactoryCreate();
    NetFrameStreamFactory* ff = netframestreamfactoryCreate();
    netsocketAddFilter(s, NetFilter(pf));
    netsocketAddFilter(s, NetFilter(ff));
    objRelease(&pf);
    objRelease(&ff);

    if (saSize(s->filters) != 2 || !s->flow || saSize(s->flow->filters) != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: saSize(s->filters)=${int} != 2 || !s->flow || saSize(s->flow->filters)=${int} != 2"), stvar(int32, saSize(s->filters)), stvar(int32, saSize(s->flow->filters)));

    const uint8 payload[] = "framed-payload";
    size_t plen           = sizeof(payload) - 1;

    if (!netflow_filterStreamSend(s->flow, q, s, payload, plen))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflow_filterStreamSend(s->flow, q, s, payload, plen)"), stvNone);

    // The wire form is a 2-byte big-endian length prefix followed by the payload.
    uint8 wire[64];
    size_t wlen = bufchainRead(&s->bufs.stream.send, wire, sizeof(wire));
    if (wlen != plen + 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: wlen=${uint} != plen + 2"), stvar(uint64, wlen));
    if (wire[0] != 0 || wire[1] != (uint8)plen)
        TEST_FAILV(ret, 1, _SL("assertion failed: wire[0] != 0 || wire[1] != (uint8)plen=${uint}"), stvar(uint32, (uint8)plen));

    // Feed the wire bytes in two halves, decoding after each. The first (partial) half must yield
    // nothing; the second completes the frame and the whole payload appears.
    size_t half = wlen / 2;
    withMutex (&s->recvLock)
        bufringWrite(&s->bufs.stream.recv, wire, half);
    netflow_filterStreamRecv(s->flow, q, s);

    uint8 got[64];
    size_t glen = netsocketRecv(s, got, sizeof(got), NULL, 0);
    if (glen != 0)
        ret = 1;   // partial frame: nothing decoded yet

    withMutex (&s->recvLock)
        bufringWrite(&s->bufs.stream.recv, wire + half, wlen - half);
    netflow_filterStreamRecv(s->flow, q, s);

    glen = netsocketRecv(s, got, sizeof(got), NULL, 0);
    if (glen != plen || memcmp(got, payload, plen) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: glen=${uint} != plen=${uint} || memcmp(got, payload, plen) != 0"), stvar(uint64, glen), stvar(uint64, plen));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A filter attached to a datagram socket applies to every flow, whichever side of the attach the
// flow came into being on: one peer is promoted first, another arrives afterwards, and both end up
// with a chain.
static int test_nettest_filter_dgram_flows(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueue* q = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    // A flow that exists before the filter does.
    NetAddr p1     = peerAddr(1, 5000);
    NetFlow* early = netqueuePromoteFlow(q, s, &p1);
    if (!early || saSize(early->filters) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: !early || saSize(early->filters)=${int} != 0"), stvar(int32, saSize(early->filters)));

    NetPassDgramFactory* f = netpassdgramfactoryCreate();
    if (!netsocketAddFilter(s, NetFilter(f)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketAddFilter(s, NetFilter(f))"), stvNone);
    objRelease(&f);

    // Attaching reached back and built the chain on the flow that was already open...
    if (!early || saSize(early->filters) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: !early || saSize(early->filters)=${int} != 1"), stvar(int32, saSize(early->filters)));

    // ...and a peer that turns up afterwards gets one from the flow's own construction.
    NetAddr p2 = peerAddr(2, 5000);
    if (!inject(q, s, &p2, 7))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p2, 7)"), stvNone);
    NetFlow* late = netqueue_findFlow(q, s, &p2, false);
    if (!late || saSize(late->filters) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: !late || saSize(late->filters)=${int} != 1"), stvar(int32, saSize(late->filters)));

    // A stream-only filter is refused outright rather than producing a chain that cannot work.
    NetPassStreamFactory* sf = netpassstreamfactoryCreate();
    if (netsocketAddFilter(s, NetFilter(sf)))
        TEST_FAILV(ret, 1, _SL("assertion failed: netsocketAddFilter(s, NetFilter(sf))"), stvNone);
    if (saSize(s->filters) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: saSize(s->filters)=${int} != 1"), stvar(int32, saSize(s->filters)));
    objRelease(&sf);

    netqueueTick(q, 0);
    if (rec.recvCount != 1)   // the injected packet made it through the chain to the handler
        TEST_FAILV(ret, 1, _SL("the injected packet made it through the chain to the handler: : assertion failed: rec.recvCount=${uint} != 1"), stvar(uint32, rec.recvCount));

    objRelease(&late);
    objRelease(&early);
    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Datagram round trip: the encode seam turns a payload into wire messages addressed to the flow's
// peer, and an injected packet is decoded through the chain into a message the handler receives.
static int test_nettest_filter_dgram(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueue* q = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetPassDgramFactory* f = netpassdgramfactoryCreate();
    netsocketAddFilter(s, NetFilter(f));
    objRelease(&f);

    NetAddr p1    = peerAddr(1, 5000);
    NetFlow* flow = netqueuePromoteFlow(q, s, &p1);
    if (!flow || saSize(flow->filters) != 1)
        TEST_FAIL(1, _SL("assertion failed: !flow || saSize(flow->filters)=${int} != 1"), stvar(int32, saSize(flow->filters)));

    // Encode: the chain produces one wire message per payload, addressed to the peer the flow is
    // keyed on. Collected rather than sent, since the synthetic socket has no handle to send on.
    NetMsgQueue wire  = { 0 };
    const uint8 out[] = "datagram-out";
    if (!netflow_filterDatagramEncode(flow, q, out, sizeof(out) - 1, &wire, NULL))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflow_filterDatagramEncode(flow, q, out, sizeof(out) - 1, &wire, NULL)"), stvNone);

    NetMessage* m = netMsgQueuePop(&wire);
    if (!m || !m->buf || m->buf->len != sizeof(out) - 1 ||
        memcmp(m->buf->data, out, sizeof(out) - 1) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: !m || !m->buf || m->buf->len != sizeof(out) - 1 || memcmp(m->buf->data, out, sizeof(out) - 1) != 0"), stvNone);
    if (m && (m->addr.port != p1.port || memcmp(m->addr.ipv4, p1.ipv4, 4) != 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: m->addr.port=${uint} != p1.port=${uint} || memcmp(m->addr.ipv4, p1.ipv4, 4) != 0"),
                   stvar(uint32, m->addr.port), stvar(uint32, p1.port));
    if (netMsgQueuePop(&wire))
        ret = 1;   // exactly one message, nothing left behind
    if (m)
        netpoolFreeMsg(q->pool, &m);

    // Decode: an injected packet goes through the chain and is delivered to the handler.
    netqueueTick(q, 0);   // drain the NET_FlowOpen from the promote
    if (!inject(q, s, &p1, 42))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p1, 42)"), stvNone);
    netqueueTick(q, 0);
    if (rec.recvCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 1"), stvar(uint32, rec.recvCount));

    objRelease(&flow);
    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Attaching then removing filters leaves the socket and its flow back on the unfiltered fast path
// with no leaks (validated in aggregate by the alltests in-process run), and teardown with a live
// chain still delivers NET_FlowClosed.
static int test_nettest_filter_teardown(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .flowClosed = onClosed };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeSocket(q, NST_Stream);
    readySocket(s);
    netsocketSetHandlers(s, &handlers, &rec);

    // Build a chain, then tear it back down explicitly: both levels go, and so does the staging
    // ring that came with the flow's chain.
    NetPassStreamFactory* pf  = netpassstreamfactoryCreate();
    NetFrameStreamFactory* ff = netframestreamfactoryCreate();
    netsocketAddFilter(s, NetFilter(pf));
    netsocketAddFilter(s, NetFilter(ff));
    objRelease(&pf);
    objRelease(&ff);

    netsocketRemoveFilters(s);
    if (saSize(s->filters) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: saSize(s->filters)=${int} != 0"), stvar(int32, saSize(s->filters)));
    if (!s->flow || saSize(s->flow->filters) != 0 || s->flow->encIn)
        TEST_FAILV(ret, 1, _SL("assertion failed: !s->flow || saSize(s->flow->filters)=${int} != 0 || s->flow->encIn"), stvar(int32, saSize(s->flow->filters)));

    // Reattach and close with the chain live: shutdown() runs on the terminal path and the flow's
    // NET_FlowClosed is still delivered.
    NetPassStreamFactory* pf2 = netpassstreamfactoryCreate();
    netsocketAddFilter(s, NetFilter(pf2));
    objRelease(&pf2);

    netsocketClose(s);
    netqueueTick(q, 0);
    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));

    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Dropping a datagram chain that still holds messages returns their buffers to the pool instead of
// destroying them. This is the whole reason a stage keeps its own NetPool reference: a pooled
// buffer that gets bufDestroy'd never comes back, so the queue's receive ceiling shrinks for good
// -- for every socket sharing the pool, not just the one whose filter was removed.
static int test_nettest_filter_dgram_poolref(void)
{
    int ret = 0;

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetPassDgramFactory* f = netpassdgramfactoryCreate();
    netsocketAddFilter(s, NetFilter(f));
    objRelease(&f);

    NetAddr p1    = peerAddr(1, 5000);
    NetFlow* flow = netqueuePromoteFlow(q, s, &p1);
    if (!flow || saSize(flow->filters) != 1)
        TEST_FAIL(1, _SL("assertion failed: !flow || saSize(flow->filters)=${int} != 1"), stvar(int32, saSize(flow->filters)));

    // The chain builder is what hands a stage its pool, so that a concrete filter cannot forget to.
    NetDatagramFilter* stage = objDynCast(NetDatagramFilter, flow->filters.a[0]);
    if (!stage || stage->pool != q->pool || flow->pool != q->pool)
        TEST_FAILV(ret, 1, _SL("assertion failed: !stage || stage->pool=${ptr} != q->pool=${ptr} || flow->pool=${ptr} != q->pool=${ptr}"), stvar(ptr, stage->pool), stvar(ptr, q->pool), stvar(ptr, flow->pool), stvar(ptr, q->pool));

    // Strand a pooled message in each place a chain can be holding one when it is dropped: a
    // stage's two boundary queues, and the flow's staging queue in front of the chain. This is the
    // state a filter mid-handshake is normally in.
    NetMessage* held[3] = { 0 };
    if (stage) {
        held[0] = netpoolAllocMsg(stage->pool);
        held[1] = netpoolAllocMsg(stage->pool);
        held[2] = netpoolAllocMsg(flow->pool);
        if (!held[0] || !held[1] || !held[2])
            TEST_FAILV(ret, 1, _SL("assertion failed: !held[0] || !held[1] || !held[2]"), stvNone);
        else {
            netMsgQueuePush(&stage->encOut, held[0]);
            netMsgQueuePush(&stage->decOut, held[1]);
            netMsgQueuePush(&flow->encInMsgs, held[2]);
        }
    }

    if (bufpoolInUse(&q->pool->msgbuf) != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: bufpoolInUse(&q->pool->msgbuf)=${uint} != 3"), stvar(uint32, bufpoolInUse(&q->pool->msgbuf)));

    netsocketRemoveFilters(s);
    if (saSize(flow->filters) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: saSize(flow->filters)=${int} != 0"), stvar(int32, saSize(flow->filters)));

    // Every one of the three came back. Before the pool was its own object none of them could have:
    // there was no way to reach it from a stage being destroyed.
    if (bufpoolInUse(&q->pool->msgbuf) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: bufpoolInUse(&q->pool->msgbuf)=${uint} != 0"), stvar(uint32, bufpoolInUse(&q->pool->msgbuf)));

    objRelease(&flow);
    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The requeue race
//
// The subtle part of the claim protocol is the handoff between "worker finishes draining" and
// "ingest pushes a new packet". Both sides re-check after their store so that a packet arriving
// exactly at the release point cannot be stranded. This is the test that actually exercises it:
// several producers push into a small number of flows while several consumers race to claim them.
//
// What must hold: every message is delivered exactly once, and no two workers are ever inside the
// same flow at the same time.
// ---------------------------------------------------------------------------------------------

#define RACE_PRODUCERS 4
#define RACE_CONSUMERS 4
#define RACE_FLOWS     8
#define RACE_MSGS      2000

typedef struct RaceState {
    NetQueue* q;
    NetSocket* sock;
    atomic(uint32) delivered;
    atomic(uint32) busy[RACE_FLOWS];   // must never exceed 1 for any single flow
    atomic(uint32) overlaps;           // times two workers were inside one flow at once
    atomic(uint32) producersDone;
    atomic(bool) stop;
} RaceState;

static RaceState raceState;

static void raceRecv(NetEvent* ev)
{
    // The claim protocol promises that events for one flow never execute concurrently. Catch a
    // violation by marking this flow busy for the duration of the callback; the peer's last
    // address octet identifies the flow.
    uint32 idx = ev->flow->peer.ipv4[3] - 1;

    if (atomicFetchAdd(uint32, &raceState.busy[idx], 1, AcqRel) != 0)
        atomicFetchAdd(uint32, &raceState.overlaps, 1, AcqRel);

    atomicFetchAdd(uint32, &raceState.delivered, 1, AcqRel);

    atomicFetchSub(uint32, &raceState.busy[idx], 1, AcqRel);
}

static int raceProducer(Thread* self)
{
    for (uint32 i = 0; i < RACE_MSGS; i++) {
        NetAddr p = peerAddr((uint8)(i % RACE_FLOWS) + 1, 7000);

        // Producers deliberately outrun the buffer pool, which is the designed behavior -- a dry
        // pool drops the datagram rather than allocating. Retry here so the expected delivery
        // count stays exact, and so the recycling path gets exercised under contention.
        while (!inject(raceState.q, raceState.sock, &p, (uint8)i))
            osYield();
    }

    atomicFetchAdd(uint32, &raceState.producersDone, 1, AcqRel);
    return 0;
}

static int raceConsumer(Thread* self)
{
    for (;;) {
        if (!netqueue_dispatch(raceState.q)) {
            if (atomicLoad(bool, &raceState.stop, Acquire))
                break;
            osYield();
        }
    }
    return 0;
}

static int test_nettest_flow_race(void)
{
    int ret = 0;

    static const NetHandlers handlers = { .recv = raceRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.maxflows  = 64;
    conf.noReclaim = true;   // the point of this test is the requeue race, not reclamation

    memset(&raceState, 0, sizeof(raceState));
    raceState.q = makeQueue(&conf);
    netqueueSetHandlers(raceState.q, &handlers, NULL);
    raceState.sock = makeSocket(raceState.q, NST_Datagram);

    Thread* producers[RACE_PRODUCERS];
    Thread* consumers[RACE_CONSUMERS];

    for (int i = 0; i < RACE_CONSUMERS; i++)
        consumers[i] = thrCreate(raceConsumer, _S"raceConsumer", stvar(int32, i));
    for (int i = 0; i < RACE_PRODUCERS; i++)
        producers[i] = thrCreate(raceProducer, _S"raceProducer", stvar(int32, i));

    for (int i = 0; i < RACE_PRODUCERS; i++) {
        if (!thrWait(producers[i], timeS(60)))
            TEST_FAILV(ret, 1, _SL("assertion failed: !thrWait(producers[i], timeS(60))"), stvNone);
        thrRelease(&producers[i]);
    }

    // Every push is complete, so once each flow drains it stays drained; the consumers can stop
    // the next time they find the runqueue empty.
    atomicStore(bool, &raceState.stop, true, Release);

    for (int i = 0; i < RACE_CONSUMERS; i++) {
        if (!thrWait(consumers[i], timeS(60)))
            TEST_FAILV(ret, 1, _SL("assertion failed: !thrWait(consumers[i], timeS(60))"), stvNone);
        thrRelease(&consumers[i]);
    }

    uint32 expected  = RACE_PRODUCERS * RACE_MSGS;
    uint32 delivered = atomicLoad(uint32, &raceState.delivered, Acquire);

    // Exactly once: nothing stranded in an inbox, nothing delivered twice.
    if (delivered != expected) {
        TEST_FAILV(ret, 1, _SL("exactly-once delivery violated: expected ${uint}, delivered ${uint}"),
                   stvar(uint32, expected), stvar(uint32, delivered));
    }

    // Per-flow serialization: two workers inside one flow at the same time is the bug this whole
    // protocol exists to prevent.
    uint32 overlaps = atomicLoad(uint32, &raceState.overlaps, Acquire);
    if (overlaps != 0) {
        TEST_FAILV(ret, 1, _SL("flow serialization violated ${uint} times"), stvar(uint32, overlaps));
    }

    netsocketClose(raceState.sock);
    objRelease(&raceState.sock);
    netqueueShutdown(raceState.q, 0);
    objRelease(&raceState.q);

    return ret;
}

// ---------------------------------------------------------------------------------------------
// Timers
//
// The deadline heap and its delivery path, driven through the synthetic backend so the timing is
// deterministic: a timer armed with a delay of 0 is due the instant the next sweep looks, which
// makes "fires exactly once per tick" a real assertion rather than a race against the clock. The
// wait-bounding and wake half of the facility needs a backend that actually sleeps, so it lives
// with the select suite below.
// ---------------------------------------------------------------------------------------------

// Records each datagram's payload byte, so the ordering test can assert the exact interleave of
// packets and timers rather than just their counts.
static void onSeqRecv(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->recvCount++;
    if (ev->recv.msg && ev->recv.msg->buf && ev->recv.msg->buf->len > 0 &&
        r->seqlen < sizeof(r->seq))
        r->seq[r->seqlen++] = ev->recv.msg->buf->data[0];
}

static void onTimer(NetEvent* ev)
{
    Recorder* r    = (Recorder*)ev->ctx;
    r->timerCount++;
    r->lastTimerId = ev->timer.id;
    if (r->seqlen < sizeof(r->seq))
        r->seq[r->seqlen++] = 0xff;   // 0xff marks a timer; packets record their own payload byte
}

// Build a stream socket registered with the queue, which gives us its single flow to arm timers on
// without needing a connection: a stream flow exists from socket init.
static NetSocket* makeTimerSocket(NetQueue* q, Recorder* rec, const NetHandlers* handlers)
{
    NetSocket* s = netqueueSocket(q, NST_Stream);
    netqueueAddSocket(q, s);
    netsocketSetHandlers(s, handlers, rec);
    return s;
}

// A timer fires once, carries its own id, and a timer that is not due yet stays put.
static int test_nettest_timer_basic(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    NetTimerId id = netflowAddTimer(s->flow, 0, NTF_None);
    if (id == 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: id=${uint} == 0"), stvar(uint64, id));

    netqueueTick(q, 0);
    if (rec.timerCount != 1 || rec.lastTimerId != id)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1 || rec.lastTimerId=${uint} != id=${uint}"), stvar(uint32, rec.timerCount), stvar(uint64, rec.lastTimerId), stvar(uint64, id));

    // One-shot: a second sweep must not produce it again, and the heap must be empty.
    netqueueTick(q, 0);
    if (rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1"), stvar(uint32, rec.timerCount));
    if (q->ntimers != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 0"), stvar(uint32, q->ntimers));

    // Ids are never reused, so a second timer is distinguishable from the first.
    NetTimerId later = netflowAddTimer(s->flow, timeS(60), NTF_None);
    if (later == 0 || later == id)
        TEST_FAILV(ret, 1, _SL("assertion failed: later=${uint} == 0 || later=${uint} == id=${uint}"), stvar(uint64, later), stvar(uint64, later), stvar(uint64, id));

    netqueueTick(q, 0);
    if (rec.timerCount != 1)
        ret = 1;   // not due
    if (q->ntimers != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 1"), stvar(uint32, q->ntimers));

    if (!netflowCancelTimer(s->flow, later))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowCancelTimer(s->flow, later)"), stvNone);

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Cancel takes a timer out of the running and reports whether it was this call that did so -- the
// property the connect state machine uses to arbitrate between a completion and a timeout.
static int test_nettest_timer_cancel(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    NetTimerId id = netflowAddTimer(s->flow, timeS(60), NTF_None);

    if (!netflowCancelTimer(s->flow, id))
        ret = 1;   // this call is the one that removed it
    if (netflowCancelTimer(s->flow, id))
        ret = 1;   // a second cancel of the same timer loses
    if (q->ntimers != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 0"), stvar(uint32, q->ntimers));

    netqueueTick(q, 0);
    if (rec.timerCount != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 0"), stvar(uint32, rec.timerCount));

    // Cancelling a timer that has already been popped for delivery must fail too, so the winner of
    // that race is unambiguous.
    NetTimerId spent = netflowAddTimer(s->flow, 0, NTF_None);
    netqueueTick(q, 0);
    if (rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1"), stvar(uint32, rec.timerCount));
    if (netflowCancelTimer(s->flow, spent))
        TEST_FAILV(ret, 1, _SL("assertion failed: netflowCancelTimer(s->flow, spent)"), stvNone);

    // An id that was never handed out is a harmless miss, not a crash.
    if (netflowCancelTimer(s->flow, 0) || netflowCancelTimer(s->flow, 999999))
        TEST_FAILV(ret, 1, _SL("assertion failed: netflowCancelTimer(s->flow, 0) || netflowCancelTimer(s->flow, 999999)"), stvNone);

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Rearm moves a deadline in both directions and keeps the id, which is what an idle timeout pushed
// out on every packet needs.
static int test_nettest_timer_rearm(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    // Two timers, so the rearm below has to sift past a sibling rather than sitting alone at the
    // root where any implementation would look correct.
    NetTimerId other = netflowAddTimer(s->flow, timeS(30), NTF_None);
    NetTimerId id    = netflowAddTimer(s->flow, timeS(60), NTF_None);

    netqueueTick(q, 0);
    if (rec.timerCount != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 0"), stvar(uint32, rec.timerCount));

    // Pull it in front of `other` and make it due.
    if (!netflowRearmTimer(s->flow, id, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowRearmTimer(s->flow, id, 0)"), stvNone);

    netqueueTick(q, 0);
    if (rec.timerCount != 1 || rec.lastTimerId != id)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1 || rec.lastTimerId=${uint} != id=${uint}"), stvar(uint32, rec.timerCount), stvar(uint64, rec.lastTimerId), stvar(uint64, id));
    if (q->ntimers != 1)
        ret = 1;   // `other` is still armed

    // Rearming a spent timer fails rather than resurrecting it.
    if (netflowRearmTimer(s->flow, id, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: netflowRearmTimer(s->flow, id, 0)"), stvNone);

    // Pushing a deadline out is the other direction, and must not fire.
    if (!netflowRearmTimer(s->flow, other, timeS(60)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowRearmTimer(s->flow, other, timeS(60))"), stvNone);
    netqueueTick(q, 0);
    if (rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1"), stvar(uint32, rec.timerCount));

    if (!netflowCancelTimer(s->flow, other))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowCancelTimer(s->flow, other)"), stvNone);

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// NTF_Repeat re-arms itself, keeps its id across firings, and stops for good on one cancel.
static int test_nettest_timer_repeat(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    NetTimerId id = netflowAddTimer(s->flow, timeMS(1), NTF_Repeat);

    // One firing per sweep, not a spin: the sweep pins `now` for the whole pass and a re-arm always
    // lands strictly after it, so a repeat cannot fire twice in the pass that re-armed it. The
    // sleep is longer than the interval, so each tick has exactly one period to collect.
    for (uint32 i = 1; i <= 3; i++) {
        osSleep(timeMS(3));
        netqueueTick(q, 0);
        if (rec.timerCount != i || rec.lastTimerId != id)
            TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != i || rec.lastTimerId=${uint} != id=${uint}"), stvar(uint32, rec.timerCount), stvar(uint64, rec.lastTimerId), stvar(uint64, id));
    }

    if (q->ntimers != 1)
        ret = 1;   // still armed between firings

    if (!netflowCancelTimer(s->flow, id))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netflowCancelTimer(s->flow, id)"), stvNone);

    osSleep(timeMS(3));
    netqueueTick(q, 0);
    if (rec.timerCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 3"), stvar(uint32, rec.timerCount));
    if (q->ntimers != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 0"), stvar(uint32, q->ntimers));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A timer rides the flow's inbox, so it lands behind data that was already queued when it came due
// -- the guarantee that stops a request deadline from overtaking the response that satisfied it.
static int test_nettest_timer_ordering(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onSeqRecv, .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p = peerAddr(1, 5000);

    // First packet creates the flow; the rest queue behind it. Nothing is dispatched yet.
    for (uint8 i = 0; i < 4; i++) {
        if (!inject(q, s, &p, i))
            TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p, i)"), stvNone);
    }

    NetFlow* flow = netqueue_findFlow(q, s, &p, false);
    if (!flow) {
        netsocketClose(s);
        objRelease(&s);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !flow"), stvNone);
    }

    // Due immediately, but queued after four packets that are already waiting.
    NetTimerId id = netflowAddTimer(flow, 0, NTF_None);
    if (id == 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: id=${uint} == 0"), stvar(uint64, id));
    objRelease(&flow);

    netqueueTick(q, 0);

    if (rec.recvCount != 4 || rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 4 || rec.timerCount=${uint} != 1"), stvar(uint32, rec.recvCount), stvar(uint32, rec.timerCount));

    // Payloads 0..3, then 0xff for the timer.
    if (rec.seqlen != 5)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 5"), stvar(uint32, rec.seqlen));
    else if (rec.seq[0] != 0 || rec.seq[1] != 1 || rec.seq[2] != 2 || rec.seq[3] != 3 ||
             rec.seq[4] != 0xff)
        TEST_FAILV(ret, 1, _SL("expected seq 0,1,2,3,0xff; got ${int},${int},${int},${int},${int}"),
                   stvar(int32, rec.seq[0]), stvar(int32, rec.seq[1]), stvar(int32, rec.seq[2]), stvar(int32, rec.seq[3]), stvar(int32, rec.seq[4]));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Teardown cancels whatever the flow still had armed: no NET_Timer after NET_FlowClosed, and no
// entry left holding the flow alive until a long deadline expires.
static int test_nettest_timer_flowclose(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv, .flowClosed = onClosed, .timer = onTimer };

    NetQueue* q  = makeQueue(NULL);
    netqueueSetHandlers(q, &handlers, &rec);
    NetSocket* s = makeSocket(q, NST_Datagram);

    NetAddr p = peerAddr(1, 5000);
    if (!inject(q, s, &p, 7))
        TEST_FAILV(ret, 1, _SL("assertion failed: !inject(q, s, &p, 7)"), stvNone);
    netqueueTick(q, 0);

    NetFlow* flow = netqueue_findFlow(q, s, &p, false);
    if (!flow) {
        netsocketClose(s);
        objRelease(&s);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !flow"), stvNone);
    }

    netflowAddTimer(flow, timeS(60), NTF_None);
    netflowAddTimer(flow, timeS(90), NTF_None);
    if (q->ntimers != 2)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 2"), stvar(uint32, q->ntimers));

    netflowClose(flow);
    objRelease(&flow);

    netqueueTick(q, 0);

    if (rec.closeCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.closeCount=${uint} != 1"), stvar(uint32, rec.closeCount));
    if (rec.timerCount != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 0"), stvar(uint32, rec.timerCount));
    if (q->ntimers != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: q->ntimers=${uint} != 0"), stvar(uint32, q->ntimers));
    // Nothing is left pinning the flow, so the queue's count is back to zero rather than waiting
    // out the 60 second deadline.
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 0"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Select backend, real sockets
//
// The tests above drive the core through a synthetic backend. These drive the *actual* select
// backend end to end over loopback: real socket creation, select() readiness, the netSock* recv
// shims, pooled buffers and the receive ring, demux, dispatch, and event delivery. Runs on every
// platform with a real socket layer (Windows, Unix, and WASM); select_* tests are grouped together
// and run everywhere in this guard, followed by the iocp_* suite (Windows only -- there is no IOCP
// stub on other platforms, so these do not even compile elsewhere), then epoll_* (Linux only) and
// kqueue_* (FreeBSD only) further down. WASM has no performance backend at all -- select is the
// only queue backend Emscripten's syscall emulation supports, so select_* is the entire suite for
// that platform.
// ---------------------------------------------------------------------------------------------

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)

// 127.0.0.1 in NetAddr's host-order storage, where ipv4[0] is the least significant octet.
static NetAddr loopbackAddr(uint16 port)
{
    NetAddr a = { .type = NA_IPv4, .port = port };
    a.ipv4[3] = 127;
    a.ipv4[2] = 0;
    a.ipv4[1] = 0;
    a.ipv4[0] = 1;
    return a;
}

// Tick until the recorder has seen `want` receives, or the budget runs out. Loopback delivery is
// effectively immediate, so this normally returns on the first productive tick.
static void tickUntil(NetQueue* q, Recorder* r, uint32 want)
{
    for (int i = 0; i < 20 && r->recvCount < want; i++)
        netqueueTick(q, 100);
}

// Drain a stream socket's ring inside its recv handler, accumulating the bytes so the test can
// verify the payload survived the reserve/commit/ring round trip. Used by both the udp/stream
// suites below and connectBody() further down.
static void onStreamRecv(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->recvCount++;

    uint8 buf[256];
    size_t n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0);
    for (size_t i = 0; i < n && r->seqlen < sizeof(r->seq); i++)
        r->seq[r->seqlen++] = buf[i];
}

// ---------------------------------------------------------------------------------------------
// Connect path: the client half, over real loopback sockets, on both backends. A
// NetSocket connects out; netsocketConnect() resolves (or takes the literal), tries each address in
// order with a fresh handle, and delivers NET_Connection through the flow. These drive the whole
// state machine -- resolver queue, sequential fallback, per-attempt timeout sweep -- end to end.
// ---------------------------------------------------------------------------------------------

// A loopback listener pair: v4 always live, v6 best-effort on the same port number. "localhost"
// can resolve to either family depending on the OS/resolver -- listening on both means the connect
// test passes regardless of which one the client picks, instead of depending on ::1 and
// 127.0.0.1 racing to be first in getaddrinfo's answer.
typedef struct LoopbackListeners {
    SOCKET v4;   // INVALID_SOCKET only if the v4 bind itself failed (fatal -- v4 must always work)
    SOCKET v6;   // INVALID_SOCKET if the platform/environment has no usable IPv6 loopback
} LoopbackListeners;

// Bind loopback listeners on both families at the same port, and start listening. v4 binds an
// ephemeral port first (it is the one guaranteed to succeed everywhere); v6 then binds that exact
// port on ::1, which does not conflict with the v4 bind since they are different local addresses.
// Returns false only if the v4 bind fails -- a missing v6 stack degrades to the old v4-only
// behavior rather than failing the test.
static bool makeLoopbackListeners(LoopbackListeners* out, uint16* outPort)
{
    out->v4 = out->v6 = INVALID_SOCKET;

    out->v4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (out->v4 == INVALID_SOCKET)
        return false;

    struct sockaddr_in la4;
    memset(&la4, 0, sizeof(la4));
    la4.sin_family      = AF_INET;
    la4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la4.sin_port        = 0;
    if (bind(out->v4, (struct sockaddr*)&la4, sizeof(la4)) != 0 || listen(out->v4, 4) != 0) {
        closesocket(out->v4);
        out->v4 = INVALID_SOCKET;
        return false;
    }

    int la4len = sizeof(la4);
    getsockname(out->v4, (struct sockaddr*)&la4, &la4len);
    *outPort = ntohs(la4.sin_port);

    SOCKET v6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (v6 == INVALID_SOCKET)
        return true;   // no IPv6 support at all -- fine, v4-only is the fallback

    struct sockaddr_in6 la6;
    memset(&la6, 0, sizeof(la6));
    la6.sin6_family = AF_INET6;
    la6.sin6_addr   = in6addr_loopback;
    la6.sin6_port   = htons(*outPort);
    if (bind(v6, (struct sockaddr*)&la6, sizeof(la6)) == 0 && listen(v6, 4) == 0)
        out->v6 = v6;
    else
        closesocket(v6);   // IPv6 disabled, port taken on that family, or similarly non-fatal

    return true;
}

static void closeLoopbackListeners(LoopbackListeners* l)
{
    if (l->v4 != INVALID_SOCKET)
        closesocket(l->v4);
    if (l->v6 != INVALID_SOCKET)
        closesocket(l->v6);
}

// Accept from whichever of the pair actually has the completed connection. Called only after the
// client's NET_Connection(Connected) has already been observed, so the handshake is done and
// exactly one of the two listeners (whichever family the client resolved to) has it queued in its
// backlog -- select with a generous timeout picks it out without guessing the family ourselves.
static SOCKET acceptFromEither(LoopbackListeners* l)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    SOCKET maxfd = 0;
    if (l->v4 != INVALID_SOCKET) {
        FD_SET(l->v4, &rfds);
        maxfd = max(maxfd, l->v4);
    }
    if (l->v6 != INVALID_SOCKET) {
        FD_SET(l->v6, &rfds);
        maxfd = max(maxfd, l->v6);
    }

    struct timeval tv = { 2, 0 };
    if (select((int)maxfd + 1, &rfds, NULL, NULL, &tv) <= 0)
        return INVALID_SOCKET;

    if (l->v4 != INVALID_SOCKET && FD_ISSET(l->v4, &rfds))
        return accept(l->v4, NULL, NULL);
    if (l->v6 != INVALID_SOCKET && FD_ISSET(l->v6, &rfds))
        return accept(l->v6, NULL, NULL);
    return INVALID_SOCKET;
}

// Tick until a NET_Connection has been delivered or the budget runs out.
static void tickUntilConn(NetQueue* q, Recorder* r, int maxTicks)
{
    for (int i = 0; i < maxTicks && r->connCount == 0; i++)
        netqueueTick(q, 100);
}

// Shared body: connect a NetSocket to a loopback listener via `host` (a literal or "localhost"),
// assert the NET_Connection(Connected) event, then move bytes both ways. Consumes q.
static int connectBody(NetQueue* q, strref host)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .connection = onConnect, .recv = onStreamRecv };
    netqueueSetHandlers(q, &handlers, &rec);

    uint16 port = 0;
    LoopbackListeners listeners;
    if (!makeLoopbackListeners(&listeners, &port)) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("couldn't set up loopback listeners to connect '${string}' to"), stvar(strref, host));
    }

    NetSocket* csock = netqueueSocket(q, NST_Stream);
    if (!csock) {
        closeLoopbackListeners(&listeners);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("netqueueSocket() returned NULL for a stream socket to '${string}'"), stvar(strref, host));
    }
    netqueueAddSocket(q, csock);

    if (!netsocketConnect(csock, host, port))
        TEST_FAILV(ret, 1, _SL("netsocketConnect() to '${string}':${uint} failed to start"), stvar(strref, host), stvar(uint32, port));

    tickUntilConn(q, &rec, 50);
    if (rec.connCount != 1 || rec.connState != NCS_Connected)
        TEST_FAILV(ret, 1, _SL("connecting to '${string}': expected 1 Connected event, got ${uint} events ending in state ${int}"),
                   stvar(strref, host), stvar(uint32, rec.connCount), stvar(int32, rec.connState));

    // Only accept once the connect actually completed -- otherwise nothing is in either backlog yet.
    SOCKET server = INVALID_SOCKET;
    if (rec.connState == NCS_Connected) {
        // The OS completed the handshake into one of the two backlogs (whichever family the client
        // resolved to), so this returns at once rather than actually waiting out its timeout.
        server = acceptFromEither(&listeners);
    }
    closeLoopbackListeners(&listeners);

    if (server == INVALID_SOCKET) {
        TEST_FAILV(ret, 1, _SL("no raw peer accepted a connection from '${string}' after it reported Connected"), stvar(strref, host));
    } else {
        // server -> client: the raw peer sends, the NetSocket ingests and delivers it.
        const char down[] = "downstream";   // 10 bytes
        send(server, down, 10, 0);
        for (int i = 0; i < 20 && rec.recvCount == 0; i++)
            netqueueTick(q, 100);
        if (rec.seqlen != 10 || memcmp(rec.seq, down, 10) != 0)
            TEST_FAILV(ret, 1, _SL("downstream payload mismatch: expected 10 bytes, got ${uint}"), stvar(uint32, rec.seqlen));

        // client -> server: netsocketSend() out the connected NetSocket, read off the raw peer.
        const char up[] = "upstream";   // 8 bytes
        if (!netsocketSend(csock, (uint8*)up, 8, NULL, 0))
            TEST_FAILV(ret, 1, _SL("netsocketSend() of the upstream payload failed"), stvNone);

        u_long nb = 1;
        ioctlsocket(server, FIONBIO, &nb);   // so the drain never blocks between ticks
        char rbuf[32];
        int got = 0;
        for (int i = 0; i < 20 && got < 8; i++) {
            int n = recv(server, rbuf + got, sizeof(rbuf) - got, 0);
            if (n > 0)
                got += n;
            else
                netqueueTick(q, 50);   // an IOCP overlapped send drains on its completion
        }
        if (got != 8 || memcmp(rbuf, up, 8) != 0)
            TEST_FAILV(ret, 1, _SL("upstream payload mismatch: expected 8 bytes, got ${int}"), stvar(int32, got));
        closesocket(server);
    }

    netsocketClose(csock);
    objRelease(&csock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connect to a port with nothing listening: every resolved address is refused, so the sequential
// fallback exhausts and NET_Connection(NotConnected) is delivered with a non-timeout error. Consumes q.
static int connectRefusedBody(NetQueue* q)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .connection = onConnect };
    netqueueSetHandlers(q, &handlers, &rec);

    // Grab an ephemeral port by binding, then close it so nothing listens there.
    SOCKET tmp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    bind(tmp, (struct sockaddr*)&a, sizeof(a));
    int alen = sizeof(a);
    getsockname(tmp, (struct sockaddr*)&a, &alen);
    uint16 port = ntohs(a.sin_port);
    closesocket(tmp);

    NetSocket* csock = netqueueSocket(q, NST_Stream);
    if (!csock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !csock"), stvNone);
    }
    netqueueAddSocket(q, csock);

    if (!netsocketConnect(csock, _SL("127.0.0.1"), port))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketConnect(csock, _SL(\"127.0.0.1\"), port)"), stvNone);

    tickUntilConn(q, &rec, 50);
    if (rec.connCount != 1 || rec.connState != NCS_NotConnected)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.connCount=${uint} != 1 || rec.connState=${int} != NCS_NotConnected"), stvar(uint32, rec.connCount), stvar(int32, rec.connState));
    if (rec.connErr == NERR_None || rec.connErr == NERR_Timeout)
        ret = 1;   // a refusal, not success and not a timeout

    netsocketClose(csock);
    objRelease(&csock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connect to a black-holed address must hit the short per-attempt timeout rather than the OS's
// multi-second SYN default. Consumes q (built with a short connectTimeout).
static int connectTimeoutBody(NetQueue* q)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .connection = onConnect };
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* csock = netqueueSocket(q, NST_Stream);
    if (!csock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !csock"), stvNone);
    }
    netqueueAddSocket(q, csock);

    // 192.0.2.0/24 (TEST-NET-1, RFC 5737) is reserved and unroutable, so the SYN is black-holed and
    // the attempt can only end at the timeout.
    int64 start = clockTimer();
    if (!netsocketConnect(csock, _SL("192.0.2.1"), 9))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketConnect(csock, _SL(\"192.0.2.1\"), 9)"), stvNone);

    for (int i = 0; i < 100 && rec.connCount == 0; i++)
        netqueueTick(q, 100);
    int64 elapsed = clockTimer() - start;

    if (rec.connCount != 1 || rec.connState != NCS_NotConnected)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.connCount=${uint} != 1 || rec.connState=${int} != NCS_NotConnected"), stvar(uint32, rec.connCount), stvar(int32, rec.connState));
    if (rec.connErr != NERR_Timeout)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.connErr=${int} != NERR_Timeout"), stvar(int32, rec.connErr));
    if (elapsed > timeS(5))
        ret = 1;   // fired at the short connectTimeout, not the OS ~21s default

    netsocketClose(csock);
    objRelease(&csock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Shared body: a NetSocket listener bound to loopback accepts an inbound raw-socket connection and
// delivers NET_Accepted. With autoAccept the queue registers the accepted socket itself; without it
// the test adds it by hand. Either way, bytes then move both directions over the accepted socket.
// Consumes q.
static int acceptBody(NetQueue* q, bool autoAccept)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .accepted = onAccept, .recv = onStreamRecv };
    netqueueSetHandlers(q, &handlers, &rec);

    // Listener: a NetSocket bound to an ephemeral loopback port, then listening.
    NetSocket* lsock = netqueueSocket(q, NST_Stream);
    if (!lsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !lsock"), stvNone);
    }

    NetAddr la;
    netAddrFromStr(&la, _SL("127.0.0.1"));
    la.port = 0;
    if (!netsocketBind(lsock, &la))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(lsock, &la)"), stvNone);

    // Discover the OS-assigned port off the socket's handle.
    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)lsock->handle, (struct sockaddr*)&sa, &salen);
    uint16 port = ntohs(sa.sin_port);

    netqueueAddSocket(q, lsock);
    if (!netsocketListen(lsock, 4))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketListen(lsock, 4)"), stvNone);

    // A raw client connects. Loopback completes the handshake into the listen backlog without the
    // NetSocket having accepted yet, so a blocking connect returns promptly.
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in ca;
    memset(&ca, 0, sizeof(ca));
    ca.sin_family      = AF_INET;
    ca.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ca.sin_port        = htons(port);
    if (client == INVALID_SOCKET || connect(client, (struct sockaddr*)&ca, sizeof(ca)) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: client == INVALID_SOCKET || connect(client, (struct sockaddr*)&ca, sizeof(ca)) != 0"), stvNone);

    // Tick until the accept is delivered.
    for (int i = 0; i < 50 && rec.acceptCount == 0; i++)
        netqueueTick(q, 100);
    if (rec.acceptCount != 1 || !rec.accepted)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.acceptCount=${uint} != 1 || !rec.accepted"), stvar(uint32, rec.acceptCount));

    NetSocket* server = rec.accepted;   // recorded with an acquired reference in onAccept

    if (server) {
        // Without auto-accept the socket is not yet managed; register it now so it is serviced.
        if (!autoAccept)
            netqueueAddSocket(q, server);

        // client -> server: the raw client sends, the accepted NetSocket ingests and delivers it.
        const char down[] = "downstream";   // 10 bytes
        send(client, down, 10, 0);
        for (int i = 0; i < 20 && rec.recvCount == 0; i++)
            netqueueTick(q, 100);
        if (rec.seqlen != 10 || memcmp(rec.seq, down, 10) != 0)
            TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 10 || memcmp(rec.seq, down, 10) != 0"), stvar(uint32, rec.seqlen));

        // server -> client: netsocketSend() out the accepted NetSocket, read off the raw client.
        const char up[] = "upstream";   // 8 bytes
        if (!netsocketSend(server, (uint8*)up, 8, NULL, 0))
            TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(server, (uint8*)up, 8, NULL, 0)"), stvNone);

        u_long nb = 1;
        ioctlsocket(client, FIONBIO, &nb);   // so the drain never blocks between ticks
        char rbuf[32];
        int got = 0;
        for (int i = 0; i < 20 && got < 8; i++) {
            int n = recv(client, rbuf + got, sizeof(rbuf) - got, 0);
            if (n > 0)
                got += n;
            else
                netqueueTick(q, 50);   // an IOCP overlapped send drains on its completion
        }
        if (got != 8 || memcmp(rbuf, up, 8) != 0)
            TEST_FAILV(ret, 1, _SL("assertion failed: got=${int} != 8 || memcmp(rbuf, up, 8) != 0"), stvar(int32, got));

        netsocketClose(server);
        objRelease(&server);
    }

    if (client != INVALID_SOCKET)
        closesocket(client);
    netsocketClose(lsock);
    objRelease(&lsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Composed helpers: netqueueListen() and netqueueConnect() compose the whole setup
// -- factory, addSocket, handlers, and the bind/listen or connect -- so this drives a complete
// loopback round trip using nothing but the two helpers on the default backend, plus the failure
// path (a helper that cannot finish returns NULL and leaves nothing behind on the queue).
// ---------------------------------------------------------------------------------------------

static int test_nettest_helpers(void)
{
    int ret       = 0;
    Recorder lrec = { 0 }, crec = { 0 }, srec = { 0 };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_AutoAccept;   // accepted sockets join the queue without an addSocket by hand
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);

    // A bind that cannot succeed (no address family) must fail the whole helper: NULL back, and
    // the half-constructed socket removed from the queue rather than left behind unserviceable.
    NetAddr bad = { .type = NA_Unknown };
    if (netqueueListen(q, &bad, 4, NULL, NULL) != NULL)
        TEST_FAILV(ret, 1, _SL("assertion failed: netqueueListen(q, &bad, 4, NULL, NULL) != NULL"), stvNone);
    withReadLock (&q->lock) {
        if (htSize(q->sockets) != 0)
            TEST_FAILV(ret, 1, _SL("assertion failed: htSize(q->sockets)=${uint} != 0"), stvar(uint32, htSize(q->sockets)));
    }

    // Listener on an ephemeral loopback port, one call.
    static const NetHandlers lhandlers = { .accepted = onAccept };
    NetAddr la       = loopbackAddr(0);
    NetSocket* lsock = netqueueListen(q, &la, 4, &lhandlers, &lrec);
    if (!lsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !lsock"), stvNone);
    }

    // Learn the OS-assigned port off the listener's handle.
    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)lsock->handle, (struct sockaddr*)&sa, &salen);
    uint16 port = ntohs(sa.sin_port);

    // Client to that port, one call. Handlers were registered by the helper before the connect
    // began, so the NET_Connection event cannot be missed.
    static const NetHandlers chandlers = { .connection = onConnect, .recv = onStreamRecv };
    NetSocket* csock = netqueueConnect(q, _SL("127.0.0.1"), port, &chandlers, &crec);
    if (!csock)
        TEST_FAILV(ret, 1, _SL("assertion failed: !csock"), stvNone);

    tickUntilConn(q, &crec, 50);
    if (crec.connCount != 1 || crec.connState != NCS_Connected)
        TEST_FAILV(ret, 1, _SL("assertion failed: crec.connCount=${uint} != 1 || crec.connState=${int} != NCS_Connected"), stvar(uint32, crec.connCount), stvar(int32, crec.connState));

    for (int i = 0; i < 50 && lrec.acceptCount == 0; i++)
        netqueueTick(q, 100);
    if (lrec.acceptCount != 1 || !lrec.accepted)
        TEST_FAILV(ret, 1, _SL("assertion failed: lrec.acceptCount=${uint} != 1 || !lrec.accepted"), stvar(uint32, lrec.acceptCount));

    NetSocket* server = lrec.accepted;   // recorded with an acquired reference in onAccept
    if (server && csock && ret == 0) {
        // Register the server side's recv handler before any bytes move, then send a payload each
        // direction between the two NetSockets -- no raw peer socket anywhere in this test.
        static const NetHandlers shandlers = { .recv = onStreamRecv };
        netsocketSetHandlers(server, &shandlers, &srec);

        const char up[] = "upstream";   // 8 bytes
        if (!netsocketSend(csock, (uint8*)up, 8, NULL, 0))
            TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(csock, (uint8*)up, 8, NULL, 0)"), stvNone);
        for (int i = 0; i < 20 && srec.recvCount == 0; i++)
            netqueueTick(q, 100);
        if (srec.seqlen != 8 || memcmp(srec.seq, up, 8) != 0)
            TEST_FAILV(ret, 1, _SL("assertion failed: srec.seqlen=${uint} != 8 || memcmp(srec.seq, up, 8) != 0"), stvar(uint32, srec.seqlen));

        const char down[] = "downstream";   // 10 bytes
        if (!netsocketSend(server, (uint8*)down, 10, NULL, 0))
            TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(server, (uint8*)down, 10, NULL, 0)"), stvNone);
        for (int i = 0; i < 20 && crec.recvCount == 0; i++)
            netqueueTick(q, 100);
        if (crec.seqlen != 10 || memcmp(crec.seq, down, 10) != 0)
            TEST_FAILV(ret, 1, _SL("assertion failed: crec.seqlen=${uint} != 10 || memcmp(crec.seq, down, 10) != 0"), stvar(uint32, crec.seqlen));
    }

    if (server) {
        netsocketClose(server);
        objRelease(&server);
    }
    if (csock) {
        netsocketClose(csock);
        objRelease(&csock);
    }
    netsocketClose(lsock);
    objRelease(&lsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A send to a peer with no flow opens one, so that the payload has a chain to be encoded by. This is
// the one filter path the synthetic socket cannot reach -- it overrides send() -- so it runs over a
// real loopback UDP socket, with a raw peer socket to read what actually reached the wire.
static int test_nettest_filter_dgram_send(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .flowOpen = onFlowOpen, .recv = onRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    NetPassDgramFactory* f = netpassdgramfactoryCreate();
    if (!netsocketAddFilter(rsock, NetFilter(f)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketAddFilter(rsock, NetFilter(f))"), stvNone);
    objRelease(&f);

    // A raw peer socket on loopback, so the test can read exactly what went out.
    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in pa;
    memset(&pa, 0, sizeof(pa));
    pa.sin_family      = AF_INET;
    pa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    pa.sin_port        = 0;
    bind(peer, (struct sockaddr*)&pa, sizeof(pa));

    int palen = sizeof(pa);
    getsockname(peer, (struct sockaddr*)&pa, &palen);

    NetAddr peerAddress = loopbackAddr(ntohs(pa.sin_port));

    // The peer is unknown at this point: no packet has ever arrived from it. The send has to open
    // the flow itself to have a filter chain to encode through.
    const char payload[] = "cold-start";
    if (!netsocketSend(rsock, (uint8*)payload, 10, &peerAddress, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(rsock, (uint8*)payload, 10, &peerAddress, 0)"), stvNone);

    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    char got[64];
    struct sockaddr_in from;
    int fromlen = sizeof(from);
    int n       = recvfrom(peer, got, sizeof(got), 0, (struct sockaddr*)&from, &fromlen);
    if (n != 10 || memcmp(got, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: n=${int} != 10 || memcmp(got, payload, 10) != 0"), stvar(int32, n));

    // The flow the send opened is a flow like any other: NET_FlowOpen fires on it, and a reply from
    // that peer decodes through the same chain.
    sendto(peer, payload, 10, 0, (struct sockaddr*)&from, fromlen);
    tickUntil(q, &rec, 1);

    if (rec.openCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.openCount=${uint} != 1"), stvar(uint32, rec.openCount));
    if (rec.recvCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 1"), stvar(uint32, rec.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        ret = 1;   // still the same flow, not a second one

    closesocket(peer);
    netsocketClose(rsock);
    objRelease(&rsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}
// A real UDP socket bound to loopback receives datagrams sent from a raw peer socket, and each one
// surfaces as a NET_DataReceived through the full select -> recvfrom -> ingest -> dispatch path.
static int test_nettest_select_udp(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin select: IOCP is the default backend now, but this is the select suite
    NetQueue* q = netqueueCreate(&conf);   // the real select backend
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);   // ephemeral port on loopback
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    // Learn the port the OS assigned so the peer can aim at it.
    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    tickUntil(q, &rec, 3);

    // All three datagrams came from one peer address, so they share one flow and arrive in order.
    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);
    netsocketClose(rsock);
    objRelease(&rsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connected loopback TCP pair: bytes sent on the client end are ingested into the server
// socket's receive ring and delivered through recvMsgs()/recv() intact.
static int test_nettest_select_stream(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onStreamRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    // Build a connected pair by hand: listen on loopback, connect, accept.
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    // Wrap the accepted end as a connected NetSocket the queue can drive.
    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    tickUntil(q, &rec, 1);

    if (rec.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} < 1"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 10 || memcmp(rec.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 10 || memcmp(rec.seq, payload, 10) != 0"), stvar(uint32, rec.seqlen));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// The two tests above run the backend in polled mode, driving tick() on this thread. The two below
// run it threaded: one dedicated ingest thread inside the queue runs the select() loop, and a pool
// of dispatch workers delivers the events. The test never calls tick(); it synchronizes on a
// semaphore the handlers post, which is exactly how a real threaded consumer would wait for work.

typedef struct ThreadedRec {
    uint32 recvCount;
    uint32 closeCount;
    NetCloseReason lastReason;
    uint8 seq[256];
    uint32 seqlen;
    Semaphore recvSem;   // posted once per NET_DataReceived, so the test can await deliveries
} ThreadedRec;

// Runs on a dispatch worker thread. The recvSem post happens-after these writes in program order,
// and the awaiting semaTryDecTimeout() establishes the matching acquire, so the test thread sees
// them without any further synchronization.
static void onRecvThreaded(NetEvent* ev)
{
    ThreadedRec* t = (ThreadedRec*)ev->ctx;
    t->recvCount++;
    semaInc(&t->recvSem, 1);
}

static void onStreamRecvThreaded(NetEvent* ev)
{
    ThreadedRec* t = (ThreadedRec*)ev->ctx;
    t->recvCount++;

    uint8 buf[256];
    size_t n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0);
    for (size_t i = 0; i < n && t->seqlen < sizeof(t->seq); i++)
        t->seq[t->seqlen++] = buf[i];

    semaInc(&t->recvSem, 1);
}

// Terminal event, also delivered on a worker. Shutdown joins the workers after queuing it, so the
// test sees closeCount settled once netqueueShutdown() returns -- no wait needed.
static void onClosedThreaded(NetEvent* ev)
{
    ThreadedRec* t  = (ThreadedRec*)ev->ctx;
    t->closeCount++;
    t->lastReason = ev->closed.reason;
}

// Datagrams delivered by the worker pool, not by tick(): the queue's own ingest thread runs
// select()/recvfrom and hands each packet to a worker. Exercises wake-on-addSocket, the ingest
// thread, and the dispatch pool end to end.
static int test_nettest_select_udp_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onRecvThreaded };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    conf.nthreads = 2;   // 1 ingest thread + 2 dispatch workers; the app never calls tick()
    NetQueue* q   = netqueueCreate(&conf);
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);   // wakes the ingest thread to start watching this socket

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    // Await all three deliveries from the worker pool rather than ticking.
    for (int i = 0; i < 3; i++) {
        if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000))) {
            TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);
            break;
        }
    }

    if (tr.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} != 3"), stvar(uint32, tr.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);

    // Shut down first: this stops the ingest thread and joins the workers, so closing the socket
    // handle afterward cannot race a select() still watching it.
    netqueueShutdown(q, 0);
    netsocketClose(rsock);
    objRelease(&rsock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// Threaded stream: the worker pool delivers the received bytes, and on shutdown a worker delivers
// the terminal NET_FlowClosed before the queue is gone -- the ordering guarantee the app relies on.
static int test_nettest_select_stream_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onStreamRecvThreaded, .flowClosed = onClosedThreaded };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    conf.nthreads = 2;
    NetQueue* q   = netqueueCreate(&conf);
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);   // wakes the ingest thread

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);

    if (tr.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} < 1"), stvar(uint32, tr.recvCount));
    if (tr.seqlen != 10 || memcmp(tr.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.seqlen=${uint} != 10 || memcmp(tr.seq, payload, 10) != 0"), stvar(uint32, tr.seqlen));

    // Shutdown closes the stream flow with NCR_Shutdown; a worker delivers the terminal event and
    // is joined before shutdown returns, so closeCount is settled here with no extra wait.
    netqueueShutdown(q, 0);

    if (tr.closeCount != 1 || tr.lastReason != NCR_Shutdown)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.closeCount=${uint} != 1 || tr.lastReason=${int} != NCR_Shutdown"), stvar(uint32, tr.closeCount), stvar(int32, tr.lastReason));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Send path: the outbound side of the same loopback round trip. netsocketSend() gathers
// the outbound chain and writes it with WSASend (stream) or sends whole datagrams (datagram); the
// bytes are read back off a raw peer to prove they went out intact.
// ---------------------------------------------------------------------------------------------

// Bytes handed to netsocketSend() on a connected stream socket are gathered from the outbound chain
// and written out, arriving intact on the raw peer.
static int test_nettest_select_stream_send(void)
{
    int ret = 0;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);

    // Connected loopback pair by hand, same as select_stream but exercised in the send direction.
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamsend";   // 10 bytes
    if (!netsocketSend(ssock, (uint8*)payload, 10, NULL, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(ssock, (uint8*)payload, 10, NULL, 0)"), stvNone);

    // Read it back off the raw client end. The send is immediate on an empty chain over loopback,
    // so the bytes are already on their way; a blocking recv collects them.
    char rbuf[32];
    int got = 0;
    while (got < 10) {
        int n = recv(client, rbuf + got, sizeof(rbuf) - got, 0);
        if (n <= 0)
            break;
        got += n;
    }
    if (got != 10 || memcmp(rbuf, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: got=${int} != 10 || memcmp(rbuf, payload, 10) != 0"), stvar(int32, got));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A datagram sent through netsocketSend() to a raw peer's address arrives as one intact datagram.
static int test_nettest_select_udp_send(void)
{
    int ret = 0;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);

    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in pa;
    memset(&pa, 0, sizeof(pa));
    pa.sin_family      = AF_INET;
    pa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    pa.sin_port        = 0;
    bind(peer, (struct sockaddr*)&pa, sizeof(pa));
    int palen = sizeof(pa);
    getsockname(peer, (struct sockaddr*)&pa, &palen);

    NetSocket* ssock = netqueueSocket(q, NST_Datagram);
    if (!ssock) {
        closesocket(peer);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !ssock"), stvNone);
    }
    NetAddr bindAddr = loopbackAddr(0);
    netsocketBind(ssock, &bindAddr);
    netqueueAddSocket(q, ssock);

    NetAddr dest         = loopbackAddr(ntohs(pa.sin_port));
    const char payload[] = "udpsend";   // 7 bytes
    if (!netsocketSend(ssock, (uint8*)payload, 7, &dest, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(ssock, (uint8*)payload, 7, &dest, 0)"), stvNone);

    char rbuf[32];
    int n = recv(peer, rbuf, sizeof(rbuf), 0);
    if (n != 7 || memcmp(rbuf, payload, 7) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: n=${int} != 7 || memcmp(rbuf, payload, 7) != 0"), stvar(int32, n));

    closesocket(peer);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

static void onSendReady(NetEvent* ev)
{
    Recorder* r = (Recorder*)ev->ctx;
    r->sendReadyCount++;
}

// Backpressure end to end: with a stalled peer and shrunken socket buffers, netsocketSend() fills
// the outbound chain until it crosses the high watermark and is refused, and then -- once the peer
// starts reading and the chain drains below the low watermark -- NET_SendReady fires on the flow.
static int test_nettest_select_stream_backpressure(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .sendReady = onSendReady };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;   // pin the select backend
    NetQueue* q = netqueueCreate(&conf);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    // Shrink both ends so the OS stops accepting after a few KB, which is what lets the outbound
    // chain accumulate to the watermark without moving megabytes of data.
    int snd = 4096, rcv = 4096;
    setsockopt(server, SOL_SOCKET, SO_SNDBUF, (const char*)&snd, sizeof(snd));
    setsockopt(client, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));
    u_long nb = 1;
    ioctlsocket(client, FIONBIO, &nb);   // so the drain below never blocks

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    ssock->sendHigh  = 32768;
    ssock->sendLow   = 4096;
    netqueueAddSocket(q, ssock);

    // Push fixed chunks, never reading on the peer, until a send is refused at the high watermark.
    uint8 chunk[4096];
    memset(chunk, 'x', sizeof(chunk));
    bool refused = false;
    for (int i = 0; i < 2000; i++) {
        if (!netsocketSend(ssock, chunk, sizeof(chunk), NULL, 0)) {
            refused = true;
            break;
        }
    }
    if (!refused)
        ret = 1;   // never hit backpressure -- the watermark is not being enforced

    // Now let it drain: keep reading everything the peer has while ticking, so the socket becomes
    // writable, the chain flushes below the low watermark, and NET_SendReady fires on the flow.
    char drain[8192];
    for (int i = 0; i < 200 && rec.sendReadyCount == 0; i++) {
        while (recv(client, drain, sizeof(drain), 0) > 0)
            ;
        netqueueTick(q, 50);
    }
    if (rec.sendReadyCount == 0)
        ret = 1;   // drained below the low watermark but the ready edge never fired

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}
static NetQueue* makeSelectQueue(int64 connectTimeout)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;
    if (connectTimeout)
        conf.connectTimeout = connectTimeout;
    return netqueueCreate(&conf);
}

// Like makeSelectQueue but with NQ_AutoAccept set, for the auto-accept accept test.
static NetQueue* makeSelectAcceptQueue(bool autoAccept)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_SelectOnly;
    if (autoAccept)
        conf.flags |= NQ_AutoAccept;
    return netqueueCreate(&conf);
}

static int test_nettest_select_connect(void)
{
    NetQueue* q = makeSelectQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("127.0.0.1"));
}

static int test_nettest_select_connect_dns(void)
{
    NetQueue* q = makeSelectQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("localhost"));
}

static int test_nettest_select_connect_refused(void)
{
    NetQueue* q = makeSelectQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectRefusedBody(q);
}

static int test_nettest_select_connect_timeout(void)
{
    NetQueue* q = makeSelectQueue(timeMS(500));
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectTimeoutBody(q);
}

static int test_nettest_select_accept(void)
{
    NetQueue* q = makeSelectAcceptQueue(false);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, false);
}

static int test_nettest_select_accept_auto(void)
{
    NetQueue* q = makeSelectAcceptQueue(true);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, true);
}

// The timer tests above use the synthetic backend, which never waits. These two cover the half that
// only exists on a backend with a real wait: capping the sleep to the nearest deadline, and
// interrupting a sleep that was already parked when a nearer timer was armed.

// A tick asked to wait far longer than the armed deadline must come back at the deadline. Without
// the nearest-deadline bound it would sit out the full wait and the timer would fire seconds late.
static int test_nettest_timer_wait(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q = makeSelectQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);

    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    int64 start = clockTimer();
    netflowAddTimer(s->flow, timeMS(50), NTF_None);

    // select() rounds its bound down to whole milliseconds, so one tick can come back a hair early
    // with nothing due; a couple of passes covers that without making the test time-dependent.
    for (int i = 0; i < 5 && rec.timerCount == 0; i++)
        netqueueTick(q, timeMS(5000));

    int64 elapsed = clockTimer() - start;

    if (rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1"), stvar(uint32, rec.timerCount));
    if (elapsed > timeS(2))
        ret = 1;   // bounded by the timer, not by the 5s wait

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

typedef struct WakeArmState {
    NetFlow* flow;
    int64 armedAt;
} WakeArmState;

// Arms a timer on a queue that is already parked in a long wait, which is the only thing the wake
// hook exists for.
static int timerWakeArmThread(Thread* self)
{
    WakeArmState* st = stvlNextPtr(&self->args);

    osSleep(timeMS(30));
    st->armedAt = clockTimer();
    netflowAddTimer(st->flow, 0, NTF_None);
    return 0;
}

// A timer armed from another thread while the queue sits in a long wait must interrupt it. Without
// the wake hook the queue would sleep out its full bound, because the bound was computed before the
// timer existed.
static int test_nettest_timer_wake(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .timer = onTimer };

    NetQueue* q = makeSelectQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);

    NetSocket* s = makeTimerSocket(q, &rec, &handlers);

    WakeArmState st = { .flow = s->flow };
    Thread* t       = thrCreate(timerWakeArmThread, _S "timerWakeArm", stvar(ptr, &st));
    if (!t) {
        netsocketClose(s);
        objRelease(&s);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !t"), stvNone);
    }

    // Parks in select() for five seconds with nothing armed; the thread above arms 30ms in.
    netqueueTick(q, timeMS(5000));

    if (rec.timerCount != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.timerCount=${uint} != 1"), stvar(uint32, rec.timerCount));
    if (st.armedAt == 0 || clockTimer() - st.armedAt > timeS(2))
        ret = 1;   // returned because of the wake, not because the 5s wait ran out

    thrWait(t, timeForever);
    objRelease(&t);

    netsocketClose(s);
    objRelease(&s);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}


#if defined(_PLATFORM_WIN)

// ---------------------------------------------------------------------------------------------
// IOCP backend: the same loopback receive round trips, but through the native completion
// port instead of select. The queue is built with netPlatformCreateIOCP() so the IOCP path is
// exercised regardless of what the default backend selection would pick; it returns NULL under Wine
// (IOCP is emulated there with no benefit), in which case the test skips.
// ---------------------------------------------------------------------------------------------

// Datagrams received through many outstanding WSARecvFrom, drained on the caller's thread by tick().
static int test_nettest_iocp_udp(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netPlatformCreateIOCP(&conf);
    if (!q)
        return 0;   // IOCP unavailable (Wine): skip
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);   // associates with the port and posts the initial receives

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    tickUntil(q, &rec, 3);

    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);
    netsocketClose(rsock);
    objRelease(&rsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connected loopback TCP pair: bytes arrive on a single outstanding WSARecv, land in the socket's
// ring via reserve/commit, and are delivered through recv() intact -- drained by tick().
static int test_nettest_iocp_stream(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onStreamRecv };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netPlatformCreateIOCP(&conf);
    if (!q)
        return 0;
    netqueueSetHandlers(q, &handlers, &rec);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    tickUntil(q, &rec, 1);

    if (rec.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} < 1"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 10 || memcmp(rec.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 10 || memcmp(rec.seq, payload, 10) != 0"), stvar(uint32, rec.seqlen));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Threaded datagram: the completion threads both ingest and dispatch, so deliveries arrive on a
// worker with no tick(). The test synchronizes on a semaphore the handler posts.
static int test_nettest_iocp_udp_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onRecvThreaded };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;   // 2 completion threads that ingest and dispatch; the app never calls tick()
    NetQueue* q   = netPlatformCreateIOCP(&conf);
    if (!q) {
        semaDestroy(&tr.recvSem);
        return 0;
    }
    netqueueSetHandlers(q, &handlers, &tr);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    for (int i = 0; i < 3; i++) {
        if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000))) {
            TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);
            break;
        }
    }

    if (tr.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} != 3"), stvar(uint32, tr.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);

    // Shut down first so the completion threads stop and outstanding I/O is cancelled and drained
    // before the socket handle is closed underneath a receive still in the kernel.
    netqueueShutdown(q, 0);
    netsocketClose(rsock);
    objRelease(&rsock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// Threaded stream: bytes delivered by a completion thread, and on shutdown a worker delivers the
// terminal NET_FlowClosed with NCR_Shutdown before the queue is gone.
static int test_nettest_iocp_stream_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onStreamRecvThreaded, .flowClosed = onClosedThreaded };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;
    NetQueue* q   = netPlatformCreateIOCP(&conf);
    if (!q) {
        semaDestroy(&tr.recvSem);
        return 0;
    }
    netqueueSetHandlers(q, &handlers, &tr);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);

    if (tr.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} < 1"), stvar(uint32, tr.recvCount));
    if (tr.seqlen != 10 || memcmp(tr.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.seqlen=${uint} != 10 || memcmp(tr.seq, payload, 10) != 0"), stvar(uint32, tr.seqlen));

    netqueueShutdown(q, 0);

    if (tr.closeCount != 1 || tr.lastReason != NCR_Shutdown)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.closeCount=${uint} != 1 || tr.lastReason=${int} != NCR_Shutdown"), stvar(uint32, tr.closeCount), stvar(int32, tr.lastReason));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// IOCP send path: the outbound side through the completion port. A send that the
// OS takes right away goes out on the synchronous fast path exactly as on select; anything left
// queued is drained by an overlapped WSASend/WSASendTo whose completion posts the next, since a
// completion port has no writability signal. Built with netPlatformCreateIOCP(), skipped under Wine.
// ---------------------------------------------------------------------------------------------

// A stream send on an empty chain goes straight out over loopback and arrives intact on the peer.
static int test_nettest_iocp_stream_send(void)
{
    int ret = 0;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netPlatformCreateIOCP(&conf);
    if (!q)
        return 0;   // IOCP unavailable (Wine): skip

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamsend";   // 10 bytes
    if (!netsocketSend(ssock, (uint8*)payload, 10, NULL, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(ssock, (uint8*)payload, 10, NULL, 0)"), stvNone);

    char rbuf[32];
    int got = 0;
    while (got < 10) {
        int n = recv(client, rbuf + got, sizeof(rbuf) - got, 0);
        if (n <= 0)
            break;
        got += n;
    }
    if (got != 10 || memcmp(rbuf, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: got=${int} != 10 || memcmp(rbuf, payload, 10) != 0"), stvar(int32, got));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A datagram sent through netsocketSend() reaches the raw peer as one intact datagram.
static int test_nettest_iocp_udp_send(void)
{
    int ret = 0;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netPlatformCreateIOCP(&conf);
    if (!q)
        return 0;

    SOCKET peer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in pa;
    memset(&pa, 0, sizeof(pa));
    pa.sin_family      = AF_INET;
    pa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    pa.sin_port        = 0;
    bind(peer, (struct sockaddr*)&pa, sizeof(pa));
    int palen = sizeof(pa);
    getsockname(peer, (struct sockaddr*)&pa, &palen);

    NetSocket* ssock = netqueueSocket(q, NST_Datagram);
    if (!ssock) {
        closesocket(peer);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !ssock"), stvNone);
    }
    NetAddr bindAddr = loopbackAddr(0);
    netsocketBind(ssock, &bindAddr);
    netqueueAddSocket(q, ssock);

    NetAddr dest         = loopbackAddr(ntohs(pa.sin_port));
    const char payload[] = "udpsend";   // 7 bytes
    if (!netsocketSend(ssock, (uint8*)payload, 7, &dest, 0))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketSend(ssock, (uint8*)payload, 7, &dest, 0)"), stvNone);

    char rbuf[32];
    int n = recv(peer, rbuf, sizeof(rbuf), 0);
    if (n != 7 || memcmp(rbuf, payload, 7) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: n=${int} != 7 || memcmp(rbuf, payload, 7) != 0"), stvar(int32, n));

    closesocket(peer);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Backpressure over the completion port: with a stalled peer and shrunken socket buffers, the
// outbound chain fills past the high watermark and netsocketSend() is refused; the overlapped sends
// pend in the kernel until the peer reads, and once the chain drains below the low watermark
// NET_SendReady fires. The overlapped completions are pulled by tick() here (polled mode).
static int test_nettest_iocp_stream_backpressure(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .sendReady = onSendReady };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netPlatformCreateIOCP(&conf);
    if (!q)
        return 0;
    netqueueSetHandlers(q, &handlers, &rec);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    int snd = 4096, rcv = 4096;
    setsockopt(server, SOL_SOCKET, SO_SNDBUF, (const char*)&snd, sizeof(snd));
    setsockopt(client, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));
    u_long nb = 1;
    ioctlsocket(client, FIONBIO, &nb);

    NetSocket* ssock = NetSocket(netsocketwinWrap(server, NST_Stream, NS_Connected));
    ssock->sendHigh  = 32768;
    ssock->sendLow   = 4096;
    netqueueAddSocket(q, ssock);

    uint8 chunk[4096];
    memset(chunk, 'x', sizeof(chunk));
    bool refused = false;
    for (int i = 0; i < 2000; i++) {
        if (!netsocketSend(ssock, chunk, sizeof(chunk), NULL, 0)) {
            refused = true;
            break;
        }
    }
    if (!refused)
        ret = 1;   // never hit backpressure -- the watermark is not being enforced

    char drain[8192];
    for (int i = 0; i < 400 && rec.sendReadyCount == 0; i++) {
        while (recv(client, drain, sizeof(drain), 0) > 0)
            ;
        netqueueTick(q, 50);
    }
    if (rec.sendReadyCount == 0)
        ret = 1;   // drained below the low watermark but the ready edge never fired

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

static NetQueue* makeIocpQueue(int64 connectTimeout)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    if (connectTimeout)
        conf.connectTimeout = connectTimeout;
    return netPlatformCreateIOCP(&conf);
}

// The IOCP accept queue, optionally with NQ_AutoAccept. Returns NULL (skip) under Wine.
static NetQueue* makeIocpAcceptQueue(bool autoAccept)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    if (autoAccept)
        conf.flags |= NQ_AutoAccept;
    return netPlatformCreateIOCP(&conf);
}

static int test_nettest_iocp_connect(void)
{
    NetQueue* q = makeIocpQueue(0);
    if (!q)
        return 0;   // IOCP unavailable (Wine): skip
    return connectBody(q, _SL("127.0.0.1"));
}

static int test_nettest_iocp_connect_dns(void)
{
    NetQueue* q = makeIocpQueue(0);
    if (!q)
        return 0;
    return connectBody(q, _SL("localhost"));
}

static int test_nettest_iocp_connect_refused(void)
{
    NetQueue* q = makeIocpQueue(0);
    if (!q)
        return 0;
    return connectRefusedBody(q);
}

static int test_nettest_iocp_connect_timeout(void)
{
    NetQueue* q = makeIocpQueue(timeMS(500));
    if (!q)
        return 0;
    return connectTimeoutBody(q);
}

static int test_nettest_iocp_accept(void)
{
    NetQueue* q = makeIocpAcceptQueue(false);
    if (!q)
        return 0;   // IOCP unavailable (Wine): skip
    return acceptBody(q, false);
}

static int test_nettest_iocp_accept_auto(void)
{
    NetQueue* q = makeIocpAcceptQueue(true);
    if (!q)
        return 0;
    return acceptBody(q, true);
}

#endif   // _PLATFORM_WIN

// ---------------------------------------------------------------------------------------------
// epoll backend, Linux only
//
// The Unix performance target, the counterpart to the iocp_* suite above. connect/accept reuse
// the same shared bodies as select and iocp -- the state machine and admission path are backend-
// independent, so there is nothing epoll-specific to add there. udp/stream get their own tests,
// as select and iocp do, since those exercise the backend's actual ingest loop; epoll_udp in
// particular sends a burst bigger than NET_EPOLL_MMSG_BATCH so the recvmmsg batching path is
// exercised, not just single-packet delivery. Send/backpressure are not duplicated a third time:
// netsocketSend() and the watermark logic are the same backend-independent code already exercised
// by the select_*_send/backpressure and iocp_*_send/backpressure tests.
// ---------------------------------------------------------------------------------------------

#if defined(_PLATFORM_LINUX)

static NetQueue* makeEpollQueue(int64 connectTimeout)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);   // nthreads = 0: polled, driven by netqueueTick() in these tests
    if (connectTimeout)
        conf.connectTimeout = connectTimeout;
    return (NetQueue*)netqueueepollCreate(&conf);
}

static NetQueue* makeEpollThreadedQueue(void)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;   // 1 ingest thread + 2 dispatch workers; the app never calls tick()
    return (NetQueue*)netqueueepollCreate(&conf);
}

static NetQueue* makeEpollAcceptQueue(bool autoAccept)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    if (autoAccept)
        conf.flags |= NQ_AutoAccept;
    return (NetQueue*)netqueueepollCreate(&conf);
}

// A real UDP socket bound to loopback receives datagrams sent from a raw peer socket, each
// surfacing as a NET_DataReceived through the full epoll -> recvmmsg -> ingest -> dispatch path.
// The burst is sized past NET_EPOLL_MMSG_BATCH (32 in unix_net_epoll.c) so a single readiness must
// drain more than one recvmmsg() batch.
static int test_nettest_epoll_udp(void)
{
    int ret      = 0;
    Recorder rec = { 0 };
    const int burst = 40;

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueue* q = makeEpollQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < burst; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    tickUntil(q, &rec, (uint32)burst);

    // All `burst` datagrams came from one peer address, so they share one flow and arrive in order
    // regardless of how recvmmsg happened to batch them.
    if (rec.recvCount != (uint32)burst)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != (uint32)burst=${uint}"), stvar(uint32, rec.recvCount), stvar(uint32, (uint32)burst));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);
    netsocketClose(rsock);
    objRelease(&rsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connected loopback TCP pair over epoll's polled tick(), mirroring test_nettest_select_stream.
static int test_nettest_epoll_stream(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onStreamRecv };

    NetQueue* q = makeEpollQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketposixWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    tickUntil(q, &rec, 1);

    if (rec.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} < 1"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 10 || memcmp(rec.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 10 || memcmp(rec.seq, payload, 10) != 0"), stvar(uint32, rec.seqlen));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Threaded: the queue's own ingest thread runs epoll_wait()/recvmmsg and hands packets to the
// dispatch pool, mirroring test_nettest_select_udp_threaded.
static int test_nettest_epoll_udp_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onRecvThreaded };

    NetQueue* q = makeEpollThreadedQueue();
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    for (int i = 0; i < 3; i++) {
        if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000))) {
            TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);
            break;
        }
    }

    if (tr.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} != 3"), stvar(uint32, tr.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);

    netqueueShutdown(q, 0);
    netsocketClose(rsock);
    objRelease(&rsock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// Threaded stream, mirroring test_nettest_select_stream_threaded: the worker pool delivers the
// received bytes, and shutdown still delivers the terminal NET_FlowClosed from a worker.
static int test_nettest_epoll_stream_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onStreamRecvThreaded, .flowClosed = onClosedThreaded };

    NetQueue* q = makeEpollThreadedQueue();
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketposixWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);

    if (tr.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} < 1"), stvar(uint32, tr.recvCount));
    if (tr.seqlen != 10 || memcmp(tr.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.seqlen=${uint} != 10 || memcmp(tr.seq, payload, 10) != 0"), stvar(uint32, tr.seqlen));

    netqueueShutdown(q, 0);

    if (tr.closeCount != 1 || tr.lastReason != NCR_Shutdown)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.closeCount=${uint} != 1 || tr.lastReason=${int} != NCR_Shutdown"), stvar(uint32, tr.closeCount), stvar(int32, tr.lastReason));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

static int test_nettest_epoll_connect(void)
{
    NetQueue* q = makeEpollQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("127.0.0.1"));
}

static int test_nettest_epoll_connect_dns(void)
{
    NetQueue* q = makeEpollQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("localhost"));
}

static int test_nettest_epoll_connect_refused(void)
{
    NetQueue* q = makeEpollQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectRefusedBody(q);
}

static int test_nettest_epoll_connect_timeout(void)
{
    NetQueue* q = makeEpollQueue(timeMS(500));
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectTimeoutBody(q);
}

static int test_nettest_epoll_accept(void)
{
    NetQueue* q = makeEpollAcceptQueue(false);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, false);
}

static int test_nettest_epoll_accept_auto(void)
{
    NetQueue* q = makeEpollAcceptQueue(true);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, true);
}

#endif   // _PLATFORM_LINUX

// ---------------------------------------------------------------------------------------------
// kqueue backend, FreeBSD only
//
// The FreeBSD performance target, the counterpart to epoll on Linux and IOCP on Windows. connect/
// accept reuse the same shared bodies as select/iocp/epoll -- the state machine and admission path
// are backend-independent. udp/stream get their own tests, as with the other backends, since those
// exercise the backend's actual ingest loop. There is no recvmmsg equivalent on FreeBSD, so unlike
// epoll_udp there is nothing batch-specific to stress here; kqueue's win over select is O(1)
// readiness reporting, not batched ingest.
// ---------------------------------------------------------------------------------------------

#if defined(_PLATFORM_FBSD)

static NetQueue* makeKqueueQueue(int64 connectTimeout)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);   // nthreads = 0: polled, driven by netqueueTick() in these tests
    if (connectTimeout)
        conf.connectTimeout = connectTimeout;
    return (NetQueue*)netqueuekqueueCreate(&conf);
}

static NetQueue* makeKqueueThreadedQueue(void)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.nthreads = 2;   // 1 ingest thread + 2 dispatch workers; the app never calls tick()
    return (NetQueue*)netqueuekqueueCreate(&conf);
}

static NetQueue* makeKqueueAcceptQueue(bool autoAccept)
{
    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    if (autoAccept)
        conf.flags |= NQ_AutoAccept;
    return (NetQueue*)netqueuekqueueCreate(&conf);
}

// A real UDP socket bound to loopback receives datagrams sent from a raw peer socket, each
// surfacing as a NET_DataReceived through the full kqueue -> recvfrom -> ingest -> dispatch path.
static int test_nettest_kqueue_udp(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onRecv };

    NetQueue* q = makeKqueueQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    tickUntil(q, &rec, 3);

    if (rec.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} != 3"), stvar(uint32, rec.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);
    netsocketClose(rsock);
    objRelease(&rsock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// A connected loopback TCP pair over kqueue's polled tick(), mirroring test_nettest_select_stream.
static int test_nettest_kqueue_stream(void)
{
    int ret      = 0;
    Recorder rec = { 0 };

    static const NetHandlers handlers = { .recv = onStreamRecv };

    NetQueue* q = makeKqueueQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    netqueueSetHandlers(q, &handlers, &rec);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketposixWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    tickUntil(q, &rec, 1);

    if (rec.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.recvCount=${uint} < 1"), stvar(uint32, rec.recvCount));
    if (rec.seqlen != 10 || memcmp(rec.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: rec.seqlen=${uint} != 10 || memcmp(rec.seq, payload, 10) != 0"), stvar(uint32, rec.seqlen));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    netqueueShutdown(q, 0);
    objRelease(&q);
    return ret;
}

// Threaded: the queue's own ingest thread runs kevent()/recvfrom and hands packets to the dispatch
// pool, mirroring test_nettest_select_udp_threaded.
static int test_nettest_kqueue_udp_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onRecvThreaded };

    NetQueue* q = makeKqueueThreadedQueue();
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    NetSocket* rsock = netqueueSocket(q, NST_Datagram);
    if (!rsock) {
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !rsock"), stvNone);
    }

    NetAddr bindAddr = loopbackAddr(0);
    if (!netsocketBind(rsock, &bindAddr))
        TEST_FAILV(ret, 1, _SL("assertion failed: !netsocketBind(rsock, &bindAddr)"), stvNone);
    netqueueAddSocket(q, rsock);

    struct sockaddr_in sa;
    int salen = sizeof(sa);
    getsockname((SOCKET)rsock->handle, (struct sockaddr*)&sa, &salen);

    SOCKET snd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = sa.sin_port;

    const char payload[] = "hello";
    for (int i = 0; i < 3; i++)
        sendto(snd, payload, 5, 0, (struct sockaddr*)&dst, sizeof(dst));

    for (int i = 0; i < 3; i++) {
        if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000))) {
            TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);
            break;
        }
    }

    if (tr.recvCount != 3)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} != 3"), stvar(uint32, tr.recvCount));
    if (atomicLoad(uint32, &q->nflows, Relaxed) != 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: atomicLoad(uint32, &q->nflows, Relaxed)=${uint} != 1"), stvar(uint32, atomicLoad(uint32, &q->nflows, Relaxed)));

    closesocket(snd);

    netqueueShutdown(q, 0);
    netsocketClose(rsock);
    objRelease(&rsock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

// Threaded stream, mirroring test_nettest_select_stream_threaded: the worker pool delivers the
// received bytes, and shutdown still delivers the terminal NET_FlowClosed from a worker.
static int test_nettest_kqueue_stream_threaded(void)
{
    int ret        = 0;
    ThreadedRec tr = { 0 };
    semaInit(&tr.recvSem, 0);

    static const NetHandlers handlers = { .recv = onStreamRecvThreaded, .flowClosed = onClosedThreaded };

    NetQueue* q = makeKqueueThreadedQueue();
    if (!q) {
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    }
    netqueueSetHandlers(q, &handlers, &tr);

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in la;
    memset(&la, 0, sizeof(la));
    la.sin_family      = AF_INET;
    la.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    la.sin_port        = 0;
    bind(listener, (struct sockaddr*)&la, sizeof(la));
    listen(listener, 1);
    int lalen = sizeof(la);
    getsockname(listener, (struct sockaddr*)&la, &lalen);

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(client, (struct sockaddr*)&la, sizeof(la));
    SOCKET server = accept(listener, NULL, NULL);
    closesocket(listener);

    if (server == INVALID_SOCKET) {
        closesocket(client);
        netqueueShutdown(q, 0);
        objRelease(&q);
        semaDestroy(&tr.recvSem);
        TEST_FAIL(1, _SL("assertion failed: server == INVALID_SOCKET"), stvNone);
    }

    NetSocket* ssock = NetSocket(netsocketposixWrap(server, NST_Stream, NS_Connected));
    netqueueAddSocket(q, ssock);

    const char payload[] = "streamdata";   // 10 bytes
    send(client, payload, 10, 0);

    if (!semaTryDecTimeout(&tr.recvSem, timeMS(2000)))
        TEST_FAILV(ret, 1, _SL("assertion failed: !semaTryDecTimeout(&tr.recvSem, timeMS(2000))"), stvNone);

    if (tr.recvCount < 1)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.recvCount=${uint} < 1"), stvar(uint32, tr.recvCount));
    if (tr.seqlen != 10 || memcmp(tr.seq, payload, 10) != 0)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.seqlen=${uint} != 10 || memcmp(tr.seq, payload, 10) != 0"), stvar(uint32, tr.seqlen));

    netqueueShutdown(q, 0);

    if (tr.closeCount != 1 || tr.lastReason != NCR_Shutdown)
        TEST_FAILV(ret, 1, _SL("assertion failed: tr.closeCount=${uint} != 1 || tr.lastReason=${int} != NCR_Shutdown"), stvar(uint32, tr.closeCount), stvar(int32, tr.lastReason));

    closesocket(client);
    netsocketClose(ssock);
    objRelease(&ssock);
    objRelease(&q);
    semaDestroy(&tr.recvSem);
    return ret;
}

static int test_nettest_kqueue_connect(void)
{
    NetQueue* q = makeKqueueQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("127.0.0.1"));
}

static int test_nettest_kqueue_connect_dns(void)
{
    NetQueue* q = makeKqueueQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectBody(q, _SL("localhost"));
}

static int test_nettest_kqueue_connect_refused(void)
{
    NetQueue* q = makeKqueueQueue(0);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectRefusedBody(q);
}

static int test_nettest_kqueue_connect_timeout(void)
{
    NetQueue* q = makeKqueueQueue(timeMS(500));
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return connectTimeoutBody(q);
}

static int test_nettest_kqueue_accept(void)
{
    NetQueue* q = makeKqueueAcceptQueue(false);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, false);
}

static int test_nettest_kqueue_accept_auto(void)
{
    NetQueue* q = makeKqueueAcceptQueue(true);
    if (!q)
        TEST_FAIL(1, _SL("assertion failed: !q"), stvNone);
    return acceptBody(q, true);
}

#endif   // _PLATFORM_FBSD

#endif   // _PLATFORM_WIN || _PLATFORM_UNIX || _PLATFORM_WASM

testfunc nettest_funcs[] = {
    { "addr",                       test_nettest_addr                       },
    { "flow_basic",                 test_nettest_flow_basic                 },
    { "flow_handlers",              test_nettest_flow_handlers              },
    { "flow_close",                 test_nettest_flow_close                 },
    { "flow_reclaim",               test_nettest_flow_reclaim               },
    { "flow_reclaim_idle",          test_nettest_flow_reclaim_idle          },
    { "flow_resurrect",             test_nettest_flow_resurrect             },
    { "flow_shutdown",              test_nettest_flow_shutdown              },
    { "flow_race",                  test_nettest_flow_race                  },
    { "timer_basic",                test_nettest_timer_basic                },
    { "timer_cancel",               test_nettest_timer_cancel               },
    { "timer_rearm",                test_nettest_timer_rearm                },
    { "timer_repeat",               test_nettest_timer_repeat               },
    { "timer_ordering",             test_nettest_timer_ordering             },
    { "timer_flowclose",            test_nettest_timer_flowclose            },
    { "filter_stream",              test_nettest_filter_stream              },
    { "filter_secured",             test_nettest_filter_secured             },
    { "filter_chain",               test_nettest_filter_chain               },
    { "filter_dgram",               test_nettest_filter_dgram               },
    { "filter_dgram_flows",         test_nettest_filter_dgram_flows         },
    { "filter_teardown",            test_nettest_filter_teardown            },
    { "filter_dgram_poolref",       test_nettest_filter_dgram_poolref       },
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_UNIX) || defined(_PLATFORM_WASM)
    { "helpers",                    test_nettest_helpers                    },
    { "filter_dgram_send",          test_nettest_filter_dgram_send          },
    { "select_udp",                 test_nettest_select_udp                 },
    { "select_stream",              test_nettest_select_stream              },
    { "select_udp_threaded",        test_nettest_select_udp_threaded        },
    { "select_stream_threaded",     test_nettest_select_stream_threaded     },
    { "select_stream_send",         test_nettest_select_stream_send         },
    { "select_udp_send",            test_nettest_select_udp_send            },
    { "select_stream_backpressure", test_nettest_select_stream_backpressure },
    { "select_connect",             test_nettest_select_connect             },
    { "select_connect_dns",         test_nettest_select_connect_dns         },
    { "select_connect_refused",     test_nettest_select_connect_refused     },
    { "select_connect_timeout",     test_nettest_select_connect_timeout     },
    { "select_accept",              test_nettest_select_accept              },
    { "select_accept_auto",         test_nettest_select_accept_auto         },
    { "timer_wait",                 test_nettest_timer_wait                 },
    { "timer_wake",                 test_nettest_timer_wake                 },
#if defined(_PLATFORM_WIN)
    { "iocp_udp",                   test_nettest_iocp_udp                   },
    { "iocp_stream",                test_nettest_iocp_stream                },
    { "iocp_udp_threaded",          test_nettest_iocp_udp_threaded          },
    { "iocp_stream_threaded",       test_nettest_iocp_stream_threaded       },
    { "iocp_stream_send",           test_nettest_iocp_stream_send           },
    { "iocp_udp_send",              test_nettest_iocp_udp_send              },
    { "iocp_stream_backpressure",   test_nettest_iocp_stream_backpressure   },
    { "iocp_connect",               test_nettest_iocp_connect               },
    { "iocp_connect_dns",           test_nettest_iocp_connect_dns           },
    { "iocp_connect_refused",       test_nettest_iocp_connect_refused       },
    { "iocp_connect_timeout",       test_nettest_iocp_connect_timeout       },
    { "iocp_accept",                test_nettest_iocp_accept                },
    { "iocp_accept_auto",           test_nettest_iocp_accept_auto           },
#endif
#if defined(_PLATFORM_LINUX)
    { "epoll_udp",                  test_nettest_epoll_udp                  },
    { "epoll_stream",               test_nettest_epoll_stream               },
    { "epoll_udp_threaded",         test_nettest_epoll_udp_threaded         },
    { "epoll_stream_threaded",      test_nettest_epoll_stream_threaded      },
    { "epoll_connect",              test_nettest_epoll_connect              },
    { "epoll_connect_dns",          test_nettest_epoll_connect_dns          },
    { "epoll_connect_refused",      test_nettest_epoll_connect_refused      },
    { "epoll_connect_timeout",      test_nettest_epoll_connect_timeout      },
    { "epoll_accept",               test_nettest_epoll_accept               },
    { "epoll_accept_auto",          test_nettest_epoll_accept_auto          },
#endif
#if defined(_PLATFORM_FBSD)
    { "kqueue_udp",                 test_nettest_kqueue_udp                 },
    { "kqueue_stream",              test_nettest_kqueue_stream              },
    { "kqueue_udp_threaded",        test_nettest_kqueue_udp_threaded        },
    { "kqueue_stream_threaded",     test_nettest_kqueue_stream_threaded     },
    { "kqueue_connect",             test_nettest_kqueue_connect             },
    { "kqueue_connect_dns",         test_nettest_kqueue_connect_dns         },
    { "kqueue_connect_refused",     test_nettest_kqueue_connect_refused     },
    { "kqueue_connect_timeout",     test_nettest_kqueue_connect_timeout     },
    { "kqueue_accept",              test_nettest_kqueue_accept              },
    { "kqueue_accept_auto",         test_nettest_kqueue_accept_auto         },
#endif
#endif
};

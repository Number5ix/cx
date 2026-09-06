// A cxquic peer for interoperability testing against other QUIC implementations.
//
// Speaks two protocols, in both roles.
//
// "echo" (ALPN cxinterop) is what cx talks to cx: the client opens some number of bidirectional
// streams, writes a deterministic byte pattern on each, ends its sending half, and checks that the
// same bytes come back; the server echoes whatever arrives on every stream the peer opens.
//
// "hq" (ALPN hq-interop) is the HTTP/0.9-shaped protocol the QUIC interop runner uses, and is what
// the ngtcp2 and picoquic test peers speak. The client writes "GET /<n>\r\n" on a stream and ends
// its sending half; the server writes n bytes back and ends its own. Only the length is checked in
// this mode, since the bytes come from whatever implementation is on the other end.
//
// A mismatch anywhere -- wrong bytes, short stream, no connection at all -- exits nonzero, which
// is what makes this usable from a script that runs it against each implementation in turn.
//
// Usage:
//   quicinterop server -port N -cert cert.pem -key key.pem [-proto echo|hq] [-alpn NAME]
//   quicinterop client -host H -port N [-ca ca.pem] [-sni NAME] [-proto echo|hq] [-alpn NAME]
//                      [-streams N] [-size N] [-0rtt] [-insecure]

#include <cxquic.h>

#include <cx/console.h>
#include <cx/container.h>
#include <cx/format.h>
#include <cx/fs/file.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define TICK_WAIT_US    timeMS(20)
#define IDLE_TIMEOUT_US timeS(15)
#define MAX_STREAMS     64

// Byte i of a stream is (i*31 + 7) mod 251. A lost or duplicated chunk shows up as a value
// mismatch at a known offset rather than only as a wrong total length.
static uint8 patternByte(size_t i)
{
    return (uint8)((i * 31 + 7) % 251);
}

typedef enum IProto {
    IP_Echo,             // cx to cx: send a pattern, get the same bytes back
    IP_Hq,              // hq-interop: "GET /<n>" in, n bytes out
} IProto;

typedef struct IStream {
    NetFlow* flow;
    uint64 id;
    size_t want;         // pattern bytes this end owes the peer
    size_t sent;         // bytes handed to netflowSend() so far
    size_t got;          // bytes read back
    bool bad;            // a byte did not match the pattern
    bool finSent;        // this end has ended its sending half
    bool fin;            // the peer ended its sending half
    bool closed;

    char req[256];       // hq server: the request line, as much of it as has arrived
    uint32 reqLen;
    bool reqDone;        // the request line has been parsed and answered
} IStream;

typedef struct ICtx {
    bool server;
    IProto proto;

    NetQueue* q;
    NetSocket* sock;     // client: the connection; server: the listener
    NetSocket* conn;     // server: the accepted connection, first one only

    size_t size;         // bytes per stream
    uint32 nstreams;     // streams the client opens

    IStream streams[MAX_STREAMS];
    uint32 nseen;

    uint32 nConnected;
    uint32 nFailed;
    uint32 nSecured;
    bool connClosed;
    bool done;
    int exitCode;
    int64 lastActivity;
} ICtx;

static void touch(_Inout_ ICtx* c)
{
    c->lastActivity = clockTimer();
}

// The connection's own control flow is not a stream. It carries the connection's events rather
// than a stream's, and it can hold the same key as stream 0, so identity is what separates them.
static bool isControl(_In_ NetEvent* ev)
{
    return ev->socket && ev->flow == ev->socket->flow;
}

_Ret_maybenull_ static IStream* istream(_Inout_ ICtx* c, _In_ NetFlow* flow)
{
    for (uint32 i = 0; i < c->nseen; i++) {
        if (c->streams[i].id == flow->key) {
            // A finished stream's flow is released, and a later stream on the same connection can
            // be allocated at the address it had. The stream id is what identifies a slot; the
            // pointer is only cached, so it is refreshed rather than compared.
            c->streams[i].flow = flow;
            return &c->streams[i];
        }
    }

    if (c->nseen >= MAX_STREAMS)
        return NULL;

    // Cleared rather than merely filled in: a server takes one connection after another, and the
    // slots are reused. Anything left from the stream that had this slot before -- above all
    // whether its end had already been sent -- would be read as this stream's own.
    IStream* s = &c->streams[c->nseen++];
    memset(s, 0, sizeof(*s));
    s->flow    = flow;
    s->id      = flow->key;
    return s;
}

// Reads a whole file into a string. Certificates and keys are small enough that reading them in
// one piece is simpler than streaming, and a file too large to be either is not one of them.
static bool slurp(_Inout_ strhandle out, _In_ strref path)
{
    FSFile* f = fsOpen(path, FS_Read);
    if (!f)
        return false;

    int64 sz = fsSeek(f, 0, FS_End);
    fsSeek(f, 0, FS_Set);
    if (sz <= 0 || sz > 1 << 20) {
        fsClose(f);
        return false;
    }

    uint8* buf = strBuffer(out, (uint32)sz);

    size_t n = 0;
    bool ok  = fsRead(f, buf, (size_t)sz, &n) && n == (size_t)sz;
    fsClose(f);

    if (!ok)
        strClear(out);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------------------------

// Push as much of the pattern as the peer's window currently allows, and end the sending half once
// all of it is out. A send is all or nothing, so what goes in one call is bounded by
// netquicWritable(); the rest waits for NET_SendReady, which is what makes a payload larger than
// the initial flow control window work at all.
//
// Both roles send this way: the echo client sends the pattern it expects back, and the hq server
// sends the pattern as the body of a response.
static void pumpPattern(_Inout_ ICtx* c, _Inout_ IStream* s)
{
    uint8 buf[16384];

    while (s->sent < s->want) {
        size_t room = netquicWritable(s->flow);
        size_t left = s->want - s->sent;
        size_t n    = room < left ? room : left;

        if (n == 0)
            return;
        if (n > sizeof(buf))
            n = sizeof(buf);

        for (size_t i = 0; i < n; i++)
            buf[i] = patternByte(s->sent + i);

        if (!netflowSend(s->flow, buf, n, 0)) {
            conPuts(conErr(), _SL("send refused with room reported\n"));
            c->exitCode = 1;
            c->done     = true;
            return;
        }

        s->sent += n;
    }

    if (!s->finSent) {
        netquicFinish(s->flow);
        s->finSent = true;
    }
}

// Server: echo what has arrived, reading only as much as there is room to send straight back.
// Leaving the rest unread is what holds the peer's window shut until this end can carry more,
// which is the whole point of not buffering here.
static void pumpEcho(_Inout_ ICtx* c, _Inout_ IStream* s)
{
    uint8 buf[16384];

    for (;;) {
        size_t room = netquicWritable(s->flow);
        if (room == 0)
            break;
        if (room > sizeof(buf))
            room = sizeof(buf);

        bool fin = false;
        size_t n = netquicRecv(s->flow, buf, room, &fin);
        if (fin)
            s->fin = true;

        if (n == 0)
            break;

        s->sent += n;

        if (!netflowSend(s->flow, buf, n, 0)) {
            conFmt(conErr(),
                   _SL("echo refused: ${uint} bytes, room was ${uint}, now ${uint}\n"),
                   stvar(uint64, (uint64)n), stvar(uint64, (uint64)room),
                   stvar(uint64, (uint64)netquicWritable(s->flow)));
            c->exitCode = 1;
            c->done     = true;
            return;
        }
    }

    if (s->fin && !s->finSent) {
        netquicFinish(s->flow);
        s->finSent = true;
    }
}

// hq server: collect the request line, then answer it. hq-interop has no framing beyond the
// newline -- the client ends its sending half right after the line -- so anything past the newline
// is not part of a request and is dropped.
static void pumpHq(_Inout_ ICtx* c, _Inout_ IStream* s)
{
    if (!s->reqDone) {
        uint8 buf[512];
        bool fin = false;
        size_t n;

        while ((n = netquicRecv(s->flow, buf, sizeof(buf), &fin)) > 0) {
            for (size_t i = 0; i < n && !s->reqDone; i++) {
                if (buf[i] == '\n') {
                    s->reqDone = true;
                    break;
                }
                if (s->reqLen < sizeof(s->req) - 1)
                    s->req[s->reqLen++] = (char)buf[i];
            }
            if (fin)
                break;
        }

        if (fin)
            s->fin = true;
        if (!s->reqDone)
            return;

        // "GET /<n>" asks for n bytes. Anything else is answered with an empty body, which reaches
        // the peer as a zero-length file rather than as a stream that never ends.
        if (s->reqLen > 0 && s->req[s->reqLen - 1] == '\r')
            s->reqLen--;
        s->req[s->reqLen] = 0;

        const char* p = s->req;
        if (s->reqLen > 5 && memcmp(p, "GET /", 5) == 0) {
            p += 5;
            size_t n = 0;
            while (*p >= '0' && *p <= '9')
                n = n * 10 + (size_t)(*p++ - '0');
            if (*p == 0)
                s->want = n;
        }

        conFmt(conOut(), _SL("stream ${uint}: request for ${uint} bytes\n"),
               stvar(uint64, s->id), stvar(uint64, (uint64)s->want));
    }

    pumpPattern(c, s);
}

// Client: open every stream and start the pattern on each. This runs when the connection is handed
// over, which with 0-RTT is before the handshake finishes -- so an early data run and an ordinary
// one take exactly the same path.
static void startStreams(_Inout_ ICtx* c)
{
    for (uint32 i = 0; i < c->nstreams; i++) {
        NetFlow* flow = netquicOpen(c->sock, false);
        if (!flow) {
            conPuts(conErr(), _SL("could not open a stream\n"));
            c->exitCode = 1;
            c->done     = true;
            break;
        }

        IStream* s = istream(c, flow);
        if (!s) {
            objRelease(&flow);
            break;
        }

        if (c->proto == IP_Hq) {
            // The whole request is one short line, so it goes out in one send and the stream ends
            // there; everything after this is the response coming back.
            string req = 0;
            strFormat(&req, _SL("GET /${uint}\r\n"), stvar(uint64, (uint64)c->size));

            if (!netflowSend(flow, (const uint8*)strC(req), strLen(req), 0)) {
                conPuts(conErr(), _SL("could not send the request\n"));
                c->exitCode = 1;
                c->done     = true;
            }
            strDestroy(&req);

            netquicFinish(flow);
            s->finSent = true;
        } else {
            s->want = c->size;
            pumpPattern(c, s);
        }

        objRelease(&flow);
    }
}

static void onConnection(_Inout_ NetEvent* ev)
{
    ICtx* c = (ICtx*)ev->ctx;
    touch(c);

    if (ev->conn.err != NERR_None) {
        conFmt(conErr(), _SL("connection failed: error ${int}\n"), stvar(int32, (int32)ev->conn.err));
        c->nFailed++;
        c->exitCode = 1;
        c->done     = true;
        return;
    }

    c->nConnected++;

    string alpn = 0;
    netquicALPN(c->sock, &alpn);
    conFmt(conOut(), _SL("connected: alpn ${string}, 0-RTT ${int}\n"),
           stvar(string, alpn), stvar(int32, (int32)netquicEarlyData(c->sock)));
    strDestroy(&alpn);

    startStreams(c);
}

static void onAccepted(_Inout_ NetEvent* ev)
{
    ICtx* c = (ICtx*)ev->ctx;
    touch(c);

    if (!c->conn)
        c->conn = objAcquire(ev->accept.newSocket);

    string alpn = 0;
    netquicALPN(ev->accept.newSocket, &alpn);
    conFmt(conOut(), _SL("accepted: alpn ${string}, 0-RTT ${int}\n"),
           stvar(string, alpn), stvar(int32, (int32)netquicEarlyData(ev->accept.newSocket)));
    strDestroy(&alpn);
}

static void onFilterNotify(_Inout_ NetEvent* ev)
{
    ICtx* c = (ICtx*)ev->ctx;
    if (ev->filter.notify == NFN_Secured) {
        c->nSecured++;
        conPuts(conOut(), _SL("0-RTT confirmed: what was sent early is no longer replayable\n"));
    }
}

static void onFlowOpen(_Inout_ NetEvent* ev)
{
    if (isControl(ev))
        return;

    ICtx* c = (ICtx*)ev->ctx;
    touch(c);
    istream(c, ev->flow);
}

static void onRecv(_Inout_ NetEvent* ev)
{
    if (isControl(ev))
        return;

    ICtx* c    = (ICtx*)ev->ctx;
    IStream* s = istream(c, ev->flow);
    touch(c);

    if (!s)
        return;

    if (c->server) {
        if (c->proto == IP_Hq)
            pumpHq(c, s);
        else
            pumpEcho(c, s);
        return;
    }

    uint8 buf[16384];
    bool fin = false;
    size_t n;

    while ((n = netquicRecv(ev->flow, buf, sizeof(buf), &fin)) > 0) {
        // Only an echo peer owes us these exact bytes. An hq response body is whatever the
        // implementation on the other end generates, so there only the length is checked.
        for (size_t i = 0; c->proto == IP_Echo && i < n; i++) {
            if (buf[i] != patternByte(s->got + i)) {
                if (!s->bad)
                    conFmt(conErr(),
                           _SL("stream ${uint}: byte ${uint} is ${uint}, expected ${uint}\n"),
                           stvar(uint64, s->id), stvar(uint64, (uint64)(s->got + i)),
                           stvar(uint32, buf[i]), stvar(uint32, patternByte(s->got + i)));
                s->bad = true;
            }
        }
        s->got += n;

        if (fin)
            break;
    }

    if (fin)
        s->fin = true;
}

static void onSendReady(_Inout_ NetEvent* ev)
{
    if (isControl(ev))
        return;

    ICtx* c    = (ICtx*)ev->ctx;
    IStream* s = istream(c, ev->flow);
    touch(c);

    if (!s)
        return;

    if (!c->server)
        pumpPattern(c, s);
    else if (c->proto == IP_Hq)
        pumpHq(c, s);
    else
        pumpEcho(c, s);
}

// What each stream of the connection got to. Printed when a connection ends, and again if the
// server gives up waiting -- a stall shows as a stream that was asked for and never finished.
// Everything the connection knows about why it stopped making progress. Only printed when a run
// gives up, where the alternative is a bare "timed out" that says nothing about which end stopped
// or what it was waiting for.
static void reportState(_Inout_ ICtx* c)
{
    NetSocket* sock = c->server ? c->conn : c->sock;
    if (!sock)
        return;

    string s = 0;
    netquicDebugState(sock, &s);
    conPuts(conErr(), s);
    strDestroy(&s);
}

static void reportStreams(_Inout_ ICtx* c)
{
    for (uint32 i = 0; i < c->nseen; i++) {
        IStream* s = &c->streams[i];
        conFmt(conOut(),
               _SL("stream ${uint}: sent ${uint} of ${uint}, peer finished ${int}, "
                   "finished back ${int}, writable ${uint}\n"),
               stvar(uint64, s->id), stvar(uint64, (uint64)s->sent), stvar(uint64, (uint64)s->want),
               stvar(int32, s->fin ? 1 : 0), stvar(int32, s->finSent ? 1 : 0),
               stvar(uint64, s->closed ? 0 : (uint64)netquicWritable(s->flow)));
    }
}

static void onError(_Inout_ NetEvent* ev)
{
    if (isControl(ev))
        return;

    ICtx* c = (ICtx*)ev->ctx;
    touch(c);

    QuicStream* qs = objDynCast(QuicStream, ev->flow);
    conFmt(conErr(), _SL("stream error ${uint}\n"), stvar(uint64, qs ? qs->error : 0));
    c->exitCode = 1;
}

static void onFlowClosed(_Inout_ NetEvent* ev)
{
    ICtx* c = (ICtx*)ev->ctx;
    touch(c);

    if (isControl(ev)) {
        // The connection itself ended, not one of its streams.
        if (!c->server) {
            c->connClosed = true;
            c->done       = true;
            return;
        }

        reportStreams(c);
        c->nseen = 0;
        return;
    }

    IStream* s = istream(c, ev->flow);
    if (s)
        s->closed = true;
}

static const NetHandlers kHandlers = {
    .connection   = onConnection,
    .accepted     = onAccepted,
    .filterNotify = onFilterNotify,
    .flowOpen     = onFlowOpen,
    .recv         = onRecv,
    .sendReady    = onSendReady,
    .error        = onError,
    .flowClosed   = onFlowClosed,
};

// ---------------------------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------------------------

typedef struct IArgs {
    string host;
    string sni;
    string alpn;
    string cert;
    string key;
    string ca;
    string qlog;
    uint32 port;
    uint32 streams;
    uint32 size;
    IProto proto;
    bool zeroRtt;
    bool insecure;
} IArgs;

static bool argUInt(_Out_ uint32* out, _In_ strref s)
{
    return strToUInt32(out, s, 10, STRNUM_NoTrailing);
}

static bool parseArgs(_Out_ IArgs* a, bool* server)
{
    memset(a, 0, sizeof(*a));
    a->sni     = _S "localhost";
    a->port    = 4433;
    a->streams = 1;
    a->size    = 4096;

    if (saSize(cmdArgs) < 1)
        return false;

    if (strEq(cmdArgs.a[0], _S "server"))
        *server = true;
    else if (strEq(cmdArgs.a[0], _S "client"))
        *server = false;
    else
        return false;

    for (int32 i = 1; i < saSize(cmdArgs); i++) {
        string f = cmdArgs.a[i];

        if (strEq(f, _S "-0rtt")) {
            a->zeroRtt = true;
            continue;
        }
        if (strEq(f, _S "-insecure")) {
            a->insecure = true;
            continue;
        }

        if (i + 1 >= saSize(cmdArgs))
            return false;
        string v = cmdArgs.a[++i];

        if (strEq(f, _S "-host"))
            a->host = v;
        else if (strEq(f, _S "-sni"))
            a->sni = v;
        else if (strEq(f, _S "-alpn"))
            a->alpn = v;
        else if (strEq(f, _S "-cert"))
            a->cert = v;
        else if (strEq(f, _S "-key"))
            a->key = v;
        else if (strEq(f, _S "-ca"))
            a->ca = v;
        else if (strEq(f, _S "-qlog"))
            a->qlog = v;
        else if (strEq(f, _S "-proto")) {
            if (strEq(v, _S "echo"))
                a->proto = IP_Echo;
            else if (strEq(v, _S "hq"))
                a->proto = IP_Hq;
            else
                return false;
        }
        else if (strEq(f, _S "-port")) {
            if (!argUInt(&a->port, v) || a->port > 65535)
                return false;
        } else if (strEq(f, _S "-streams")) {
            if (!argUInt(&a->streams, v) || a->streams < 1 || a->streams > MAX_STREAMS)
                return false;
        } else if (strEq(f, _S "-size")) {
            if (!argUInt(&a->size, v))
                return false;
        } else
            return false;
    }

    // The ALPN a protocol is normally reached under, unless the run named one of its own.
    if (strEmpty(a->alpn))
        a->alpn = (a->proto == IP_Hq) ? _S "hq-interop" : _S "cxinterop";

    return true;
}

static void usage(void)
{
    conPuts(conErr(),
            _SLL("usage: quicinterop server -port N -cert cert.pem -key key.pem\n"
                 "                          [-proto echo|hq] [-alpn NAME]\n"
                 "       quicinterop client -host H -port N [-ca ca.pem] [-sni NAME]\n"
                 "                          [-proto echo|hq] [-alpn NAME]\n"
                 "                          [-streams N] [-size N] [-0rtt] [-insecure]\n"
                 "       both accept -qlog DIR to write a qlog file per connection\n"
                 "\n"
                 "       echo (default) is cx to cx; hq is the hq-interop protocol the ngtcp2 and\n"
                 "       picoquic test peers speak, where the client asks for -size bytes and only\n"
                 "       the length of what comes back is checked\n"));
}

// ---------------------------------------------------------------------------------------------

_Ret_maybenull_ static TlsConfig* buildTls(_In_ const IArgs* a, bool server)
{
    TlsConfig* cfg = NULL;

    if (server) {
        string certPem = 0, keyPem = 0;
        if (!slurp(&certPem, a->cert) || !slurp(&keyPem, a->key)) {
            conPuts(conErr(), _SL("could not read the certificate or key\n"));
            strDestroy(&certPem);
            strDestroy(&keyPem);
            return NULL;
        }

        TlsCreds* creds = tlscredsCreatePEM(certPem, keyPem, NULL);
        strDestroy(&certPem);
        strDestroy(&keyPem);
        if (!creds) {
            conPuts(conErr(), _SL("the certificate or key would not load\n"));
            return NULL;
        }

        cfg = tlsconfigCreateServer(creds);
        objRelease(&creds);
    } else {
        cfg = tlsconfigCreateClient();
        if (!cfg)
            return NULL;

        if (a->insecure) {
            tlsconfigSetAuthMode(cfg, TLSAUTH_None);
        } else {
            string caPem = 0;
            if (!slurp(&caPem, a->ca)) {
                conPuts(conErr(), _SL("could not read the CA certificate\n"));
                objRelease(&cfg);
                return NULL;
            }

            TlsCAStore* ca = tlscastoreCreate();
            bool ok        = ca && tlscastoreAddPEM(ca, caPem);
            strDestroy(&caPem);
            if (!ok) {
                conPuts(conErr(), _SL("the CA certificate would not load\n"));
                objRelease(&ca);
                objRelease(&cfg);
                return NULL;
            }

            tlsconfigSetCA(cfg, ca);
            objRelease(&ca);
        }
    }

    if (!cfg)
        return NULL;

    sa_string alpn;
    saInit(&alpn, string, 1);
    saPush(&alpn, string, a->alpn);
    tlsconfigSetALPN(cfg, &alpn);
    saDestroy(&alpn);

    return cfg;
}

int entryPoint()
{
    IArgs args;
    bool server = false;
    if (!parseArgs(&args, &server)) {
        usage();
        conShutdown();
        return 2;
    }

    if (strLen(args.qlog) > 0)
        netquicQlog(args.qlog);

    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    ICtx ctx    = { 0 };
    ctx.server  = server;
    ctx.proto   = args.proto;
    ctx.size    = args.size;
    ctx.nstreams = args.streams;

    TlsConfig* tls = buildTls(&args, server);
    if (!tls) {
        logShutdown();
        conShutdown();
        return 2;
    }

    if (args.zeroRtt) {
        tlsconfigSetResumption(tls, true, 0);
        tlsconfigSetEarlyData(tls, true);
    }

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    ctx.q = netqueueCreate(&conf);

    QuicConfig qcfg = { .tls = tls };

    if (server) {
        NetAddr addr = { .type = NA_IPv4, .port = (uint16)args.port };
        addr.ipv4[3] = 127;
        addr.ipv4[0] = 1;

        ctx.sock = netquicListen(ctx.q, &addr, &qcfg, &kHandlers, &ctx);
        if (!ctx.sock) {
            conPuts(conErr(), _SL("could not listen\n"));
            ctx.exitCode = 1;
        } else {
            conFmt(conOut(), _SL("listening on 127.0.0.1:${uint}\n"),
                   stvar(uint16, ctx.sock->local.port));
            touch(&ctx);
            while (!ctx.done) {
                netqueueTick(ctx.q, TICK_WAIT_US);
                if (clockTimer() - ctx.lastActivity > IDLE_TIMEOUT_US) {
                    // A connection that ended cleanly has already reported and cleared its
                    // streams, so anything still here is a stream that stopped making progress.
                    if (ctx.nseen > 0) {
                        conPuts(conErr(), _SL("gave up waiting\n"));
                        reportStreams(&ctx);
                        reportState(&ctx);
                        ctx.exitCode = 1;
                    }
                    break;
                }
            }
        }
    } else {
        ctx.sock = netquicConnect(ctx.q, args.host, (uint16)args.port, args.sni, &qcfg, &kHandlers,
                                  &ctx);
        if (!ctx.sock) {
            conPuts(conErr(), _SL("could not start the connection\n"));
            ctx.exitCode = 1;
        } else {
            touch(&ctx);
            while (!ctx.done) {
                netqueueTick(ctx.q, TICK_WAIT_US);

                // Done when every stream has had the whole pattern echoed back and ended.
                uint32 finished = 0;
                for (uint32 i = 0; i < ctx.nseen; i++) {
                    if (ctx.streams[i].fin && ctx.streams[i].got == ctx.size)
                        finished++;
                }
                if (ctx.nseen >= ctx.nstreams && finished >= ctx.nstreams)
                    break;

                if (clockTimer() - ctx.lastActivity > IDLE_TIMEOUT_US) {
                    conPuts(conErr(), _SL("timed out\n"));
                    reportState(&ctx);
                    ctx.exitCode = 1;
                    break;
                }
            }

            for (uint32 i = 0; i < ctx.nseen; i++) {
                IStream* s = &ctx.streams[i];
                if (s->bad || s->got != ctx.size || !s->fin) {
                    conFmt(conErr(),
                           _SL("stream ${uint}: sent ${uint}, echoed ${uint} of ${uint}, "
                               "peer finished ${int}, writable ${uint}\n"),
                           stvar(uint64, s->id), stvar(uint64, (uint64)s->sent),
                           stvar(uint64, (uint64)s->got), stvar(uint64, (uint64)ctx.size),
                           stvar(int32, s->fin ? 1 : 0),
                           stvar(uint64, s->closed ? 0 : (uint64)netquicWritable(s->flow)));
                    ctx.exitCode = 1;
                }
            }

            if (ctx.exitCode == 0)
                conFmt(conOut(), _SL("ok: ${uint} stream${string} of ${uint} bytes echoed\n"),
                       stvar(uint32, ctx.nstreams),
                       stvar(strref, ctx.nstreams == 1 ? _S "" : _S "s"),
                       stvar(uint64, (uint64)ctx.size));

            netquicClose(ctx.sock, 0, _S "done");
        }
    }

    if (ctx.sock) {
        netsocketClose(ctx.sock);
        objRelease(&ctx.sock);
    }
    objRelease(&ctx.conn);

    netqueueShutdown(ctx.q, 0);
    objRelease(&ctx.q);
    objRelease(&tls);

    logShutdown();
    conShutdown();
    return ctx.exitCode;
}

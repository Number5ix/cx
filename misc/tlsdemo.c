// Minimal TLS client: connects to a named server on port 443, reports what the handshake
// negotiated and the peer certificate it presented, then sends a hardcoded GET / and prints
// the response as it arrives.
//
// This is not how a real HTTP client is built -- there is no redirect handling, no chunked
// decoding, nothing beyond the one hardcoded request. It exists to be pointed at a real server
// and show cxtls doing its one job: netsocketSend()/netsocketRecv() on a TlsClientFilter behave
// exactly like they would on a plaintext socket.
//
// Usage: tlsdemo <host> [port]

#include <cxtls.h>
#include <cxtls_mbed.h>

#include <cx/console.h>
#include <cx/format.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define DEFAULT_PORT    443
#define TICK_WAIT_MS    100
#define IDLE_TIMEOUT_US timeS(20)   // give up after 20s of silence

STR_CONST(kRequestFmt, "GET / HTTP/1.1\r\n"
                       "Host: ${string}\r\n"
                       "User-Agent: cxtls-demo\r\n"
                       "Connection: close\r\n"
                       "\r\n");

typedef struct DemoCtx {
    strref host;
    uint16 port;
    bool sentRequest;
    bool done;
    int exitCode;
    size_t bodyBytes;
    int64 lastActivity;   // clockTimer(), reset on every event so idle time can be measured
} DemoCtx;

static strref ynStr(bool b)
{
    return b ? _S"yes" : _S"no";
}

static const char* connStateName(NetConnectionState s)
{
    switch (s) {
    case NCS_NotConnected:
        return "not connected";
    case NCS_Connecting:
        return "connecting";
    case NCS_Connected:
        return "connected";
    default:
        return "?";
    }
}

static const char* closeReasonName(NetCloseReason r)
{
    switch (r) {
    case NCR_AppClosed:
        return "closed by this program";
    case NCR_PeerClosed:
        return "closed cleanly by the peer";
    case NCR_Error:
        return "connection error";
    case NCR_SocketClosed:
        return "socket closed";
    case NCR_Shutdown:
        return "queue shutting down";
    case NCR_Reclaimed:
        return "reclaimed";
    default:
        return "?";
    }
}

// ---------------------------------------------------------------------------------------------
// certificate reporting
// ---------------------------------------------------------------------------------------------

// The detail TlsInfo does not carry -- serial number, validity window, SAN, key type -- read
// straight off the live session. This is the escape hatch nettlsFlowWithSsl() exists for: cxtls
// is not a crypto wrapper, so anything beyond the TlsInfo snapshot is a direct mbedTLS call.
static void printPeerCertDetail(_In_ void* sslv, _In_opt_ void* unused)
{
    (void)unused;
    mbedtls_ssl_context* ssl    = (mbedtls_ssl_context*)sslv;
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(ssl);
    if (!crt) {
        conPuts(conOut(), _SL("  <no peer certificate available>\n"));
        return;
    }

    char buf[4096];
    int n = mbedtls_x509_crt_info(buf, sizeof(buf), "  ", crt);
    if (n > 0)
        conWrite(conOut(), buf, (size_t)n);
}

static void printSecured(_In_ NetFlow* flow)
{
    TlsInfo info;
    if (!nettlsFlowInfo(flow, &info)) {
        conPuts(conOut(), _SL("NFN_Secured fired but no TLS state was available\n"));
        return;
    }

    conPuts(conOut(), _SL("\n=== TLS channel up ===\n"));
    conFmt(conOut(), _SL("  protocol       ${string}\n"), stvar(strref, info.version));
    conFmt(conOut(), _SL("  ciphersuite    ${string}\n"), stvar(strref, info.ciphersuite));
    conFmt(conOut(),
           _SL("  alpn           ${string}\n"),
           stvar(strref, strEmpty(info.alpn) ? _S"(none negotiated)" : info.alpn));
    conFmt(conOut(), _SL("  peer verified  ${string}\n"), stvar(strref, ynStr(info.peerVerified)));
    if (info.peerVerified) {
        conFmt(conOut(),
               _SL("  verify flags   0x${0uint(8,hex)}${string}\n"),
               stvar(uint32, (uint32)info.verifyFlags),
               stvar(strref,
                     info.verifyFlags == 0 ? _S" (clean)" :
                                              _S" (see mbedtls_x509_crt_verify_info below)"));
        conFmt(conOut(), _SL("  subject        ${string}\n"), stvar(strref, info.peerSubject));
        conFmt(conOut(), _SL("  issuer         ${string}\n"), stvar(strref, info.peerIssuer));
    }
    nettlsInfoDestroy(&info);

    conPuts(conOut(), _SL("\n--- peer certificate ---\n"));
    if (!nettlsFlowWithSsl(flow, printPeerCertDetail, NULL))
        conPuts(conOut(), _SL("  <session already gone>\n"));
    conPuts(conOut(), _SL("\n"));
}

// ---------------------------------------------------------------------------------------------
// event handlers
// ---------------------------------------------------------------------------------------------

static void touch(_Inout_ DemoCtx* ctx)
{
    ctx->lastActivity = clockTimer();
}

static void onConnection(_In_ NetEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    touch(ctx);

    conFmt(conOut(), _SL("[tcp] ${string}"), stvar(strref, (strref)connStateName(ev->conn.state)));
    if (ev->conn.state == NCS_NotConnected && ev->conn.err != NERR_None)
        conFmt(conOut(), _SL(" (error ${int})"), stvar(int32, (int32)ev->conn.err));
    conPuts(conOut(), _SL("\n"));

    if (ev->conn.state == NCS_NotConnected) {
        // The handshake may still be running -- this fires only for the underlying TCP
        // connection, which for a fresh socket that never got this far means the connect
        // itself failed. A handshake failure instead closes the flow; see onFlowClosed.
        ctx->exitCode = 1;
        ctx->done     = true;
    }
}

static void onFilterNotify(_In_ NetEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    touch(ctx);

    if (ev->filter.notify != NFN_Secured)
        return;

    printSecured(ev->flow);

    // Sending before this point is allowed and correct -- the payload would simply wait in the
    // flow's staging ring until the handshake finished -- but the request names the host being
    // authenticated, so waiting for confirmation that the peer really is that host is the more
    // honest demonstration.
    if (!ctx->sentRequest) {
        string req = 0;
        strFormat(&req, kRequestFmt, stvar(strref, ctx->host));

        conFmt(conOut(), _SL("--- request ---\n${string}"), stvar(strref, req));
        conPuts(conOut(), _SL("--- response ---\n"));

        netsocketSend(ev->socket, (uint8*)strC(req), strLen(req), NULL, 0);
        strDestroy(&req);
        ctx->sentRequest = true;
    }
}

static void onRecv(_In_ NetEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    touch(ctx);

    uint8 buf[4096];
    size_t n;
    while ((n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0)) > 0) {
        conWrite(conOut(), buf, n);
        ctx->bodyBytes += n;
    }
}

static void onError(_In_ NetEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    touch(ctx);
    conFmt(conErr(), _SL("[net] asynchronous error ${int}\n"), stvar(int32, (int32)ev->error.err));
    ctx->exitCode = 1;
}

static void onFlowClosed(_In_ NetEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    touch(ctx);

    conFmt(conOut(),
           _SL("\n[tcp] flow closed: ${string}\n"),
           stvar(strref, (strref)closeReasonName(ev->closed.reason)));
    if (ev->closed.reason != NCR_PeerClosed && ev->closed.reason != NCR_AppClosed)
        ctx->exitCode = 1;

    ctx->done = true;
}

static const NetHandlers kHandlers = {
    .connection   = onConnection,
    .filterNotify = onFilterNotify,
    .recv         = onRecv,
    .error        = onError,
    .flowClosed   = onFlowClosed,
};

// ---------------------------------------------------------------------------------------------

int entryPoint()
{
    if (saSize(cmdArgs) < 1) {
        conPuts(conErr(), _SL("usage: tlsdemo <host> [port]\n"));
        conShutdown();
        return 2;
    }

    // Route cx's own diagnostics -- handshake failures above all -- to stderr, leaving stdout
    // for the response body. The channel filter is not optional: everything the framework logs
    // lives under the `cx` channel, which is declared LOG_Restricted, so a destination registered
    // with a NULL filter deliberately sees an application's own logging and nothing else.
    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    DemoCtx ctx = { 0 };
    ctx.host    = cmdArgs.a[0];
    ctx.port    = DEFAULT_PORT;
    if (saSize(cmdArgs) >= 2) {
        uint32 port = 0;
        if (!strToUInt32(&port, cmdArgs.a[1], 10, STRNUM_NoTrailing) || port > 65535) {
            conPuts(conErr(), _SL("invalid port\n"));
            logShutdown();
            conShutdown();
            return 2;
        }
        ctx.port = (uint16)port;
    }

    TlsCAStore* ca = tlscastoreCreate();
    if (!ca || !tlscastoreAddSystem(ca))
        conPuts(conErr(),
                _SL("warning: no system trust store could be loaded; the handshake will fail "
                    "verification\n"));

    TlsConfig* cfg = tlsconfigCreateClient();
    tlsconfigSetCA(cfg, ca);

    sa_string alpn;
    saInit(&alpn, string, 1);
    saPush(&alpn, string, _S"http/1.1");
    tlsconfigSetALPN(cfg, &alpn);
    saDestroy(&alpn);

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netqueueCreate(&conf);

    conFmt(conOut(),
           _SL("Connecting to ${string}:${uint}...\n"),
           stvar(strref, ctx.host),
           stvar(uint16, ctx.port));
    NetSocket* sock = nettlsConnect(q, ctx.host, ctx.port, NULL, cfg, &kHandlers, &ctx);
    if (!sock) {
        conPuts(conErr(), _SL("could not start the connection\n"));
        ctx.exitCode = 1;
    } else {
        touch(&ctx);
        while (!ctx.done) {
            netqueueTick(q, TICK_WAIT_MS);
            if (clockTimer() - ctx.lastActivity > IDLE_TIMEOUT_US) {
                conPuts(conErr(), _SL("\ntimed out waiting for a response\n"));
                ctx.exitCode = 1;
                break;
            }
        }

        conFmt(conOut(),
               _SL("\n(${uint} byte${string} of response body)\n"),
               stvar(uint64, (uint64)ctx.bodyBytes),
               stvar(strref, ctx.bodyBytes == 1 ? _S"" : _S"s"));

        netsocketClose(sock);
        objRelease(&sock);
    }

    netqueueShutdown(q, 0);
    objRelease(&q);
    objRelease(&cfg);
    objRelease(&ca);

    // Log records are queued and written by the logging system's own thread, so without this a
    // handshake failure -- the one thing worth reading when this program fails -- is still sitting
    // in the queue when the process exits.
    logShutdown();

    // conOut()/conErr() buffer their writes; without this, everything above is still sitting
    // unflushed when the process exits.
    conShutdown();

    return ctx.exitCode;
}

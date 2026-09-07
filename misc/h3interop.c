// An HTTP/3 peer for interoperability testing against other HTTP/3 implementations.
//
// quicinterop is the same idea one layer down: it exercises QUIC itself, with either a private
// echo protocol or hq-interop on top. This one speaks real HTTP/3 through the public cxhttp API,
// so what is being tested is the framing layer and QPACK rather than the transport, and it is
// built only when the build has HTTP/3 in it.
//
// Both roles are here, because an interop run needs each end tested against the other
// implementation in turn.
//
// The server answers three things:
//
//   /            a short index
//   /<n>         n bytes of a deterministic pattern, streamed, with a known content length
//   /echo        the request body sent straight back
//
// The client asks for /<size> on -streams requests at once and checks what comes back. Byte i of
// the pattern is (i*31 + 7) mod 251, so a lost or reordered piece shows up as a mismatch at a
// known offset rather than only as a wrong total.
//
// Against another implementation there is no pattern to check, since the bytes are whatever that
// server generates. The response says which case this is: the server here marks a pattern body
// with `X-Interop-Pattern`, and only a response carrying that header has its bytes and its length
// checked. Everything else is held to the status and to having a body at all. -post is exempt --
// an echo of what this end sent is checkable whoever sent it back.
//
// A mismatch anywhere -- wrong bytes, wrong status, a response that arrived over HTTP/1.1, no
// connection at all -- exits nonzero, which is what makes this usable from a script that runs it
// against each implementation in turn.
//
// Usage:
//   h3interop server -port N -cert cert.pem -key key.pem [-size N]
//   h3interop client -host H -port N [-ca ca.pem] [-insecure] [-streams N] [-size N]
//                    [-path P] [-post] [-race]
//   both accept -qlog DIR to write a qlog file per connection

#include <cxhttp.h>
#include <cxquic.h>

#include <cx/console.h>
#include <cx/container.h>
#include <cx/format.h>
#include <cx/fs/file.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/platform.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define TICK_WAIT_US    timeMS(20)
#define IDLE_TIMEOUT_US timeS(30)
#define MAX_REQUESTS    256

STR_CONST(kPatternHdr, "X-Interop-Pattern");

// Byte i of a body is (i*31 + 7) mod 251, the same pattern quicinterop uses. A lost or duplicated
// piece shows up as a value mismatch at a known offset rather than only as a wrong total length.
static uint8 patternByte(uint64 i)
{
    return (uint8)((i * 31 + 7) % 251);
}

// ---------------------------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------------------------

typedef struct PatternSrc {
    uint64 sent;
    uint64 want;
} PatternSrc;

static void patternCleanup(void* ctx)
{
    xaFree(ctx);
}

// A pull producer: cxhttp asks for more whenever the stream has room, so a body of any size is
// generated as it goes out instead of existing all at once. The length is promised up front, so
// this is the content-length path rather than the chunked one.
static size_t patternPull(StreamBuffer* sb, uint8* buf, size_t sz, void* ctx)
{
    PatternSrc* p = (PatternSrc*)ctx;

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    if (p->sent >= p->want) {
        sbufPUnregister(sb);
        return 0;
    }

    uint64 left = p->want - p->sent;
    size_t n    = (uint64)sz < left ? sz : (size_t)left;

    for (size_t i = 0; i < n; i++)
        buf[i] = patternByte(p->sent + i);

    p->sent += n;
    return n;
}

STR_CONST(kIndex,
          "h3interop server\n"
          "\n"
          "  /<n>    n bytes of the interop pattern\n"
          "  /echo   the request body sent back\n");

// "/<n>" asks for n bytes. Anything that is not entirely digits after the slash is some other
// path, which is a 404 rather than a zero-length body.
static bool wantedBytes(_Out_ uint64* out, _In_ strref path)
{
    *out = 0;
    if (strLen(path) < 2 || strGetChar(path, 0) != '/')
        return false;

    for (uint32 i = 1; i < strLen(path); i++) {
        uint8 ch = strGetChar(path, i);
        if (ch < '0' || ch > '9')
            return false;
        if (*out > (UINT64_MAX - 9) / 10)
            return false;
        *out = *out * 10 + (ch - '0');
    }
    return true;
}

static void onRequest(HttpServerEvent* ev)
{
    HttpServerRequest* req = ev->request;

    conFmt(conErr(), _SL("${string} ${string} -> "), stvar(strref, req->methodName),
           stvar(strref, req->path));

    if (strEq(req->path, _SL("/"))) {
        conPuts(conErr(), _SL("index\n"));
        httpsrvreqRespond(req, kIndex, _SL("text/plain"));
        return;
    }

    if (strEq(req->path, _SL("/echo"))) {
        conFmt(conErr(), _SL("echo ${uint} bytes\n"), stvar(uint64, (uint64)strLen(req->body)));
        httpsrvreqRespond(req, req->body, _SL("application/octet-stream"));
        return;
    }

    uint64 want;
    if (!wantedBytes(&want, req->path)) {
        conPuts(conErr(), _SL("404\n"));
        httpsrvreqRespondStatus(req, HTTP_NotFound);
        return;
    }

    conFmt(conErr(), _SL("${uint} pattern bytes\n"), stvar(uint64, want));

    PatternSrc* p = xaAllocStruct(PatternSrc, XA_Zero);
    p->want       = want;

    StreamBuffer* sb = sbufCreate(64 * 1024);
    if (!sbufPRegisterPull(sb, patternPull, patternCleanup, p)) {
        sbufRelease(&sb);
        xaFree(p);
        httpsrvreqRespondStatus(req, HTTP_InternalError);
        return;
    }

    // Marks the body as one whose bytes the other end can predict. A client that does not know
    // this header simply downloads it; h3interop's own client uses it to decide whether checking
    // the bytes means anything.
    httpsrvreqSetHeader(req, kPatternHdr, _SL("1"));
    httpsrvreqRespondStream(req, sb, (int64)want, _SL("application/octet-stream"));

    // The request holds its own reference now, so the one sbufCreate() handed back is done with.
    sbufRelease(&sb);
}

static void onServerError(HttpServerEvent* ev)
{
    conFmt(conErr(), _SL("connection error: ${uint}\n"), stvar(uint32, (uint32)ev->err));
}

static const HttpServerHandlers kServerHandlers = {
    .request = onRequest,
    .error   = onServerError,
};

// ---------------------------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------------------------

typedef struct ICtx ICtx;

typedef struct IReq {
    ICtx* c;
    uint32 idx;
    uint64 got;          // body bytes that have arrived
    bool checkBytes;     // the bytes are predictable, so they are being compared
    bool bad;            // a byte did not match, or the response was unacceptable
    bool done;
    uint16 status;
    HttpVersion version;
    HttpError err;
} IReq;

struct ICtx {
    uint64 size;         // bytes each request asks for or uploads
    uint32 nreq;
    uint32 nDone;
    bool post;           // upload the pattern to -path instead of downloading it
    bool race;           // HTTPV_Any: an HTTP/1.1 answer is reported rather than failed
    IReq reqs[MAX_REQUESTS];
    int exitCode;
    int64 lastActivity;
};

static void touch(_Inout_ ICtx* c)
{
    c->lastActivity = clockTimer();
}

static void onHeaders(HttpEvent* ev)
{
    IReq* r = (IReq*)ev->ctx;
    touch(r->c);

    r->status  = ev->status;
    r->version = ev->version;

    // A POST is echoed back by whoever received it, so those bytes are known whatever sent them.
    // A download is only checkable when the peer says it sent the pattern.
    if (r->c->post) {
        r->checkBytes = true;
    } else {
        string val = 0;
        if (httpHeadersGet(ev->headers, kPatternHdr, &val))
            r->checkBytes = true;
        strDestroy(&val);
    }

    if (ev->status != HTTP_OK)
        r->bad = true;
    if (ev->version != HTTPVER_3 && !r->c->race)
        r->bad = true;
}

// Where a mismatch happened, and what it looks like. The pattern repeats every 251 bytes but is
// distinctive over any longer run, so searching the next few bytes for their real position says
// whether the body was corrupted or merely shifted -- a run of the pattern that belongs somewhere
// else means bytes were dropped or repeated rather than damaged.
static void reportMismatch(_In_ IReq* r, _In_ HttpEvent* ev, size_t at)
{
    uint64 off = r->got + at;

    conFmt(conErr(), _SL("request ${uint}: byte ${uint} is ${uint}, expected ${uint}"),
           stvar(uint32, r->idx), stvar(uint64, off), stvar(uint32, ev->data[at]),
           stvar(uint32, patternByte(off)));

    // The pattern's period, which is where a search for the same bytes has to stop being news.
    size_t run = min(ev->len - at, (size_t)16);
    if (run >= 8) {
        for (uint64 cand = 0; cand < 251 * 31; cand++) {
            size_t i = 0;
            while (i < run && ev->data[at + i] == patternByte(cand + i))
                i++;
            if (i == run) {
                conFmt(conErr(), _SL(" (these bytes belong at ${uint}, ${int} away)"),
                       stvar(uint64, cand), stvar(int64, (int64)cand - (int64)off));
                break;
            }
        }
    }

    conPuts(conErr(), _SL("\n"));
}

// The body is checked as it arrives and never kept: an interop run may ask for far more than fits
// in memory, and the whole point is the bytes rather than what they add up to.
static void onData(HttpEvent* ev)
{
    IReq* r = (IReq*)ev->ctx;
    touch(r->c);

    if (r->checkBytes) {
        for (size_t i = 0; i < ev->len; i++) {
            if (ev->data[i] != patternByte(r->got + i)) {
                if (!r->bad)
                    reportMismatch(r, ev, i);
                r->bad = true;
                break;
            }
        }
    }

    r->got += ev->len;
}

static void finish(_Inout_ IReq* r)
{
    if (r->done)
        return;
    r->done = true;
    r->c->nDone++;
}

static void onComplete(HttpEvent* ev)
{
    IReq* r = (IReq*)ev->ctx;
    touch(r->c);
    finish(r);
}

static void onReqError(HttpEvent* ev)
{
    IReq* r = (IReq*)ev->ctx;
    touch(r->c);

    r->err = ev->err;
    r->bad = true;
    finish(r);
}

static const HttpHandlers kClientHandlers = {
    .headers  = onHeaders,
    .data     = onData,
    .complete = onComplete,
    .error    = onReqError,
};

// ---------------------------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------------------------

typedef struct IArgs {
    string host;
    string cert;
    string key;
    string ca;
    string path;
    string qlog;
    uint32 port;
    uint32 streams;
    uint32 size;
    int32 workers;       // netqueue worker threads, or negative for the preset's own count
    bool insecure;
    bool post;
    bool race;
} IArgs;

static bool argUInt(_Out_ uint32* out, _In_ strref s)
{
    return strToUInt32(out, s, 10, STRNUM_NoTrailing);
}

static bool parseArgs(_Out_ IArgs* a, _Out_ bool* server)
{
    memset(a, 0, sizeof(*a));
    a->host    = _S "localhost";
    a->port    = 4433;
    a->streams = 1;
    a->size    = 65536;
    a->workers = -1;

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

        if (strEq(f, _S "-insecure")) {
            a->insecure = true;
            continue;
        }
        if (strEq(f, _S "-post")) {
            a->post = true;
            continue;
        }
        if (strEq(f, _S "-race")) {
            a->race = true;
            continue;
        }

        if (i + 1 >= saSize(cmdArgs))
            return false;
        string v = cmdArgs.a[++i];

        if (strEq(f, _S "-host"))
            a->host = v;
        else if (strEq(f, _S "-cert"))
            a->cert = v;
        else if (strEq(f, _S "-key"))
            a->key = v;
        else if (strEq(f, _S "-ca"))
            a->ca = v;
        else if (strEq(f, _S "-path"))
            a->path = v;
        else if (strEq(f, _S "-qlog"))
            a->qlog = v;
        else if (strEq(f, _S "-port")) {
            if (!argUInt(&a->port, v) || a->port > 65535)
                return false;
        } else if (strEq(f, _S "-streams")) {
            if (!argUInt(&a->streams, v) || a->streams < 1 || a->streams > MAX_REQUESTS)
                return false;
        } else if (strEq(f, _S "-size")) {
            if (!argUInt(&a->size, v))
                return false;
        } else if (strEq(f, _S "-w")) {
            if (!strToInt32(&a->workers, v, 10, STRNUM_NoTrailing))
                return false;
        } else
            return false;
    }

    return true;
}

static void usage(void)
{
    conPuts(conErr(),
            _SLL("usage: h3interop server -port N -cert cert.pem -key key.pem [-size N]\n"
                "       h3interop client -host H -port N [-ca ca.pem] [-insecure]\n"
                "                        [-streams N] [-size N] [-path P] [-post] [-race]\n"
                "       both accept -qlog DIR to write a qlog file per connection\n"
                "\n"
                "       the server answers /<n> with n bytes of the interop pattern and /echo\n"
                "       with the request body; -size is the largest body it will accept\n"
                "\n"
                "       the client asks for /<size> on -streams requests at once, or with -post\n"
                "       uploads that many pattern bytes to /echo and checks the echo. -path asks\n"
                "       for something else, which is what pointing this at another server takes:\n"
                "       only a body marked as the pattern has its bytes checked, so any other\n"
                "       response is held to its status and to arriving over HTTP/3\n"
                "\n"
                "       -race allows HTTP/1.1 and reports which version answered, instead of\n"
                "       requiring HTTP/3\n"));
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

// The client's TLS configuration, when the run needs one that is not the default. cxhttp adds the
// protocols it can speak to whatever it is handed, so this only supplies what differs.
_Ret_maybenull_ static TlsConfig* clientTls(_In_ const IArgs* a)
{
    if (!a->insecure && strEmpty(a->ca))
        return NULL;

    TlsConfig* cfg = tlsconfigCreateClient();
    if (!cfg)
        return NULL;

    if (a->insecure) {
        tlsconfigSetAuthMode(cfg, TLSAUTH_None);
        return cfg;
    }

    string pem = 0;
    if (!slurp(&pem, a->ca)) {
        conPuts(conErr(), _SL("could not read the CA certificate\n"));
        objRelease(&cfg);
        return NULL;
    }

    TlsCAStore* ca = tlscastoreCreate();
    bool ok        = ca && tlscastoreAddPEM(ca, pem);
    strDestroy(&pem);

    if (!ok) {
        conPuts(conErr(), _SL("the CA certificate would not load\n"));
        objRelease(&ca);
        objRelease(&cfg);
        return NULL;
    }

    tlsconfigSetCA(cfg, ca);
    objRelease(&ca);
    return cfg;
}

// ---------------------------------------------------------------------------------------------

static int runServer(_In_ const IArgs* a, _Inout_ NetQueue* q)
{
    string certPem = 0, keyPem = 0;
    if (!slurp(&certPem, a->cert) || !slurp(&keyPem, a->key)) {
        conPuts(conErr(), _SL("could not read the certificate or key\n"));
        strDestroy(&certPem);
        strDestroy(&keyPem);
        return 2;
    }

    TlsCreds* creds = tlscredsCreatePEM(certPem, keyPem, NULL);
    strDestroy(&certPem);
    strDestroy(&keyPem);
    if (!creds) {
        conPuts(conErr(), _SL("the certificate or key would not load\n"));
        return 2;
    }

    TlsConfig* cfg  = tlsconfigCreateServer(creds);
    HttpServer* srv = cfg ? httpserverCreate(q) : NULL;
    objRelease(&creds);

    if (!srv) {
        conPuts(conErr(), _SL("could not create the server\n"));
        objRelease(&cfg);
        return 1;
    }

    httpserverSetHandlers(srv, &kServerHandlers, NULL);

    // -post uploads a body of -size, and the default limit would refuse it. A little headroom
    // above that covers the request head, which counts against nothing else here.
    srv->limits.maxBodyBytes = (size_t)a->size + 64 * 1024;

    NetAddr addr = { 0 };
    netAddrFromStr(&addr, _SL("0.0.0.0"));
    addr.port = (uint16)a->port;

    int rc = 0;
    if (!httpserverListenQuic(srv, &addr, cfg)) {
        conFmt(conErr(), _SL("could not listen on udp/${uint}\n"), stvar(uint32, a->port));
        rc = 1;
    } else {
        conFmt(conOut(), _SL("listening for HTTP/3 on udp/${uint}\n"),
               stvar(uint32, (uint32)httpserverPort(srv)));

        // Polled and threaded are exclusive: a threaded queue has its own thread in the readiness
        // call and its own workers dispatching, and netqueueTick() is what drives a queue that has
        // neither. Calling it here anyway would put two threads in that one call.
        if (a->workers > 0) {
            for (;;)
                osSleep(timeS(1));
        } else {
            for (;;)
                netqueueTick(q, TICK_WAIT_US);
        }
    }

    httpserverShutdown(srv);
    objRelease(&srv);
    objRelease(&cfg);
    return rc;
}

static int runClient(_In_ const IArgs* a, _Inout_ NetQueue* q)
{
    ICtx ctx  = { 0 };
    ctx.size  = a->size;
    ctx.nreq  = a->streams;
    ctx.post  = a->post;
    ctx.race  = a->race;

    HttpClient* http = httpclientCreate(q);
    if (!http)
        return 1;

    TlsConfig* cfg = clientTls(a);
    if (cfg) {
        httpclientSetTlsConfig(http, cfg);
        objRelease(&cfg);
    } else if (a->insecure || !strEmpty(a->ca)) {
        objRelease(&http);
        return 2;
    }

    if (!httpclientSetVersions(http, a->race ? HTTPV_Any : HTTPV_Http3)) {
        conPuts(conErr(), _SL("this build has no HTTP/3 in it\n"));
        objRelease(&http);
        return 2;
    }

    // What every request asks for. Without -path that is the size the run wants, which the server
    // here reads straight out of the path.
    string path = 0;
    if (!strEmpty(a->path))
        strDup(&path, a->path);
    else if (a->post)
        strDup(&path, _SL("/echo"));
    else
        strFormat(&path, _SL("/${uint}"), stvar(uint32, a->size));

    string url = 0;
    strFormat(&url, _SL("https://${string}:${uint}${string}"), stvar(strref, a->host),
              stvar(uint32, a->port), stvar(strref, path));
    strDestroy(&path);

    // The body -post uploads. It is built once and shared by every request, since a string is
    // copy-on-write and none of them modifies it.
    string body = 0;
    if (a->post) {
        uint8* buf = strBuffer(&body, a->size);
        for (uint64 i = 0; i < a->size; i++)
            buf[i] = patternByte(i);
    }

    conFmt(conOut(), _SL("${uint} request${string} to ${string}\n"), stvar(uint32, ctx.nreq),
           stvar(strref, ctx.nreq == 1 ? _S "" : _S "s"), stvar(strref, url));

    touch(&ctx);

    // Every request goes out before any of them is waited on, which is what puts them on one
    // connection as concurrent streams rather than one after another.
    for (uint32 i = 0; i < ctx.nreq; i++) {
        IReq* r = &ctx.reqs[i];
        r->c    = &ctx;
        r->idx  = i;

        HttpRequest* req = httprequestCreate(a->post ? HTTP_Post : HTTP_Get, url);
        if (!req) {
            conFmt(conErr(), _SL("could not parse URL: ${string}\n"), stvar(strref, url));
            ctx.exitCode = 2;
            finish(r);
            break;
        }

        if (a->post)
            httprequestSetBody(req, body, _SL("application/octet-stream"));

        if (!httpclientSend(http, req, &kClientHandlers, r)) {
            conFmt(conErr(), _SL("request ${uint} would not start\n"), stvar(uint32, i));
            ctx.exitCode = 1;
            r->bad       = true;
            finish(r);
        }

        objRelease(&req);
    }

    while (ctx.nDone < ctx.nreq) {
        netqueueTick(q, TICK_WAIT_US);
        if (clockTimer() - ctx.lastActivity > IDLE_TIMEOUT_US) {
            conPuts(conErr(), _SL("gave up waiting\n"));
            ctx.exitCode = 1;
            break;
        }
    }

    // What each request got to. A run that asked for a known number of pattern bytes owes exactly
    // that many; one pointed at another server's own path is held to having a body at all.
    uint32 ok = 0;
    for (uint32 i = 0; i < ctx.nreq; i++) {
        IReq* r     = &ctx.reqs[i];
        bool sizeOk = r->checkBytes ? r->got == ctx.size : r->got > 0;

        if (r->done && !r->bad && sizeOk) {
            ok++;
            continue;
        }

        conFmt(conErr(),
               _SL("request ${uint}: status ${uint}, version ${string}, ${uint} bytes, "
                   "error ${uint}${string}\n"),
               stvar(uint32, i), stvar(uint32, (uint32)r->status),
               stvar(strref, r->version == HTTPVER_3 ? _S "HTTP/3" : _S "HTTP/1.x"),
               stvar(uint64, r->got), stvar(uint32, (uint32)r->err),
               stvar(strref, r->done ? _S "" : _S ", never finished"));
        ctx.exitCode = 1;
    }

    if (ctx.exitCode == 0) {
        conFmt(conOut(), _SL("ok: ${uint} ${string} of ${uint} bytes over ${string}\n"),
               stvar(uint32, ok), stvar(strref, a->post ? _S "echoes" : _S "downloads"),
               stvar(uint64, ctx.reqs[0].got),
               stvar(strref, ctx.reqs[0].version == HTTPVER_3 ? _S "HTTP/3" : _S "HTTP/1.1"));
    }

    strDestroy(&url);
    strDestroy(&body);
    objRelease(&http);
    return ctx.exitCode;
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

    if (!strEmpty(args.qlog))
        netquicQlog(args.qlog);

    // cx's own diagnostics go to stderr. The `cx/**` filter is not optional -- the framework's
    // channels are restricted, so a destination registered with a NULL filter sees the
    // application's logging and nothing else.
    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    NetQueueConfig conf;
    if (server)
        netqueuePresetServer(&conf);
    else
        netqueuePresetClient(&conf);

    if (args.workers >= 0)
        conf.nthreads = args.workers;

    // Resolved rather than requested: the run loop picks polled or threaded from this.
    args.workers = conf.nthreads;

    NetQueue* q = netqueueCreate(&conf);
    if (!q) {
        conPuts(conErr(), _SL("could not create the queue\n"));
        logShutdown();
        conShutdown();
        return 1;
    }

    int rc = server ? runServer(&args, q) : runClient(&args, q);

    netqueueShutdown(q, 0);
    objRelease(&q);

    logShutdown();
    conShutdown();
    return rc;
}

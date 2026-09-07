// A real HTTP client: fetches a URL and writes the body to stdout or to a file, following
// redirects, decoding chunked transfer-encoding, and keeping cookies for the length of the run.
//
// Unlike tlsdemo, which sends one hardcoded request to show the TLS filter working, this is the
// tool to point at a real server when something in cxhttp looks wrong. Everything it does goes
// through the same public API an application would use.
//
// Usage: httpdemo [-o file] [-X method] [-H "Name: value"] [-d body] [--no-redirect]
//                 [--http3] [--http3-only] [--ca ca.pem] [--qlog dir] <url>
//
// --http3 races QUIC against TCP and uses whichever answers first; --http3-only fails the
// request if HTTP/3 is not reachable. --ca trusts one certificate authority instead of the
// system store, which is what pointing this at a local test server takes. --qlog writes a qlog
// file per QUIC connection into an existing directory, which is how to see why a handshake that
// never reaches HTTP went wrong.

#include <cxhttp.h>
#include <cxquic/quicnet.h>

#include <cx/console.h>
#include <cx/format.h>
#include <cx/fs.h>
#include <cx/serialize.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define TICK_WAIT_US timeMS(50)
#define OVERALL_LIMIT_US timeS(120)

typedef struct DemoCtx {
    bool done;
    int exitCode;
    uint64 bodyBytes;
    bool toFile;
    StreamBuffer* sink;
    VFS* vfs;
} DemoCtx;

// ---------------------------------------------------------------------------------------------
// response handlers
// ---------------------------------------------------------------------------------------------

static strref versionName(HttpVersion v)
{
    switch (v) {
    case HTTPVER_1_0: return _S "1.0";
    case HTTPVER_3:   return _S "3";
    default:          return _S "1.1";
    }
}

static void onHeaders(HttpEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;

    conFmt(conErr(),
           _SL("HTTP/${string} ${uint}\n"),
           stvar(strref, versionName(ev->version)),
           stvar(uint32, (uint32)ev->status));

    for (int32 i = 0; i < httpHeadersCount(ev->headers); i++) {
        conFmt(conErr(),
               _SL("  ${string}: ${string}\n"),
               stvar(strref, ev->headers->names.a[i]),
               stvar(strref, ev->headers->values.a[i]));
    }
    conPuts(conErr(), _SL("\n"));

    if (ev->status >= 400)
        ctx->exitCode = 1;
}

// Only registered when the body is going to stdout; with -o the sink writes the file itself and
// this never runs.
static void onData(HttpEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    ctx->bodyBytes += ev->len;
    conWrite(conOut(), ev->data, ev->len);
}

static void onComplete(HttpEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;
    ctx->done    = true;
}

static void onError(HttpEvent* ev)
{
    DemoCtx* ctx = (DemoCtx*)ev->ctx;

    strref why;
    switch (ev->err) {
    case HTTPERR_BadUrl:           why = _S"the URL could not be used"; break;
    case HTTPERR_BadMessage:       why = _S"the server sent something unacceptable"; break;
    case HTTPERR_TooLarge:         why = _S"the response exceeded a limit"; break;
    case HTTPERR_Closed:           why = _S"the connection ended early"; break;
    case HTTPERR_Timeout:          why = _S"the request timed out"; break;
    case HTTPERR_Network:          why = _S"the connection failed"; break;
    case HTTPERR_TooManyRedirects: why = _S"too many redirects"; break;
    default:                       why = _S"the request was aborted"; break;
    }

    conFmt(conErr(), _SL("error: ${string}"), stvar(strref, why));
    if (ev->err == HTTPERR_Network && ev->neterr != NERR_None)
        conFmt(conErr(), _SL(" (net error ${int})"), stvar(int32, (int32)ev->neterr));
    conPuts(conErr(), _SL("\n"));

    ctx->exitCode = 1;
    ctx->done     = true;
}

static const HttpHandlers kBufferedHandlers = {
    .headers  = onHeaders,
    .complete = onComplete,
    .error    = onError,
};

static const HttpHandlers kStreamingHandlers = {
    .headers  = onHeaders,
    .data     = onData,
    .complete = onComplete,
    .error    = onError,
};

// ---------------------------------------------------------------------------------------------

static HttpMethod methodFromName(strref name)
{
    if (strEqi(name, _SL("GET")))     return HTTP_Get;
    if (strEqi(name, _SL("HEAD")))    return HTTP_Head;
    if (strEqi(name, _SL("POST")))    return HTTP_Post;
    if (strEqi(name, _SL("PUT")))     return HTTP_Put;
    if (strEqi(name, _SL("DELETE")))  return HTTP_Delete;
    if (strEqi(name, _SL("OPTIONS"))) return HTTP_Options;
    if (strEqi(name, _SL("PATCH")))   return HTTP_Patch;
    return HTTP_MethodOther;
}

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

// Trust one certificate authority instead of the system store. The client builds its own
// configuration when it is not given one, so this only has to supply the part that differs --
// cxhttp adds the protocols it can speak to whatever it is handed.
static bool useCA(HttpClient* http, strref path)
{
    string pem = 0;
    if (!slurp(&pem, path)) {
        conPuts(conErr(), _SL("could not read the CA certificate\n"));
        return false;
    }

    TlsCAStore* ca = tlscastoreCreate();
    bool ok        = ca && tlscastoreAddPEM(ca, pem);
    strDestroy(&pem);

    TlsConfig* cfg = NULL;
    if (ok) {
        cfg = tlsconfigCreateClient();
        ok  = cfg != NULL;
    }

    if (ok) {
        tlsconfigSetCA(cfg, ca);
        httpclientSetTlsConfig(http, cfg);
    } else {
        conPuts(conErr(), _SL("the CA certificate would not load\n"));
    }

    objRelease(&cfg);
    objRelease(&ca);
    return ok;
}

static void usage(void)
{
    conPuts(conErr(),
            _SL("usage: httpdemo [-o file] [-X method] [-H \"Name: value\"] [-d body]\n"
                "                [--no-redirect] [--http3] [--http3-only] [--ca ca.pem]\n"
                "                [--qlog dir] <url>\n"));
}

int entryPoint()
{
    string url = 0, outPath = 0, methodName = 0, bodyText = 0, caPath = 0, qlogDir = 0;
    sa_string extraHeaders;
    saInit(&extraHeaders, string, 4);
    flags_t reqFlags           = HTTPREQ_None;
    HttpVersionPolicy versions = HTTPV_Default;
    int rc                     = 0;

    for (int32 i = 0; i < saSize(cmdArgs); i++) {
        strref a = cmdArgs.a[i];

        if (strEq(a, _SL("-o")) && i + 1 < saSize(cmdArgs))
            strDup(&outPath, cmdArgs.a[++i]);
        else if (strEq(a, _SL("-X")) && i + 1 < saSize(cmdArgs))
            strDup(&methodName, cmdArgs.a[++i]);
        else if (strEq(a, _SL("-H")) && i + 1 < saSize(cmdArgs))
            saPush(&extraHeaders, strref, cmdArgs.a[++i]);
        else if (strEq(a, _SL("-d")) && i + 1 < saSize(cmdArgs))
            strDup(&bodyText, cmdArgs.a[++i]);
        else if (strEq(a, _SL("--no-redirect")))
            reqFlags |= HTTPREQ_NoRedirect;
        else if (strEq(a, _SL("--http3")))
            versions = HTTPV_Any;
        else if (strEq(a, _SL("--http3-only")))
            versions = HTTPV_Http3;
        else if (strEq(a, _SL("--ca")) && i + 1 < saSize(cmdArgs))
            strDup(&caPath, cmdArgs.a[++i]);
        else if (strEq(a, _SL("--qlog")) && i + 1 < saSize(cmdArgs))
            strDup(&qlogDir, cmdArgs.a[++i]);
        else if (strEmpty(url))
            strDup(&url, a);
        else {
            usage();
            rc = 2;
        }
    }

    if (rc == 0 && strEmpty(url)) {
        usage();
        rc = 2;
    }

    if (rc != 0) {
        strDestroy(&url);
        strDestroy(&outPath);
        strDestroy(&methodName);
        strDestroy(&bodyText);
        strDestroy(&caPath);
        saDestroy(&extraHeaders);
        conShutdown();
        return rc;
    }

    // cx's own diagnostics go to stderr; stdout is reserved for the body. The `cx/**` filter is not
    // optional -- the framework's channels are restricted, so a destination registered with a NULL
    // filter sees the application's logging and nothing else.
    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);
    if (!strEmpty(qlogDir))
        netquicQlog(qlogDir);

    DemoCtx ctx = { 0 };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    NetQueue* q = netqueueCreate(&conf);

    HttpClient* http   = httpclientCreate(q);
    HttpCookieJar* jar = httpcookiejarCreate();
    httpclientSetCookieJar(http, jar);

    HttpMethod method = strEmpty(methodName) ? (strEmpty(bodyText) ? HTTP_Get : HTTP_Post)
                                             : methodFromName(methodName);

    bool setupOk = strEmpty(caPath) || useCA(http, caPath);

    if (setupOk && versions != HTTPV_Default && !httpclientSetVersions(http, versions)) {
        conPuts(conErr(), _SL("this build has no HTTP/3 in it\n"));
        setupOk = false;
    }

    HttpRequest* req = setupOk ? httprequestCreate(method, url) : NULL;
    if (!req) {
        if (setupOk)
            conFmt(conErr(), _SL("could not parse URL: ${string}\n"), stvar(strref, url));
        ctx.exitCode = 2;
    } else {
        if (method == HTTP_MethodOther)
            httprequestSetMethodName(req, methodName);

        req->flags = reqFlags;

        for (int32 i = 0; i < saSize(extraHeaders); i++) {
            int32 colon = strFindChar(extraHeaders.a[i], 0, ':');
            if (colon < 0)
                continue;

            string hn = 0, hv = 0;
            strSubStr(&hn, extraHeaders.a[i], 0, colon);
            strSubStr(&hv, extraHeaders.a[i], colon + 1, strEnd);
            strTrim(&hv, hv, _SL(" \t"));
            httprequestSetHeader(req, hn, hv);
            strDestroy(&hn);
            strDestroy(&hv);
        }

        if (!strEmpty(bodyText))
            httprequestSetBody(req, bodyText, _SL("application/x-www-form-urlencoded"));

        // With -o the body goes straight into a file through a StreamBuffer and never becomes
        // resident: cxhttp produces, the file adapter consumes. Two calls, no glue.
        if (!strEmpty(outPath)) {
            ctx.vfs     = vfsCreateFromFS();
            VFSFile* vf = ctx.vfs ? vfsOpen(ctx.vfs, outPath, FS_Write | FS_Create) : NULL;
            if (!vf) {
                conFmt(conErr(), _SL("could not open ${string} for writing\n"),
                       stvar(strref, outPath));
                ctx.exitCode = 1;
            } else {
                // Push mode, not sbufFileOut(): that one drains the buffer synchronously on this
                // thread, which is the opposite of what an async transfer wants. Registered as a
                // push consumer, the file is written as the response arrives and closed when
                // cxhttp ends the stream.
                ctx.sink = sbufCreate(64 * 1024);
                if (sbufFileCRegisterPush(ctx.sink, vf, true)) {
                    httprequestSetSink(req, ctx.sink);
                    ctx.toFile = true;
                } else {
                    conPuts(conErr(), _SL("could not attach the output file\n"));
                    ctx.exitCode = 1;
                }
            }
        }

        if (ctx.exitCode == 0) {
            const HttpHandlers* handlers = ctx.toFile ? &kBufferedHandlers : &kStreamingHandlers;

            if (!httpclientSend(http, req, handlers, &ctx)) {
                conPuts(conErr(), _SL("could not start the request\n"));
                ctx.exitCode = 1;
            } else {
                int64 started = clockTimer();
                while (!ctx.done) {
                    netqueueTick(q, TICK_WAIT_US);
                    if (clockTimer() - started > OVERALL_LIMIT_US) {
                        conPuts(conErr(), _SL("giving up\n"));
                        ctx.exitCode = 1;
                        break;
                    }
                }
            }
        }

        if (ctx.toFile) {
            conFmt(conErr(), _SL("wrote ${string}\n"), stvar(strref, outPath));
        } else {
            conFmt(conErr(), _SL("\n(${uint} bytes of body)\n"), stvar(uint64, ctx.bodyBytes));
        }

        objRelease(&req);
    }

    if (ctx.sink)
        sbufRelease(&ctx.sink);
    if (ctx.vfs)
        objRelease(&ctx.vfs);

    objRelease(&jar);
    objRelease(&http);
    netqueueShutdown(q, 0);
    objRelease(&q);

    strDestroy(&url);
    strDestroy(&outPath);
    strDestroy(&methodName);
    strDestroy(&bodyText);
    strDestroy(&caPath);
    strDestroy(&qlogDir);
    saDestroy(&extraHeaders);

    rc = ctx.exitCode;
    logShutdown();
    conShutdown();
    return rc;
}

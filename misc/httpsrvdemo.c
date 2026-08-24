// A real HTTP server: serves a handful of endpoints over http or https, and shows both ways of
// answering a request -- from the netqueue callback where it arrives, and from a worker thread that
// picked it up and finished later.
//
// The deferred half is the part worth reading. cxhttp does not link cx/taskqueue and ships no task
// type; the TaskQueue below belongs to this demo. What makes the handoff work is that
// httpsrvreqRespond() may be called from any thread, so an application is free to answer from
// wherever its work happens to finish.
//
// Usage: httpsrvdemo [-p port] [-t threads] [--tls cert.pem key.pem]
//
// Endpoints:
//   /            a short index listing the rest
//   /fast        answered inline, from the connection's own worker
//   /slow        handed to a task queue and answered from there
//   /stream      a chunked response produced a piece at a time
//   /echo        echoes the request body back, with its method and headers

#include <cxhttp.h>

#include <cx/console.h>
#include <cx/format.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/platform.h>
#include <cx/taskqueue.h>
#include <cx/thread/thread.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define TICK_WAIT_MS 100

typedef struct DemoCtx {
    TaskQueue* tq;
    uint32 served;
} DemoCtx;

// ---------------------------------------------------------------------------------------------
// The deferred path
// ---------------------------------------------------------------------------------------------

// Runs on a task queue worker, which is not the thread the request arrived on and not the thread
// its connection is serviced by. Answering from here is the whole point of the demo.
static bool slowWork(TaskQueue* tq, void* data)
{
    unused_noeval(tq);

    HttpServerRequest* req = (HttpServerRequest*)data;

    // Stand in for whatever the real work would be -- a database call, an image resize, a request
    // to something else. The connection is not blocked while this runs; it simply has no answer
    // yet, and nothing else happens on it until it does.
    osSleep(timeMS(250));

    string body = 0;
    strAppend(&body, _SL("answered from a task queue worker, thread "));

    string tid = 0;
    strFromInt64(&tid, (int64)thrCurrentOSThreadID(), 10);
    strAppend(&body, tid);
    strAppend(&body, _SL("\n"));
    strDestroy(&tid);

    httpsrvreqSetHeader(req, _SL("X-Answered-By"), _SL("taskqueue"));
    httpsrvreqRespond(req, body, _SL("text/plain"));
    strDestroy(&body);

    // The reference the handler took when it decided to defer.
    objRelease(&req);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The streamed path
// ---------------------------------------------------------------------------------------------

typedef struct StreamCtx {
    int remaining;
} StreamCtx;

static void streamCleanup(void* ctx)
{
    xaFree(ctx);
}

// A pull producer: cxhttp calls this whenever it has room on the wire, so the body is built a piece
// at a time and never exists all at once. The response goes out chunked, because a length was not
// known when the head was written.
static size_t streamPull(StreamBuffer* sb, uint8* buf, size_t sz, void* ctx)
{
    StreamCtx* s = (StreamCtx*)ctx;

    if (s->remaining <= 0) {
        sbufPFinish(sb);
        return 0;
    }

    string line = 0;
    string n    = 0;
    strFromInt64(&n, s->remaining, 10);
    strAppend(&line, _SL("chunk "));
    strAppend(&line, n);
    strAppend(&line, _SL("\n"));
    strDestroy(&n);

    uint32 len = strLen(line);
    if (len > sz)
        len = (uint32)sz;
    memcpy(buf, strC(line), len);
    strDestroy(&line);

    s->remaining--;
    return len;
}

// ---------------------------------------------------------------------------------------------
// Request handling
// ---------------------------------------------------------------------------------------------

STR_CONST(kIndex,
          "cxhttp server demo\n"
          "\n"
          "  /fast    answered inline, on the connection's own worker\n"
          "  /slow    handed to a task queue and answered from there\n"
          "  /stream  a chunked response produced a piece at a time\n"
          "  /echo    echoes the request body back\n");

static void onRequest(HttpServerEvent* ev)
{
    DemoCtx* ctx           = (DemoCtx*)ev->ctx;
    HttpServerRequest* req = ev->request;

    ctx->served++;

    conFmt(conErr(),
           _SL("${string} ${string} -> "),
           stvar(strref, req->methodName),
           stvar(strref, req->path));

    if (strEq(req->path, _SL("/"))) {
        conPuts(conErr(), _SL("index\n"));
        httpsrvreqRespond(req, kIndex, _SL("text/plain"));
        return;
    }

    // Inline: the answer is written before this callback returns.
    if (strEq(req->path, _SL("/fast"))) {
        conPuts(conErr(), _SL("inline\n"));
        httpsrvreqSetHeader(req, _SL("X-Answered-By"), _SL("netqueue-worker"));
        httpsrvreqRespond(req, _SL("answered inline\n"), _SL("text/plain"));
        return;
    }

    // Deferred: the handler returns without answering, and something else answers later. Holding a
    // reference is what keeps the request alive in the meantime -- the connection's own reference
    // is not ours to rely on.
    if (strEq(req->path, _SL("/slow"))) {
        conPuts(conErr(), _SL("deferred\n"));

        if (!ctx->tq) {
            httpsrvreqRespond(req, _SL("no task queue in this build\n"), _SL("text/plain"));
            return;
        }

        objAcquire(req);
        if (!tqCall(ctx->tq, slowWork, req)) {
            objRelease(&req);
            httpsrvreqRespondStatus(req, HTTP_ServiceUnavailable);
        }
        return;
    }

    if (strEq(req->path, _SL("/stream"))) {
        conPuts(conErr(), _SL("streamed\n"));

        StreamCtx* s  = xaAllocStruct(StreamCtx);
        s->remaining  = 8;

        StreamBuffer* sb = sbufCreate(64);
        if (!sbufPRegisterPull(sb, streamPull, streamCleanup, s)) {
            sbufPFinish(sb);
            httpsrvreqRespondStatus(req, HTTP_InternalError);
            return;
        }

        // A negative length means the size is not known up front, so it goes out chunked.
        httpsrvreqRespondStream(req, sb, -1, _SL("text/plain"));

        // Both sides are registered and each holds its own reference, so the one sbufCreate()
        // handed back is done with.
        sbufRelease(&sb);
        return;
    }

    if (strEq(req->path, _SL("/echo"))) {
        conPuts(conErr(), _SL("echo\n"));

        string body = 0;
        strAppend(&body, req->methodName);
        strAppend(&body, _SL(" "));
        strAppend(&body, req->target);
        strAppend(&body, _SL("\n"));

        for (int32 i = 0; i < httpHeadersCount(&req->headers); i++) {
            strAppend(&body, req->headers.names.a[i]);
            strAppend(&body, _SL(": "));
            strAppend(&body, req->headers.values.a[i]);
            strAppend(&body, _SL("\n"));
        }

        strAppend(&body, _SL("\n"));
        strAppend(&body, req->body);

        httpsrvreqRespond(req, body, _SL("text/plain"));
        strDestroy(&body);
        return;
    }

    conPuts(conErr(), _SL("404\n"));
    httpsrvreqRespondStatus(req, HTTP_NotFound);
}

static void onError(HttpServerEvent* ev)
{
    conFmt(conErr(), _SL("  connection error: ${uint}\n"), stvar(uint32, (uint32)ev->err));
}

static const HttpServerHandlers kHandlers = {
    .request = onRequest,
    .error   = onError,
};

// ---------------------------------------------------------------------------------------------

static bool loadTls(HttpServer* srv, NetAddr* addr, strref certPath, strref keyPath)
{
    TlsCreds* creds = tlscredsCreateFiles(certPath, keyPath, NULL);
    if (!creds) {
        conPuts(conErr(), _SL("could not load the certificate or key\n"));
        return false;
    }

    TlsConfig* cfg = tlsconfigCreateServer(creds);
    bool ok        = cfg && httpserverListenTls(srv, addr, 16, cfg);

    objRelease(&cfg);
    objRelease(&creds);
    return ok;
}

static void usage(void)
{
    conPuts(conErr(),
            _SL("usage: httpsrvdemo [-p port] [-t threads] [--tls cert.pem key.pem]\n"));
}

int entryPoint()
{
    DemoCtx ctx  = { 0 };
    int rc       = 0;
    int32 port   = 8080;
    int32 nthreads = 2;
    string cert  = 0;
    string key   = 0;

    NetQueue* q     = NULL;
    HttpServer* srv = NULL;

    for (int32 i = 0; i < saSize(cmdArgs); i++) {
        strref a = cmdArgs.a[i];

        if (strEq(a, _SL("-p")) && i + 1 < saSize(cmdArgs)) {
            strToInt32(&port, cmdArgs.a[++i], 10, STRNUM_NoTrailing);
        } else if (strEq(a, _SL("-t")) && i + 1 < saSize(cmdArgs)) {
            strToInt32(&nthreads, cmdArgs.a[++i], 10, STRNUM_NoTrailing);
        } else if (strEq(a, _SL("--tls")) && i + 2 < saSize(cmdArgs)) {
            strDup(&cert, cmdArgs.a[++i]);
            strDup(&key, cmdArgs.a[++i]);
        } else {
            usage();
            strDestroy(&cert);
            strDestroy(&key);
            return 2;
        }
    }

    // cx's own diagnostics go to stderr. The `cx/**` filter is not optional -- the framework's
    // channels are restricted, so a destination registered with a NULL filter sees the
    // application's logging and nothing else.
    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    NetQueueConfig conf;
    netqueuePresetServer(&conf);
    q = netqueueCreate(&conf);
    if (!q) {
        conPuts(conErr(), _SL("could not create the queue\n"));
        rc = 1;
        goto out;
    }

    // The demo's own task queue, not cxhttp's: the library neither links cx/taskqueue nor requires
    // one. It exists here only to have a thread that is not a netqueue worker.
    if (nthreads > 0) {
        TaskQueueConfig tqconf;
        tqPresetMinimal(&tqconf);
        ctx.tq = tqCreate(_SL("httpsrvdemo"), &tqconf);
        if (ctx.tq)
            tqStart(ctx.tq);
    }

    srv = httpserverCreate(q);
    if (!srv) {
        conPuts(conErr(), _SL("could not create the server\n"));
        rc = 1;
        goto out;
    }

    httpserverSetHandlers(srv, &kHandlers, &ctx);

    // A server that accepts uploads has to say how big one may be; the default is unlimited, which
    // suits a client downloading a file and does not suit this.
    srv->limits.maxBodyBytes = 1024 * 1024;

    NetAddr addr;
    netAddrFromStr(&addr, _SL("0.0.0.0"));
    addr.port = (uint16)port;

    bool listening;
    if (!strEmpty(cert)) {
        listening = loadTls(srv, &addr, cert, key);
    } else {
        listening = httpserverListen(srv, &addr, 16);
    }

    if (!listening) {
        conFmt(conErr(), _SL("could not listen on port ${uint}\n"), stvar(uint32, (uint32)port));
        rc = 1;
        goto out;
    }

    conFmt(conErr(),
           _SL("listening on ${string}://0.0.0.0:${uint}\n"),
           stvar(strref, strEmpty(cert) ? _S "http" : _S "https"),
           stvar(uint32, (uint32)httpserverPort(srv)));
    conPuts(conErr(), _SL("try /fast, /slow, /stream, /echo -- ctrl-c to stop\n\n"));

    for (;;)
        netqueueTick(q, TICK_WAIT_MS);

out:
    if (srv) {
        httpserverShutdown(srv);
        objRelease(&srv);
    }
    if (ctx.tq) {
        tqShutdown(ctx.tq, timeS(5));
        objRelease(&ctx.tq);
    }
    if (q) {
        netqueueShutdown(q, 0);
        objRelease(&q);
    }

    strDestroy(&cert);
    strDestroy(&key);
    return rc;
}

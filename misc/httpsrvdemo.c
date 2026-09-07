// A real HTTP server: serves a handful of endpoints over http or https, and shows both ways of
// answering a request -- from the netqueue callback where it arrives, and from a worker thread that
// picked it up and finished later.
//
// The deferred half is the part worth reading. cxhttp does not link cx/taskqueue and ships no task
// type; the TaskQueue below belongs to this demo. What makes the handoff work is that
// httpsrvreqRespond() may be called from any thread, so an application is free to answer from
// wherever its work happens to finish.
//
// Usage: httpsrvdemo [-p port] [-t threads] [--tls cert.pem key.pem] [--http3] [-w workers] [-v]
//
// --http3 adds an HTTP/3 listener on the same port number over UDP, alongside the TCP one, so the
// same endpoints answer over either. It needs --tls, because QUIC is always encrypted.
//
// -t is the demo's own task queue, which answers /slow. -w is the netqueue's worker count, where 0
// means polled and this thread drives everything. -v turns cx's own logging up to Trace.
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

#define IDLE_SLEEP_US timeMS(100)
#define TICK_WAIT_US  timeMS(100)

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

    if (sz == 0) {
        // A status check rather than a request for data. Once the stream is over there is nothing
        // left to feed it, so hand the slot back.
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    if (s->remaining <= 0) {
        sbufPUnregister(sb);
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
            httpsrvreqRespondStatus(req, HTTP_ServiceUnavailable);
            objRelease(&req);
        }
        return;
    }

    if (strEq(req->path, _SL("/stream"))) {
        conPuts(conErr(), _SL("streamed\n"));

        StreamCtx* s  = xaAllocStruct(StreamCtx);
        s->remaining  = 8;

        StreamBuffer* sb = sbufCreate(64);
        if (!sbufPRegisterPull(sb, streamPull, streamCleanup, s)) {
            sbufRelease(&sb);
            httpsrvreqRespondStatus(req, HTTP_InternalError);
            return;
        }

        // A negative length means the size is not known up front, so it goes out chunked.
        httpsrvreqRespondStream(req, sb, -1, _SL("text/plain"));

        // The request holds its own reference now, so the one sbufCreate() handed back is done
        // with.
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

// Each listener gets its own TlsConfig and they share the credentials, because the two offer
// different ALPN protocols and the first connection built from a configuration freezes it.
static bool listenSecure(HttpServer* srv, NetAddr* addr, TlsCreds* creds, bool quic)
{
    TlsConfig* cfg = tlsconfigCreateServer(creds);
    if (!cfg)
        return false;

    bool ok = quic ? httpserverListenQuic(srv, addr, cfg) : httpserverListenTls(srv, addr, 16, cfg);

    objRelease(&cfg);
    return ok;
}

static void usage(void)
{
    conPuts(conErr(),
            _SL("usage: httpsrvdemo [-p port] [-t threads] [--tls cert.pem key.pem] [--http3]\n"
                "                   [-w workers] [-v]\n"));
}

int entryPoint()
{
    DemoCtx ctx  = { 0 };
    int rc       = 0;
    int32 port   = 8080;
    int32 nthreads = 2;
    string cert    = 0;
    string key     = 0;
    bool http3     = false;
    int32 workers  = -1;   // negative leaves the preset's own count alone
    int loglevel   = LOG_Warn;

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
        } else if (strEq(a, _SL("--http3"))) {
            http3 = true;
        } else if (strEq(a, _SL("-w")) && i + 1 < saSize(cmdArgs)) {
            strToInt32(&workers, cmdArgs.a[++i], 10, STRNUM_NoTrailing);
        } else if (strEq(a, _SL("-v"))) {
            loglevel = LOG_Trace;
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
    logconsoleRegister(loglevel, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    NetQueueConfig conf;
    netqueuePresetServer(&conf);
    if (workers >= 0)
        conf.nthreads = workers;
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
        TlsCreds* creds = tlscredsCreateFiles(cert, key, NULL);
        if (!creds) {
            conPuts(conErr(), _SL("could not load the certificate or key\n"));
            rc = 1;
            goto out;
        }

        listening = listenSecure(srv, &addr, creds, false);

        // The port the TCP listener actually got, which matters when -p asked for 0: HTTP/3 is
        // advertised as the same port number over UDP, so the two have to agree.
        if (listening && http3) {
            addr.port = httpserverPort(srv);
            listening = listenSecure(srv, &addr, creds, true);
        }

        objRelease(&creds);
    } else if (http3) {
        conPuts(conErr(), _SL("--http3 needs --tls: QUIC is always encrypted\n"));
        rc = 2;
        goto out;
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
    if (http3) {
        conFmt(conErr(),
               _SL("  and HTTP/3 on udp/${uint}\n"),
               stvar(uint32, (uint32)httpserverPort(srv)));
    }
    conPuts(conErr(), _SL("try /fast, /slow, /stream, /echo -- ctrl-c to stop\n\n"));

    // netqueuePresetServer() sizes a worker pool to the machine, so this queue normally runs
    // threaded: an ingest thread and dispatch workers are already driving it. Calling
    // netqueueTick() here too would race that ingest thread's own select() wait -- tick() is for
    // polled mode only, and the main thread has nothing left to do but stay alive until ctrl-c.
    //
    // -w 0 asks for no workers at all, and that queue has nothing driving it but this loop.
    if (conf.nthreads > 0) {
        for (;;)
            osSleep(IDLE_SLEEP_US);
    } else {
        for (;;)
            netqueueTick(q, TICK_WAIT_US);
    }

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

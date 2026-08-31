// A working log forwarder, both halves of it, over a real NetQueue.
//
// This is a demo, not API. Nothing in cx depends on it, and an application is free to build a
// completely different transport -- that freedom is the reason the seam exists at all. What this
// program is for is proving the seam is sufficient: cx supplies a codec, a spool and loop
// prevention, and everything that touches a wire is here, above both cx/log and cx/net.
//
// Usage:
//   logfwddemo --collect <port>          listen, decode, re-inject, print
//   logfwddemo --leaf <host> <port>      connect, forward this process's own logging
//
// What to watch for, each of which is a claim the design makes:
//
//   * Backpressure end to end. Stop the collector reading (SIGSTOP it) and the leaf's send starts
//     refusing, the spool fills, and NET_SendReady resumes it when the collector comes back.
//   * Disconnect and reconnect. Kill the collector, let the leaf spool, restart it: the backfill
//     arrives, with a gap record if the spool's bound was passed.
//   * Subscription. The leaf ships nothing until the collector asks. Start the leaf on its own
//     and watch it stay silent no matter how much it logs.
//   * Loop prevention. The leaf's filter names `cx/**` explicitly, and the network chatter of the
//     very connection carrying the log traffic still does not get forwarded. Without that this
//     program would amplify without bound the moment the collector went away.

#include <cx/console.h>
#include <cx/format.h>
#include <cx/log.h>
#include <cx/net.h>
#include <cx/platform.h>
#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/time/clock.h>

DEFINE_ENTRY_POINT;

#define TICK_WAIT_US   timeMS(50)
#define RECONNECT_US   timeS(2)
#define TRAFFIC_US     timeMS(400)
#define DEMO_SPOOL     (256 * 1024)
#define DEMO_SEGMENT   (16 * 1024)

// ---------------------------------------------------------------------------------------------
// The leaf: forwards this process's own logging to a collector
// ---------------------------------------------------------------------------------------------

typedef struct LeafCtx {
    NetQueue* q;
    NetSocket* sock;
    strref host;
    uint16 port;

    LogForwarder* fwd;
    bool connected;
    bool announced;   // the catalog has been sent for the current subscription
    int64 retryat;   // clockTimer() deadline for the next connect attempt, 0 if none pending
} LeafCtx;

// The whole of the egress seam. netsocketSend() queues the bytes and returns; false means the
// backlog is over the socket's high watermark, which is exactly what the forwarder's contract
// calls "not now", so it maps straight across with nothing in between.
//
// Anything this function logs is marked local-only by cx and never comes back to the forwarder,
// which is what makes it safe for a send path to be chatty.
static bool leafSend(void* vctx, const uint8* buf, size_t len)
{
    LeafCtx* ctx = (LeafCtx*)vctx;
    if (!ctx->sock || !ctx->connected)
        return false;

    return netsocketSend(ctx->sock, buf, len, NULL, 0);
}

static void leafClose(void* vctx)
{
    unused_noeval(vctx);
}

static const LogForwardHandlers kLeafForward = {
    .send  = leafSend,
    .close = leafClose,
};

static void leafConnect(_Inout_ LeafCtx* ctx);

static void leafOnConnection(_In_ NetEvent* ev)
{
    LeafCtx* ctx = (LeafCtx*)ev->ctx;

    if (ev->conn.state != NCS_Connected) {
        if (ev->conn.err != NERR_None) {
            conFmt(conErr(),
                   _SL("[leaf] connect failed (${int}); retrying\n"),
                   stvar(int32, (int32)ev->conn.err));
            ctx->connected = false;
            ctx->retryat   = clockTimer() + RECONNECT_US;
        }
        return;
    }

    conPuts(conOut(), _SL("[leaf] connected; draining whatever was spooled\n"));
    ctx->connected = true;

    // Everything logged while there was nowhere to send it is still here, cut into independently
    // decodable segments, and goes out now ahead of anything new.
    logForwardConnected(ctx->fwd);
}

// The transport has drained below its low watermark and can take more. This is the callback the
// forwarder's refuse/resume contract was shaped around.
static void leafOnSendReady(_In_ NetEvent* ev)
{
    LeafCtx* ctx = (LeafCtx*)ev->ctx;
    logForwardResume(ctx->fwd);
}

// The control plane, running the other way over the same connection. A leaf ships nothing until
// this hands it a subscription -- start the leaf on its own and watch it stay silent.
static void leafOnRecv(_In_ NetEvent* ev)
{
    LeafCtx* ctx = (LeafCtx*)ev->ctx;

    uint8 buf[1024];
    size_t n;
    while ((n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0)) > 0) {
        if (!logForwardRecv(ctx->fwd, buf, n)) {
            conPuts(conErr(), _SL("[leaf] malformed control stream; dropping the connection\n"));
            netsocketClose(ev->socket);
            return;
        }
    }

    LogForwardStats st;
    logForwardStats(ctx->fwd, &st);
    if (st.subscribed && !ctx->announced) {
        ctx->announced = true;
        conPuts(conOut(), _SL("[leaf] subscribed; sending the channel catalog\n"));

        // What this binary is capable of logging, so an operator on the other end can browse it
        // rather than guess at channel names.
        Buffer cat = 0;
        if (logForwardCatalog(ctx->fwd, &cat))
            netsocketSend(ev->socket, cat->data, cat->len, NULL, 0);
        bufDestroy(&cat);
    }
}

static void leafOnFlowClosed(_In_ NetEvent* ev)
{
    LeafCtx* ctx = (LeafCtx*)ev->ctx;

    conPuts(conOut(), _SL("[leaf] connection lost; spooling\n"));
    ctx->connected = false;
    ctx->announced = false;

    // Spool from here, and cut the segment: what was already sent is gone with the connection, so
    // the next thing spooled has to stand on its own.
    logForwardDisconnected(ctx->fwd);
    ctx->retryat = clockTimer() + RECONNECT_US;
}

static const NetHandlers kLeafHandlers = {
    .connection = leafOnConnection,
    .recv       = leafOnRecv,
    .sendReady  = leafOnSendReady,
    .flowClosed = leafOnFlowClosed,
};

static void leafConnect(_Inout_ LeafCtx* ctx)
{
    ctx->retryat = 0;

    if (ctx->sock) {
        netsocketClose(ctx->sock);
        objRelease(&ctx->sock);
    }

    ctx->sock = netqueueConnect(ctx->q, ctx->host, ctx->port, &kLeafHandlers, ctx);
    if (!ctx->sock) {
        conPuts(conErr(), _SL("[leaf] could not start a connection; retrying\n"));
        ctx->retryat = clockTimer() + RECONNECT_US;
    }
}

// Something to watch. Three channels at three levels, so a subscription filter has something to
// select between once there is one.
static void leafTraffic(uint32 n)
{
    LogChannel* db   = logChan(_SL("demo/db"));
    LogChannel* http = logChan(_SL("demo/http"));
    LogChannel* job  = logChan(_SL("demo/job"));

    logFmtC(Info, db, _S "query finished in ${int}ms", stvar(int32, (int32)(n % 37) + 3));
    logFmtC(Info,
            http,
            _S "GET /orders/${uint} -> ${int}",
            stvar(uint32, n),
            stvar(int32, (n % 11 == 0) ? 500 : 200));

    if (n % 5 == 0) {
        withLogCtx (stvark(job, uint32, n)) {
            logFmtC(Notice, job, _S "batch ${uint} started", stvar(uint32, n));
        }
    }
    if (n % 11 == 0)
        logFmtC(Error, http, _S "upstream refused request ${uint}", stvar(uint32, n));
}

static int runLeaf(_In_ strref host, uint16 port)
{
    LeafCtx ctx = { .host = host, .port = port };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    ctx.q = netqueueCreate(&conf);
    if (!ctx.q) {
        conPuts(conErr(), _SL("could not create a network queue\n"));
        return 1;
    }

    // The filter deliberately names cx's own subtree as well as the application's. cx/net is still
    // never forwarded -- that is not something a filter gets to override -- so this is the
    // configuration that proves the rule rather than one that relies on it not being written.
    LogForwardConfig fcfg = {
        .origin     = _SL("leaf-demo"),
        .spoolbytes = DEMO_SPOOL,
        .segbytes   = DEMO_SEGMENT,
    };
    ctx.fwd = logforwardRegister(LOG_Info, _SL("demo/**"), &kLeafForward, &ctx, &fcfg);
    if (!ctx.fwd) {
        conPuts(conErr(), _SL("could not register a forwarder\n"));
        objRelease(&ctx.q);
        return 1;
    }
    logDestAddFilter(logForwardDest(ctx.fwd), _SL("cx/**"), false);

    // There is no connection yet, so everything logged from here until there is one is spooled.
    logForwardDisconnected(ctx.fwd);

    conFmt(conOut(),
           _SL("[leaf] forwarding demo/** and cx/** to ${string}:${uint}\n"),
           stvar(strref, host),
           stvar(uint16, port));
    leafConnect(&ctx);

    uint32 n          = 0;
    int64 nexttraffic = clockTimer();
    for (;;) {
        netqueueTick(ctx.q, TICK_WAIT_US);

        // This runs until it is interrupted, so nothing may sit in the console's buffer waiting
        // for an exit that only ever arrives as a signal.
        conFlush(conOut());
        conFlush(conErr());

        int64 now = clockTimer();
        if (ctx.retryat && now >= ctx.retryat)
            leafConnect(&ctx);

        if (now >= nexttraffic) {
            nexttraffic = now + TRAFFIC_US;
            leafTraffic(n++);

            if (n % 25 == 0) {
                LogForwardStats st;
                logForwardStats(ctx.fwd, &st);
                conFmt(conOut(),
                       _SL("[leaf] sent=${uint} spooled=${uint} dropped=${uint} looped=${uint} "
                           "pending=${uint}B\n"),
                       stvar(uint64, st.sent),
                       stvar(uint64, st.spooled),
                       stvar(uint64, st.dropped),
                       stvar(uint64, st.looped),
                       stvar(uint64, st.pending));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The collector: decodes what arrives and re-injects it into the local log system
// ---------------------------------------------------------------------------------------------

typedef struct CollectPeer {
    LogWireDecoder* dec;
    NetSocket* sock;
    struct CollectPeer* next;
} CollectPeer;

// The collector's half of the control plane. A leaf is silent until this arrives, so the
// subscription is not an optimization -- it is the only thing that makes a leaf talk.
static void collectSubscribe(_In_ NetSocket* sock)
{
    LogWireEncoder* enc = logWireEncoderCreate(NULL, 0);

    LogSubSpec spec = { .maxlevel = LOG_Info };
    saInit(&spec.patterns, string, 2);
    saPush(&spec.patterns, string, _S "demo/**");

    Buffer frames = 0;
    if (logWireEncodeSub(enc, &frames, &spec))
        netsocketSend(sock, frames->data, frames->len, NULL, 0);

    bufDestroy(&frames);
    saDestroy(&spec.patterns);
    logWireEncoderDestroy(&enc);
}

typedef struct CollectCtx {
    NetQueue* q;
    CollectPeer* peers;
    uint64 injected;
} CollectCtx;

// The entire receiving side. The record arrives with the sender's timestamps, sequence numbers and
// channel paths; logInject() puts it through this process's own routing, filters and per-channel
// level checks, so what the collector's own configuration says still decides where it goes.
static bool collectFrame(const LogWireFrame* frame, void* vctx)
{
    CollectCtx* ctx = (CollectCtx*)vctx;

    if (frame->kind == LOG_WireGap) {
        conFmt(conOut(),
               _SL("[collect] --- ${uint} records were dropped by the sender (seq ${uint}..${uint}) "
                   "---\n"),
               stvar(uint64, frame->gap->count),
               stvar(uint64, frame->gap->firstseq),
               stvar(uint64, frame->gap->lastseq));
        return true;
    }

    if (frame->kind == LOG_WireCatalog) {
        conFmt(conOut(),
               _SL("[collect] the leaf can log on ${int} channels:\n"),
               stvar(int32, frame->cat->nchans));
        for (int i = 0; i < frame->cat->nchans; i++) {
            conFmt(conOut(),
                   _SL("            ${string}${string}\n"),
                   stvar(strref, frame->cat->chans[i].path),
                   stvar(strref,
                         (frame->cat->chans[i].flags & LOG_Restricted) ? _S " (restricted)"
                                                                       : _S ""));
        }
        return true;
    }

    if (frame->kind != LOG_WireEntry)
        return true;

    // The channel path stays exactly what the sender logged it to, so a rule written here for
    // `demo/http` matches whether the record came from this machine or another one. Which machine
    // it came from is a field, not part of the path.
    if (logInject(frame->rec->chanpath, frame->rec))
        ctx->injected++;

    return true;
}

static _Ret_opt_valid_ CollectPeer* collectPeerFor(_Inout_ CollectCtx* ctx, _In_ NetSocket* sock)
{
    for (CollectPeer* p = ctx->peers; p; p = p->next) {
        if (p->sock == sock)
            return p;
    }

    CollectPeer* p = xaAllocStruct(CollectPeer, XA_Zero);
    p->dec         = logWireDecoderCreate();
    p->sock        = sock;
    p->next        = ctx->peers;
    ctx->peers     = p;
    return p;
}

static void collectPeerDrop(_Inout_ CollectCtx* ctx, _In_ NetSocket* sock)
{
    CollectPeer** prev = &ctx->peers;
    for (CollectPeer* p = ctx->peers; p; prev = &p->next, p = p->next) {
        if (p->sock != sock)
            continue;

        *prev = p->next;
        logWireDecoderDestroy(&p->dec);
        xaFree(p);
        return;
    }
}

static void collectOnAccepted(_In_ NetEvent* ev)
{
    conPuts(conOut(), _SL("[collect] a leaf connected; asking it for demo/** at Info\n"));
    collectPeerFor((CollectCtx*)ev->ctx, ev->accept.newSocket);
    collectSubscribe(ev->accept.newSocket);
}

static void collectOnRecv(_In_ NetEvent* ev)
{
    CollectCtx* ctx = (CollectCtx*)ev->ctx;
    CollectPeer* p  = collectPeerFor(ctx, ev->socket);

    uint8 buf[4096];
    size_t n;
    while ((n = netsocketRecv(ev->socket, buf, sizeof(buf), NULL, 0)) > 0) {
        // A decoder holds an incomplete frame until the rest of it arrives, so the transport is
        // free to deliver bytes in whatever sizes it happens to have them.
        if (!logWireDecode(p->dec, buf, n, collectFrame, ctx)) {
            conPuts(conErr(), _SL("[collect] malformed stream; closing the connection\n"));
            netsocketClose(ev->socket);
            return;
        }
    }
}

static void collectOnFlowClosed(_In_ NetEvent* ev)
{
    conPuts(conOut(), _SL("[collect] a leaf disconnected\n"));
    collectPeerDrop((CollectCtx*)ev->ctx, ev->socket);
}

static const NetHandlers kCollectHandlers = {
    .accepted   = collectOnAccepted,
    .recv       = collectOnRecv,
    .flowClosed = collectOnFlowClosed,
};

static int runCollect(uint16 port)
{
    CollectCtx ctx = { 0 };

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    conf.flags |= NQ_AutoAccept;   // the queue services accepted sockets; the demo just reads them
    ctx.q = netqueueCreate(&conf);
    if (!ctx.q) {
        conPuts(conErr(), _SL("could not create a network queue\n"));
        return 1;
    }

    NetAddr addr;
    netAddrFromStr(&addr, _SL("0.0.0.0"));
    addr.port = port;

    // Queue-wide rather than per-socket: under NQ_AutoAccept the queue registers each accepted
    // socket itself, and an accepted socket carries no handlers of its own, so the events that
    // matter here arrive through the queue's fallback level.
    netqueueSetHandlers(ctx.q, &kCollectHandlers, &ctx);

    NetSocket* lsock = netqueueListen(ctx.q, &addr, 0, &kCollectHandlers, &ctx);
    if (!lsock) {
        conPuts(conErr(), _SL("could not listen\n"));
        objRelease(&ctx.q);
        return 1;
    }

    // Re-injected records land on the sender's channel paths, so this is an ordinary console
    // destination naming an ordinary channel. LOG_IncludeContext is what prints the `origin`
    // field the injector attaches, which is where the sending machine's identity lives.
    LogTextConfig tcfg = {
        .dateFormat = LOG_DateISO,
        .flags      = LOG_IncludeChannel | LOG_BracketChannel | LOG_AddColon | LOG_IncludeContext,
    };
    LogConsoleConfig ccfg = { 0 };
    logconsoleRegister(LOG_Debug, _SL("demo/**"), NULL, NULL, &ccfg, logTextSerializer(&tcfg));

    conFmt(conOut(), _SL("[collect] listening on port ${uint}\n"), stvar(uint16, port));

    uint64 last = 0;
    for (;;) {
        netqueueTick(ctx.q, TICK_WAIT_US);

        // Re-injected records are written by the log system's own drain thread, so the flush has
        // to happen from somewhere that runs regardless of whether anything arrived.
        conFlush(conOut());
        conFlush(conErr());

        if (ctx.injected - last >= 25) {
            last = ctx.injected;
            conFmt(conOut(),
                   _SL("[collect] ${uint} records re-injected so far\n"),
                   stvar(uint64, ctx.injected));
        }
    }
}

// ---------------------------------------------------------------------------------------------

static void usage(void)
{
    conPuts(conErr(),
            _SL("usage: logfwddemo --collect <port>\n"
                "       logfwddemo --leaf <host> <port>\n"));
}

static bool parsePort(_Out_ uint16* out, _In_ strref s)
{
    uint32 v = 0;
    if (!strToUInt32(&v, s, 10, STRNUM_NoTrailing) || v == 0 || v > 65535)
        return false;
    *out = (uint16)v;
    return true;
}

int entryPoint()
{
    // cx's own diagnostics to stderr. The leaf asks for these to be forwarded as well, and they
    // are -- everything except cx/net, which cx refuses to forward however it is asked.
    LogConsoleConfig logcfg = { .stderrLevel = LOG_Count };
    logconsoleRegister(LOG_Warn, _SL("cx/**"), NULL, NULL, &logcfg, NULL);

    int rc = 2;

    if (saSize(cmdArgs) == 2 && strEq(cmdArgs.a[0], _SL("--collect"))) {
        uint16 port;
        if (parsePort(&port, cmdArgs.a[1]))
            rc = runCollect(port);
        else
            usage();
    } else if (saSize(cmdArgs) == 3 && strEq(cmdArgs.a[0], _SL("--leaf"))) {
        uint16 port;
        if (parsePort(&port, cmdArgs.a[2]))
            rc = runLeaf(cmdArgs.a[1], port);
        else
            usage();
    } else {
        usage();
    }

    logShutdown();
    conShutdown();
    return rc;
}

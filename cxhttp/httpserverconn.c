// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpserverconn.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// One accepted connection
//
// The mirror of HttpConn: the same socket handlers, the same drain-into-a-ring-then-step-the-parser
// loop, the same flow timers. The parser is built with isRequest = true and nothing else changes,
// which is the payoff for having written it direction-agnostic.
//
// One request is read at a time. While a request is waiting for its answer the parser is not
// stepped at all, so a second request cannot begin being understood before the first has been
// answered -- which makes responses arriving out of order impossible rather than merely avoided.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include "httpserver.h"

#include <cx/net.h>
#include <cx/serialize/streambuf.h>
#include <cx/thread/thread.h>

// How much to pull off the socket per read, matching the client side.
#define HTTPSRV_READ_CHUNK 8192

// ---------------------------------------------------------------------------------------------
// Timers
//
// Both deadlines are flow timers, so both are ordered against everything else happening on this
// connection -- an idle reap cannot overtake the client's own close, and a head-read deadline
// cannot fire between two halves of a request that already arrived.
// ---------------------------------------------------------------------------------------------

static void cancelTimers(HttpServerConn* self)
{
    NetFlow* flow = self->sock ? self->sock->flow : NULL;
    if (!flow) {
        self->readTimer = self->idleTimer = 0;
        return;
    }

    if (self->readTimer) {
        netflowCancelTimer(flow, self->readTimer);
        self->readTimer = 0;
    }
    if (self->idleTimer) {
        netflowCancelTimer(flow, self->idleTimer);
        self->idleTimer = 0;
    }
}

// Arm whichever deadline fits the state the connection is now in: reading a request head, or
// sitting idle between requests. Only ever one of them, because a connection is only ever doing one
// of those two things.
static void armTimers(HttpServerConn* self, bool reading)
{
    NetFlow* flow = self->sock ? self->sock->flow : NULL;
    if (!flow)
        return;

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv)
        return;

    cancelTimers(self);

    if (reading) {
        if (srv->readHeadTimeout > 0)
            self->readTimer = netflowAddTimer(flow, srv->readHeadTimeout, NTF_None);
    } else if (srv->idleTimeout > 0) {
        self->idleTimer = netflowAddTimer(flow, srv->idleTimeout, NTF_None);
    }

    objRelease(&srv);
}

// ---------------------------------------------------------------------------------------------
// Socket handlers
// ---------------------------------------------------------------------------------------------

// Claim this thread as the one dispatching on the connection, answering the previous value so it
// can be put back. Nesting is ordinary rather than exceptional -- responding from a request handler
// runs _respond() inside the _pump() that delivered the request -- so this saves and restores
// rather than setting and clearing.
static Thread* enterDispatch(HttpServerConn* c)
{
    Thread* prev = (Thread*)atomicLoad(ptr, &c->dispatchThread, Relaxed);
    atomicStore(ptr, &c->dispatchThread, thrCurrent(), Release);
    return prev;
}

static void leaveDispatch(HttpServerConn* c, Thread* prev)
{
    atomicStore(ptr, &c->dispatchThread, prev, Release);
}

// True when this thread is the one currently dispatching on the connection, and may therefore
// touch its parser, ring and deadlines directly.
static bool onDispatchThread(HttpServerConn* c)
{
    return atomicLoad(ptr, &c->dispatchThread, Acquire) == thrCurrent();
}

// Ask a worker to come and move this connection along. A zero-delay flow timer is the handoff:
// NET_Timer arrives on a worker, ordered behind everything already pending for this connection, so
// what it does cannot overtake an event the application has not seen yet.
//
// Whether that turns out to be writing a response or pushing more of a body is decided when it
// lands rather than here -- by then the connection's own state says which, and this side is not
// allowed to read it.
static bool handoffToWorker(HttpServerConn* c)
{
    NetFlow* flow = c->sock ? c->sock->flow : NULL;
    if (!flow)
        return false;

    atomicStore(uint32, &c->respondPending, 1, Release);
    if (netflowAddTimer(flow, 0, NTF_None) == 0) {
        // The flow is already dying, so no worker is coming.
        atomicStore(uint32, &c->respondPending, 0, Relaxed);
        return false;
    }
    return true;
}

// Same reasoning as the client's: the connection is reached through a borrowed pointer, and the
// terminal path is free to be the last thing holding it -- the server drops its reference from
// inside the very callback that reports the connection dying.
static HttpServerConn* enterConn(NetEvent* ev)
{
    HttpServerConn* c = (HttpServerConn*)ev->ctx;
    return c ? objAcquire(c) : NULL;
}

static void onNetRecv(NetEvent* ev)
{
    HttpServerConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);
    httpsrvconn_pump(c);
    leaveDispatch(c, prev);
    objRelease(&c);
}

// The socket's backlog drained below its low watermark, so a response body that stopped against the
// high one can carry on. Nothing else waits on this: a head is small enough that failing to queue it
// is a dead connection rather than backpressure.
static void onNetSendReady(NetEvent* ev)
{
    HttpServerConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);
    if (c->writing)
        httpsrvconn_pumpRespBody(c);
    leaveDispatch(c, prev);
    objRelease(&c);
}

// Let go of whichever StreamBuffers a request was using. `ok` is false when the exchange is being
// abandoned rather than finishing, which is the difference between a consumer seeing a complete
// body and seeing an error.
//
// Finish, never release: a StreamBuffer's reference *is* its registration, and the finish call is
// what hands it back. sbufError() only marks the buffer, so it is never enough on its own.
static void releaseStreams(HttpServerRequest* req, bool ok)
{
    if (!req)
        return;

    if (req->sink) {
        if (!ok)
            sbufError(req->sink);
        sbufPFinish(req->sink);
        req->sink = NULL;
    }

    if (req->respStream) {
        if (!ok)
            sbufError(req->respStream);
        sbufCFinish(req->respStream);
        req->respStream = NULL;
    }
}

// The connection is over, whatever the reason. Reports it, then asks the server to let go -- which
// is normally the last reference, so the caller's own must still be held across this.
static void connDied(HttpServerConn* c, HttpError err, NetErrorCode neterr)
{
    if (c->failed)
        return;
    c->failed = true;

    cancelTimers(c);

    // Before the handlers run, so an application waiting on a body it will never receive is
    // released rather than left holding a buffer nothing is going to finish.
    releaseStreams(c->req, false);

    HttpServer* srv = objAcquireFromWeak(HttpServer, c->server);
    if (srv) {
        if (err != HTTPERR_None)
            httpserver_deliver(srv, HTTPSRVEV_Error, c, c->req, NULL, 0, err, neterr);

        httpserver_deliver(srv, HTTPSRVEV_Closed, c, (HttpServerRequest*)NULL, NULL, 0,
                           HTTPERR_None, NERR_None);
        httpserver_forget(srv, c);
        objRelease(&srv);
    }

    c->awaiting = false;
    c->writing  = false;
    objRelease(&c->req);
}

static void onNetClosed(NetEvent* ev)
{
    HttpServerConn* c = enterConn(ev);
    if (!c)
        return;

    // A close with nothing in flight is how every keep-alive connection ends, and is not a failure.
    // One arriving mid-request is a client that gave up, which is.
    Thread* prev = enterDispatch(c);
    connDied(c, c->req ? HTTPERR_Closed : HTTPERR_None, NERR_None);
    leaveDispatch(c, prev);
    objRelease(&c);
}

static void onNetError(NetEvent* ev)
{
    HttpServerConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);
    connDied(c, HTTPERR_Network, ev->error.err);
    leaveDispatch(c, prev);
    objRelease(&c);
}

static void onNetTimer(NetEvent* ev)
{
    HttpServerConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);

    if (ev->timer.id == c->readTimer) {
        c->readTimer = 0;

        // A client that opened a connection and then sent its request a byte at a time, or not at
        // all. Answering 408 before closing is worth the one write: it tells a client whose network
        // merely stalled what happened, and costs a slowloris nothing it was not already spending.
        httpsrvconn_sendError(c, 408, HTTPERR_Timeout);
    } else if (ev->timer.id == c->idleTimer) {
        c->idleTimer = 0;
        httpsrvconnClose(c);
    } else if (atomicExchange(uint32, &c->respondPending, 0, AcqRel)) {
        // A handoff from another thread. Any id that is neither deadline is one: nothing else arms
        // a timer on this connection, and a stale id simply finds the flag already clear.
        //
        // What was written off-thread became visible here through the queue's timer lock, which
        // both arming and firing take.
        if (c->writing)
            httpsrvconn_pumpRespBody(c);
        else if (c->req)
            httpsrvconn_respond(c, c->req);
    }

    leaveDispatch(c, prev);
    objRelease(&c);
}

static const NetHandlers kSrvConnHandlers = {
    .recv       = onNetRecv,
    .sendReady  = onNetSendReady,
    .flowClosed = onNetClosed,
    .error      = onNetError,
    .timer      = onNetTimer,
};

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_objfactory_check HttpServerConn* HttpServerConn_create(_In_ HttpServer* server,
                                                        _In_ NetSocket* sock)
{
    _httpInit();

    if (!server || !sock || sock->type != NST_Stream)
        return NULL;

    HttpServerConn* self;
    self = objInstCreate(HttpServerConn);

    self->sock   = objAcquire(sock);
    self->server = objGetWeak(HttpServer, server);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    httpParserInit(self->parser, true, &server->limits);

    // Borrowed, like the client's: the socket must not own the connection, or the two would keep
    // each other alive.
    netsocketSetHandlers(sock, &kSrvConnHandlers, self);

    return self;
}

_objinit_guaranteed bool HttpServerConn_init(_In_ HttpServerConn* self)
{
    self->parser = (HttpParser*)xaAlloc(sizeof(HttpParser), XA_Zero);
    bufringInit(&self->in, HTTPSRV_READ_CHUNK);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void HttpServerConn_destroy(_In_ HttpServerConn* self)
{
    // Stop the socket calling back into an object that is going away. Everything else here would be
    // safe in any order; this must be first.
    if (self->sock)
        netsocketSetHandlers(self->sock, NULL, NULL);

    cancelTimers(self);

    if (self->parser) {
        httpParserDestroy(self->parser);
        xaFree(self->parser);
        self->parser = NULL;
    }

    bufringDestroy(&self->in);
    // Autogen begins -----
    objRelease(&self->sock);
    objDestroyWeak(&self->server);
    bufringDestroy(&self->in);
    objRelease(&self->req);
    // Autogen ends -------
}

void HttpServerConn_close(_In_ HttpServerConn* self)
{
    cancelTimers(self);

    if (self->sock)
        netsocketClose(self->sock);
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

// strPC() rather than strC(): a head built by appending is very likely a rope, and strC() would
// flatten it into a rotating thread-local scratch buffer that anything the send calls -- the filter
// chain included -- is free to take for itself before it has finished reading the payload.
static bool sendStr(HttpServerConn* self, strhandle s)
{
    uint32 len = strLen(*s);
    if (len == 0)
        return true;
    return netsocketSend(self->sock, (uint8*)strPC(s), len, NULL, 0);
}

void HttpServerConn__sendError(_In_ HttpServerConn* self, uint16 status, HttpError err)
{
    if (self->failed)
        return;

    // Nothing an error response says leaves room for another request on this connection: by the
    // time one is needed the parser is somewhere it cannot resynchronize from, or a deadline has
    // already expired.
    self->closing = true;

    string head = 0;
    strAppend(&head, _SL("HTTP/1.1 "));

    string code = 0;
    strFromInt64(&code, status, 10);
    strAppend(&head, code);
    strDestroy(&code);

    strref reason = _httpReasonPhrase(status);
    if (!strEmpty(reason)) {
        strAppendChar(&head, ' ');
        strAppend(&head, reason);
    }

    strAppend(&head, _SL("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    sendStr(self, &head);
    strDestroy(&head);

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (srv) {
        httpserver_deliver(srv, HTTPSRVEV_Error, self, self->req, NULL, 0, err, NERR_None);
        objRelease(&srv);
    }

    // The exchange is over and has been reported. Letting go of the request here is what stops the
    // close below from reporting a second, misleading HTTPERR_Closed on top of the real reason.
    releaseStreams(self->req, false);
    self->awaiting = false;
    self->writing  = false;
    objRelease(&self->req);

    httpsrvconnClose(self);
}

bool HttpServerConn__sendContinue(_In_ HttpServerConn* self)
{
    if (self->failed)
        return false;

    string s = 0;
    strAppend(&s, _SL("HTTP/1.1 100 Continue\r\n\r\n"));
    bool ok = sendStr(self, &s);
    strDestroy(&s);
    return ok;
}

// The exchange is over: let the request go and decide what the connection does next.
static void finishResponse(HttpServerConn* self, bool ok)
{
    releaseStreams(self->req, ok);

    self->awaiting = false;
    self->writing  = false;
    objRelease(&self->req);

    if (self->closing || !ok) {
        httpsrvconnClose(self);
        return;
    }

    // Another request may already have arrived while this one was being answered. Parsing it now
    // rather than waiting for a read event matters because a stream recv event is an edge: those
    // bytes are already in the ring, and nothing else is going to wake this connection.
    httpsrvconn_pump(self);
}

void HttpServerConn__pumpRespBody(_In_ HttpServerConn* self)
{
    HttpServerRequest* req = self->req;
    if (self->failed || !req || !req->respStream)
        return;

    uint8 buf[HTTPSRV_READ_CHUNK];

    for (;;) {
        // How much to ask for is the one place the two buffer modes differ. A pull-mode buffer is
        // asked for a full read and calls its producer to satisfy it, short-reading only at the end
        // of the body. A push-mode buffer holds whatever has been written so far and *fails* a read
        // larger than that -- so it has to be asked for exactly what it has.
        size_t want = sizeof(buf);
        if (!sbufIsPull(req->respStream)) {
            want = min(sbufCAvail(req->respStream), sizeof(buf));
            if (want == 0)
                break;
        }

        size_t got = 0;
        if (!sbufCRead(req->respStream, buf, want, &got) || got == 0)
            break;

        bool ok;
        if (req->respStreamLen < 0) {
            string enc = 0;
            httpChunkAppend(&enc, buf, got);
            ok = sendStr(self, &enc);
            strDestroy(&enc);
        } else {
            ok = netsocketSend(self->sock, buf, got, NULL, 0);
        }

        // A refused send is backpressure, not a failure. Stop here and let NET_SendReady start this
        // up again once the socket's backlog has drained.
        if (!ok)
            return;
    }

    if (!sbufIsPFinished(req->respStream))
        return;   // the producer has more to come; its next write wakes us through the notify

    if (req->respStreamLen < 0) {
        string fin = 0;
        httpChunkFinish(&fin);
        sendStr(self, &fin);
        strDestroy(&fin);
    }

    finishResponse(self, true);
}

// The producer wrote into a response body buffer that had run dry. Without this a push-mode
// producer would stall for good: the pump stops when the buffer empties, and nothing else on this
// connection is going to wake it -- the client is waiting for the body, so it sends nothing, and a
// socket that is not blocked never fires NET_SendReady either.
static void respStreamNotify(StreamBuffer* sb, size_t sz, void* ctx)
{
    unused_noeval(sb);
    unused_noeval(sz);

    HttpServerConn* self = (HttpServerConn*)ctx;
    if (!self || !self->writing)
        return;

    // A producer is free to feed the buffer from any thread, so this takes the same fork respond()
    // does: do it here if this is already the connection's worker, otherwise ask one to.
    if (onDispatchThread(self))
        httpsrvconn_pumpRespBody(self);
    else
        handoffToWorker(self);
}

// Register cxhttp as the consumer of a response body buffer, matching whichever mode the
// application registered its producer in.
static bool adoptRespStream(HttpServerConn* self, HttpServerRequest* req)
{
    if (sbufIsPull(req->respStream))
        return sbufCRegisterPull(req->respStream, NULL, NULL);

    return sbufCRegisterPush(req->respStream, respStreamNotify, NULL, self);
}

bool HttpServerConn__respond(_In_ HttpServerConn* self, _In_ HttpServerRequest* req)
{
    if (self->failed)
        return false;

    // Not this connection's worker, so the write is handed over rather than done here. The send
    // itself would have been safe -- netsocketSend takes the flow's filter lock, so even a TLS
    // stream survives being written from anywhere -- but finishing a response is not only a send.
    // It advances the parser, drains the receive ring, re-arms deadlines and may start the next
    // request, and all of that belongs to one worker at a time.
    //
    // A zero-delay flow timer is the handoff: NET_Timer arrives on a worker, ordered behind
    // everything already pending for this connection, so the response cannot overtake an event the
    // application has not seen yet.
    if (!onDispatchThread(self))
        return handoffToWorker(self);

    // A response for a request this connection has already moved past, which is what a late answer
    // from a handler that held onto the object looks like.
    if (self->req != req)
        return false;

    bool close = self->closing || !httpsrvreqKeepAlive(req);

    // Answered before the body had been read, which is what refusing an upload from the head
    // handler looks like. The rest of that body is still on its way and it is not a request:
    // reading it as one is precisely how request smuggling works. So this response is the last
    // thing the connection does.
    if (!self->awaiting)
        close = true;

    self->closing = close;

    string head = 0;
    httpsrvreq_buildHead(req, &head, close);
    bool ok = sendStr(self, &head);
    strDestroy(&head);

    // A response to HEAD carries the same headers its GET would and no body, so the Content-Length
    // already written is the length the body would have had. That is what the RFC asks for.
    bool wantBody = req->method != HTTP_Head && !_httpStatusHasNoBody(req->status);

    if (ok && wantBody && req->respStream) {
        if (!adoptRespStream(self, req)) {
            req->respStream = NULL;
            finishResponse(self, false);
            return false;
        }

        // The head is out and the body is not, so the exchange is still open. `writing` holds the
        // parser off for exactly the same reason `awaiting` did.
        self->awaiting = false;
        self->writing  = true;
        httpsrvconn_pumpRespBody(self);
        return true;
    }

    if (ok && wantBody)
        sendStr(self, &req->respBody);

    finishResponse(self, ok);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------

// Start a request from a head the parser has just finished. Everything the parser holds is copied
// out here, because the parser is reset for the next message well before the application is
// necessarily done with this one.
static bool beginRequest(HttpServerConn* self)
{
    HttpParser* p = self->parser;

    // Only 1.0 and 1.1 exist to us. A client speaking anything else will not understand the answer
    // either way, so it gets the one status that says exactly that.
    if (p->version != HTTPVER_1_0 && p->version != HTTPVER_1_1) {
        httpsrvconn_sendError(self, 505, HTTPERR_BadMessage);
        return false;
    }

    // The request line is untrusted. A target in a form a server may not accept is refused here, so
    // nothing above ever sees one.
    HttpUrl u;
    if (!httpUrlParseTarget(&u, p->target)) {
        httpsrvconn_sendError(self, HTTP_BadRequest, HTTPERR_BadUrl);
        return false;
    }

    HttpServerRequest* req = httpsrvreqCreate();

    req->conn    = objGetWeak(HttpServerConn, self);
    req->method  = p->method;
    req->version = p->version;
    strDup(&req->methodName, p->methodName);
    strDup(&req->target, p->target);
    strDup(&req->path, u.path);
    strDup(&req->query, u.query);
    httpUrlDestroy(&u);

    for (int32 i = 0; i < httpHeadersCount(&p->headers); i++)
        httpHeadersAdd(&req->headers, p->headers.names.a[i], p->headers.values.a[i]);

    self->req = req;

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv) {
        httpsrvconn_sendError(self, HTTP_InternalError, HTTPERR_None);
        return false;
    }

    // An Expect this server does not understand is a 417 rather than something to ignore: the
    // client is waiting for permission it will never get, and RFC 9110 says to say so.
    string expect = 0;
    if (httpHeadersGet(&req->headers, _SL("Expect"), &expect) && !strEmpty(expect)) {
        if (strEqi(expect, _SL("100-continue"))) {
            req->expectContinue = true;
        } else {
            strDestroy(&expect);
            objRelease(&srv);
            httpsrvconn_sendError(self, 417, HTTPERR_BadMessage);
            return false;
        }
    }
    strDestroy(&expect);

    // The one chance to say where the body goes, and the only place a body can be refused before
    // it is sent. A handler that answers from here ends the exchange without the body ever being
    // read.
    httpserver_deliver(srv, HTTPSRVEV_Head, self, req, NULL, 0, HTTPERR_None, NERR_None);

    bool autoContinue = srv->autoContinue;
    objRelease(&srv);

    // After the head handler, so an application that turned auto-continue off has already had its
    // say -- either by sending one itself or by answering the request outright.
    if (autoContinue && req->expectContinue && !req->continueSent && !req->responded) {
        req->continueSent = true;
        httpsrvconn_sendContinue(self);
    }

    // The head handler answered, so the exchange is already over and the connection is closing.
    // Saying so stops the parse loop from reading the abandoned body as the next request.
    return self->req != NULL;
}

// Hand one run of decoded body bytes to whichever disposition this request chose.
static void deliverBody(HttpServerConn* self, const uint8* data, size_t len)
{
    HttpServerRequest* req = self->req;
    if (!req)
        return;

    if (req->sink) {
        sbufPWrite(req->sink, data, len);
        return;
    }

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (srv && srv->handlers && srv->handlers->data) {
        httpserver_deliver(srv, HTTPSRVEV_Data, self, req, data, len, HTTPERR_None, NERR_None);
        objRelease(&srv);
        return;
    }
    objRelease(&srv);

    // Default: accumulate on the request, bounded by the parser's maxBodyBytes.
    _httpAppendBytes(&req->body, data, len);
}

// Hand a finished request to the application.
static void deliverRequest(HttpServerConn* self)
{
    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv) {
        // The server was released out from under its own connection, so nothing can answer.
        httpsrvconnClose(self);
        return;
    }

    if (srv->handlers && srv->handlers->request) {
        httpserver_deliver(srv, HTTPSRVEV_Request, self, self->req, NULL, 0, HTTPERR_None,
                           NERR_None);

        // A handler that returned without answering has not failed -- it may be holding the request
        // to answer later. Nothing more happens on this connection until it does, which is the
        // one-request-at-a-time rule doing its job.
    } else {
        // No handler at all, so nothing could ever answer. Say so rather than holding the
        // connection open until a deadline notices.
        httpsrvreqRespondStatus(self->req, HTTP_InternalError);
    }

    objRelease(&srv);
}

void HttpServerConn__pump(_In_ HttpServerConn* self)
{
    if (self->failed)
        return;

    // Responding from inside the request handler re-enters here through _respond(). The loop
    // already running owns the ring; a second one over the same bytes would parse them twice.
    if (self->pumping)
        return;
    self->pumping = true;

    // Always drain the socket, even while a response is outstanding. A stream recv event is an
    // edge: leaving bytes in the socket's ring means nothing wakes this connection again until the
    // peer sends more, which for a client waiting on its answer is never.
    uint8 buf[HTTPSRV_READ_CHUNK];
    size_t n;
    while ((n = netsocketRecv(self->sock, buf, sizeof(buf), NULL, 0)) > 0) {
        bufringWrite(&self->in, buf, n);

        if (!self->started) {
            self->started = true;
            armTimers(self, true);
        }
    }

    // Draining unconditionally is what makes this cap necessary: a client may pipeline while it
    // waits, and without a bound the ring is somewhere it can make us hold whatever it likes. The
    // response in flight still completes -- the connection simply will not carry another request.
    if ((self->awaiting || self->writing) && self->in.total > self->parser->limits.maxHeadBytes)
        self->closing = true;

    // Not parsed at all while an exchange is still open -- waiting for its answer, or writing one.
    // This is the whole of the one-request-at-a-time rule.
    while (!self->awaiting && !self->writing && !self->failed) {
        HttpParseResult r = httpParserStep(self->parser, &self->in);

        if (r == HTTPP_NeedMore)
            break;

        if (r == HTTPP_Head) {
            // False means the exchange is already finished -- refused with an error status, or
            // answered outright by the head handler. Either way the connection is closing and
            // whatever else is in the ring is not ours to read.
            if (!beginRequest(self))
                break;
            continue;
        }

        if (r == HTTPP_Body) {
            size_t remaining = self->parser->bodyReady;
            while (remaining > 0) {
                size_t got = bufringRead(&self->in, buf, min(remaining, sizeof(buf)));
                deliverBody(self, buf, got);
                remaining -= got;
            }
            continue;
        }

        if (r == HTTPP_Complete) {
            cancelTimers(self);
            self->started = false;

            // The body is complete, so a consumer streaming it is released now rather than when
            // the exchange ends -- it has everything it is ever going to get.
            if (self->req && self->req->sink) {
                sbufPFinish(self->req->sink);
                self->req->sink = NULL;
            }

            // Asked while the parser still remembers: the reset below takes the answer with it.
            if (!httpParserKeepAlive(self->parser))
                self->closing = true;

            httpParserReset(self->parser);

            // Set before the handler runs, so an inline respond() finds the state it expects.
            self->awaiting = true;
            deliverRequest(self);
            continue;
        }

        // HTTPP_Error. Which status depends on what the parser objected to: a head that was too big
        // is a different conversation from one that was malformed, and a client answered 400 for an
        // oversized header field will retry it unchanged forever.
        uint16 status = HTTP_BadRequest;
        if (self->parser->err == HTTPERR_TooLarge)
            status = self->parser->headDone ? 413 : 431;

        httpsrvconn_sendError(self, status, self->parser->err);
        break;
    }

    // Nothing outstanding and nothing half-read: the connection is idle between requests.
    if (!self->failed && !self->awaiting && !self->writing && !self->started &&
        self->in.total == 0) {
        if (self->closing)
            httpsrvconnClose(self);
        else
            armTimers(self, false);
    }

    self->pumping = false;
}

// Autogen begins -----
// clang-format off
void HttpServerConn__pump(_In_ HttpServerConn* self);
bool HttpServerConn__respond(_In_ HttpServerConn* self, _In_ HttpServerRequest* req);
void HttpServerConn__sendError(_In_ HttpServerConn* self, uint16 status, HttpError err);
bool HttpServerConn__sendContinue(_In_ HttpServerConn* self);
void HttpServerConn__pumpRespBody(_In_ HttpServerConn* self);
#include "httpserverconn.auto.inc"
// clang-format on
// Autogen ends -------

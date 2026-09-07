// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "http3srvconn.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// One accepted QUIC connection
//
// The HTTP/3 mirror of HttpServerConn, and much less machinery than its counterpart despite doing
// more. A request is a stream is a flow, so per-request state lives on the flow -- the one place cx
// guarantees a single worker at a time -- and requests on one connection run genuinely in parallel
// rather than being serialized behind each other. HttpServerConn's `awaiting`, `writing` and
// `pumping` have no counterpart here because there is nothing left for them to serialize.
//
// What this file owns beyond that is the connection's three unidirectional streams and the
// mapping between a field section and an HttpServerRequest, the second of which lives in
// http3msg.c so that the client uses the same one.
// ---------------------------------------------------------------------------------------------

#include "http3_private.h"

#include "httpserver.h"

#include <cx/serialize/streambuf.h>

#include <string.h>

// ---------------------------------------------------------------------------------------------
// Per-stream state
// ---------------------------------------------------------------------------------------------

// One request stream, held as the flow's handler context so that only that flow's worker ever
// reaches it. Freed on the flow's terminal event, which netqueue guarantees arrives exactly once.
typedef struct H3Stream {
    uint64 id;
    BufRing in;         // bytes read off the stream and not yet framed
    H3MsgReader msg;

    // The connection this belongs to. Weak, because the connection owns the socket that owns the
    // flow that holds this.
    Weak(Http3ServerConn)* conn;

    // The request, created when the stream opens rather than when its head arrives, so that every
    // handler below has somewhere to put its dispatch claim from the outset.
    HttpServerRequest* req;

    bool headDone;      // the head has been decoded and delivered
    bool eof;           // the peer finished its half; the request is complete
    bool done;          // the request has been handed to the application
    bool failed;        // this stream has been reset or has failed
    bool headSent;      // the response field section has gone out
    bool writing;       // a response body is still being pumped

    // Body bytes taken off the stream that the application's sink would not accept yet. Held
    // rather than dropped, and flushed when the sink drains.
    string pending;
    bool paused;        // the sink is full, so nothing more is read and the peer's window closes

    // Running result of one pass of the response body pump. Owned by whichever worker is inside
    // _pumpRespBody(); the body's StreamBuffer carries this stream as its consumer context, and
    // this is how the send callback reports back to the loop that drives it.
    size_t bodySent;
    bool bodyRefused;

    NetTimerId readTimer;   // bounds how long the request head may take to arrive, or 0
} H3Stream;

// ---------------------------------------------------------------------------------------------
// Connection-level failure
// ---------------------------------------------------------------------------------------------

static Http3Session* sessionOf(_In_ Http3ServerConn* self)
{
    return (Http3Session*)self->session;
}

// Close the whole connection with an HTTP/3 error code. Everything on it goes down through the
// ordinary path, so each stream still reports itself closed.
//
// Shaped as an H3ConnErrorCB because the peer's control stream reports through it, and that path
// is shared with the client.
static void connError(_Inout_ ObjInst* conn, uint64 code)
{
    Http3ServerConn* self = objDynCast(Http3ServerConn, conn);
    if (!self || self->failed)
        return;
    self->failed = true;

    if (self->sock)
        netquicClose(self->sock, code, NULL);
}

// ---------------------------------------------------------------------------------------------
// Talking to the server
// ---------------------------------------------------------------------------------------------

// HttpServerEvent::conn names an HttpServerConn, and an HTTP/3 request has none -- the two
// connection types share no base class, which is the deliberate cost of not renaming every
// httpconn* entry point. Every event from here therefore carries a NULL `conn`, and the request is
// what an application works from.
static void deliver(_In_ Http3ServerConn* self, HttpServerEventType type,
                    _In_opt_ HttpServerRequest* req, _In_reads_bytes_opt_(len) const uint8* data,
                    size_t len, HttpError err, NetErrorCode neterr)
{
    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv)
        return;

    httpserver_deliver(srv, type, (HttpServerConn*)NULL, req, data, len, err, neterr);
    objRelease(&srv);
}

// Add to one of the request's progress counters and deliver an event if the interval says one is
// due. The counters are written only on the stream's own worker, so the increment needs no
// read-modify-write; the store is atomic because the accessors load it from wherever asked.
static void reportProgress(_In_ Http3ServerConn* self, _In_opt_ HttpServerRequest* req,
                           HttpProgressDir dir, size_t n, bool final)
{
    if (!req)
        return;

    bool send               = (dir == HTTPPROG_Send);
    atomic(uint64)* counter = send ? &req->progSent : &req->progRecv;
    uint64* seen            = send ? &req->progSentSeen : &req->progRecvSeen;

    uint64 done = atomicLoad(uint64, counter, Relaxed);
    if (n > 0) {
        done += (uint64)n;
        atomicStore(uint64, counter, done, Relaxed);
    }

    if (!_httpProgressDue(done, seen, req->progressInterval, final))
        return;

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv)
        return;

    atomic(int64)* total = send ? &req->progSendTotal : &req->progRecvTotal;
    httpserver_deliverProgress(srv, (HttpServerConn*)NULL, req, dir, done,
                               atomicLoad(int64, total, Relaxed));
    objRelease(&srv);
}

// ---------------------------------------------------------------------------------------------
// Stream teardown
// ---------------------------------------------------------------------------------------------

// Give up on one stream without touching the connection. `code` is what the peer is told, and
// `err` is what the application is told; H3ERR_NO_ERROR means the stream simply ended.
static void streamFail(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow,
                       uint64 code, HttpError err)
{
    if (st->failed)
        return;
    st->failed = true;

    if (st->readTimer) {
        netflowCancelTimer(flow, st->readTimer);
        st->readTimer = 0;
    }

    // Before the handlers run, so an application waiting on a body it will never receive is
    // released rather than left waiting on a stream nothing is ever going to end.
    _httpSrvReqReleaseStreams(st->req, false);

    if (err != HTTPERR_None)
        deliver(self, HTTPSRVEV_Error, st->headDone ? st->req : NULL, NULL, 0, err, NERR_None);

    if (code != H3ERR_NO_ERROR) {
        netquicReset(flow, code);
        netquicStopSending(flow, code);
    }

    st->writing = false;
}

// ---------------------------------------------------------------------------------------------
// Writing a response
// ---------------------------------------------------------------------------------------------

// Send a status with no body and end the stream. What answers a request that failed before the
// application could look at it, and what a `100 Continue` is not -- that one leaves the stream
// open, and goes out through _sendContinue() instead.
static bool sendStatus(_In_ NetFlow* flow, uint16 status)
{
    HttpHeaders fields;
    HttpHeaders none;
    httpHeadersInit(&none);

    bool ok = _h3RespToFields(&fields, status, &none, 0) && _h3SendFields(flow, &fields, true);

    httpHeadersDestroy(&fields);
    httpHeadersDestroy(&none);
    return ok;
}

// Hand one slice of the response body to the stream, straight out of the buffer's own storage.
// Runs with the buffer's lock held, so it must not touch the buffer at all.
static bool respSendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                       size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    unused_noeval(sb);
    unused_noeval(off);

    H3Stream* st = (H3Stream*)ctx;
    if (!st || sz == 0 || st->bodyRefused)
        return false;

    // Always retain, and let the pump consume exactly what went out. One sbufCSend() can walk
    // several ring segments and the ring consumes either all of them or none, so a stream that
    // takes the first and refuses the second would otherwise have to choose between sending those
    // bytes twice and losing them.
    NetFlow* flow = st->req ? st->req->h3flow : NULL;
    if (!flow || !_h3SendData(flow, buf, sz)) {
        st->bodyRefused = true;
        return false;
    }

    st->bodySent += sz;
    return false;
}

void Http3ServerConn__pumpRespBody(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req)
{
    NetFlow* flow = req->h3flow;
    H3Stream* st  = flow ? (H3Stream*)flow->handlerCtx : NULL;
    if (!st || st->failed || self->failed)
        return;

    StreamBuffer* sb = req->respStream;

    while (sb) {
        // How much to ask for is the one place the two buffer modes differ. A pull-mode buffer is
        // asked for a full read and calls its producer to satisfy it; a push-mode buffer holds
        // whatever has been written so far and fails a read larger than that.
        size_t want = HTTP3_READ_CHUNK;
        if (!sbufIsPull(sb)) {
            want = min(sbufCAvail(sb), (size_t)HTTP3_READ_CHUNK);
            if (want == 0)
                break;
        }

        st->bodySent    = 0;
        st->bodyRefused = false;

        if (!sbufCSend(sb, respSendCB, want, st))
            break;

        if (st->bodySent > 0) {
            sbufCSkip(sb, st->bodySent);
            reportProgress(self, req, HTTPPROG_Send, st->bodySent, false);
        }

        // A refused send is backpressure, not a failure. Nothing has left the stream buffer that
        // the stream did not take, so the retry picks up exactly where this left off.
        if (st->bodyRefused)
            return;

        if (st->bodySent == 0)
            break;
    }

    if (sb) {
        if (sbufCMore(sb) || sbufCAvail(sb) > 0)
            return;   // the producer has more to come; its next write wakes us through the notify

        StreamBuffer* done = req->respStream;
        req->respStream    = NULL;
        sbufCUnregister(done);
        sbufClose(done);
        sbufRelease(&done);
    }

    reportProgress(self, req, HTTPPROG_Send, 0, true);

    // The end of the body is the end of the stream. There is no terminator to write and no
    // connection to decide the fate of: HTTP/3 frames a body with the stream's own FIN.
    netquicFinish(flow);
    st->writing = false;
    _httpSrvReqReleaseStreams(req, true);
}

// The producer wrote into a response body buffer that had run dry. Without this a push-mode
// producer would stall for good: the pump stops when the buffer empties, and nothing else on this
// stream is going to wake it.
static void respStreamNotify(StreamBuffer* sb, size_t sz, void* ctx)
{
    unused_noeval(sb);
    unused_noeval(sz);

    HttpServerRequest* req = (HttpServerRequest*)ctx;
    if (!req || !req->h3flow)
        return;

    // A producer is free to feed the buffer from any thread, so this takes the same fork respond()
    // does: do it here if this is already the stream's worker, otherwise ask one to come.
    if (!_httpDispatchOwned(&req->dispatch)) {
        _httpDispatchHandoff(&req->dispatch, req->h3flow);
        return;
    }

    Http3ServerConn* conn = objAcquireFromWeakDyn(Http3ServerConn, req->h3conn);
    if (!conn)
        return;
    h3srvconn_pumpRespBody(conn, req);
    objRelease(&conn);
}

bool Http3ServerConn__respond(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req)
{
    if (self->failed)
        return false;

    // Not this stream's worker, so the write is handed over rather than done here. The send itself
    // would have been safe, but finishing a response is not only a send: it advances the stream,
    // cancels its deadline and releases its body buffers, and all of that belongs to one worker.
    if (!_httpDispatchOwned(&req->dispatch))
        return _httpDispatchHandoff(&req->dispatch, req->h3flow);

    NetFlow* flow = req->h3flow;
    H3Stream* st  = flow ? (H3Stream*)flow->handlerCtx : NULL;
    if (!st || st->failed || st->headSent)
        return false;

    if (st->readTimer) {
        netflowCancelTimer(flow, st->readTimer);
        st->readTimer = 0;
    }

    // A response to HEAD carries the same fields its GET would, Content-Length included, and no
    // body. Framing the length from the body that was actually set and then dropping the bytes is
    // what keeps that honest.
    bool wantBody = req->method != HTTP_Head && !_httpStatusHasNoBody(req->status);
    int64 bodyLen = 0;
    if (wantBody)
        bodyLen = req->respStream ? req->respStreamLen : (int64)strLen(req->respBody);

    HttpHeaders fields;
    bool ok = _h3RespToFields(&fields, req->status, &req->respHeaders, bodyLen);

    // Nothing follows a response with no body, so the field section carries the end of the stream
    // and saves a packet.
    bool fin = !wantBody || (bodyLen == 0 && !req->respStream);
    ok       = ok && _h3SendFields(flow, &fields, fin);
    httpHeadersDestroy(&fields);

    if (!ok) {
        streamFail(self, st, flow, H3ERR_INTERNAL_ERROR, HTTPERR_Network);
        return false;
    }

    st->headSent = true;
    atomicStore(int64, &req->progSendTotal, bodyLen, Relaxed);

    if (fin) {
        _httpSrvReqReleaseStreams(req, true);
        return true;
    }

    // A body the application set as a string becomes a stream like every other body, but only once
    // the field section is out: the length written there came from respBody, and a HEAD wants that
    // length without the bytes.
    if (!req->respStream && !strEmpty(req->respBody)) {
        StreamBuffer* sb = sbufCreate(HTTP_BODY_CHUNK);
        if (!sbufStrPRegisterPull(sb, req->respBody)) {
            sbufRelease(&sb);
            streamFail(self, st, flow, H3ERR_INTERNAL_ERROR, HTTPERR_Network);
            return false;
        }
        req->respStream    = sb;   // takes over the reference sbufCreate() started with
        req->respStreamLen = (int64)strLen(req->respBody);
    }

    // In pull mode cxhttp drives the buffer and has nothing to register; in push mode it is the
    // one being called back.
    if (req->respStream && !sbufIsPull(req->respStream) &&
        !sbufCRegisterPush(req->respStream, respStreamNotify, NULL, req)) {
        streamFail(self, st, flow, H3ERR_INTERNAL_ERROR, HTTPERR_Network);
        return false;
    }

    st->writing = true;
    h3srvconn_pumpRespBody(self, req);
    return true;
}

bool Http3ServerConn__sendContinue(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req)
{
    if (self->failed || !req->h3flow)
        return false;

    // An interim response is another field section ahead of the real one, and nothing else. There
    // is no separate wire form for it the way HTTP/1.1 has a second status line.
    HttpHeaders fields;
    HttpHeaders none;
    httpHeadersInit(&none);

    bool ok = _h3RespToFields(&fields, HTTP_Continue, &none, 0) &&
              _h3SendFields(req->h3flow, &fields, false);

    httpHeadersDestroy(&fields);
    httpHeadersDestroy(&none);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Reading a request
// ---------------------------------------------------------------------------------------------

// The reading side is mutually recursive: a sink draining resumes the pump, and the pump is what
// filled the sink.
static void streamPump(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow);
static void sinkResume(StreamBuffer* sb, void* ctx);

// The head is complete: fill in the request from the field section and give the application its
// one chance to say where the body goes.
static void beginRequest(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow)
{
    HttpServerRequest* req = st->req;

    uint64 err = _h3ReqFromFields(req, &st->msg.headers);
    if (err != 0) {
        streamFail(self, st, flow, err, HTTPERR_BadMessage);
        return;
    }

    st->headDone = true;

    if (st->readTimer) {
        netflowCancelTimer(flow, st->readTimer);
        st->readTimer = 0;
    }

    // Content-Length is still carried and still checked, even though the stream's own end is what
    // frames the body. A message whose declared length disagrees with what it sent is one two
    // implementations will read differently.
    string clen = 0;
    if (httpHeadersGet(&req->headers, _SL("Content-Length"), &clen)) {
        uint64 n = 0;
        if (!strToUInt64(&n, clen, 10, HTTP_STRICTNUM)) {
            strDestroy(&clen);
            streamFail(self, st, flow, H3ERR_MESSAGE_ERROR, HTTPERR_BadMessage);
            return;
        }
        atomicStore(int64, &req->progRecvTotal, (int64)n, Relaxed);
    } else {
        // No length announced, and HTTP/3 has no chunked encoding to announce one with: the body
        // runs to the end of the stream and its size is not known up front.
        atomicStore(int64, &req->progRecvTotal, -1, Relaxed);
    }
    strDestroy(&clen);

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv) {
        streamFail(self, st, flow, H3ERR_INTERNAL_ERROR, HTTPERR_None);
        return;
    }

    string expect = 0;
    if (httpHeadersGet(&req->headers, _SL("Expect"), &expect) && !strEmpty(expect)) {
        if (strEqi(expect, _SL("100-continue"))) {
            req->expectContinue = true;
        } else {
            strDestroy(&expect);
            objRelease(&srv);
            sendStatus(flow, 417);
            streamFail(self, st, flow, H3ERR_NO_ERROR, HTTPERR_BadMessage);
            return;
        }
    }
    strDestroy(&expect);

    deliver(self, HTTPSRVEV_Head, req, NULL, 0, HTTPERR_None, NERR_None);

    // A sink the head handler installed is where the body goes, and a full one is what stops this
    // stream being read at all. Registering here rather than in setSink() keeps the request object
    // free of anything protocol-specific.
    if (req->sink)
        sbufPSetResume(req->sink, sinkResume, req);

    bool autoContinue = srv->autoContinue;
    objRelease(&srv);

    if (autoContinue && req->expectContinue && !req->continueSent && !req->responded) {
        req->continueSent = true;
        h3srvconn_sendContinue(self, req);
    }
}

// Hand one run of decoded body bytes to whichever disposition this request chose.
static void deliverBody(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st,
                        _In_reads_bytes_(len) const uint8* data, size_t len)
{
    HttpServerRequest* req = st->req;

    reportProgress(self, req, HTTPPROG_Recv, len, false);

    if (req->sink) {
        // A sink that is full holds these bytes rather than dropping them, and stops the stream
        // being read at all until it drains -- which is what closes the peer's flow control window.
        // No threshold of our own is needed: not reading is the backpressure.
        if (st->paused || !sbufPWrite(req->sink, data, len)) {
            _httpAppendBytes(&st->pending, data, len);
            st->paused = true;
        }
        return;
    }

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    bool toHandler  = srv && srv->handlers && srv->handlers->data;
    if (toHandler)
        httpserver_deliver(srv, HTTPSRVEV_Data, (HttpServerConn*)NULL, req, data, len,
                           HTTPERR_None, NERR_None);
    objRelease(&srv);

    if (!toHandler)
        _httpAppendBytes(&req->body, data, len);
}

// The whole request has arrived: end whatever was streaming it and hand it to the application.
static void finishRequest(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow)
{
    HttpServerRequest* req = st->req;
    st->done               = true;

    if (st->readTimer) {
        netflowCancelTimer(flow, st->readTimer);
        st->readTimer = 0;
    }

    // The declared length and what actually arrived have to agree. They can only disagree by the
    // peer ending the stream early, since anything longer would have been refused as it arrived.
    int64 total = atomicLoad(int64, &req->progRecvTotal, Relaxed);
    if (total >= 0 && (uint64)total != atomicLoad(uint64, &req->progRecv, Relaxed)) {
        streamFail(self, st, flow, H3ERR_MESSAGE_ERROR, HTTPERR_BadMessage);
        return;
    }

    // The body is complete, so a consumer streaming it is released now rather than when the
    // exchange ends: it has everything it is ever going to get.
    if (req->sink) {
        StreamBuffer* sink = req->sink;
        req->sink          = NULL;
        sbufClose(sink);
        sbufRelease(&sink);
    }

    reportProgress(self, req, HTTPPROG_Recv, 0, true);

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (!srv) {
        streamFail(self, st, flow, H3ERR_INTERNAL_ERROR, HTTPERR_None);
        return;
    }

    if (srv->handlers && srv->handlers->request) {
        httpserver_deliver(srv, HTTPSRVEV_Request, (HttpServerConn*)NULL, req, NULL, 0,
                           HTTPERR_None, NERR_None);
    } else {
        // No handler at all, so nothing could ever answer. Say so rather than holding the stream
        // open until a deadline notices.
        httpsrvreqRespondStatus(req, HTTP_InternalError);
    }

    objRelease(&srv);
}

static void streamPump(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow)
{
    if (st->failed || self->failed)
        return;

    uint8 buf[HTTP3_READ_CHUNK];

    for (;;) {
        // Frame and deliver what is already in hand before asking the stream for more. Reading is
        // what reopens the receive window, so a stream that has stopped delivering stops reading
        // too -- that, and nothing else, is the receive backpressure here. There is no shared
        // socket ring that has to be drained anyway, so no threshold of cxhttp's own is needed.
        while (!st->paused && !st->failed) {
            H3MsgResult r = _h3MsgStep(&st->msg, &st->in);

            if (r == H3MSG_NeedMore)
                break;

            if (r == H3MSG_Error) {
                streamFail(self, st, flow, st->msg.err, _h3ErrorToHttp(st->msg.err));
                return;
            }

            if (r == H3MSG_Head) {
                // A request has no interim responses, so a second field section before the body is
                // not something this endpoint can make sense of.
                if (st->headDone) {
                    streamFail(self, st, flow, H3ERR_MESSAGE_ERROR, HTTPERR_BadMessage);
                    return;
                }
                beginRequest(self, st, flow);

                // Answered from the head handler, which is what refusing a body before it is sent
                // looks like. The rest of that body is not ours to read.
                if (st->failed || st->req->responded)
                    return;
                continue;
            }

            if (r == H3MSG_Body) {
                size_t remaining = st->msg.bodyReady;
                while (remaining > 0) {
                    size_t got = bufringRead(&st->in, buf, min(remaining, sizeof(buf)));
                    deliverBody(self, st, buf, got);
                    remaining -= got;
                }
                continue;
            }

            // H3MSG_Trailers: read, checked, and discarded, exactly as in HTTP/1.1. Nothing in
            // cxhttp consumes a trailer, and letting them through as ordinary headers would let a
            // peer rewrite a message after the application had already seen its head.
        }

        if (st->paused || st->failed || st->done)
            break;

        bool fin = false;
        size_t n = netquicRecv(flow, buf, sizeof(buf), &fin);
        st->eof  = st->eof || fin;
        if (n == 0)
            break;

        bufringWrite(&st->in, buf, n);
    }

    if (st->eof && st->headDone && !st->done && !st->failed && st->in.total == 0 && !st->paused)
        finishRequest(self, st, flow);
}

// The application's sink drained back below its low mark, so reading may resume.
static void sinkResume(StreamBuffer* sb, void* ctx)
{
    unused_noeval(sb);

    HttpServerRequest* req = (HttpServerRequest*)ctx;
    if (!req || !req->h3flow)
        return;

    // The consumer may drain from any thread, so this takes the same fork respond() does.
    if (!_httpDispatchOwned(&req->dispatch)) {
        _httpDispatchHandoff(&req->dispatch, req->h3flow);
        return;
    }

    Http3ServerConn* conn = objAcquireFromWeakDyn(Http3ServerConn, req->h3conn);
    if (!conn)
        return;

    H3Stream* st = (H3Stream*)req->h3flow->handlerCtx;
    if (st) {
        st->paused = false;
        streamPump(conn, st, req->h3flow);
    }
    objRelease(&conn);
}

// Push whatever the sink refused earlier, then let reading start again.
static void flushPending(_Inout_ Http3ServerConn* self, _Inout_ H3Stream* st, _In_ NetFlow* flow)
{
    HttpServerRequest* req = st->req;
    if (!st->paused)
        return;

    if (!strEmpty(st->pending) && req->sink) {
        uint32 len  = strLen(st->pending);
        uint8* data = xaAlloc(len);
        strCopyRaw(st->pending, 0, data, len);
        bool ok = sbufPWrite(req->sink, data, len);
        xaFree(data);
        if (!ok)
            return;
        strClear(&st->pending);
    }

    st->paused = false;
    streamPump(self, st, flow);
}

// ---------------------------------------------------------------------------------------------
// Request stream handlers
// ---------------------------------------------------------------------------------------------

static void streamOnRecv(NetEvent* ev)
{
    H3Stream* st          = (H3Stream*)ev->ctx;
    Http3ServerConn* conn = objAcquireFromWeak(Http3ServerConn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    streamPump(conn, st, ev->flow);
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

static void streamOnSendReady(NetEvent* ev)
{
    H3Stream* st          = (H3Stream*)ev->ctx;
    Http3ServerConn* conn = objAcquireFromWeak(Http3ServerConn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    if (st->writing)
        h3srvconn_pumpRespBody(conn, st->req);
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

// The peer reset its sending half, or asked this endpoint to stop sending. Either way the code it
// gave is on the flow, because a plain flow has nowhere to put one.
static void streamOnError(NetEvent* ev)
{
    H3Stream* st          = (H3Stream*)ev->ctx;
    Http3ServerConn* conn = objAcquireFromWeak(Http3ServerConn, st->conn);
    if (!conn)
        return;

    QuicStream* qs = objDynCast(QuicStream, ev->flow);
    uint64 code    = qs ? qs->error : 0;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    streamFail(conn, st, ev->flow, H3ERR_NO_ERROR, _h3ErrorToHttp(code));
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

static void streamOnTimer(NetEvent* ev)
{
    H3Stream* st          = (H3Stream*)ev->ctx;
    Http3ServerConn* conn = objAcquireFromWeak(Http3ServerConn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);

    if (ev->timer.id == st->readTimer) {
        st->readTimer = 0;

        // A client that opened a stream and then sent its head a byte at a time, or not at all.
        // Answering 408 costs one small frame and tells a client whose network merely stalled what
        // happened.
        sendStatus(ev->flow, 408);
        streamFail(conn, st, ev->flow, H3ERR_NO_ERROR, HTTPERR_Timeout);
    } else if (_httpDispatchClaim(&st->req->dispatch)) {
        // A handoff from another thread: a response composed elsewhere, a body producer that
        // wrote more, or a sink that drained.
        if (st->paused)
            flushPending(conn, st, ev->flow);
        else if (st->writing)
            h3srvconn_pumpRespBody(conn, st->req);
        else if (st->req->responded && !st->headSent)
            h3srvconn_respond(conn, st->req);
    }

    _httpDispatchLeave(&st->req->dispatch, prev);
    objRelease(&conn);
}

static void streamOnClosed(NetEvent* ev)
{
    H3Stream* st = (H3Stream*)ev->ctx;

    Http3ServerConn* conn = objAcquireFromWeak(Http3ServerConn, st->conn);
    if (conn) {
        Thread* prev = _httpDispatchEnter(&st->req->dispatch);

        // A stream that ends with a request still unanswered is a client that gave up.
        if (!st->failed && st->headDone && !st->headSent)
            streamFail(conn, st, ev->flow, H3ERR_NO_ERROR, HTTPERR_Closed);
        else
            _httpSrvReqReleaseStreams(st->req, st->headSent);

        atomicFetchSub(uint32, &conn->nstreams, 1, Relaxed);
        _httpDispatchLeave(&st->req->dispatch, prev);
        objRelease(&conn);
    }

    // The flow is going away, so nothing may reach this state again. Clearing the registration is
    // what makes a later read of flow->handlerCtx answer NULL rather than freed memory.
    netflowSetHandlers(ev->flow, NULL, NULL);

    objRelease(&st->req);
    objDestroyWeak(&st->conn);
    bufringDestroy(&st->in);
    _h3MsgDestroy(&st->msg);
    strDestroy(&st->pending);
    xaFree(st);
}

static const NetHandlers kStreamHandlers = {
    .recv       = streamOnRecv,
    .sendReady  = streamOnSendReady,
    .error      = streamOnError,
    .timer      = streamOnTimer,
    .flowClosed = streamOnClosed,
};

// ---------------------------------------------------------------------------------------------
// Connection handlers
// ---------------------------------------------------------------------------------------------

// The stream id says everything about a new flow: bit 0 is which endpoint opened it and bit 1 is
// whether it is unidirectional.
#define H3_STREAM_IS_UNI(id)          (((id) & 0x02) != 0)
#define H3_STREAM_IS_CLIENT_INIT(id)  (((id) & 0x01) == 0)

static void onFlowOpen(NetEvent* ev)
{
    Http3ServerConn* self = (Http3ServerConn*)ev->ctx;
    NetFlow* flow         = ev->flow;

    // The socket's own control flow is not a stream.
    if (!flow || (ev->socket && flow == ev->socket->flow))
        return;

    // A flow this endpoint opened already has its handlers.
    if (!H3_STREAM_IS_CLIENT_INIT(flow->key))
        return;

    if (H3_STREAM_IS_UNI(flow->key)) {
        _h3UniAttach(flow, ObjInst(self), sessionOf(self), connError);
        return;
    }

    // A request stream. The request object is created now rather than when its head arrives, so
    // that every handler on this stream has somewhere to make its dispatch claim from the outset.
    HttpServerRequest* req = httpsrvreqCreate();
    if (!req) {
        netquicReset(flow, H3ERR_INTERNAL_ERROR);
        return;
    }

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);

    H3Stream* st = xaAlloc(sizeof(H3Stream), XA_Zero);
    st->id       = flow->key;
    st->conn     = objGetWeak(Http3ServerConn, self);
    st->req      = req;
    bufringInit(&st->in, HTTP3_READ_CHUNK);
    _h3MsgInit(&st->msg, true, srv ? &srv->limits : NULL);

    req->h3conn  = objGetWeak(ObjInst, self);
    req->h3flow  = objAcquire(flow);
    req->version = HTTPVER_3;

    netflowSetHandlers(flow, &kStreamHandlers, st);
    atomicFetchAdd(uint32, &self->nstreams, 1, Relaxed);

    // The peer has already been told this endpoint is going away and named a stream at or above
    // this one, so the promise made in that GOAWAY has to be kept.
    Http3Session* s = sessionOf(self);
    if (s->goawaySent && flow->key >= s->goawaySentId) {
        netquicReset(flow, H3ERR_REQUEST_REJECTED);
        netquicStopSending(flow, H3ERR_REQUEST_REJECTED);
        st->failed = true;
    } else if (srv && srv->readHeadTimeout > 0) {
        st->readTimer = netflowAddTimer(flow, srv->readHeadTimeout, NTF_None);
    }

    objRelease(&srv);
}

static void onNetClosed(NetEvent* ev)
{
    Http3ServerConn* self = (Http3ServerConn*)ev->ctx;

    // Only the socket's own control flow ending means the connection ended; every other flow is a
    // stream, and those are handled by their own registrations.
    if (ev->socket && ev->flow != ev->socket->flow)
        return;

    self->failed = true;

    deliver(self, HTTPSRVEV_Closed, NULL, NULL, 0, HTTPERR_None, NERR_None);

    HttpServer* srv = objAcquireFromWeak(HttpServer, self->server);
    if (srv) {
        httpserver_forget(srv, ObjInst(self));
        objRelease(&srv);
    }
}

static void onNetError(NetEvent* ev)
{
    Http3ServerConn* self = (Http3ServerConn*)ev->ctx;

    if (ev->socket && ev->flow != ev->socket->flow)
        return;

    deliver(self, HTTPSRVEV_Error, NULL, NULL, 0, HTTPERR_Network, ev->error.err);
}

static const NetHandlers kConnHandlers = {
    .flowOpen   = onFlowOpen,
    .flowClosed = onNetClosed,
    .error      = onNetError,
};

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_objfactory_check Http3ServerConn* Http3ServerConn_create(_In_ HttpServer* server,
                                                          _In_ NetSocket* sock)
{
    _httpInit();

    if (!server || !sock || sock->type != NST_Quic)
        return NULL;

    Http3ServerConn* self;
    self = objInstCreate(Http3ServerConn);

    self->sock   = objAcquire(sock);
    self->server = objGetWeak(HttpServer, server);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    _h3SessionInit(sessionOf(self), true, &server->limits);

    // Held weakly, like HttpServerConn's: the socket must not own the connection, or the two would
    // keep each other alive.
    netsocketSetHandlersObj(sock, &kConnHandlers, self);

    if (!_h3OpenUniStreams(sock, sessionOf(self), &self->control, &self->qpackEnc,
                           &self->qpackDec)) {
        objRelease(&self);
        return NULL;
    }
    self->started = true;

    return self;
}

_objinit_guaranteed bool Http3ServerConn_init(_In_ Http3ServerConn* self)
{
    self->session = xaAlloc(sizeof(Http3Session), XA_Zero);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Http3ServerConn_destroy(_In_ Http3ServerConn* self)
{
    if (self->sock)
        netsocketSetHandlers(self->sock, NULL, NULL);

    if (self->session) {
        _h3SessionDestroy(sessionOf(self));
        xaFree(self->session);
        self->session = NULL;
    }
    // Autogen begins -----
    objRelease(&self->sock);
    objDestroyWeak(&self->server);
    objRelease(&self->control);
    objRelease(&self->qpackEnc);
    objRelease(&self->qpackDec);
    // Autogen ends -------
}

void Http3ServerConn_close(_In_ Http3ServerConn* self)
{
    if (self->sock)
        netquicClose(self->sock, H3ERR_NO_ERROR, NULL);
}

void Http3ServerConn_goaway(_In_ Http3ServerConn* self, uint64 id)
{
    if (self->failed || !self->control)
        return;

    string out = 0;
    if (_h3SessionGoawayFrame(sessionOf(self), &out, id))
        netflowSend(self->control, (uint8*)strPC(&out), strLen(out), 0);
    strDestroy(&out);
}

// ---------------------------------------------------------------------------------------------
// The seam the version-agnostic sources reach HTTP/3 through
//
// Everything below is declared in http_private.h and defined again by http3stub.c, which is what
// keeps httpserver.c and httpserverreq.c free of a feature test.
// ---------------------------------------------------------------------------------------------

static void onAcceptedQuic(NetEvent* ev)
{
    HttpServer* self   = (HttpServer*)ev->ctx;
    NetSocket* newsock = ev->accept.newSocket;
    if (!newsock)
        return;

    // Without NQ_AutoAccept the queue leaves registration to the application, and a socket that is
    // on no queue never receives anything.
    if (!newsock->queue && !netqueueAddSocket(self->queue, newsock))
        return;

    // The listener offers http/1.1 as well, so that a TCP listener sharing its configuration has
    // something to negotiate. Nothing over QUIC may pick it: HTTP/1.1 has no framing there, and a
    // client that asked for it is asking for something that does not exist.
    if (!_http3Negotiated(newsock)) {
        netquicClose(newsock, H3ERR_GENERAL_PROTOCOL_ERROR, NULL);
        return;
    }

    Http3ServerConn* conn = h3srvconnCreate(self, newsock);
    if (!conn) {
        netsocketClose(newsock);
        return;
    }

    bool taken = false;
    withMutex (&self->lock) {
        if (!self->shuttingDown)
            taken = htInsert(&self->connections, ptr, conn, object, conn, HT_Ignore) != 0;
    }

    if (!taken) {
        // Shutting down. The connection is closed rather than served, and the reference from
        // create() is the only one there ever was.
        h3srvconnClose(conn);
        objRelease(&conn);
        return;
    }

    // The table holds its own reference, so the factory's is handed back here.
    objRelease(&conn);
}

static const NetHandlers kQuicListenHandlers = {
    .accepted = onAcceptedQuic,
};

_Use_decl_annotations_
bool _http3ServerListen(HttpServer* srv, const NetAddr* addr, TlsConfig* cfg)
{
    // A server that does not offer h3 fails every handshake with a client that insists on it, and
    // a client is entitled to insist. A configuration an earlier listener already sealed cannot be
    // given one, which is what a server that shared one TlsConfig with its TCP listener finds here
    // rather than at its first connection.
    if (!_httpAlpnAdd(cfg, _SL(H3_ALPN))) {
        logStr(Error,
               _SLL("http: httpserverListenQuic() needs a TlsConfig offering h3, and this one is "
                    "already sealed. Give each listener its own TlsConfig -- they can share a "
                    "TlsCreds -- because the two need different ALPN lists."));
        return false;
    }

    // Only the idle timeout is carried across. The stream and connection windows are deliberately
    // left at cxquic's defaults rather than derived from HttpLimits: `maxBodyBytes` of 0 means
    // unlimited at the message layer, which is not a window a transport can advertise.
    QuicConfig qcfg  = { 0 };
    qcfg.tls         = cfg;
    qcfg.idleTimeout = srv->idleTimeout;

    NetSocket* sock = netquicListen(srv->queue, addr, &qcfg, NULL, NULL);
    if (!sock)
        return false;

    // Held weakly, as everywhere else: the socket must not own the server.
    netsocketSetHandlersObj(sock, &kQuicListenHandlers, srv);

    saPush(&srv->listeners, NetSocket, sock);

    // A default Alt-Svc naming the port just bound, so an HTTP/1.1 client learns that HTTP/3 is
    // here without the application having to say so. Only when nothing has been set: an
    // application behind a load balancer knows a port this code cannot.
    if (strEmpty(srv->altSvc)) {
        string port = 0;
        strFromUInt64(&port, sock->local.port, 10);
        strNConcat(&srv->altSvc, _SL("h3=\":"), port, _SL("\"; ma=86400"));
        strDestroy(&port);
    }

    objRelease(&sock);   // the array holds its own reference
    return true;
}

_Use_decl_annotations_
bool _http3ConnClose(ObjInst* conn)
{
    Http3ServerConn* h3 = objDynCast(Http3ServerConn, conn);
    if (!h3)
        return false;

    h3srvconnClose(h3);
    return true;
}

_Use_decl_annotations_
bool _http3SrvReqRespond(HttpServerRequest* req)
{
    Http3ServerConn* h3 = objAcquireFromWeakDyn(Http3ServerConn, req->h3conn);
    if (!h3)
        return false;   // the connection died while the application was thinking

    bool ok = h3srvconn_respond(h3, req);
    objRelease(&h3);
    return ok;
}

_Use_decl_annotations_
bool _http3SrvReqContinue(HttpServerRequest* req)
{
    Http3ServerConn* h3 = objAcquireFromWeakDyn(Http3ServerConn, req->h3conn);
    if (!h3)
        return false;

    bool ok = h3srvconn_sendContinue(h3, req);
    objRelease(&h3);
    return ok;
}

// Autogen begins -----
// clang-format off
bool Http3ServerConn__respond(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req);
bool Http3ServerConn__sendContinue(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req);
void Http3ServerConn__pumpRespBody(_In_ Http3ServerConn* self, _In_ HttpServerRequest* req);
#include "http3srvconn.auto.inc"
// clang-format on
// Autogen ends -------

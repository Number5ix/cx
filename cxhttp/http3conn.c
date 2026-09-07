// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "http3conn.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// The client's HTTP/3 connection
//
// The mirror of Http3ServerConn, and the same shape for the same reasons: a request is a stream is
// a flow, per-request state lives on the flow, and requests run in parallel with nothing to
// serialize them.
//
// What is different from HttpConn is that this one is shared. HttpConn carries one request at a
// time and leaves the pool while it does; this stays in the pool and hands out references, which
// is what makes ten concurrent requests to one origin cost one connection instead of ten.
// ---------------------------------------------------------------------------------------------

#include "http3_private.h"

#include <cx/serialize/streambuf.h>

#include <string.h>

// Per-request-stream state, held as the flow's handler context so that only that flow's worker
// ever reaches it. Freed on the flow's terminal event, which netqueue delivers exactly once.
typedef struct H3CStream {
    uint64 id;
    BufRing in;
    H3MsgReader msg;

    Weak(Http3Conn)* conn;    // weak, for the same reason the server's is
    HttpRequest* req;         // strong

    const HttpHandlers* handlers;
    void* hctx;

    uint16 status;
    uint32 interim;      // 1xx field sections seen ahead of the real one

    bool headDone;
    bool complete;       // the response ended and its terminal event has been delivered
    bool failed;
    bool writing;        // a request body is still being pumped out
    bool finished;       // this endpoint has ended its sending half

    // Running result of one pass of the request body pump. Owned by whichever worker is inside it;
    // the body's StreamBuffer carries this stream as its consumer context, and this is how the
    // send callback reports back to the loop that drives it.
    size_t bodySent;
    bool bodyRefused;

    NetTimerId deadline;   // bounds the whole response, or 0
} H3CStream;

static Http3Session* sessionOf(_In_ Http3Conn* self)
{
    return (Http3Session*)self->session;
}

static H3CStream* streamOf(_In_opt_ NetFlow* flow)
{
    return flow ? (H3CStream*)flow->handlerCtx : NULL;
}

// ---------------------------------------------------------------------------------------------
// Delivering to the application
// ---------------------------------------------------------------------------------------------

// HttpEvent::conn names an HttpConn, and an HTTP/3 request has none -- the two connection types
// share no base class. Every event from here therefore carries a NULL `conn`, and the request is
// what an application works from.
static void deliver(_Inout_ H3CStream* st, HttpEventType type, HttpError err)
{
    HttpRequest* req = st->req;

    HttpEvent ev = { 0 };
    ev.event     = type;
    ev.request   = req;
    ev.ctx       = st->hctx;
    ev.status    = st->status;
    ev.version   = HTTPVER_3;
    ev.headers   = &req->respHeaders;
    ev.err       = err;
    ev.neterr    = req->neterr;

    HttpEventCB cb = NULL;
    if (st->handlers) {
        switch (type) {
        case HTTPEV_Status:
            cb = st->handlers->status;
            break;
        case HTTPEV_Headers:
            cb = st->handlers->headers;
            break;
        case HTTPEV_Data:
            cb = st->handlers->data;
            break;
        case HTTPEV_Progress:
            // Never routed through here -- reportProgress() fills in counts this has no way to
            // know, and delivers it itself.
            break;
        case HTTPEV_Complete:
        case HTTPEV_Error:
            cb = (type == HTTPEV_Complete) ? st->handlers->complete : st->handlers->error;
            break;
        }
    }

    if (type == HTTPEV_Complete || type == HTTPEV_Error) {
        // A response sink is closed out here rather than at each of the several places a message
        // can end, so a caller waiting on a StreamBuffer is released on every one of them --
        // including a timeout and a transport error. A body being discarded for a redirect is the
        // exception: the sink belongs to the exchange, not to this hop.
        if (req->respSink && !req->discardBody) {
            if (type != HTTPEV_Complete)
                sbufError(req->respSink);
            _httpReqReleaseSink(req);
        }

        _httpReqReleaseBodyStream(req);
        req->err     = err;
        req->status  = st->status;
        req->version = HTTPVER_3;
        st->complete = true;
    }

    if (cb)
        cb(&ev);
}

static void reportProgress(_Inout_ H3CStream* st, HttpProgressDir dir, size_t n, bool final)
{
    HttpRequest* req = st->req;

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

    if (!st->handlers || !st->handlers->progress)
        return;

    atomic(int64)* total = send ? &req->progSendTotal : &req->progRecvTotal;

    HttpEvent ev = { 0 };
    ev.event     = HTTPEV_Progress;
    ev.request   = req;
    ev.ctx       = st->hctx;
    ev.status    = st->status;
    ev.version   = HTTPVER_3;
    ev.headers   = &req->respHeaders;
    ev.dir       = dir;
    ev.done      = done;
    ev.total     = atomicLoad(int64, total, Relaxed);

    st->handlers->progress(&ev);
}

// Give up on one stream without touching the connection.
static void streamFail(_Inout_ H3CStream* st, _In_ NetFlow* flow, uint64 code, HttpError err)
{
    if (st->failed || st->complete)
        return;
    st->failed = true;

    if (st->deadline) {
        netflowCancelTimer(flow, st->deadline);
        st->deadline = 0;
    }

    if (code != H3ERR_NO_ERROR) {
        netquicReset(flow, code);
        netquicStopSending(flow, code);
    }

    st->writing = false;
    deliver(st, HTTPEV_Error, err);
}

// ---------------------------------------------------------------------------------------------
// Writing the request
// ---------------------------------------------------------------------------------------------

// Hand one slice of the request body to the stream, straight out of the buffer's own storage. Runs
// with the buffer's lock held, so it must not touch the buffer at all.
static bool bodySendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                       size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    unused_noeval(sb);
    unused_noeval(off);

    H3CStream* st = (H3CStream*)ctx;
    if (!st || sz == 0 || st->bodyRefused)
        return false;

    NetFlow* flow = st->req ? st->req->h3flow : NULL;
    if (!flow || !_h3SendData(flow, buf, sz)) {
        st->bodyRefused = true;
        return false;
    }

    st->bodySent += sz;
    return false;
}

void Http3Conn__pumpBody(_In_ Http3Conn* self, _In_ HttpRequest* req)
{
    unused_noeval(self);

    NetFlow* flow = req->h3flow;
    H3CStream* st = streamOf(flow);
    if (!st || st->failed || st->complete)
        return;

    StreamBuffer* sb = req->reqBodyStream;

    while (sb) {
        size_t want = HTTP3_READ_CHUNK;
        if (!sbufIsPull(sb)) {
            want = min(sbufCAvail(sb), (size_t)HTTP3_READ_CHUNK);
            if (want == 0)
                break;
        }

        st->bodySent    = 0;
        st->bodyRefused = false;

        if (!sbufCSend(sb, bodySendCB, want, st))
            break;

        if (st->bodySent > 0) {
            sbufCSkip(sb, st->bodySent);
            reportProgress(st, HTTPPROG_Send, st->bodySent, false);
        }

        if (st->bodyRefused)
            return;   // backpressure; NET_SendReady starts this up again

        if (st->bodySent == 0)
            break;
    }

    if (sb) {
        if (sbufCMore(sb) || sbufCAvail(sb) > 0)
            return;   // the producer has more to come; its next write wakes us through the notify
        _httpReqReleaseBodyStream(req);
    }

    reportProgress(st, HTTPPROG_Send, 0, true);

    // The end of the body is the end of this endpoint's half of the stream. There is no chunked
    // terminator to write: HTTP/3 frames a body with the stream's own FIN.
    if (!st->finished) {
        netquicFinish(flow);
        st->finished = true;
    }
    st->writing = false;
}

// The producer wrote into a request body buffer that had run dry.
_Use_decl_annotations_
void _http3ReqBodyNotify(HttpRequest* req)
{
    if (!req || !req->h3flow)
        return;

    // A producer is free to feed the buffer from any thread, so this takes the same fork the
    // response path does: do it here if this is already the stream's worker, otherwise ask one to.
    if (!_httpDispatchOwned(&req->dispatch)) {
        _httpDispatchHandoff(&req->dispatch, req->h3flow);
        return;
    }

    Http3Conn* conn = objAcquireFromWeakDyn(Http3Conn, req->h3conn);
    if (!conn)
        return;
    h3conn_pumpBody(conn, req);
    objRelease(&conn);
}

// ---------------------------------------------------------------------------------------------
// Reading the response
// ---------------------------------------------------------------------------------------------

// Hand one run of body bytes to whichever disposition the request chose.
static void deliverBody(_Inout_ H3CStream* st, _In_reads_bytes_(len) const uint8* data, size_t len)
{
    HttpRequest* req = st->req;

    // A redirect that is going to be followed still has a body, and it is not the answer to
    // anything. Reading it is unavoidable; delivering it is not.
    if (req->discardBody)
        return;

    reportProgress(st, HTTPPROG_Recv, len, false);

    if (req->respSink) {
        sbufPWrite(req->respSink, data, len);
        return;
    }

    if (st->handlers && st->handlers->data) {
        HttpEvent ev = { 0 };
        ev.event     = HTTPEV_Data;
        ev.request   = req;
        ev.ctx       = st->hctx;
        ev.status    = st->status;
        ev.version   = HTTPVER_3;
        ev.headers   = &req->respHeaders;
        ev.data      = data;
        ev.len       = len;
        st->handlers->data(&ev);
        return;
    }

    _httpAppendBytes(&req->respBody, data, len);
}

// A field section arrived. Which one it is -- an interim response or the answer -- is decided
// here, because the frame layer cannot tell them apart.
static bool beginResponse(_Inout_ H3CStream* st, _In_ NetFlow* flow)
{
    HttpRequest* req = st->req;

    uint16 status = 0;
    HttpHeaders fields;
    uint64 err = _h3RespFromFields(&status, &fields, &st->msg.headers);
    if (err != 0) {
        httpHeadersDestroy(&fields);
        streamFail(st, flow, err, HTTPERR_BadMessage);
        return false;
    }

    // A 100 or a 103 ahead of the real response. Nothing here wants one yet, and delivering it as
    // HTTPEV_Status would hand the application an interim message as the answer.
    if (status >= 100 && status < 200) {
        httpHeadersDestroy(&fields);

        HttpLimits def;
        httpLimitsDefault(&def);
        if (++st->interim > def.maxInterim) {
            streamFail(st, flow, H3ERR_EXCESSIVE_LOAD, HTTPERR_TooLarge);
            return false;
        }
        return true;
    }

    // A second real field section before the body is a second answer to one request.
    if (st->headDone) {
        httpHeadersDestroy(&fields);
        streamFail(st, flow, H3ERR_MESSAGE_ERROR, HTTPERR_BadMessage);
        return false;
    }

    st->status   = status;
    st->headDone = true;

    httpHeadersDestroy(&req->respHeaders);
    req->respHeaders = fields;
    req->status      = status;
    req->version     = HTTPVER_3;

    // Content-Length is still carried and still checked, even though the stream's own end is what
    // frames the body. Without one the length is simply not known up front, which is what HTTP/3
    // has instead of chunked encoding.
    string clen = 0;
    if (httpHeadersGet(&req->respHeaders, _SL("Content-Length"), &clen)) {
        uint64 n = 0;
        if (!strToUInt64(&n, clen, 10, HTTP_STRICTNUM)) {
            strDestroy(&clen);
            streamFail(st, flow, H3ERR_MESSAGE_ERROR, HTTPERR_BadMessage);
            return false;
        }
        atomicStore(int64, &req->progRecvTotal, (int64)n, Relaxed);
    } else {
        atomicStore(int64, &req->progRecvTotal, -1, Relaxed);
    }
    strDestroy(&clen);

    deliver(st, HTTPEV_Status, HTTPERR_None);
    deliver(st, HTTPEV_Headers, HTTPERR_None);
    return true;
}

static void streamPump(_Inout_ H3CStream* st, _In_ NetFlow* flow)
{
    if (st->failed || st->complete)
        return;

    uint8 buf[HTTP3_READ_CHUNK];
    bool eof = false;

    for (;;) {
        while (!st->failed && !st->complete) {
            H3MsgResult r = _h3MsgStep(&st->msg, &st->in);

            if (r == H3MSG_NeedMore)
                break;

            if (r == H3MSG_Error) {
                streamFail(st, flow, st->msg.err, _h3ErrorToHttp(st->msg.err));
                return;
            }

            if (r == H3MSG_Head) {
                if (!beginResponse(st, flow))
                    return;
                continue;
            }

            if (r == H3MSG_Body) {
                size_t remaining = st->msg.bodyReady;
                while (remaining > 0) {
                    size_t got = bufringRead(&st->in, buf, min(remaining, sizeof(buf)));
                    deliverBody(st, buf, got);
                    remaining -= got;
                }
                continue;
            }

            // H3MSG_Trailers: parsed and discarded, as in HTTP/1.1.
        }

        if (st->failed || st->complete)
            return;

        bool fin = false;
        size_t n = netquicRecv(flow, buf, sizeof(buf), &fin);
        eof      = eof || fin;
        if (n == 0)
            break;

        bufringWrite(&st->in, buf, n);
    }

    if (!eof)
        return;

    if (!st->headDone) {
        // The stream ended without a field section, which is a response that never was.
        streamFail(st, flow, H3ERR_NO_ERROR, HTTPERR_Closed);
        return;
    }

    // The declared length and what actually arrived have to agree; they can only disagree by the
    // peer ending the stream early, since anything longer would have been refused as it arrived.
    int64 total = atomicLoad(int64, &st->req->progRecvTotal, Relaxed);
    if (total >= 0 && (uint64)total != atomicLoad(uint64, &st->req->progRecv, Relaxed)) {
        streamFail(st, flow, H3ERR_NO_ERROR, HTTPERR_Closed);
        return;
    }

    if (st->deadline) {
        netflowCancelTimer(flow, st->deadline);
        st->deadline = 0;
    }

    reportProgress(st, HTTPPROG_Recv, 0, true);
    deliver(st, HTTPEV_Complete, HTTPERR_None);
}

// ---------------------------------------------------------------------------------------------
// Stream handlers
// ---------------------------------------------------------------------------------------------

static void streamOnRecv(NetEvent* ev)
{
    H3CStream* st   = (H3CStream*)ev->ctx;
    Http3Conn* conn = objAcquireFromWeak(Http3Conn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    streamPump(st, ev->flow);
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

static void streamOnSendReady(NetEvent* ev)
{
    H3CStream* st   = (H3CStream*)ev->ctx;
    Http3Conn* conn = objAcquireFromWeak(Http3Conn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    if (st->writing)
        h3conn_pumpBody(conn, st->req);
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

static void streamOnError(NetEvent* ev)
{
    H3CStream* st   = (H3CStream*)ev->ctx;
    Http3Conn* conn = objAcquireFromWeak(Http3Conn, st->conn);
    if (!conn)
        return;

    QuicStream* qs = objDynCast(QuicStream, ev->flow);
    uint64 code    = qs ? qs->error : 0;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);
    streamFail(st, ev->flow, H3ERR_NO_ERROR, _h3ErrorToHttp(code));
    _httpDispatchLeave(&st->req->dispatch, prev);

    objRelease(&conn);
}

static void streamOnTimer(NetEvent* ev)
{
    H3CStream* st   = (H3CStream*)ev->ctx;
    Http3Conn* conn = objAcquireFromWeak(Http3Conn, st->conn);
    if (!conn)
        return;

    Thread* prev = _httpDispatchEnter(&st->req->dispatch);

    if (ev->timer.id == st->deadline) {
        st->deadline = 0;
        streamFail(st, ev->flow, H3ERR_REQUEST_CANCELLED, HTTPERR_Timeout);
    } else if (_httpDispatchClaim(&st->req->dispatch)) {
        // A handoff from another thread: a body producer that wrote more, or a cancel.
        if (st->writing)
            h3conn_pumpBody(conn, st->req);
    }

    _httpDispatchLeave(&st->req->dispatch, prev);
    objRelease(&conn);
}

static void streamOnClosed(NetEvent* ev)
{
    H3CStream* st = (H3CStream*)ev->ctx;

    Http3Conn* conn = objAcquireFromWeak(Http3Conn, st->conn);
    if (conn) {
        Thread* prev = _httpDispatchEnter(&st->req->dispatch);

        // A stream that ends without the response having completed is a request that died.
        if (!st->complete && !st->failed)
            streamFail(st, ev->flow, H3ERR_NO_ERROR, HTTPERR_Closed);

        atomicFetchSub(uint32, &conn->nreqs, 1, Relaxed);
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

static void connError(_Inout_ ObjInst* conn, uint64 code)
{
    Http3Conn* self = objDynCast(Http3Conn, conn);
    if (!self || self->failed)
        return;
    self->failed = true;

    if (self->sock)
        netquicClose(self->sock, code, NULL);
}

static void onFlowOpen(NetEvent* ev)
{
    Http3Conn* self = (Http3Conn*)ev->ctx;
    NetFlow* flow   = ev->flow;

    if (!flow || (ev->socket && flow == ev->socket->flow))
        return;

    // Only the peer's unidirectional streams arrive this way. A server-initiated bidirectional
    // stream is not a thing HTTP/3 has, and a push stream is refused by the session.
    if ((flow->key & 0x01) == 0)
        return;

    if ((flow->key & 0x02) != 0)
        _h3UniAttach(flow, ObjInst(self), sessionOf(self), connError);
}

static void onNetClosed(NetEvent* ev)
{
    Http3Conn* self = (Http3Conn*)ev->ctx;

    // Only the socket's own control flow ending means the connection ended.
    if (ev->socket && ev->flow != ev->socket->flow)
        return;

    self->failed = true;

    // The pool needs to hear about this even when no request was running, because a pooled
    // connection the peer closed produces no response event at all: there is no response.
    Http3ConnClosedCB cb = (Http3ConnClosedCB)self->closedCB;
    if (cb)
        cb(ObjInst(self), self->closedCtx);
}

static const NetHandlers kConnHandlers = {
    .flowOpen   = onFlowOpen,
    .flowClosed = onNetClosed,
};

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_objfactory_check Http3Conn* Http3Conn_create(_In_ NetSocket* sock, _In_opt_ strref host)
{
    _httpInit();

    if (!sock || sock->type != NST_Quic)
        return NULL;

    Http3Conn* self;
    self = objInstCreate(Http3Conn);

    self->sock = objAcquire(sock);
    strDup(&self->host, host);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    _h3SessionInit(sessionOf(self), false, NULL);

    // Held weakly: the socket must not own the connection, or the two would keep each other alive.
    netsocketSetHandlersObj(sock, &kConnHandlers, self);

    if (!_h3OpenUniStreams(sock, sessionOf(self), &self->control, &self->qpackEnc,
                           &self->qpackDec)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objinit_guaranteed bool Http3Conn_init(_In_ Http3Conn* self)
{
    self->session = xaAlloc(sizeof(Http3Session), XA_Zero);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void Http3Conn_destroy(_In_ Http3Conn* self)
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
    objRelease(&self->control);
    objRelease(&self->qpackEnc);
    objRelease(&self->qpackDec);
    strDestroy(&self->host);
    // Autogen ends -------
}

void Http3Conn_close(_In_ Http3Conn* self)
{
    self->failed = true;
    if (self->sock)
        netquicClose(self->sock, H3ERR_NO_ERROR, NULL);
}

bool Http3Conn_usable(_In_ Http3Conn* self)
{
    // A connection the peer has said goodbye to still finishes what is on it and takes nothing
    // new: everything at or above the identifier it named was never processed.
    return !self->failed && !sessionOf(self)->goawayRecvd;
}

// ---------------------------------------------------------------------------------------------
// Starting a request
// ---------------------------------------------------------------------------------------------

bool Http3Conn_request(_In_ Http3Conn* self, _In_ HttpRequest* req,
                       _In_opt_ const HttpHandlers* handlers, _In_opt_ void* ctx)
{
    if (!req || self->failed)
        return false;

    NetFlow* flow = netquicOpen(self->sock, false);
    if (!flow)
        return false;   // the peer's stream limit is reached; the caller dials another connection

    H3CStream* st = xaAlloc(sizeof(H3CStream), XA_Zero);
    st->id        = flow->key;
    st->conn      = objGetWeak(Http3Conn, self);
    st->req       = objAcquire(req);
    st->handlers  = handlers;
    st->hctx      = ctx;
    bufringInit(&st->in, HTTP3_READ_CHUNK);
    _h3MsgInit(&st->msg, false, NULL);

    // The response body cap is the request's if it set one, and the parser default otherwise. Set
    // both ways round: a shared connection carries whatever the last request asked for.
    HttpLimits def;
    httpLimitsDefault(&def);
    st->msg.maxBody = req->maxBody ? req->maxBody : def.maxBodyBytes;

    objRelease(&req->h3flow);
    req->h3flow = objAcquire(flow);
    objDestroyWeak(&req->h3conn);
    req->h3conn  = objGetWeak(ObjInst, self);
    req->version = HTTPVER_3;

    netflowSetHandlers(flow, &kStreamHandlers, st);
    atomicFetchAdd(uint32, &self->nreqs, 1, Relaxed);

    // Before the field section, because its framing is derived from the body that is actually
    // armed.
    if (!_httpReqArmBody(req)) {
        netquicReset(flow, H3ERR_INTERNAL_ERROR);
        objRelease(&flow);
        return false;
    }

    // Progress starts over for every hop. A redirect sends the body again and reads a different
    // response, so carrying the previous hop's counts forward would report both as one transfer.
    atomicStore(uint64, &req->progSent, 0, Relaxed);
    atomicStore(uint64, &req->progRecv, 0, Relaxed);
    atomicStore(int64, &req->progSendTotal, req->reqBodyStream ? req->reqBodyLen : 0, Relaxed);
    atomicStore(int64, &req->progRecvTotal, 0, Relaxed);
    req->progSentSeen = 0;
    req->progRecvSeen = 0;

    // A body of unknown length has no Content-Length to announce -- the stream's end is what frames
    // it -- which is what HTTP/3 has instead of chunked transfer coding.
    int64 bodyLen = req->reqBodyStream ? req->reqBodyLen : 0;

    HttpHeaders fields;
    bool ok = _h3ReqToFields(&fields, _httpMethodName(req), &req->url, &req->reqHeaders, bodyLen);

    // Nothing follows a request with no body, so the field section carries the end of the stream.
    bool fin = !req->reqBodyStream;
    ok       = ok && _h3SendFields(flow, &fields, fin);
    httpHeadersDestroy(&fields);

    if (!ok) {
        netquicReset(flow, H3ERR_INTERNAL_ERROR);
        objRelease(&flow);
        return false;
    }

    st->finished = fin;

    // The clock starts once the request is on the wire and covers the whole response rather than
    // the gap between packets: a peer that trickles one byte a second is exactly what a per-packet
    // timer fails to catch.
    if (self->timeout > 0)
        st->deadline = netflowAddTimer(flow, self->timeout, NTF_None);

    if (req->reqBodyStream) {
        st->writing = true;

        // The caller may be any thread at all, and a push-mode producer's notify has to be able to
        // tell whether it is this one.
        Thread* prev = _httpDispatchEnter(&req->dispatch);
        h3conn_pumpBody(self, req);
        _httpDispatchLeave(&req->dispatch, prev);
    }

    objRelease(&flow);   // the flow table holds it; the stream state reaches it through the request
    return true;
}

// ---------------------------------------------------------------------------------------------
// The seam the version-agnostic sources reach HTTP/3 through
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
NetSocket* _http3Dial(NetQueue* q, strref host, uint16 port, strref hostname, TlsConfig* cfg,
                      const NetHandlers* handlers, void* ctx, NetConnectPrepCB prep, void* prepctx)
{
    // A client that does not offer h3 cannot be answered with it. One configuration carries both
    // protocols, which under HTTPV_Any means the TCP handshake offers h3 as well -- harmless,
    // because a server selecting it over TCP would be answering with a protocol that does not
    // exist there, and the TCP dialer already refuses anything but http/1.1.
    if (!_httpAlpnAdd(cfg, _SL(H3_ALPN)))
        return NULL;

    QuicConfig qcfg = { .tls = cfg };
    return netquicConnectPrep(q, host, port, hostname, &qcfg, handlers, ctx, prep, prepctx);
}

_Use_decl_annotations_
bool _http3Negotiated(NetSocket* sock)
{
    string alpn = 0;
    bool ok     = netquicALPN(sock, &alpn) && strEq(alpn, _SL(H3_ALPN));
    strDestroy(&alpn);
    return ok;
}

_Use_decl_annotations_
ObjInst* _http3ConnCreate(NetSocket* sock, strref host)
{
    return ObjInst(h3connCreate(sock, host));
}

_Use_decl_annotations_
bool _http3ConnRequest(ObjInst* conn, HttpRequest* req, const HttpHandlers* handlers, void* ctx,
                       int64 timeout)
{
    Http3Conn* self = objDynCast(Http3Conn, conn);
    if (!self)
        return false;

    self->timeout = timeout;
    return h3connRequest(self, req, handlers, ctx);
}

_Use_decl_annotations_
bool _http3ConnUsable(ObjInst* conn)
{
    Http3Conn* self = objDynCast(Http3Conn, conn);
    return self && h3connUsable(self);
}

_Use_decl_annotations_
uint32 _http3ConnRequests(ObjInst* conn)
{
    Http3Conn* self = objDynCast(Http3Conn, conn);
    return self ? atomicLoad(uint32, &self->nreqs, Relaxed) : 0;
}

_Use_decl_annotations_
void _http3ConnSetClosed(ObjInst* conn, Http3ConnClosedCB cb, void* ctx)
{
    Http3Conn* self = objDynCast(Http3Conn, conn);
    if (!self)
        return;

    self->closedCB  = (void*)cb;
    self->closedCtx = ctx;
}

_Use_decl_annotations_
void _http3ReqCancel(HttpRequest* req)
{
    // A cancel costs this request's stream and nothing else on the connection, which is the whole
    // difference from HTTP/1.1: there, abandoning a response leaves the connection at an unknown
    // point in a message and the connection has to go too.
    if (req->h3flow) {
        netquicReset(req->h3flow, H3ERR_REQUEST_CANCELLED);
        netquicStopSending(req->h3flow, H3ERR_REQUEST_CANCELLED);
    }
}

_Use_decl_annotations_
bool _http3ReqRetryable(HttpRequest* req)
{
    Http3Conn* conn = objAcquireFromWeakDyn(Http3Conn, req->h3conn);
    if (!conn)
        return false;

    // The identifier in a GOAWAY is the first stream the peer will not serve. Everything at or
    // above it was not processed, which the peer has just promised, so starting the request again
    // somewhere else cannot repeat anything -- and that holds for a POST as much as for a GET.
    bool retry = req->h3flow && _h3SessionGoneAway(sessionOf(conn), req->h3flow->key);
    objRelease(&conn);
    return retry;
}

_Use_decl_annotations_
void _http3ReqRelease(HttpRequest* req)
{
    objDestroyWeak(&req->h3conn);
    objRelease(&req->h3flow);
}

// Autogen begins -----
// clang-format off
void Http3Conn__pumpBody(_In_ Http3Conn* self, _In_ HttpRequest* req);
#include "http3conn.auto.inc"
// clang-format on
// Autogen ends -------

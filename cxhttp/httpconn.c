// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpconn.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// The low-level connection
//
// Writes a request, reads a response, and calls back. Everything above it -- dialing, TLS,
// redirects, cookies, pooling -- is HttpClient's business, and everything below it is the socket's.
//
// This is a plain consumer of NetHandlers rather than a NetStreamFilter: a filter's only channel
// back to the application is a bare enum, and HTTP has to deliver a status, a header set and a
// body. The composition still works, because cxtls makes netsocketSend() take plaintext and
// netsocketRecv() return plaintext whether or not a filter is attached, so nothing here knows or
// cares which it is talking to.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/net.h>

// How much to pull off the socket per read. Sized to hold a typical response head in one pass while
// staying small enough to sit on the stack of a worker callback.
#define HTTPCONN_READ_CHUNK 8192

// ---------------------------------------------------------------------------------------------
// Timers
//
// Both deadlines are flow timers, so both are ordered against everything else happening on this
// connection -- an idle reap cannot overtake the peer's own close, and a response deadline cannot
// fire between two halves of a message that already arrived.
// ---------------------------------------------------------------------------------------------

static void cancelTimers(HttpConn* self)
{
    NetFlow* flow = self->sock ? self->sock->flow : NULL;
    if (!flow) {
        self->deadlineTimer = self->idleTimer = 0;
        return;
    }

    if (self->deadlineTimer) {
        netflowCancelTimer(flow, self->deadlineTimer);
        self->deadlineTimer = 0;
    }
    if (self->idleTimer) {
        netflowCancelTimer(flow, self->idleTimer);
        self->idleTimer = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Socket handlers
// ---------------------------------------------------------------------------------------------

// The one thing every handler here has in common: the connection is reached through a borrowed
// pointer, and a terminal callback can be the last thing holding it -- a completion handler that
// releases its connection to start the next request elsewhere is the ordinary case, not an exotic
// one. Taking a reference for the length of the dispatch means that release destroys the object
// after this returns rather than underneath it.
static HttpConn* enterConn(NetEvent* ev)
{
    HttpConn* c = (HttpConn*)ev->ctx;
    return c ? objAcquire(c) : NULL;
}

static bool pumpBodyStream(HttpConn* self, HttpRequest* req);

// ---------------------------------------------------------------------------------------------
// Which thread we are on
//
// A request body may be fed from any thread the application likes, but writing it also touches the
// socket, the request and the connection's own state, and all of that belongs to one worker at a
// time. These are the same three helpers the server uses, for the same reason.
// ---------------------------------------------------------------------------------------------

static Thread* enterDispatch(HttpConn* c)
{
    Thread* prev = (Thread*)atomicLoad(ptr, &c->dispatchThread, Relaxed);
    atomicStore(ptr, &c->dispatchThread, thrCurrent(), Release);
    return prev;
}

static void leaveDispatch(HttpConn* c, Thread* prev)
{
    atomicStore(ptr, &c->dispatchThread, prev, Release);
}

// True when this thread is the one currently dispatching on the connection, and may therefore
// touch its socket, parser and ring directly. Comparing against our own Thread is what makes a
// stale read harmless: another thread's Thread is never ours, so a reader that loses the race
// takes the handoff, which is always correct.
static bool onDispatchThread(HttpConn* c)
{
    return atomicLoad(ptr, &c->dispatchThread, Acquire) == thrCurrent();
}

// Ask a worker to come and move this connection along. A zero-delay flow timer is the handoff:
// NET_Timer arrives on a worker, ordered behind everything already pending for this connection, so
// what it does cannot overtake an event the application has not seen yet.
static bool handoffToWorker(HttpConn* c)
{
    NetFlow* flow = c->sock ? c->sock->flow : NULL;
    if (!flow)
        return false;

    atomicStore(uint32, &c->pumpPending, 1, Release);
    if (netflowAddTimer(flow, 0, NTF_None) == 0) {
        // The flow is already dying, so no worker is coming.
        atomicStore(uint32, &c->pumpPending, 0, Relaxed);
        return false;
    }
    return true;
}

_Use_decl_annotations_
void _httpReqBodyNotify(StreamBuffer* sb, size_t sz, void* ctx)
{
    unused_noeval(sb);
    unused_noeval(sz);

    HttpRequest* req = (HttpRequest*)ctx;
    HttpConn* self   = req ? req->bodyConn : NULL;
    if (!self || !self->writing)
        return;

    // The producer may be on any thread, so this takes the same fork the server's response body
    // does: write here if this is already the connection's worker, otherwise ask one to.
    if (onDispatchThread(self))
        pumpBodyStream(self, req);
    else
        handoffToWorker(self);
}

static void onNetRecv(NetEvent* ev)
{
    HttpConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);
    httpconn_pump(c);
    leaveDispatch(c, prev);

    objRelease(&c);
}

// The connection is over, whatever the reason. Ends the request in flight, if there is one, and
// then tells whoever registered an interest -- which is how a pool learns that what it is holding
// is no longer worth handing out.
static void connDied(HttpConn* c, HttpError err, bool eof)
{
    // "We gave up" and "the peer went away" arrive through the same teardown, and only the flag set
    // before the close can tell them apart. An abort also has no half-read message to salvage, so
    // the parser is not consulted for it the way an ordinary close is.
    if (c->aborted) {
        err = HTTPERR_Aborted;
        eof = false;
    }

    if (!c->failed && !c->responseDone) {
        if (eof) {
            // A close is how a close-delimited response ends and how every other framing is
            // truncated. The parser knows which of those this is; asking it is the whole of the
            // decision.
            HttpParseResult r = httpParserEOF(c->parser);
            if (r == HTTPP_Complete) {
                c->responseDone = true;
                c->spent        = true;   // the body ended by closing; there is no next message
                httpconn_deliver(c, HTTPEV_Complete, HTTPERR_None);
            } else {
                c->failed = true;
                httpconn_deliver(c, HTTPEV_Error, c->parser->err ? c->parser->err : err);
            }
        } else {
            c->failed = true;
            httpconn_deliver(c, HTTPEV_Error, err);
        }
    }

    c->spent = true;
    cancelTimers(c);

    if (c->closedCB)
        c->closedCB(c, c->closedCtx);
}

static void onNetClosed(NetEvent* ev)
{
    HttpConn* c = enterConn(ev);
    if (!c)
        return;

    connDied(c, HTTPERR_Closed, true);
    objRelease(&c);
}

static void onNetError(NetEvent* ev)
{
    HttpConn* c = enterConn(ev);
    if (!c)
        return;

    if (c->req)
        c->req->neterr = ev->error.err;
    connDied(c, HTTPERR_Network, false);
    objRelease(&c);
}

static void onNetTimer(NetEvent* ev)
{
    HttpConn* c = enterConn(ev);
    if (!c)
        return;

    if (ev->timer.id == c->deadlineTimer) {
        // A response that ran out of time leaves the connection unusable whatever else is true: we
        // are somewhere in the middle of a message and cannot tell where the next one would start.
        c->deadlineTimer = 0;
        connDied(c, HTTPERR_Timeout, false);
    } else if (ev->timer.id == c->idleTimer) {
        c->idleTimer = 0;
        httpconnClose(c);
    } else if (atomicExchange(uint32, &c->pumpPending, 0, AcqRel)) {
        // A producer on another thread fed the request body and asked for a worker. This is that
        // worker; the connection's own state says what is left to write.
        Thread* prev = enterDispatch(c);
        if (c->writing && c->req)
            pumpBodyStream(c, c->req);
        leaveDispatch(c, prev);
    }

    objRelease(&c);
}

// The socket's backlog drained below its low watermark, so a request body that stopped against the
// high one can carry on. Without this a streamed body larger than the send buffer stops for good:
// the pump backs off at the watermark, and nothing else on the connection is going to restart it --
// the server is waiting for the rest of the request, so it sends nothing to wake us with.
static void onNetSendReady(NetEvent* ev)
{
    HttpConn* c = enterConn(ev);
    if (!c)
        return;

    Thread* prev = enterDispatch(c);
    if (c->writing && c->req)
        pumpBodyStream(c, c->req);
    leaveDispatch(c, prev);

    objRelease(&c);
}

// Registered on the socket for the lifetime of the connection. Deliberately not per-request: the
// peer can close or error between requests on a pooled connection, and there has to be somebody
// listening when it does.
static const NetHandlers kConnHandlers = {
    .recv       = onNetRecv,
    .sendReady  = onNetSendReady,
    .flowClosed = onNetClosed,
    .error      = onNetError,
    .timer      = onNetTimer,
};

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

_objfactory_check HttpConn* HttpConn_create(_In_ NetSocket* sock, _In_opt_ strref host)
{
    _httpInit();

    if (!sock || sock->type != NST_Stream)
        return NULL;

    HttpConn* self;
    self = objInstCreate(HttpConn);

    self->sock = objAcquire(sock);
    strDup(&self->host, host);

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    // The handler ctx is the connection itself, which is a borrowed pointer rather than a held
    // reference: the socket outlives the connection only if the caller made it so, and taking a
    // reference here would make the two own each other.
    netsocketSetHandlers(sock, &kConnHandlers, self);

    return self;
}

_objinit_guaranteed bool HttpConn_init(_In_ HttpConn* self)
{
    self->parser = (HttpParser*)xaAlloc(sizeof(HttpParser), XA_Zero);
    httpParserInit(self->parser, false, NULL);
    bufringInit(&self->in, HTTPCONN_READ_CHUNK);
    return true;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void HttpConn_destroy(_In_ HttpConn* self)
{
    // Stop the socket calling back into an object that is going away. Everything else here would be
    // safe to do in any order; this must be first.
    if (self->sock)
        netsocketSetHandlers(self->sock, NULL, NULL);

    // An armed timer holds a strong reference to the flow, and the flow's terminal path is not the
    // only way this object goes away. Dropping them here keeps a released connection from pinning
    // a flow until a deadline nobody is waiting for expires.
    cancelTimers(self);

    if (self->parser) {
        httpParserDestroy(self->parser);
        xaFree(self->parser);
        self->parser = NULL;
    }

    bufringDestroy(&self->in);
    // Autogen begins -----
    objRelease(&self->sock);
    strDestroy(&self->host);
    bufringDestroy(&self->in);
    // Autogen ends -------
}

bool HttpConn_idle(_In_ HttpConn* self)
{
    return !self->failed && !self->spent && !self->req && !self->writing;
}

// ---------------------------------------------------------------------------------------------
// Writing a request
// ---------------------------------------------------------------------------------------------

static strref methodText(HttpRequest* req)
{
    switch (req->method) {
    case HTTP_Get:
        return _S "GET";
    case HTTP_Head:
        return _S "HEAD";
    case HTTP_Post:
        return _S "POST";
    case HTTP_Put:
        return _S "PUT";
    case HTTP_Delete:
        return _S "DELETE";
    case HTTP_Connect:
        return _S "CONNECT";
    case HTTP_Options:
        return _S "OPTIONS";
    case HTTP_Trace:
        return _S "TRACE";
    case HTTP_Patch:
        return _S "PATCH";
    default:
        return req->methodName;
    }
}

// Build the request line and header block. The framing headers are filled in from the body that was
// actually set rather than trusted from the application, so a request cannot claim one length and
// send another -- which is the same conflict the parser refuses to accept from a peer.
static bool buildHead(HttpConn* self, HttpRequest* req, string* out)
{
    strref method = methodText(req);
    if (strEmpty(method))
        return false;

    string target = 0;
    httpUrlTarget(&target, &req->url);

    strAppend(out, method);
    strAppendChar(out, ' ');
    strAppend(out, target);
    strAppend(out, _SL(" HTTP/1.1\r\n"));
    strDestroy(&target);

    // Host is mandatory in 1.1 and comes from the connection rather than the URL, because they are
    // not always the same: a proxy or an IP literal with a virtual host pulls them apart.
    if (!httpHeadersHas(&req->reqHeaders, _SL("Host"))) {
        string hosthdr = 0;
        if (!strEmpty(self->host))
            strDup(&hosthdr, self->host);
        else
            httpUrlHostHeader(&hosthdr, &req->url);

        strAppend(out, _SL("Host: "));
        strAppend(out, hosthdr);
        strAppend(out, _SL("\r\n"));
        strDestroy(&hosthdr);
    }

    httpHeadersFormat(out, &req->reqHeaders);

    // Framing, derived from the body rather than taken on trust.
    if (req->reqBodyStream && req->reqBodyLen < 0) {
        strAppend(out, _SL("Transfer-Encoding: chunked\r\n"));
    } else if (req->reqBodyStream) {
        uint64 len = (uint64)req->reqBodyLen;
        string n   = 0;
        strFromUInt64(&n, len, 10);
        strAppend(out, _SL("Content-Length: "));
        strAppend(out, n);
        strAppend(out, _SL("\r\n"));
        strDestroy(&n);
    }

    strAppend(out, _SL("\r\n"));
    return true;
}

// Send a whole string. strPC() rather than strC(): a built head or an encoded chunk is very likely
// a rope, and strC() would flatten it into a rotating thread-local scratch buffer -- which anything
// the send calls, filter chain included, is free to take for itself before it has finished reading
// the payload. strPC() flattens the handle itself, so the bytes belong to the string.
static bool sendStr(HttpConn* self, strhandle s)
{
    uint32 len = strLen(*s);
    if (len == 0)
        return true;
    return netsocketSend(self->sock, (uint8*)strPC(s), len, NULL, 0);
}

// Add to one of the request's progress counters and deliver an event if the interval says one is
// due. `final` is for the end of a body, where an event fires for whatever has not been reported
// yet so a progress bar reaches its total.
//
// The counters are written only here, on the flow's worker, so the increment needs no
// read-modify-write of its own. The store is atomic because httprequestSentBytes() and its
// siblings load it from whichever thread asked.
static void reportProgress(HttpConn* self, HttpRequest* req, HttpProgressDir dir, size_t n,
                           bool final)
{
    if (!req)
        return;

    bool send                = (dir == HTTPPROG_Send);
    atomic(uint64)* counter  = send ? &req->progSent : &req->progRecv;
    uint64* seen             = send ? &req->progSentSeen : &req->progRecvSeen;

    uint64 done = atomicLoad(uint64, counter, Relaxed);
    if (n > 0) {
        done += (uint64)n;
        atomicStore(uint64, counter, done, Relaxed);
    }

    if (!_httpProgressDue(done, seen, req->progressInterval, final))
        return;

    if (!self->handlers || !self->handlers->progress)
        return;

    atomic(int64)* total = send ? &req->progSendTotal : &req->progRecvTotal;

    HttpEvent ev = { 0 };
    ev.event     = HTTPEV_Progress;
    ev.conn      = self;
    ev.request   = req;
    ev.ctx       = self->handlerCtx;
    ev.status    = self->parser->status;
    ev.version   = self->parser->version;
    ev.headers   = &self->parser->headers;
    ev.dir       = dir;
    ev.done      = done;
    ev.total     = atomicLoad(int64, total, Relaxed);

    self->handlers->progress(&ev);
}

// Hand one slice of the request body to the socket, straight out of the stream buffer's own
// storage. Runs with the buffer's lock held, so it must not touch the buffer at all.
static bool bodySendCB(_Pre_valid_ StreamBuffer* sb, _In_reads_bytes_(sz) const uint8* buf,
                       size_t off, size_t sz, _Pre_opt_valid_ void* ctx)
{
    unused_noeval(sb);
    unused_noeval(off);

    HttpRequest* req = (HttpRequest*)ctx;
    HttpConn* self   = req ? req->bodyConn : NULL;

    // Always retain, and let the pump consume exactly what went out. One sbufCSend() can walk
    // several ring segments and the ring consumes either all of them or none, so a socket that
    // takes the first and refuses the second would otherwise have to choose between sending those
    // bytes twice and losing them.
    if (!self || sz == 0 || req->bodyRefused)
        return false;

    bool ok;
    if (req->reqBodyLen < 0) {
        string enc = 0;
        httpChunkAppend(&enc, buf, sz);
        ok = sendStr(self, &enc);
        strDestroy(&enc);
    } else {
        ok = netsocketSend(self->sock, (uint8*)buf, sz, NULL, 0);
    }

    if (ok)
        req->bodySent += sz;
    else
        req->bodyRefused = true;

    return false;
}

// Push as much of the request body onto the socket as it will take, encoding it as chunks when the
// length was not known up front. Nothing leaves the stream buffer until the socket has accepted it,
// so a refusal costs a retry and never a byte. Returns false on a fatal send failure.
static bool pumpBodyStream(HttpConn* self, HttpRequest* req)
{
    StreamBuffer* sb = req->reqBodyStream;

    while (sb) {
        // How much to ask for is the one place the two buffer modes differ. A pull-mode buffer is
        // asked for a full slice and calls its producer to satisfy it, short-reading only at the
        // end of the body. A push-mode buffer holds whatever has been written so far and fails a
        // read larger than that, so it has to be asked for exactly what it has.
        size_t want = HTTPCONN_READ_CHUNK;
        if (!sbufIsPull(sb)) {
            want = min(sbufCAvail(sb), (size_t)HTTPCONN_READ_CHUNK);
            if (want == 0)
                break;
        }

        req->bodySent    = 0;
        req->bodyRefused = false;

        if (!sbufCSend(sb, bodySendCB, want))
            break;

        if (req->bodySent > 0) {
            sbufCSkip(sb, req->bodySent);
            reportProgress(self, req, HTTPPROG_Send, req->bodySent, false);
        }

        // A refused send is backpressure, not an error -- and on a TLS connection mid-handshake it
        // is the normal answer, because staged filter bytes count toward the watermark. Stop here
        // and resume when NET_SendReady says the backlog drained.
        if (req->bodyRefused)
            return true;

        if (req->bodySent == 0)
            break;
    }

    if (sb) {
        if (!sbufIsPFinished(sb) || sbufCAvail(sb) > 0)
            return true;   // more to come; the producer's next write wakes us through the notify

        // The body is spent, so give the producer's side the end-of-consumption it is waiting for
        // -- a file being uploaded closes here rather than when the request object happens to go
        // away. sbufCFinish() returns our reference with it, hence the NULL.
        sbufCFinish(sb);
        req->reqBodyStream = NULL;
    }

    // The terminator is the last thing on the wire for a chunked body, so it is retried on the next
    // NET_SendReady like everything else rather than being dropped when the socket is full.
    if (req->reqBodyLen < 0) {
        string fin = 0;
        httpChunkFinish(&fin);
        bool ok = sendStr(self, &fin);
        strDestroy(&fin);
        if (!ok)
            return true;
    }

    reportProgress(self, req, HTTPPROG_Send, 0, true);

    self->writing = false;
    req->bodyConn = NULL;
    return true;
}

bool HttpConn_request(_In_ HttpConn* self, _In_ HttpRequest* req,
                      _In_opt_ const HttpHandlers* handlers, _In_opt_ void* ctx)
{
    // One at a time. Pipelining is deliberately not supported, which also means the response parser
    // never has to associate a response with anything but "the request this connection is running".
    if (!req || self->failed || self->req)
        return false;

    self->req          = req;
    self->handlers     = (HttpHandlers*)handlers;
    self->handlerCtx   = ctx;
    self->responseDone = false;

    // Whatever this connection was waiting for while idle, it is not idle now.
    httpconnSetIdleTimeout(self, 0);

    httpParserReset(self->parser);

    // The parser needs the method because a response to HEAD carries no body however it is framed.
    self->parser->reqMethod = req->method;

    // Set both ways round: on a pooled connection the parser still carries whatever the previous
    // request asked for, and a request that set no cap must not inherit one.
    if (req->maxBody) {
        self->parser->limits.maxBodyBytes = req->maxBody;
    } else {
        HttpLimits def;
        httpLimitsDefault(&def);
        self->parser->limits.maxBodyBytes = def.maxBodyBytes;
    }

    // Before the head, because the head's framing is derived from the body that is actually armed.
    if (!_httpReqArmBody(req)) {
        self->req = NULL;
        return false;
    }

    // Progress starts over for every hop. A redirect sends the body again and reads a different
    // response, so carrying the previous hop's counts forward would report both as one transfer.
    atomicStore(uint64, &req->progSent, 0, Relaxed);
    atomicStore(uint64, &req->progRecv, 0, Relaxed);
    int64 sendTotal = req->reqBodyStream ? req->reqBodyLen : 0;
    atomicStore(int64, &req->progSendTotal, sendTotal, Relaxed);
    atomicStore(int64, &req->progRecvTotal, 0, Relaxed);
    req->progSentSeen = 0;
    req->progRecvSeen = 0;

    string head = 0;
    if (!buildHead(self, req, &head)) {
        strDestroy(&head);
        self->req = NULL;
        return false;
    }

    bool ok = sendStr(self, &head);
    strDestroy(&head);

    if (!ok) {
        // The head could not even be queued, which at this point means the socket is over its
        // watermark before we have sent anything. Nothing has been written, so failing outright is
        // honest -- there is no partial request on the wire to clean up.
        self->req = NULL;
        return false;
    }

    // The clock starts once the request is on the wire, and covers the whole response rather than
    // the gap between packets: a peer that trickles one byte a second is exactly what a per-packet
    // timer fails to catch.
    if (self->timeout > 0 && self->sock->flow)
        self->deadlineTimer = netflowAddTimer(self->sock->flow, self->timeout, NTF_None);

    if (req->reqBodyStream) {
        self->writing = true;
        req->bodyConn = self;

        // The caller may be any thread at all, and a push-mode producer's notify has to be able to
        // tell whether it is this one.
        Thread* prev = enterDispatch(self);
        bool sent    = pumpBodyStream(self, req);
        leaveDispatch(self, prev);
        return sent;
    }

    return true;
}

// ---------------------------------------------------------------------------------------------
// Reading a response
// ---------------------------------------------------------------------------------------------

void HttpConn__deliver(_In_ HttpConn* self, HttpEventType type, HttpError err)
{
    HttpEvent ev = { 0 };

    ev.event   = type;
    ev.conn    = self;
    ev.request = self->req;
    ev.ctx     = self->handlerCtx;
    ev.status  = self->parser->status;
    ev.version = self->parser->version;
    ev.headers = &self->parser->headers;
    ev.err     = err;

    if (self->req)
        ev.neterr = self->req->neterr;

    HttpEventCB cb = NULL;
    if (self->handlers) {
        switch (type) {
        case HTTPEV_Status:
            cb = self->handlers->status;
            break;
        case HTTPEV_Headers:
            cb = self->handlers->headers;
            break;
        case HTTPEV_Data:
            cb = self->handlers->data;
            break;
        case HTTPEV_Progress:
            // Never routed through here -- reportProgress() fills in counts this has no way to
            // know, and delivers it itself.
            break;
        case HTTPEV_Complete:
            cb = self->handlers->complete;
            break;
        case HTTPEV_Error:
            cb = self->handlers->error;
            break;
        }
    }

    // The request is detached before a terminal event runs, so a handler is free to start the next
    // request on this connection from inside its own completion callback.
    if (type == HTTPEV_Complete || type == HTTPEV_Error) {
        if (self->req) {
            // Close out a response sink here rather than at each of the several places a message
            // can end, so a caller waiting on a StreamBuffer is released on every one of them --
            // including a timeout and a transport error, which do not go through the parser at all.
            //
            // A body being discarded for a redirect is the exception: the sink belongs to the
            // exchange, not to this hop, and the next one still needs it.
            if (self->req->respSink && !self->req->discardBody) {
                if (type != HTTPEV_Complete)
                    sbufError(self->req->respSink);

                // sbufPFinish() hands back the reference sbufPRegisterPush() took, so the request
                // must stop pointing at a buffer it no longer holds. sbufError() does not release,
                // which is why it is not enough on its own.
                sbufPFinish(self->req->respSink);
                self->req->respSink = NULL;
            }
            // Whatever state the body write was in, this connection is not going to finish it.
            self->req->bodyConn = NULL;

            self->req->err = err;
            httpHeadersClear(&self->req->respHeaders);
            for (int32 i = 0; i < httpHeadersCount(&self->parser->headers); i++) {
                httpHeadersAdd(&self->req->respHeaders,
                               self->parser->headers.names.a[i],
                               self->parser->headers.values.a[i]);
            }
            self->req->status  = self->parser->status;
            self->req->version = self->parser->version;
            strDup(&self->req->reason, self->parser->reason);
        }
        self->req = NULL;
    }

    if (cb)
        cb(&ev);
}

// Hand one run of decoded body bytes to whichever disposition the request chose.
static void deliverBody(HttpConn* self, const uint8* data, size_t len)
{
    HttpRequest* req = self->req;

    // A redirect that is going to be followed still has a body, and it is not the answer to
    // anything. Reading it is unavoidable -- it has to come off the wire before the next request
    // can use the connection -- but delivering it is not.
    if (req && req->discardBody)
        return;

    // Counted here rather than in each disposition below: the bytes have arrived whatever happens
    // to them next.
    reportProgress(self, req, HTTPPROG_Recv, len, false);

    if (req && req->respSink) {
        sbufPWrite(req->respSink, data, len);
        return;
    }

    if (self->handlers && self->handlers->data) {
        HttpEvent ev = { 0 };
        ev.event     = HTTPEV_Data;
        ev.conn      = self;
        ev.request   = req;
        ev.ctx       = self->handlerCtx;
        ev.status    = self->parser->status;
        ev.version   = self->parser->version;
        ev.headers   = &self->parser->headers;
        ev.data      = data;
        ev.len       = len;
        self->handlers->data(&ev);
        return;
    }

    // Default: accumulate on the request. The parser's maxBodyBytes is what bounds this, so a peer
    // cannot make us buffer without limit.
    if (req)
        _httpAppendBytes(&req->respBody, data, len);
}

void HttpConn__pump(_In_ HttpConn* self)
{
    if (self->failed)
        return;

    // Move everything the socket has into our own ring first. With a TLS filter attached this is
    // decoded plaintext; the parser cannot read the socket's ring directly because that holds
    // ciphertext in the filtered case.
    uint8 buf[HTTPCONN_READ_CHUNK];
    size_t n;
    while ((n = netsocketRecv(self->sock, buf, sizeof(buf), NULL, 0)) > 0)
        bufringWrite(&self->in, buf, n);

    // A completed parser stays in its terminal state until the next request resets it, so stepping
    // it with nothing in flight would report the previous response complete a second time -- with
    // the request already handed back, and handlers that dereference it. Only a request in flight
    // can produce a response, so that is the condition to parse under. Anything the peer sent while
    // idle stays in the ring for the next request to read.
    if (!self->req)
        return;

    for (;;) {
        HttpParseResult r = httpParserStep(self->parser, &self->in);

        if (r == HTTPP_NeedMore)
            break;

        if (r == HTTPP_Head) {
            if (self->req) {
                uint64 total = _httpBodyTotal(self->parser);
                atomicStore(int64, &self->req->progRecvTotal, (int64)total, Relaxed);
            }

            // Status first, then headers: a caller that only wants to know whether to keep going
            // can decide before the header block is even complete.
            httpconn_deliver(self, HTTPEV_Status, HTTPERR_None);
            httpconn_deliver(self, HTTPEV_Headers, HTTPERR_None);
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
            self->responseDone = true;

            // Ask the parser about reuse now, while it still remembers: the next request resets it,
            // and by then the answer is gone.
            if (!httpParserKeepAlive(self->parser))
                self->spent = true;

            cancelTimers(self);

            // Ahead of the terminal event, which hands the request back: after it there is no
            // request on this connection to report against.
            reportProgress(self, self->req, HTTPPROG_Recv, 0, true);

            httpconn_deliver(self, HTTPEV_Complete, HTTPERR_None);
            break;
        }

        // HTTPP_Error. The connection cannot be reused after a protocol error: we no longer know
        // where the next message would start.
        self->failed = true;
        self->spent  = true;
        cancelTimers(self);

        httpconn_deliver(self, HTTPEV_Error, self->parser->err);
        break;
    }
}

void HttpConn_setClosedHandler(_In_ HttpConn* self, HttpConnClosedCB cb, _In_opt_ void* ctx)
{
    self->closedCB  = cb;
    self->closedCtx = ctx;
}

bool HttpConn_setIdleTimeout(_In_ HttpConn* self, int64 us)
{
    NetFlow* flow = self->sock ? self->sock->flow : NULL;
    if (!flow)
        return false;

    if (self->idleTimer) {
        netflowCancelTimer(flow, self->idleTimer);
        self->idleTimer = 0;
    }

    if (us <= 0)
        return true;

    self->idleTimer = netflowAddTimer(flow, us, NTF_None);
    return self->idleTimer != 0;
}

void HttpConn_close(_In_ HttpConn* self)
{
    cancelTimers(self);
    self->spent = true;

    if (self->sock)
        netsocketClose(self->sock);
}

bool HttpConn_cancel(_In_ HttpConn* self)
{
    if (self->failed || self->spent)
        return false;

    // Set before closing, and read on the terminal path that the close provokes. The ordering is
    // the flow's: netsocketClose() queues the teardown, and nothing delivers NET_FlowClosed until
    // after this store has happened on this thread.
    self->aborted = true;

    httpconnClose(self);
    return true;
}

// Autogen begins -----
// clang-format off
void HttpConn__pump(_In_ HttpConn* self);
void HttpConn__deliver(_In_ HttpConn* self, HttpEventType type, HttpError err);
#include "httpconn.auto.inc"
// clang-format on
// Autogen ends -------

// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httprequest.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// Requests
//
// Almost all of this is bookkeeping: what to send, and where the answer lands. The one thing worth
// reading carefully is the body handling, because the request and response sides are deliberate
// mirror images -- see the StreamBuffer contract in cxhttp.h. cxhttp always takes the side opposite
// the caller, which is what lets an upload be one sbufFilePRegisterPull() call and a download one
// sbufFileCRegisterPush() call with no glue in between.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

_Use_decl_annotations_
void _httpReqReleaseBodyStream(HttpRequest* self)
{
    StreamBuffer* sb = self->reqBodyStream;
    if (!sb)
        return;
    self->reqBodyStream = NULL;

    // In pull mode this request drives the buffer, so it is the one that closes it. In push mode
    // the caller drives and this request is only the registered consumer -- detaching, not
    // closing, is what leaves the caller free to reuse the buffer for another request.
    bool drives = sbufIsPull(sb);

    // Detach first, so the end-of-stream callback cannot come back into a request that has already
    // stopped pointing at this buffer. In pull mode there is no consumer slot filled and this does
    // nothing.
    sbufCUnregister(sb);
    if (drives)
        sbufClose(sb);
    sbufRelease(&sb);
}

_Use_decl_annotations_
void _httpReqReleaseSink(HttpRequest* self)
{
    StreamBuffer* sb = self->respSink;
    if (!sb)
        return;
    self->respSink = NULL;

    // cxhttp is the producer here, so it is the side that ends the stream.
    sbufClose(sb);
    sbufRelease(&sb);
}

_objfactory_check HttpRequest* HttpRequest_create(HttpMethod method, _In_opt_ strref url)
{
    _httpInit();

    HttpUrl parsed;
    if (!httpUrlParse(&parsed, url))
        return NULL;   // an unparseable URL is not a request that could ever be sent

    HttpRequest* self;
    self = objInstCreate(HttpRequest);

    self->method     = method;
    self->url        = parsed;
    self->reqBodyLen = 0;

    if (!objInstInit(self)) {
        objRelease(&self);
        return NULL;
    }

    return self;
}

_objinit_guaranteed bool HttpRequest_init(_In_ HttpRequest* self)
{
    httpHeadersInit(&self->reqHeaders);
    httpHeadersInit(&self->respHeaders);
    self->progressInterval = HTTP_PROGRESS_INTERVAL;
    return true;
    // Autogen begins -----
    mutexInit(&self->exLock);
    return true;
    // Autogen ends -------
}

void HttpRequest_destroy(_In_ HttpRequest* self)
{
    // The three by-value struct members are invisible to codegen -- it emits destructors for cx
    // types it knows, and HttpUrl and HttpHeaders are plain aggregates -- so they are torn down by
    // hand here.
    httpUrlDestroy(&self->url);
    httpHeadersDestroy(&self->reqHeaders);
    httpHeadersDestroy(&self->respHeaders);

    // Each of these holds a reference of its own, taken when the request started pointing at the
    // buffer; both helpers are no-ops if a completed transfer already let go.
    _httpReqReleaseBodyStream(self);
    if (self->reqBodyOwn)
        bufDestroy(&self->reqBodyBuf);
    _httpReqReleaseSink(self);
    // Autogen begins -----
    strDestroy(&self->methodName);
    strDestroy(&self->reqBody);
    bufDestroy(&self->reqBodyBuf);
    strDestroy(&self->reason);
    strDestroy(&self->respBody);
    objRelease(&self->conn);
    objRelease(&self->dialSock);
    strDestroy(&self->redirectTo);
    mutexDestroy(&self->exLock);
    // Autogen ends -------
}

bool HttpRequest_setMethodName(_In_ HttpRequest* self, _In_opt_ strref name)
{
    if (strEmpty(name))
        return false;

    // Refusing a non-token verb here means nothing downstream has to wonder whether a method can
    // contain a space, which is one of the ways a request line gets split in two.
    if (!_httpIsToken(name))
        return false;

    strDup(&self->methodName, name);
    return true;
}

bool HttpRequest_setHeader(_In_ HttpRequest* self, _In_opt_ strref name, _In_opt_ strref value)
{
    return httpHeadersSet(&self->reqHeaders, name, value);
}

bool HttpRequest_addHeader(_In_ HttpRequest* self, _In_opt_ strref name, _In_opt_ strref value)
{
    return httpHeadersAdd(&self->reqHeaders, name, value);
}

// Shared tail of the body setters: record the content type, if one was given.
static void setContentType(HttpRequest* self, strref contentType)
{
    if (!strEmpty(contentType))
        httpHeadersSet(&self->reqHeaders, _SL("Content-Type"), contentType);
}

// Clear whatever body was set before, so two setters in a row cannot leave two bodies armed -- the
// writer would then have to guess which one the caller meant.
static void clearBody(HttpRequest* self)
{
    _httpReqReleaseBodyStream(self);

    strDestroy(&self->reqBody);
    if (self->reqBodyOwn)
        bufDestroy(&self->reqBodyBuf);
    self->reqBodyBuf      = NULL;
    self->reqBodyOwn      = false;
    self->reqBodyExternal = false;

    self->reqBodyLen  = 0;
    self->bodyConn    = NULL;
    self->bodySent    = 0;
    self->bodyRefused = false;
}

// Take the consumer side of a body buffer, whichever mode its producer registered in. Push mode
// exists so that a producer feeding the body from another thread -- an upload thread, or an
// sbufFileIn() -- reaches the connection through the notify rather than being waited on.
static bool adoptBody(HttpRequest* self, StreamBuffer* sb)
{
    // In pull mode cxhttp drives the buffer and has nothing to register; in push mode it is the
    // one being called back. Either way the request keeps a reference of its own.
    if (!sbufIsPull(sb) && !sbufCRegisterPush(sb, _httpReqBodyNotify, NULL, self))
        return false;

    self->reqBodyStream = sbufAcquire(sb);
    return true;
}

_Use_decl_annotations_
bool _httpReqArmBody(HttpRequest* self)
{
    if (self->reqBodyStream)
        return true;   // already armed, or the caller's own stream

    if (!self->reqBodyBuf && strEmpty(self->reqBody))
        return true;   // no body to send

    // Pull mode throughout: the bytes are already resident, so there is nothing to gain from
    // copying them into the ring before the socket is ready for them. Both adapters keep their own
    // hold on the data, and the request keeps the original either way, so a redirect can build a
    // second stream over the same bytes once this one has been drained.
    StreamBuffer* sb = sbufCreate(HTTP_BODY_CHUNK);

    bool ok;
    if (self->reqBodyBuf)
        ok = sbufBufPRegisterPull(sb, self->reqBodyBuf, false);
    else
        ok = sbufStrPRegisterPull(sb, self->reqBody);

    if (ok) {
        ok = adoptBody(self, sb);
        if (!ok)
            sbufClose(sb);   // nothing is going to read it, so let the producer go
    }

    // adoptBody() took a reference of its own, so the one sbufCreate() started with goes back
    // either way.
    sbufRelease(&sb);
    return ok;
}

bool HttpRequest_setBody(_In_ HttpRequest* self, _In_opt_ strref body, _In_opt_ strref contentType)
{
    clearBody(self);
    setContentType(self, contentType);

    strDup(&self->reqBody, body);
    self->reqBodyLen = (int64)strLen(self->reqBody);
    return true;
}

bool HttpRequest_setBodyBytes(_In_ HttpRequest* self, _In_ const uint8* data, size_t len,
                              _In_opt_ strref contentType)
{
    if (!data || len == 0) {
        clearBody(self);
        setContentType(self, contentType);
        return true;
    }

    // A body-sized allocation is the caller's size rather than ours, so it fails rather than
    // aborting the process.
    Buffer buf = bufTryCreate(len);
    if (!buf)
        return false;

    memcpy(buf->data, data, len);
    buf->len = len;

    return httprequestSetBodyBuffer(self, buf, true, contentType);
}

bool HttpRequest_setBodyBuffer(_In_ HttpRequest* self, _In_ Buffer buf, bool own,
                               _In_opt_ strref contentType)
{
    clearBody(self);
    setContentType(self, contentType);

    if (!buf)
        return true;

    self->reqBodyBuf = buf;
    self->reqBodyOwn = own;
    self->reqBodyLen = (int64)buf->len;
    return true;
}

bool HttpRequest_setBodyStream(_In_ HttpRequest* self, _In_ StreamBuffer* sb, int64 len,
                               _In_opt_ strref contentType)
{
    if (!sb)
        return false;

    clearBody(self);

    // cxhttp is the consumer of this buffer; the caller registered the producer. Registering here
    // rather than at send time means a caller that never sends still leaves the buffer in a
    // consistent state when the request is released.
    if (!adoptBody(self, sb))
        return false;

    self->reqBodyLen      = len;   // < 0 means chunked: the length is not known up front
    self->reqBodyExternal = true;
    setContentType(self, contentType);
    return true;
}

bool HttpRequest_setSink(_In_ HttpRequest* self, _In_ StreamBuffer* sb)
{
    if (!sb)
        return false;

    _httpReqReleaseSink(self);

    // The mirror image of setBodyStream: here cxhttp produces and the caller consumes. A push
    // producer registers nothing, so all this takes is a reference of its own.
    self->respSink = sbufAcquire(sb);
    return true;
}

bool HttpRequest_cancel(_In_ HttpRequest* self)
{
    HttpConn* conn     = NULL;
    NetSocket* dialing = NULL;
    bool claimed       = false;

    // The binding and the flag are read and written together, because they are one decision. A
    // recycler that pooled this connection between "read conn" and "set cancelled" would leave us
    // closing a connection now serving somebody else's request; taking both under one lock is what
    // makes that impossible rather than unlikely.
    //
    // No early return out of this block -- withMutex is a scope wrapper, and returning through it
    // would walk away holding the lock.
    withMutex (&self->exLock) {
        if (self->client) {
            claimed         = true;
            self->cancelled = true;

            if (self->conn)
                conn = objAcquire(self->conn);
            else if (self->dialSock)
                dialing = objAcquire(self->dialSock);
        }
    }

    // Outside the lock: both of these reach the socket layer, and a per-request mutex has no
    // business being held across that.
    if (conn) {
        httpconnCancel(conn);
        objRelease(&conn);
    } else if (dialing) {
        // No connection yet, so there is nothing to tell -- closing the half-open socket is the
        // whole of it, and the dial handlers turn the resulting teardown into the terminal event.
        netsocketClose(dialing);
        objRelease(&dialing);
    }

    // Claimed with neither in hand means the exchange is momentarily between transports, which a
    // redirect is. startExchange() checks the flag before it commits to the next hop.
    return claimed;
}

uint64 HttpRequest_sentBytes(_In_ HttpRequest* self)
{
    return atomicLoad(uint64, &self->progSent, Relaxed);
}

uint64 HttpRequest_recvBytes(_In_ HttpRequest* self)
{
    return atomicLoad(uint64, &self->progRecv, Relaxed);
}

int64 HttpRequest_sentTotal(_In_ HttpRequest* self)
{
    return atomicLoad(int64, &self->progSendTotal, Relaxed);
}

int64 HttpRequest_recvTotal(_In_ HttpRequest* self)
{
    return atomicLoad(int64, &self->progRecvTotal, Relaxed);
}

// Autogen begins -----
// clang-format off
#include "httprequest.auto.inc"
// clang-format on
// Autogen ends -------

// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpserverreq.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// The server's request/response object
//
// Bookkeeping on the received half, and a small builder on the response half. The one rule worth
// stating is that the framing headers are written from the body that was actually set rather than
// from anything the application declared -- the same rule the client's request writer follows, and
// for the same reason: a message whose declared length disagrees with its real one is how two
// parsers end up disagreeing about where the next message starts.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include "httpserverconn.h"

#include <cx/time/clock.h>

_objfactory_guaranteed HttpServerRequest* HttpServerRequest_create()
{
    _httpInit();

    HttpServerRequest* self;
    self = objInstCreate(HttpServerRequest);

    objInstInit(self);

    return self;
}

_objinit_guaranteed bool HttpServerRequest_init(_In_ HttpServerRequest* self)
{
    httpHeadersInit(&self->headers);
    httpHeadersInit(&self->respHeaders);
    self->status           = HTTP_OK;
    self->progressInterval = HTTP_PROGRESS_INTERVAL;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void HttpServerRequest_destroy(_In_ HttpServerRequest* self)
{
    // Plain aggregates are invisible to codegen, so they come down by hand.
    httpHeadersDestroy(&self->headers);
    httpHeadersDestroy(&self->respHeaders);
    // Autogen begins -----
    objDestroyWeak(&self->conn);
    strDestroy(&self->methodName);
    strDestroy(&self->target);
    strDestroy(&self->path);
    strDestroy(&self->query);
    strDestroy(&self->body);
    strDestroy(&self->reason);
    strDestroy(&self->respBody);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// Building the response
// ---------------------------------------------------------------------------------------------

// The reason phrase for a status, or empty for one this does not know. Nothing reads a reason
// phrase -- RFC 9112 says a client must ignore it -- so an unrecognized code simply travels without
// one rather than needing a table of every status ever registered.
strref _httpReasonPhrase(uint16 status)
{
    switch (status) {
    case 100: return _S "Continue";
    case 101: return _S "Switching Protocols";
    case 200: return _S "OK";
    case 201: return _S "Created";
    case 202: return _S "Accepted";
    case 204: return _S "No Content";
    case 206: return _S "Partial Content";
    case 301: return _S "Moved Permanently";
    case 302: return _S "Found";
    case 303: return _S "See Other";
    case 304: return _S "Not Modified";
    case 307: return _S "Temporary Redirect";
    case 308: return _S "Permanent Redirect";
    case 400: return _S "Bad Request";
    case 401: return _S "Unauthorized";
    case 403: return _S "Forbidden";
    case 404: return _S "Not Found";
    case 405: return _S "Method Not Allowed";
    case 408: return _S "Request Timeout";
    case 409: return _S "Conflict";
    case 411: return _S "Length Required";
    case 413: return _S "Content Too Large";
    case 414: return _S "URI Too Long";
    case 415: return _S "Unsupported Media Type";
    case 417: return _S "Expectation Failed";
    case 429: return _S "Too Many Requests";
    case 431: return _S "Request Header Fields Too Large";
    case 500: return _S "Internal Server Error";
    case 501: return _S "Not Implemented";
    case 502: return _S "Bad Gateway";
    case 503: return _S "Service Unavailable";
    case 504: return _S "Gateway Timeout";
    case 505: return _S "HTTP Version Not Supported";
    default: return NULL;
    }
}

// True for a status whose response carries no body however it is framed (RFC 9110 6.4.1). A
// Content-Length on one of these is not merely redundant, it is a framing conflict: the peer is
// required to ignore the length and read the next message immediately.
bool _httpStatusHasNoBody(uint16 status)
{
    return (status >= 100 && status < 200) || status == 204 || status == 304;
}

void HttpServerRequest__buildHead(_In_ HttpServerRequest* self, _Inout_ string* out, bool close)
{
    strAppend(out, _SL("HTTP/1.1 "));

    string code = 0;
    strFromInt64(&code, self->status, 10);
    strAppend(out, code);
    strDestroy(&code);

    strref reason = !strEmpty(self->reason) ? (strref)self->reason
                                            : _httpReasonPhrase(self->status);
    if (!strEmpty(reason)) {
        strAppendChar(out, ' ');
        strAppend(out, reason);
    }
    strAppend(out, _SL("\r\n"));

    // Date is required of any server that has a clock, and it is what lets a cache reason about
    // freshness at all.
    if (!httpHeadersHas(&self->respHeaders, _SL("Date"))) {
        string date = 0;
        if (httpDateFormat(&date, clockWall())) {
            strAppend(out, _SL("Date: "));
            strAppend(out, date);
            strAppend(out, _SL("\r\n"));
        }
        strDestroy(&date);
    }

    if (!httpHeadersHas(&self->respHeaders, _SL("Server")))
        strAppend(out, _SL("Server: cxhttp\r\n"));

    // Framing is decided here rather than taken from the application, so the two can never
    // disagree. Anything the application set is dropped on the way past.
    for (int32 i = 0; i < httpHeadersCount(&self->respHeaders); i++) {
        strref name = self->respHeaders.names.a[i];
        if (strEqi(name, _SL("Content-Length")) || strEqi(name, _SL("Transfer-Encoding")) ||
            strEqi(name, _SL("Connection")))
            continue;

        strAppend(out, name);
        strAppend(out, _SL(": "));
        strAppend(out, self->respHeaders.values.a[i]);
        strAppend(out, _SL("\r\n"));
    }

    strAppend(out, close ? _SL("Connection: close\r\n") : _SL("Connection: keep-alive\r\n"));

    if (!_httpStatusHasNoBody(self->status)) {
        if (self->respStream && self->respStreamLen < 0) {
            // A streamed body of unknown length. Chunked is the only framing that can end it
            // without closing the connection.
            strAppend(out, _SL("Transfer-Encoding: chunked\r\n"));
        } else {
            uint64 len = self->respStream ? (uint64)self->respStreamLen : (uint64)strLen(self->respBody);
            string n   = 0;
            strFromUInt64(&n, len, 10);
            strAppend(out, _SL("Content-Length: "));
            strAppend(out, n);
            strAppend(out, _SL("\r\n"));
            strDestroy(&n);
        }
    }

    strAppend(out, _SL("\r\n"));
}

// ---------------------------------------------------------------------------------------------
// The application's side
// ---------------------------------------------------------------------------------------------

bool HttpServerRequest_setStatus(_In_ HttpServerRequest* self, uint16 status)
{
    if (self->responded || status < 100 || status > 999)
        return false;

    self->status = status;
    return true;
}

bool HttpServerRequest_setReason(_In_ HttpServerRequest* self, _In_opt_ strref reason)
{
    if (self->responded)
        return false;

    // A reason phrase goes on the status line, so a CR or LF in one would end the line early and
    // let the rest be read as headers of the application's choosing.
    striter it;
    striBorrow(&it, reason);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c < 0x20 || c == 0x7f)
            return false;
    }

    strDup(&self->reason, reason);
    return true;
}

// Neither a field name nor a field value may carry anything that would end the line it sits on.
// This is response splitting, and the usual way in is an application echoing a query parameter into
// a Location or a Set-Cookie.
static bool validHeader(strref name, strref value)
{
    if (!_httpIsToken(name))
        return false;

    striter it;
    striBorrow(&it, value);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c == '\r' || c == '\n' || c == 0)
            return false;
    }
    return true;
}

bool HttpServerRequest_setHeader(_In_ HttpServerRequest* self, _In_opt_ strref name,
                                 _In_opt_ strref value)
{
    if (self->responded || !validHeader(name, value))
        return false;

    return httpHeadersSet(&self->respHeaders, name, value);
}

bool HttpServerRequest_addHeader(_In_ HttpServerRequest* self, _In_opt_ strref name,
                                 _In_opt_ strref value)
{
    if (self->responded || !validHeader(name, value))
        return false;

    return httpHeadersAdd(&self->respHeaders, name, value);
}

bool HttpServerRequest_respond(_In_ HttpServerRequest* self, _In_opt_ strref body,
                               _In_opt_ strref contentType)
{
    if (self->responded)
        return false;

    // A response to HEAD carries the headers its GET would, Content-Length included, and no body.
    // Setting the body and dropping it at the write is what keeps the length honest.
    if (!strEmpty(body) && !_httpStatusHasNoBody(self->status)) {
        strDup(&self->respBody, body);
        if (!strEmpty(contentType))
            httpHeadersSet(&self->respHeaders, _SL("Content-Type"), contentType);
    }

    HttpServerConn* conn = objAcquireFromWeak(HttpServerConn, self->conn);
    if (!conn) {
        // The connection died while the application was thinking. There is nowhere to write, and
        // nothing has gone wrong that anyone can still be told about.
        self->responded = true;
        return false;
    }

    self->responded = true;
    bool ok         = httpsrvconn_respond(conn, self);
    objRelease(&conn);
    return ok;
}

bool HttpServerRequest_respondStatus(_In_ HttpServerRequest* self, uint16 status)
{
    if (!httpsrvreqSetStatus(self, status))
        return false;

    return httpsrvreqRespond(self, NULL, NULL);
}

bool HttpServerRequest_keepAlive(_In_ HttpServerRequest* self)
{
    // 1.0 closes unless the client asked for otherwise; 1.1 persists unless it asked to close.
    if (self->version == HTTPVER_1_0)
        return httpHeadersHasToken(&self->headers, _SL("Connection"), _SL("keep-alive"));

    return !httpHeadersHasToken(&self->headers, _SL("Connection"), _SL("close"));
}

bool HttpServerRequest_respondStream(_In_ HttpServerRequest* self, _In_ StreamBuffer* sb,
                                     int64 len, _In_opt_ strref contentType)
{
    if (!sb || self->responded)
        return false;

    // A status that carries no body has nowhere to put this, and a HEAD wants the length without
    // the bytes. Both are answered as an empty response of the declared length rather than being
    // refused, which is what keeps Content-Length honest for the HEAD case.
    if (!_httpStatusHasNoBody(self->status)) {
        self->respStream    = sb;
        self->respStreamLen = len;
        if (!strEmpty(contentType))
            httpHeadersSet(&self->respHeaders, _SL("Content-Type"), contentType);
    }

    return httpsrvreqRespond(self, NULL, NULL);
}

bool HttpServerRequest_setSink(_In_ HttpServerRequest* self, _In_ StreamBuffer* sb)
{
    if (!sb || self->sink || self->responded)
        return false;

    // cxhttp is the producer into this buffer; the application registered the consumer. Push mode
    // is the only one that fits: bytes arrive when the client sends them, not when a reader asks.
    if (!sbufPRegisterPush(sb, NULL, NULL))
        return false;

    self->sink = sb;
    return true;
}

bool HttpServerRequest_sendContinue(_In_ HttpServerRequest* self)
{
    if (!self->expectContinue || self->continueSent || self->responded)
        return false;

    HttpServerConn* conn = objAcquireFromWeak(HttpServerConn, self->conn);
    if (!conn)
        return false;

    self->continueSent = true;
    bool ok            = httpsrvconn_sendContinue(conn);
    objRelease(&conn);
    return ok;
}

uint64 HttpServerRequest_sentBytes(_In_ HttpServerRequest* self)
{
    return (uint64)atomicLoad(uintptr, &self->progSent, Relaxed);
}

uint64 HttpServerRequest_recvBytes(_In_ HttpServerRequest* self)
{
    return (uint64)atomicLoad(uintptr, &self->progRecv, Relaxed);
}

int64 HttpServerRequest_sentTotal(_In_ HttpServerRequest* self)
{
    return (int64)atomicLoad(intptr, &self->progSendTotal, Relaxed);
}

int64 HttpServerRequest_recvTotal(_In_ HttpServerRequest* self)
{
    return (int64)atomicLoad(intptr, &self->progRecvTotal, Relaxed);
}

// Autogen begins -----
// clang-format off
void HttpServerRequest__buildHead(_In_ HttpServerRequest* self, _Inout_ string* out, bool close);
#include "httpserverreq.auto.inc"
// clang-format on
// Autogen ends -------

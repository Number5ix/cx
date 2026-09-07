#include "http3_private.h"

#include "httpserverreq.h"

#include <cx/time/clock.h>

#include <string.h>

// Where an HTTP/3 stream becomes an HTTP message.
//
// Two halves: the frame reader that turns a stream of bytes into field sections and body runs, and
// the translation between a field section and the request and response objects the rest of cxhttp
// already uses. Nothing above this layer knows which protocol carried a message, which is the whole
// point -- HTTP/3 is a second framing layer under an unchanged object model, not a second stack.

// ---------------------------------------------------------------------------------------------
// The frame reader
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _h3MsgInit(H3MsgReader* r, bool server, const HttpLimits* limits)
{
    memset(r, 0, sizeof(*r));
    _h3FrameReaderInit(&r->fr);
    httpHeadersInit(&r->headers);

    r->server = server;
    if (limits) {
        r->maxHeaderCount = limits->maxHeaderCount;
        r->maxFieldSize   = limits->maxHeadBytes;
        r->maxBody        = limits->maxBodyBytes;
    }
}

_Use_decl_annotations_
void _h3MsgDestroy(H3MsgReader* r)
{
    httpHeadersDestroy(&r->headers);
    xaDestroy(&r->fields);
    r->fieldsLen = 0;
}

static H3MsgResult msgFail(_Inout_ H3MsgReader* r, uint64 err)
{
    r->err = err;
    _h3FrameReaderFail(&r->fr);
    return H3MSG_Error;
}

// Whether a frame type may appear on a request or response stream at all.
static uint64 streamFrameAllowed(_In_ const H3MsgReader* r, uint64 type)
{
    switch (type) {
    case H3_FRAME_HEADERS:
        return 0;

    case H3_FRAME_DATA:
        // A body with nothing to say what it is the body of.
        return r->headDone ? 0 : H3ERR_FRAME_UNEXPECTED;

    case H3_FRAME_PUSH_PROMISE:
        // A server may not receive one at all. A client may only after saying how many pushes it
        // will take, which cxhttp never does -- never sending MAX_PUSH_ID is the whole of refusing
        // server push.
        return r->server ? H3ERR_FRAME_UNEXPECTED : H3ERR_ID_ERROR;

    case H3_FRAME_SETTINGS:
    case H3_FRAME_GOAWAY:
    case H3_FRAME_MAX_PUSH_ID:
    case H3_FRAME_CANCEL_PUSH:
        // Connection-wide frames, which belong on the control stream and nowhere else.
        return H3ERR_FRAME_UNEXPECTED;

    default:
        // The HTTP/2 frame types with no HTTP/3 counterpart are reserved rather than merely
        // unknown, so a peer sending one is speaking the wrong protocol.
        if (type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09)
            return H3ERR_FRAME_UNEXPECTED;
        return 0;   // unknown or reserved for greasing: read and discarded
    }
}

_Use_decl_annotations_
H3MsgResult _h3MsgStep(H3MsgReader* r, BufRing* src)
{
    for (;;) {
        H3FrameResult fr = _h3FrameReaderStep(&r->fr, src);

        switch (fr) {
        case H3FR_NeedMore:
            return H3MSG_NeedMore;

        case H3FR_Error:
            return H3MSG_Error;

        case H3FR_Header: {
            uint64 err = streamFrameAllowed(r, r->fr.type);
            if (err != 0)
                return msgFail(r, err);

            xaDestroy(&r->fields);
            r->fieldsLen = 0;
            r->inFields  = (r->fr.type == H3_FRAME_HEADERS);

            // Marked from the header rather than from the first payload byte, so that an empty
            // DATA frame still makes the next field section trailers rather than a second head.
            if (r->fr.type == H3_FRAME_DATA)
                r->bodyStarted = true;

            if (r->inFields) {
                // A field section is held whole before it can be decoded, so its declared length
                // is the one thing a peer could use to make this endpoint allocate on its behalf.
                // Refused from the header alone, before a byte of it has been read.
                if (r->maxFieldSize > 0 && r->fr.len > r->maxFieldSize)
                    return msgFail(r, H3ERR_EXCESSIVE_LOAD);
                if (r->fr.len > 0)
                    r->fields = xaAlloc((size_t)r->fr.len);
            }
            break;
        }

        case H3FR_Payload:
            if (r->inFields) {
                r->fieldsLen += bufringRead(src, r->fields + r->fieldsLen, r->fr.avail);
                break;
            }

            if (r->fr.type != H3_FRAME_DATA) {
                bufringSkip(src, r->fr.avail);   // an unknown frame's payload
                break;
            }

            r->bodyTotal += r->fr.avail;
            if (r->maxBody > 0 && r->bodyTotal > r->maxBody)
                return msgFail(r, H3ERR_EXCESSIVE_LOAD);

            r->bodyReady = r->fr.avail;
            return H3MSG_Body;

        case H3FR_Complete: {
            if (!r->inFields)
                break;
            r->inFields = false;

            uint64 err = _qpackDecode(&r->headers, r->fields, r->fieldsLen, r->maxHeaderCount,
                                      r->maxFieldSize);
            xaDestroy(&r->fields);
            r->fieldsLen = 0;
            if (err != 0)
                return msgFail(r, err);

            // A section before any body is the message's own; one after it is trailers. That is
            // the only distinction the frame layer can make -- whether a second section before the
            // body is an interim response or a malformed request is the caller's to decide.
            if (r->bodyStarted)
                return H3MSG_Trailers;

            r->headDone = true;
            return H3MSG_Head;
        }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Field sections
// ---------------------------------------------------------------------------------------------

STR_CONST(kConnection, "connection");
STR_CONST(kKeepAlive, "keep-alive");
STR_CONST(kTransferEncoding, "transfer-encoding");
STR_CONST(kUpgrade, "upgrade");
STR_CONST(kProxyConnection, "proxy-connection");
STR_CONST(kTE, "te");
STR_CONST(kTrailers, "trailers");
STR_CONST(kHost, "host");
STR_CONST(kContentLength, "content-length");
STR_CONST(kDate, "date");
STR_CONST(kServer, "server");
STR_CONST(kServerName, "cxhttp");

STR_CONST(kPMethod, ":method");
STR_CONST(kPScheme, ":scheme");
STR_CONST(kPAuthority, ":authority");
STR_CONST(kPPath, ":path");
STR_CONST(kPStatus, ":status");
STR_CONST(kPProtocol, ":protocol");

_Use_decl_annotations_
bool _h3FieldForbidden(strref name)
{
    return strEqi(name, kConnection) || strEqi(name, kKeepAlive) ||
           strEqi(name, kTransferEncoding) || strEqi(name, kUpgrade) ||
           strEqi(name, kProxyConnection);
}

// Whether a pseudo-header name is one this endpoint understands. An unknown one is malformed
// rather than ignorable: pseudo-headers are the message's own structure, so a peer using one this
// endpoint has never heard of is describing a message it cannot read.
static bool knownPseudo(_In_opt_ strref name)
{
    return strEq(name, kPMethod) || strEq(name, kPScheme) || strEq(name, kPAuthority) ||
           strEq(name, kPPath) || strEq(name, kPStatus) || strEq(name, kPProtocol);
}

_Use_decl_annotations_
uint64 _h3SplitFields(HttpHeaders* out, sa_string* pseudoNames, sa_string* pseudoValues,
                      const HttpHeaders* fields)
{
    httpHeadersInit(out);

    int32 n     = httpHeadersCount(fields);
    bool ordinary = false;

    for (int32 i = 0; i < n; i++) {
        string name  = fields->names.a[i];
        string value = fields->values.a[i];

        if (strGetChar(name, 0) == ':') {
            // Every pseudo-header precedes every ordinary field, always. A peer that interleaves
            // them is one whose message this endpoint has no agreed reading of.
            if (ordinary || !knownPseudo(name))
                return H3ERR_MESSAGE_ERROR;

            // Each appears at most once. A duplicate :path is two different requests in one
            // message, and choosing either is how a proxy and its origin end up serving
            // different things.
            for (int32 j = 0; j < saSize(*pseudoNames); j++) {
                if (strEq(pseudoNames->a[j], name))
                    return H3ERR_MESSAGE_ERROR;
            }

            saPush(pseudoNames, string, name);
            saPush(pseudoValues, string, value);
            continue;
        }

        ordinary = true;

        if (_h3FieldForbidden(name))
            return H3ERR_MESSAGE_ERROR;

        // TE survives, but only saying `trailers`. Every other value asks for a transfer coding,
        // and HTTP/3 has none.
        if (strEqi(name, kTE) && !strEqi(value, kTrailers))
            return H3ERR_MESSAGE_ERROR;

        if (!httpHeadersAdd(out, name, value))
            return H3ERR_INTERNAL_ERROR;
    }

    return 0;
}

// The value of a pseudo-header in a split section, or NULL if it was absent.
static strref pseudoGet(_In_ const sa_string* names, _In_ const sa_string* values, strref want)
{
    for (int32 i = 0; i < saSize(*names); i++) {
        if (strEq(names->a[i], want))
            return values->a[i];
    }
    return NULL;
}

// The method name as an HttpMethod, or HTTP_MethodOther. Case-sensitive, because a method name is.
static HttpMethod methodFromName(strref name)
{
    if (strEq(name, _SL("GET")))
        return HTTP_Get;
    if (strEq(name, _SL("HEAD")))
        return HTTP_Head;
    if (strEq(name, _SL("POST")))
        return HTTP_Post;
    if (strEq(name, _SL("PUT")))
        return HTTP_Put;
    if (strEq(name, _SL("DELETE")))
        return HTTP_Delete;
    if (strEq(name, _SL("CONNECT")))
        return HTTP_Connect;
    if (strEq(name, _SL("OPTIONS")))
        return HTTP_Options;
    if (strEq(name, _SL("TRACE")))
        return HTTP_Trace;
    if (strEq(name, _SL("PATCH")))
        return HTTP_Patch;
    return HTTP_MethodOther;
}

_Use_decl_annotations_
uint64 _h3ReqFromFields(HttpServerRequest* req, const HttpHeaders* fields)
{
    sa_string pn, pv;
    saInit(&pn, string, 6);
    saInit(&pv, string, 6);

    HttpHeaders ordinary;
    uint64 err = _h3SplitFields(&ordinary, &pn, &pv, fields);
    if (err != 0)
        goto out;

    strref method    = pseudoGet(&pn, &pv, kPMethod);
    strref scheme    = pseudoGet(&pn, &pv, kPScheme);
    strref path      = pseudoGet(&pn, &pv, kPPath);
    strref authority = pseudoGet(&pn, &pv, kPAuthority);

    // A response's pseudo-header has no business in a request.
    if (pseudoGet(&pn, &pv, kPStatus)) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    if (strEmpty(method)) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    HttpMethod m = methodFromName(method);

    // CONNECT is the one method with no scheme and no path, and cxhttp does not implement it --
    // there is no tunnel for it to become. Refusing it here is what stops the ordinary request
    // path from being handed a message it has no target for.
    if (m == HTTP_Connect) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    if (strEmpty(scheme) || strEmpty(path)) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    // The target is untrusted and arrives in the same forms HTTP/1.1 puts on the request line, so
    // it is parsed by the same code -- which is also what rejects a path with a fragment or in a
    // form a server may not accept.
    HttpUrl u;
    if (!httpUrlParseTarget(&u, path)) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    req->method  = m;
    req->version = HTTPVER_3;
    strDup(&req->methodName, method);
    strDup(&req->target, path);
    strDup(&req->path, u.path);
    strDup(&req->query, u.query);
    httpUrlDestroy(&u);

    // :authority is what Host was, so it arrives as Host: an application that reads the header
    // does not have to know which protocol carried the request. RFC 9114 section 4.3.1 lets a
    // request carry both as long as they agree, and the one this endpoint keeps is :authority.
    httpHeadersRemove(&ordinary, kHost);
    if (!strEmpty(authority))
        httpHeadersSet(&req->headers, kHost, authority);

    for (int32 i = 0; i < httpHeadersCount(&ordinary); i++)
        httpHeadersAdd(&req->headers, ordinary.names.a[i], ordinary.values.a[i]);

out:
    httpHeadersDestroy(&ordinary);
    saDestroy(&pn);
    saDestroy(&pv);
    return err;
}

_Use_decl_annotations_
uint64 _h3RespFromFields(uint16* status, HttpHeaders* out, const HttpHeaders* fields)
{
    *status = 0;

    sa_string pn, pv;
    saInit(&pn, string, 6);
    saInit(&pv, string, 6);

    uint64 err = _h3SplitFields(out, &pn, &pv, fields);
    if (err != 0)
        goto out;

    strref st = pseudoGet(&pn, &pv, kPStatus);

    // A response is :status and nothing else. A request's pseudo-headers on one mean the peer has
    // its roles confused.
    if (strEmpty(st) || pseudoGet(&pn, &pv, kPMethod) || pseudoGet(&pn, &pv, kPScheme) ||
        pseudoGet(&pn, &pv, kPPath) || pseudoGet(&pn, &pv, kPAuthority)) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    // Exactly three digits, which is what makes a status a status. strToUInt16 would take "+2" and
    // " 200" as well, and neither is a status code.
    if (strLen(st) != 3) {
        err = H3ERR_MESSAGE_ERROR;
        goto out;
    }

    uint16 code = 0;
    for (uint32 i = 0; i < 3; i++) {
        uint8 c = strGetChar(st, i);
        if (c < '0' || c > '9') {
            err = H3ERR_MESSAGE_ERROR;
            goto out;
        }
        code = (uint16)(code * 10 + (c - '0'));
    }

    *status = code;

out:
    saDestroy(&pn);
    saDestroy(&pv);
    return err;
}

// Copy the fields an application set, dropping the ones HTTP/3 forbids and the framing ones cxhttp
// decides for itself. The same rule the HTTP/1.1 writer follows: what goes out describes the
// message that is actually being sent, never what the application declared about it.
static bool copyAppFields(_Inout_ HttpHeaders* out, _In_ const HttpHeaders* headers)
{
    for (int32 i = 0; i < httpHeadersCount(headers); i++) {
        strref name = headers->names.a[i];

        if (_h3FieldForbidden(name) || strEqi(name, kContentLength) || strEqi(name, kHost))
            continue;
        if (strEqi(name, kTE) && !strEqi(headers->values.a[i], kTrailers))
            continue;
        if (strGetChar(name, 0) == ':')
            continue;   // an application does not get to write its own pseudo-headers

        if (!httpHeadersAdd(out, name, headers->values.a[i]))
            return false;
    }
    return true;
}

// Add the Content-Length that describes a body of `bodyLen`, if one belongs. A negative length
// means the body ends with the stream and nothing is announced, which is what HTTP/3 has instead
// of chunked transfer coding.
static bool addContentLength(_Inout_ HttpHeaders* out, int64 bodyLen)
{
    if (bodyLen < 0)
        return true;

    string n = 0;
    strFromUInt64(&n, (uint64)bodyLen, 10);
    bool ok = httpHeadersAdd(out, kContentLength, n);
    strDestroy(&n);
    return ok;
}

_Use_decl_annotations_
bool _h3ReqToFields(HttpHeaders* out, strref method, const HttpUrl* url,
                    const HttpHeaders* headers, int64 bodyLen)
{
    httpHeadersInit(out);

    string target = 0;
    string auth   = 0;
    bool ok       = httpUrlTarget(&target, url) && httpUrlHostHeader(&auth, url);

    ok = ok && httpHeadersAdd(out, kPMethod, method) &&
         httpHeadersAdd(out, kPScheme, url->scheme) && httpHeadersAdd(out, kPAuthority, auth) &&
         httpHeadersAdd(out, kPPath, target);

    strDestroy(&target);
    strDestroy(&auth);

    return ok && copyAppFields(out, headers) && addContentLength(out, bodyLen);
}

_Use_decl_annotations_
bool _h3RespToFields(HttpHeaders* out, uint16 status, const HttpHeaders* headers, int64 bodyLen)
{
    httpHeadersInit(out);

    string code = 0;
    strFromUInt64(&code, status, 10);
    bool ok = httpHeadersAdd(out, kPStatus, code);
    strDestroy(&code);

    if (!ok || !copyAppFields(out, headers))
        return false;

    // The same two fields the HTTP/1.1 writer adds, for the same reasons: Date is what lets a
    // cache reason about freshness at all, and both are only added when the application has not.
    if (!httpHeadersHas(out, kDate)) {
        string date = 0;
        if (httpDateFormat(&date, clockWall()))
            httpHeadersAdd(out, kDate, date);
        strDestroy(&date);
    }
    if (!httpHeadersHas(out, kServer))
        httpHeadersAdd(out, kServer, kServerName);

    // A status that carries no body under any framing must not announce a length either.
    if (_httpStatusHasNoBody(status))
        return true;

    return addContentLength(out, bodyLen);
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

// Put one whole frame on a stream, header and payload together.
//
// It has to be one netflowSend(), because a send is all or nothing: two calls could have the
// header taken and the payload refused, and the peer would then read the bytes of whatever is
// written next as this frame's payload -- silent corruption rather than a failure, since a DATA
// header is a valid start to a body.
//
// Measuring the room first and then sending in two parts does not avoid that. A QUIC connection's
// send window is shared by every stream on it, so between the measurement and the second call
// another stream's worker can take the room the first call left. Only one worker at a time runs on
// a given flow; nothing holds still across the whole connection.
//
// The send is attempted even when there is plainly no room, because being refused is how a stream
// is told a sender is waiting and how much it is waiting for. A sender that measures the window
// and stays quiet is never woken again.
static bool sendFrame(_In_ NetFlow* flow, _In_reads_bytes_(hlen) const uint8* hdr, size_t hlen,
                      _In_reads_bytes_opt_(len) const uint8* data, size_t len)
{
    uint8* frame = xaAlloc(hlen + len);
    memcpy(frame, hdr, hlen);
    if (len > 0)
        memcpy(frame + hlen, data, len);

    bool ok = netflowSend(flow, frame, hlen + len, 0);
    xaFree(frame);

    return ok;
}

_Use_decl_annotations_
bool _h3SendFields(NetFlow* flow, const HttpHeaders* fields, bool fin)
{
    string payload = 0;
    if (!_qpackEncode(&payload, fields)) {
        strDestroy(&payload);
        return false;
    }

    uint32 plen = strLen(payload);
    uint8 hdr[H3_FRAME_HDR_MAX];
    size_t hlen = _h3WrFrameHdr(hdr, sizeof(hdr), H3_FRAME_HEADERS, plen);

    bool ok = hlen > 0;
    if (ok)
        ok = sendFrame(flow, hdr, hlen, (const uint8*)strPC(&payload), plen);

    strDestroy(&payload);

    if (ok && fin)
        netquicFinish(flow);
    return ok;
}

_Use_decl_annotations_
bool _h3SendData(NetFlow* flow, const uint8* data, size_t len)
{
    if (len == 0)
        return true;

    uint8 hdr[H3_FRAME_HDR_MAX];
    size_t hlen = _h3WrFrameHdr(hdr, sizeof(hdr), H3_FRAME_DATA, len);
    if (hlen == 0)
        return false;

    return sendFrame(flow, hdr, hlen, data, len);
}

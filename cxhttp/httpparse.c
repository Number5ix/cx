// ---------------------------------------------------------------------------------------------
// Message parsing
//
// One state machine for both directions. A response and a request differ only in their start line
// and in two framing rules, so writing this once means the client and the server cannot disagree
// about what a chunked body is -- which is exactly the disagreement request smuggling exploits.
//
// Nothing here ever requires a whole message to be resident. Each step consumes what it can from
// the caller's ring and reports what that produced; on HTTPP_Body, the decoded bytes are still
// sitting at the head of that same ring for the caller to drain. A 200-byte API reply and a
// gigabyte download take the same path.
//
// Line-terminator policy is deliberately asymmetric, and the reason is worth stating:
//
//   head (start line, header fields)   CRLF or a bare LF
//   chunked framing (size, data CRLF)  CRLF only
//
// Leniency in the head is what every implementation does and is not by itself a way to make two
// parsers disagree. Leniency in chunk framing is: a bare LF in a chunk-size line is one of the
// classic ways to get a proxy and an origin to see different message boundaries. It costs nothing
// to be strict exactly where it matters.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/container.h>
#include <cx/string.h>

// Internal-only step result: this pass consumed something but produced nothing the caller has to
// act on. httpParserStep() loops on it, so a caller only ever sees the four public results and
// HTTPP_NeedMore genuinely means "starved" rather than "call me again". Conflating those two is a
// trap: a caller that stops on the first NeedMore would stop after the status line.
//
// Every path that answers this has consumed at least one line, which is what makes the loop finite.
#define STEP_Again ((HttpParseResult) - 1)

_Use_decl_annotations_
void httpLimitsDefault(HttpLimits* out)
{
    // Sized for the deployment cxhttp is written for -- an API behind a proxy, and a client
    // fetching files. Generous enough that no legitimate peer trips them, small enough that a
    // hostile one cannot make us allocate on its behalf.
    out->maxLineLen     = 8 * 1024;
    out->maxHeaderCount = 128;
    out->maxHeadBytes   = 64 * 1024;
    out->maxBodyBytes   = 0;   // unlimited: a download has no natural bound, so the caller sets one
    out->maxChunkSize   = 64 * 1024 * 1024;
}

_Use_decl_annotations_
void httpParserInit(HttpParser* p, bool isRequest, const HttpLimits* limits)
{
    _httpInit();

    memset(p, 0, sizeof(*p));
    p->isRequest = isRequest;

    if (limits)
        p->limits = *limits;
    else
        httpLimitsDefault(&p->limits);

    httpHeadersInit(&p->headers);
}

_Use_decl_annotations_
void httpParserReset(HttpParser* p)
{
    strDestroy(&p->reason);
    strDestroy(&p->methodName);
    strDestroy(&p->target);
    httpHeadersClear(&p->headers);

    p->state          = HTTPS_Start;
    p->err            = HTTPERR_None;
    p->headDone       = false;
    p->version        = HTTPVER_Unknown;
    p->status         = 0;
    p->method         = HTTP_MethodOther;
    p->noBody         = false;
    p->chunked        = false;
    p->hasLength      = false;
    p->closeDelimited = false;
    p->length         = 0;
    p->bodyTotal      = 0;
    p->headBytes      = 0;
    p->headerCount    = 0;
    p->bodyReady      = 0;
}

_Use_decl_annotations_
void httpParserDestroy(HttpParser* p)
{
    strDestroy(&p->reason);
    strDestroy(&p->methodName);
    strDestroy(&p->target);
    httpHeadersDestroy(&p->headers);
}

// Move the parser to its terminal failure state. Every rejection funnels through here so there is
// one place to look when asking what a parser can refuse and why.
static HttpParseResult fail(HttpParser* p, HttpError err)
{
    p->state = HTTPS_Failed;
    p->err   = err;
    return HTTPP_Error;
}

// ---------------------------------------------------------------------------------------------
// Line reading
// ---------------------------------------------------------------------------------------------

// Outcome of trying to pull one CRLF-terminated line out of the ring.
typedef enum {
    LINE_Got = 0,   // a complete line was consumed; `out` holds it without its terminator
    LINE_Partial,   // no terminator yet, and the ring is not over the limit -- wait for more
    LINE_TooLong,   // no terminator within maxLineLen, so there is not going to be one worth having
    LINE_Malformed   // a CR not followed by LF, which is not a line terminator in any dialect
} LineResult;

// Pull one line out of `src`. `allowBareLF` follows the policy in the file header: true in the
// head, false in chunk framing.
//
// Peeks rather than reads, so a partial line stays in the ring untouched for the next pass. The
// peek is bounded by maxLineLen, which is what stops a peer from making this scan an unbounded
// amount of memory by never sending a terminator.
static LineResult readLine(HttpParser* p, BufRing* src, string* out, bool allowBareLF)
{
    size_t avail = src->total;
    size_t limit = p->limits.maxLineLen;

    // +2 so a line of exactly maxLineLen plus its terminator still fits and is accepted rather
    // than being rejected for the length of its own CRLF.
    size_t want = min(avail, limit + 2);
    if (want == 0)
        return LINE_Partial;

    uint8* scan = (uint8*)xaAlloc(want, XA_Opt);
    if (!scan)
        return LINE_TooLong;

    size_t got = bufringPeek(src, scan, 0, want);

    LineResult res = LINE_Partial;
    size_t eol     = 0;   // index of the terminator's first byte
    size_t skip    = 0;   // bytes to consume, terminator included

    for (size_t i = 0; i < got; i++) {
        if (scan[i] == '\n') {
            if (i > 0 && scan[i - 1] == '\r') {
                eol  = i - 1;
                skip = i + 1;
                res  = LINE_Got;
            } else if (allowBareLF) {
                eol  = i;
                skip = i + 1;
                res  = LINE_Got;
            } else {
                res = LINE_Malformed;
            }
            break;
        }

        // A CR that is not part of a CRLF. Rejecting it rather than treating it as data keeps
        // "what counts as a line" from depending on what arrives next.
        if (scan[i] == '\r' && i + 1 < got && scan[i + 1] != '\n') {
            res = LINE_Malformed;
            break;
        }
    }

    if (res == LINE_Partial && got > limit)
        res = LINE_TooLong;

    if (res == LINE_Got) {
        strDestroy(out);
        strFromBytes(out, (const char*)scan, (uint32)eol);
        bufringSkip(src, skip);
        p->headBytes += (uint32)skip;
    }

    xaFree(scan);
    return res;
}

// Turn a LINE_* other than LINE_Got into the parser result it implies. Only ever called when the
// line was not complete, so LINE_Got is not a case here.
static HttpParseResult lineProblem(HttpParser* p, LineResult r)
{
    if (r == LINE_TooLong)
        return fail(p, HTTPERR_TooLarge);
    if (r == LINE_Malformed)
        return fail(p, HTTPERR_BadMessage);
    return HTTPP_NeedMore;
}

// A numeric field that must be *nothing but* digits in its base: no sign, no "0x" prefix, no
// surrounding whitespace, and not empty. Every one of those is a request-smuggling primitive
// here -- a peer that sends "0x5" or " 5" as a chunk size is betting that an intermediary and an
// origin will read it differently, which is how two parsers end up disagreeing about where a
// message ends.
#define HTTP_STRICTNUM (STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix | STRNUM_NoSign)

// ---------------------------------------------------------------------------------------------
// Start lines
// ---------------------------------------------------------------------------------------------

// "HTTP/1.1" -> HTTPVER_1_1. Anything else, including HTTP/2 and a malformed token, is Unknown and
// the caller rejects it -- answering a version we cannot speak is worse than refusing to.
static HttpVersion parseVersion(strref s)
{
    if (strEq(s, _SL("HTTP/1.1")))
        return HTTPVER_1_1;
    if (strEq(s, _SL("HTTP/1.0")))
        return HTTPVER_1_0;
    return HTTPVER_Unknown;
}

static HttpMethod methodFromName(strref name)
{
    // Case-sensitive on purpose: HTTP methods are, and "get" is not a request for the same thing
    // "GET" is. A lowercase verb goes to HTTP_MethodOther and is rejected by the caller rather than
    // being quietly upgraded.
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

// "HTTP/1.1 200 OK". The reason phrase is optional and may contain spaces, so it is everything
// after the second space rather than a third field.
static HttpParseResult parseStatusLine(HttpParser* p, strref line)
{
    int32 sp1 = strFindChar(line, 0, ' ');
    if (sp1 <= 0)
        return fail(p, HTTPERR_BadMessage);

    string ver = 0;
    strSubStr(&ver, line, 0, sp1);
    p->version = parseVersion(ver);
    strDestroy(&ver);

    if (p->version == HTTPVER_Unknown)
        return fail(p, HTTPERR_BadMessage);

    int32 sp2 = strFindChar(line, sp1 + 1, ' ');

    string code = 0;
    strSubStr(&code, line, sp1 + 1, sp2 >= 0 ? sp2 : strEnd);

    uint64 st = 0;
    bool ok   = strToUInt64(&st, code, 10, HTTP_STRICTNUM) && st >= 100 && st <= 999;
    strDestroy(&code);

    if (!ok)
        return fail(p, HTTPERR_BadMessage);

    p->status = (uint16)st;

    if (sp2 >= 0)
        strSubStr(&p->reason, line, sp2 + 1, strEnd);

    p->state = HTTPS_Headers;
    return STEP_Again;
}

// "GET /path HTTP/1.1". Exactly three fields; a target containing a space is not a target.
static HttpParseResult parseRequestLine(HttpParser* p, strref line)
{
    int32 sp1 = strFindChar(line, 0, ' ');
    if (sp1 <= 0)
        return fail(p, HTTPERR_BadMessage);

    int32 sp2 = strFindChar(line, sp1 + 1, ' ');
    if (sp2 <= sp1 + 1)
        return fail(p, HTTPERR_BadMessage);

    strSubStr(&p->methodName, line, 0, sp1);
    strSubStr(&p->target, line, sp1 + 1, sp2);

    // The method must be a token. Rejecting anything else here means the request line cannot
    // smuggle whitespace or control characters into whatever routes on it later.
    if (!_httpIsToken(p->methodName))
        return fail(p, HTTPERR_BadMessage);

    p->method = methodFromName(p->methodName);

    string ver = 0;
    strSubStr(&ver, line, sp2 + 1, strEnd);
    p->version = parseVersion(ver);
    strDestroy(&ver);

    if (p->version == HTTPVER_Unknown)
        return fail(p, HTTPERR_BadMessage);

    p->state = HTTPS_Headers;
    return STEP_Again;
}

// ---------------------------------------------------------------------------------------------
// Header fields
// ---------------------------------------------------------------------------------------------

// Decide how the body is delimited, once the whole head is in hand. The order here is the order RFC
// 9110 resolves it in, and the order matters: a no-body status overrides a Content-Length the peer
// sent anyway, and chunked overrides Content-Length when a peer sends both.
static HttpParseResult finishHead(HttpParser* p)
{
    p->headDone = true;

    bool te = httpHeadersHas(&p->headers, _SL("Transfer-Encoding"));
    bool cl = httpHeadersHas(&p->headers, _SL("Content-Length"));

    // Both present is a request-smuggling primitive: two implementations that resolve the conflict
    // differently see different message boundaries. There is no interpretation worth guessing at.
    if (te && cl)
        return fail(p, HTTPERR_BadMessage);

    if (te) {
        // Only `chunked` is supported, and it has to be the last coding. Anything else means a
        // body we cannot frame, which is not something to continue past.
        if (!httpHeadersHasToken(&p->headers, _SL("Transfer-Encoding"), _SL("chunked")))
            return fail(p, HTTPERR_BadMessage);

        // HTTP/1.0 has no chunked encoding, so a 1.0 message claiming it is malformed rather than
        // merely unusual.
        if (p->version == HTTPVER_1_0)
            return fail(p, HTTPERR_BadMessage);

        p->chunked = true;
    }

    if (cl) {
        // A repeated Content-Length is only acceptable if every copy agrees; httpHeadersGetAll
        // sees them all, where a hashtable-backed header store would have silently kept one.
        sa_string vals;
        httpHeadersGetAll(&p->headers, _SL("Content-Length"), &vals);

        uint64 len = 0;
        bool ok    = strToUInt64(&len, vals.a[0], 10, HTTP_STRICTNUM);

        for (int32 i = 1; ok && i < saSize(vals); i++) {
            uint64 other = 0;
            ok           = strToUInt64(&other, vals.a[i], 10, HTTP_STRICTNUM) && other == len;
        }

        saDestroy(&vals);

        if (!ok)
            return fail(p, HTTPERR_BadMessage);

        p->hasLength = true;
        p->length    = len;
    }

    // Statuses and methods that forbid a body win over everything above: a response to HEAD is
    // framed exactly like the GET response would have been, but the body is not sent.
    if (!p->isRequest) {
        if (p->status < 200 || p->status == HTTP_NoContent || p->status == HTTP_NotModified ||
            p->reqMethod == HTTP_Head)
            p->noBody = true;
    } else if (!te && !cl) {
        // A request with no framing has no body. Unlike a response it cannot be close-delimited,
        // because the sender has to be able to send a response afterwards.
        p->noBody = true;
    }

    if (p->noBody) {
        p->state = HTTPS_Done;
    } else if (p->chunked) {
        p->state = HTTPS_ChunkSize;
    } else if (p->hasLength) {
        p->state = p->length > 0 ? HTTPS_BodyLength : HTTPS_Done;
    } else {
        // Response with no framing at all: the body runs to EOF. This is what makes
        // `Connection: close` responses work, and it is why the connection cannot be reused after.
        p->state          = HTTPS_BodyClose;
        p->closeDelimited = true;
    }

    return HTTPP_Head;
}

// One header field, or the blank line that ends the block.
static HttpParseResult parseHeaderLine(HttpParser* p, strref line)
{
    if (strEmpty(line))
        return finishHead(p);

    // A continuation line (obs-fold). Deprecated for a decade, a smuggling vector, and sent by
    // nothing current -- rejected rather than unfolded.
    if (_httpIsOWS(strGetChar(line, 0)))
        return fail(p, HTTPERR_BadMessage);

    int32 colon = strFindChar(line, 0, ':');
    if (colon <= 0)
        return fail(p, HTTPERR_BadMessage);

    string name = 0;
    strSubStr(&name, line, 0, colon);

    // Whitespace between the field name and the colon is forbidden precisely because
    // implementations disagree about whether to trim it, which is a smuggling primitive. Every byte
    // of the name has to be a token character, which rules that out along with everything else
    // strange.
    if (!_httpIsToken(name)) {
        strDestroy(&name);
        return fail(p, HTTPERR_BadMessage);
    }

    string value = 0;
    strSubStr(&value, line, colon + 1, strEnd);
    _httpTrimOWS(&value);

    if (++p->headerCount > p->limits.maxHeaderCount) {
        strDestroy(&name);
        strDestroy(&value);
        return fail(p, HTTPERR_TooLarge);
    }

    httpHeadersAdd(&p->headers, name, value);

    strDestroy(&name);
    strDestroy(&value);
    return STEP_Again;
}

// ---------------------------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------------------------

// Claim up to `want` bytes of body at the head of `src`, reporting how many so the caller can
// count down whatever framing it is inside. The bytes themselves are left in `src` -- the caller
// drains them directly (see `bodyReady`) rather than having them copied through the parser.
static HttpParseResult emitBody(HttpParser* p, BufRing* src, uint64 want, size_t* moved)
{
    *moved = 0;

    size_t n = (size_t)min(want, (uint64)src->total);
    if (n == 0)
        return HTTPP_NeedMore;

    if (p->limits.maxBodyBytes && p->bodyTotal + n > p->limits.maxBodyBytes)
        return fail(p, HTTPERR_TooLarge);

    p->bodyTotal += n;
    p->bodyReady  = n;
    *moved        = n;
    return HTTPP_Body;
}

// "1a3f" or "1a3f;ext=value". The extension is parsed off and discarded -- nothing defines one that
// matters, and ignoring rather than rejecting keeps us working against a server that sends one.
static HttpParseResult parseChunkSize(HttpParser* p, strref line)
{
    int32 semi = strFindChar(line, 0, ';');

    string sizestr = 0;
    strSubStr(&sizestr, line, 0, semi >= 0 ? semi : strEnd);

    // Strict hex: see HTTP_STRICTNUM. A permissive size parse is the other classic smuggling
    // primitive, since a proxy that reads it differently frames the body differently.
    uint64 sz = 0;
    bool ok   = strToUInt64(&sz, sizestr, 16, HTTP_STRICTNUM);
    strDestroy(&sizestr);

    if (!ok)
        return fail(p, HTTPERR_BadMessage);
    if (sz > p->limits.maxChunkSize)
        return fail(p, HTTPERR_TooLarge);

    if (sz == 0) {
        p->state = HTTPS_Trailer;
        return STEP_Again;
    }

    p->length = sz;
    p->state  = HTTPS_ChunkData;
    return STEP_Again;
}

// ---------------------------------------------------------------------------------------------
// The step function
// ---------------------------------------------------------------------------------------------

// Advance by one unit: one line, or one run of body bytes. Answers STEP_Again when that produced
// nothing the caller needs to see.
static HttpParseResult stepOnce(HttpParser* p, BufRing* src)
{
    string line = 0;
    LineResult lr;
    size_t moved        = 0;
    HttpParseResult ret = HTTPP_NeedMore;

    // A single step advances one unit: one line, or one run of body bytes. The caller loops, which
    // keeps each case below a straight line rather than a nest of continues.
    switch (p->state) {
    case HTTPS_Start:
        // The head has its own budget, checked before each line so a peer cannot spend an unbounded
        // amount of our memory on headers we were going to accept individually.
        if (p->headBytes > p->limits.maxHeadBytes) {
            ret = fail(p, HTTPERR_TooLarge);
            break;
        }

        lr = readLine(p, src, &line, true);
        if (lr != LINE_Got) {
            ret = lineProblem(p, lr);
            break;
        }

        // A blank line before the start line is legacy tolerance: some clients emit a stray CRLF
        // after a POST body, and RFC 9112 says to ignore at least one.
        if (strEmpty(line)) {
            ret = STEP_Again;
            break;
        }

        ret = p->isRequest ? parseRequestLine(p, line) : parseStatusLine(p, line);
        break;

    case HTTPS_Headers:
        if (p->headBytes > p->limits.maxHeadBytes) {
            ret = fail(p, HTTPERR_TooLarge);
            break;
        }

        lr = readLine(p, src, &line, true);
        if (lr != LINE_Got) {
            ret = lineProblem(p, lr);
            break;
        }

        ret = parseHeaderLine(p, line);
        break;

    case HTTPS_BodyLength:
        ret = emitBody(p, src, p->length, &moved);
        if (ret == HTTPP_Body) {
            p->length -= moved;
            if (p->length == 0)
                p->state = HTTPS_Done;   // reported next step, once the caller has drained this
        }
        break;

    case HTTPS_BodyClose:
        // No length to count down; every byte is body until the peer closes.
        ret = emitBody(p, src, (uint64)src->total, &moved);
        break;

    case HTTPS_ChunkSize:
        lr = readLine(p, src, &line, false);
        if (lr != LINE_Got) {
            ret = lineProblem(p, lr);
            break;
        }
        ret = parseChunkSize(p, line);
        break;

    case HTTPS_ChunkData:
        ret = emitBody(p, src, p->length, &moved);
        if (ret == HTTPP_Body) {
            p->length -= moved;
            if (p->length == 0)
                p->state = HTTPS_ChunkCRLF;
        }
        break;

    case HTTPS_ChunkCRLF:
        lr = readLine(p, src, &line, false);
        if (lr != LINE_Got) {
            ret = lineProblem(p, lr);
            break;
        }
        // The bytes after a chunk's data must be exactly CRLF. Anything else means the size we were
        // given did not match the data that followed it.
        if (!strEmpty(line)) {
            ret = fail(p, HTTPERR_BadMessage);
            break;
        }
        p->state = HTTPS_ChunkSize;
        ret      = STEP_Again;
        break;

    case HTTPS_Trailer:
        lr = readLine(p, src, &line, false);
        if (lr != LINE_Got) {
            ret = lineProblem(p, lr);
            break;
        }
        // Trailer fields are read and dropped. Nothing cxhttp does with a body cares about one, and
        // merging them into the header block after the application has already seen the headers
        // would be a surprise rather than a feature.
        if (strEmpty(line)) {
            p->state = HTTPS_Done;
            ret      = HTTPP_Complete;
        } else {
            ret = STEP_Again;
        }
        break;

    case HTTPS_Done:
        ret = HTTPP_Complete;
        break;

    case HTTPS_Failed:
        ret = HTTPP_Error;
        break;
    }

    strDestroy(&line);
    return ret;
}

_Use_decl_annotations_
HttpParseResult httpParserStep(HttpParser* p, BufRing* src)
{
    HttpParseResult r;
    while ((r = stepOnce(p, src)) == STEP_Again);
    return r;
}

_Use_decl_annotations_
HttpParseResult httpParserEOF(HttpParser* p)
{
    switch (p->state) {
    case HTTPS_BodyClose:
        // The one framing where a close is the message ending rather than the message being cut
        // off.
        p->state = HTTPS_Done;
        return HTTPP_Complete;

    case HTTPS_Done:
        return HTTPP_Complete;

    case HTTPS_Failed:
        return HTTPP_Error;

    default:
        // Anything else was mid-message. Reporting this as a truncation rather than as a completion
        // is what stops a partial response being handed up as if it were whole.
        return fail(p, HTTPERR_Closed);
    }
}

_Use_decl_annotations_
uint64 _httpBodyTotal(const HttpParser* p)
{
    if (p->noBody)
        return 0;

    if (p->chunked || p->closeDelimited)
        return -1;

    return p->length;
}

_Use_decl_annotations_
bool httpParserKeepAlive(const HttpParser* p)
{
    if (httpHeadersHasToken(&p->headers, _SL("Connection"), _SL("close")))
        return false;

    // A close-delimited body has no other way to end, so the connection is spent by definition.
    // Recorded as a flag rather than read off `state`: this is normally asked after the message
    // completed, by which point the state has moved to Done and the evidence would be gone.
    if (p->closeDelimited)
        return false;

    if (p->version == HTTPVER_1_0)
        return httpHeadersHasToken(&p->headers, _SL("Connection"), _SL("keep-alive"));

    return p->version == HTTPVER_1_1;
}

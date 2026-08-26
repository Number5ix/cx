#pragma once

#include <cxhttp.h>

#include <cx/log.h>
#include <cx/serialize.h>
#include <cx/string.h>

extern LogChannel* HttpLogChannel;

// Everything in cxhttp logs to the same channel.
#undef LOG_CHANNEL
#define LOG_CHANNEL HttpLogChannel

// Process-wide initialization: registers the log channel. Every public entry point that can be the
// first thing an application calls runs this, so there is no init call for a consumer to forget.
void _httpInit(void);

// Target size for a StreamBuffer cxhttp creates to carry a body that is already in memory. Matches
// the read chunk the connections use, so one pump pass drains one ring node.
#define HTTP_BODY_CHUNK 8192

// Notify callback for a request body whose producer works in push mode. Registered from
// httprequest.c, where the buffer is adopted, but implemented on HttpConn because waking the
// connection is the only thing it does.
void _httpReqBodyNotify(_Pre_valid_ StreamBuffer* sb, size_t sz, _Pre_opt_valid_ void* ctx);

// Build the stream that carries an in-memory request body, if there is one and it is not already
// built. Called just before the head goes out, so a redirect gets a fresh stream over the same
// bytes rather than the drained one the previous hop left behind. No-op for a body the caller is
// streaming itself.
bool _httpReqArmBody(_Inout_ HttpRequest* self);

// ---------------------------------------------------------------------------------------------
// Shared parsing primitives
//
// HTTP's grammar is small but every field uses the same handful of rules, so they live here rather
// than being re-derived in each parser.
// ---------------------------------------------------------------------------------------------

// True for the whitespace HTTP calls OWS: space and horizontal tab, and nothing else. Notably not
// \r or \n, which are structure rather than whitespace.
_meta_inline bool _httpIsOWS(uint8 c)
{
    return c == ' ' || c == '\t';
}

// True for a character allowed in a `token` (RFC 9110 5.6.2) -- what a method, a header field name,
// and a transfer-coding name are made of.
bool _httpIsTokenChar(uint8 c);

// True if `s` is a non-empty `token` -- every byte a tchar. What a method name, a header field
// name, and a transfer-coding name each have to be.
bool _httpIsToken(_In_opt_ strref s);

// Trim leading and trailing OWS from a string in place. The usual last step of pulling a field
// value off the wire, where "Name:   value  " has to become "value".
void _httpTrimOWS(_Inout_ strhandle s);

// The standard reason phrase for a status code, or NULL for one this does not know. Nothing reads
// a reason phrase, so an unrecognized code travels without one rather than needing a full table.
strref _httpReasonPhrase(uint16 status);

// True for a status whose response carries no body however it is framed (RFC 9110 6.4.1): 1xx, 204
// and 304. A Content-Length on one of these is a framing conflict, not merely redundant.
bool _httpStatusHasNoBody(uint16 status);

// Append bytes whose length is a size_t. strAppendBytes() takes a uint32, and on 64-bit Windows
// that narrowing is a warning -- rightly, because a body length here comes off the wire. Appending
// in uint32-sized runs turns a silent truncation into either a correct append or an honest failure.
bool _httpAppendBytes(_Inout_ strhandle out, _In_reads_bytes_opt_(len) const uint8* data,
                      size_t len);

// ---------------------------------------------------------------------------------------------
// Message parser
//
// One incremental state machine for both directions: the client feeds it responses, the server
// feeds it requests. It never needs a whole message resident -- it consumes from a BufRing and
// reports what it produced, so a gigabyte download and a 200-byte API reply take the same path.
//
// Internal rather than public API: HttpConn is the supported low-level entry point. This is
// exposed to the test suite through <cxhttp/http_private.h> the same way cx/net/net_private.h is.
// ---------------------------------------------------------------------------------------------

// What one parser step produced. The caller loops until NeedMore, Complete, or Error.
typedef enum {
    HTTPP_NeedMore = 0,   // everything available is consumed; feed more bytes
    HTTPP_Head,           // the start line and headers are now readable on the parser
    HTTPP_Body,           // body bytes are waiting in the parser's `out` ring
    HTTPP_Complete,       // the message ended
    HTTPP_Error           // protocol error; `err` says which
} HttpParseResult;

// Internal parser states. Public only to the extent that the struct below is.
typedef enum {
    HTTPS_Start = 0,    // before the start line
    HTTPS_Headers,      // inside the header block
    HTTPS_BodyLength,   // body delimited by Content-Length
    HTTPS_BodyClose,    // body delimited by connection close
    HTTPS_ChunkSize,    // expecting a chunk-size line
    HTTPS_ChunkData,    // inside a chunk's data
    HTTPS_ChunkCRLF,    // expecting the CRLF that follows a chunk's data
    HTTPS_Trailer,      // in the trailer section after the last chunk
    HTTPS_Done,
    HTTPS_Failed
} HttpParseState;

typedef struct HttpParser {
    HttpParseState state;
    HttpError err;    // meaningful once state is HTTPS_Failed

    bool isRequest;   // parsing requests (server) rather than responses (client)
    bool headDone;    // HTTPP_Head has been reported for this message

    HttpLimits limits;

    // --- start line, filled in when the head completes -------------------------------------
    HttpVersion version;
    uint16 status;       // responses: the status code
    string reason;       // responses: the reason phrase, which may legitimately be empty
    HttpMethod method;   // requests: the method, HTTP_MethodOther if unrecognized
    string methodName;   // requests: the method as sent, always populated
    string target;       // requests: the request target, exactly as sent

    HttpHeaders headers;

    // --- body framing, decided once the head completes --------------------------------------
    bool noBody;   // framing says this message cannot have one (HEAD, 1xx, 204, 304)
    bool chunked;
    bool hasLength;

    // The body runs to EOF, so the connection is spent whatever the headers say. Recorded when the
    // framing is decided rather than read back off `state`, because by the time anyone asks about
    // reuse the state has already moved on to Done.
    bool closeDelimited;
    uint64 length;      // remaining bytes of a Content-Length body or of the current chunk
    uint64 bodyTotal;   // decoded body bytes produced so far, for maxBodyBytes

    // Bytes at the head of the source ring that are body and ready for the caller to drain, set
    // alongside HTTPP_Body. The caller must remove all of them from the same ring passed to
    // httpParserStep() before calling it again.
    size_t bodyReady;

    uint32 headBytes;   // start line + headers consumed, for maxHeadBytes
    uint32 headerCount;

    // The request method, which a response parser needs because a response to HEAD has no body
    // however it is framed. Set by the caller before feeding the response.
    HttpMethod reqMethod;
} HttpParser;

void httpParserInit(_Out_ HttpParser* p, bool isRequest, _In_opt_ const HttpLimits* limits);

// Reset for the next message on the same connection, keeping the limits. Cheaper than destroy and
// re-init, and it is what connection reuse does between requests.
void httpParserReset(_Inout_ HttpParser* p);

void httpParserDestroy(_Inout_ HttpParser* p);

// Consume what it can from `src` and report what that produced. Call in a loop until it answers
// NeedMore, Complete, or Error.
HttpParseResult httpParserStep(_Inout_ HttpParser* p, _Inout_ BufRing* src);

// Tell the parser the peer closed. Ends a close-delimited body successfully; anything else is a
// truncated message. Returns the final result.
HttpParseResult httpParserEOF(_Inout_ HttpParser* p);

// True once the message is framed well enough to know whether the connection may be reused: 1.1
// unless the peer said `Connection: close`, and 1.0 only if it said `Connection: keep-alive`.
bool httpParserKeepAlive(_In_ const HttpParser* p);

// ---------------------------------------------------------------------------------------------
// Chunked encoding (httpchunk.c)
//
// Only the writing half lives here; decoding is part of the parser state machine above, because it
// is inseparable from the framing decisions around it.
// ---------------------------------------------------------------------------------------------

// Append one chunk: the size in hex, CRLF, the data, CRLF. A zero-length payload would encode as
// the terminating chunk, so it is refused -- use httpChunkFinish() to end the body deliberately.
bool httpChunkAppend(_Inout_ strhandle out, _In_reads_bytes_(len) const uint8* data, size_t len);

// Append the terminating zero-length chunk and the empty trailer section that ends a chunked body.
bool httpChunkFinish(_Inout_ strhandle out);

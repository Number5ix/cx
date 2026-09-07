#pragma once

#include <cxhttp.h>

#include <cx/log.h>
#include <cx/net.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/thread/thread.h>

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

// Let go of the stream buffers a request was using, whichever side of each one cxhttp took.
void _httpReqReleaseBodyStream(_Inout_ HttpRequest* self);
void _httpReqReleaseSink(_Inout_ HttpRequest* self);
void _httpSrvReqReleaseStreams(_Inout_opt_ HttpServerRequest* req, bool ok);

// How every number that arrives in a header field is read: no whitespace, no sign, no base prefix
// and no trailing junk. Shared by both framing layers, because a Content-Length one of them accepts
// and the other refuses is exactly the disagreement a length field must not be able to cause.
#define HTTP_STRICTNUM (STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix | STRNUM_NoSign)

// ---------------------------------------------------------------------------------------------
// Alternative services, and what a client remembers about an origin (httpaltsvc.c)
// ---------------------------------------------------------------------------------------------

#define HTTPORIGIN_H3Works  0x01u   // HTTP/3 has worked here, or Alt-Svc says it should
#define HTTPORIGIN_H3Failed 0x02u   // a QUIC dial to this origin failed

// How long a failure is remembered. A network where UDP is blocked today may not be tomorrow -- a
// laptop moves between them -- so a failure is not remembered for good. What an Alt-Svc header
// says is remembered for as long as it asked, bounded by HTTPORIGIN_MAXAGE.
#define HTTPORIGIN_TTL     timeS(600)
#define HTTPORIGIN_MAXAGE  timeS(7 * 24 * 60 * 60)

// Record what just happened to an origin. `flags` replaces what was there rather than joining it:
// a dial that worked settles a question a previous failure had answered the other way. `altPort`
// is the port HTTP/3 is reached on, or 0 for the origin's own.
void _httpOriginRemember(_Inout_ HttpClient* cl, _In_opt_ strref key, uint32 flags, uint16 altPort,
                         int64 ttl);

// What is known about an origin, or 0. `altPort` receives the port HTTP/3 should be dialled on,
// which is 0 unless an Alt-Svc header named a different one.
uint32 _httpOriginRecall(_Inout_ HttpClient* cl, _In_opt_ strref key, _Out_ uint16* altPort);

// Read the Alt-Svc header of a response and record what it says about this origin. How a client
// that has only ever spoken HTTP/1.1 finds out that the same origin also answers HTTP/3.
void _httpOriginLearnAltSvc(_Inout_ HttpClient* cl, _In_opt_ strref key, _In_ const HttpUrl* url,
                            _In_ const HttpHeaders* resp);

// One alternative service, as an Alt-Svc header advertises it.
typedef struct HttpAltSvc {
    string proto;    // ALPN protocol identifier, percent-decoded
    string host;     // empty means the same host as the origin
    uint16 port;
    int64 maxAge;    // how long it may be remembered, in microseconds
    bool clear;      // the header was `clear`: forget every alternative for this origin
} HttpAltSvc;

// Find the alternative for `proto` in an Alt-Svc field value.
//
// Exposed to the test suite the way HttpParser is. Release the result with
// _httpAltSvcDestroy(); a `clear` header answers true with `clear` set and nothing else filled in.
_Success_(return) bool _httpAltSvcFind(_Out_ HttpAltSvc* out, _In_opt_ strref value,
                                       _In_opt_ strref proto);
void _httpAltSvcDestroy(_Inout_ HttpAltSvc* alt);

// The ALPN protocol identifiers cxhttp knows. Here rather than with the rest of the HTTP/3
// constants because the client's dialer and the server's listener both name them from files that
// are in every build.
#define HTTP_ALPN_H3     "h3"
#define HTTP_ALPN_HTTP11 "http/1.1"

// Add an ALPN protocol to a TLS configuration if it is not already offered, leaving the rest of
// the list untouched. Both listeners and both dialers need this, and a config missing the protocol
// its peer will insist on fails every handshake that negotiates ALPN at all.
//
// False means the protocol is missing and cannot be added, because the configuration has already
// been sealed by an earlier listen or connect. The caller reports that rather than proceeding: a
// listener that cannot offer its own protocol would fail every handshake instead.
_Success_(return) bool _httpAlpnAdd(_Inout_ TlsConfig* cfg, _In_opt_ strref proto);

// Whether a configuration already offers a protocol. A configuration with no ALPN list at all
// offers nothing and answers false, which is not the same as being unusable: a listener with no
// list negotiates no protocol and speaks whatever it was going to speak anyway.
_Pure bool _httpAlpnHas(_In_ TlsConfig* cfg, _In_opt_ strref proto);

// How many protocols a configuration offers. Zero means it has no list, which is the state a
// TlsConfig starts in and the one a plain https listener is happy with.
_Pure int32 _httpAlpnCount(_In_ TlsConfig* cfg);

// ---------------------------------------------------------------------------------------------
// The seam between the two framing layers
//
// Everything the version-agnostic sources need from HTTP/3, in one place. Each of these is defined
// twice: by the HTTP/3 sources, and by http3stub.c in a build that has none. That is what keeps
// every other file in cxhttp -- and every public header -- free of a feature test.
// ---------------------------------------------------------------------------------------------

// Whether this build has HTTP/3 in it. httpclientSetVersions() refusing HTTPV_Http3 is this
// answering false.
_Pure bool _http3Available(void);

// Bind a QUIC endpoint and serve HTTP/3 on it. What httpserverListenQuic() is.
_Success_(return) bool _http3ServerListen(_Inout_ HttpServer* srv, _In_ const NetAddr* addr,
                                          _In_ TlsConfig* cfg);

// Close a connection from the server's table if it is an HTTP/3 one. False means it was not, and
// the caller should try the HTTP/1.1 kind.
bool _http3ConnClose(_In_ ObjInst* conn);

// Write a response, or a bare "100 Continue", for a request that arrived over HTTP/3. Reached
// only when HttpServerRequest::h3conn is set, which cannot happen in a build with no HTTP/3.
bool _http3SrvReqRespond(_Inout_ HttpServerRequest* req);
bool _http3SrvReqContinue(_Inout_ HttpServerRequest* req);

// Told that an HTTP/3 connection has died, so a pool can stop handing it out. Shaped like
// HttpConnClosedCB but carrying an ObjInst, because the two connection types share no base class.
typedef void (*Http3ConnClosedCB)(_In_opt_ ObjInst* conn, _In_opt_ void* ctx);

// Open a QUIC connection for HTTP/3. The socket comes back at once; NET_Connection on its control
// flow says when the handshake finished. NULL in a build with no HTTP/3.
_Ret_maybenull_ NetSocket* _http3Dial(_In_ NetQueue* q, _In_opt_ strref host, uint16 port,
                                      _In_opt_ strref hostname, _In_ TlsConfig* cfg,
                                      _In_opt_ const NetHandlers* handlers, _In_opt_ void* ctx);

// Whether a connected QUIC socket actually negotiated HTTP/3. A server that answered with anything
// else is not speaking a protocol this client can read.
bool _http3Negotiated(_In_ NetSocket* sock);

// Build an HTTP/3 connection over a socket whose handshake has finished. The reference is the
// caller's.
_Ret_maybenull_ ObjInst* _http3ConnCreate(_In_ NetSocket* sock, _In_opt_ strref host);

// Start a request on one, on a stream of its own. False means it could not be started at all,
// which for a connection at the peer's stream limit means dialling another.
bool _http3ConnRequest(_Inout_ ObjInst* conn, _Inout_ HttpRequest* req,
                       _In_opt_ const HttpHandlers* handlers, _In_opt_ void* ctx, int64 timeout);

// Whether a connection will take another request: alive, and not past a GOAWAY.
bool _http3ConnUsable(_In_ ObjInst* conn);

// Requests running on a connection right now. A pool reaps one only at zero, because unlike an
// HttpConn an HTTP/3 connection stays in the pool while it is in use.
uint32 _http3ConnRequests(_In_ ObjInst* conn);

// Register the callback that says a connection died.
void _http3ConnSetClosed(_Inout_ ObjInst* conn, Http3ConnClosedCB cb, _In_opt_ void* ctx);

// A push-mode request body producer wrote more. The HTTP/3 half of _httpReqBodyNotify().
void _http3ReqBodyNotify(_Inout_ HttpRequest* req);

// Abandon a request in flight over HTTP/3, which costs its stream and nothing else on the
// connection.
void _http3ReqCancel(_Inout_ HttpRequest* req);

// Whether the peer's GOAWAY named a stream at or below this request's, which means this request
// was never processed and may be started again on a fresh connection whatever its method was. That
// single retry is what makes a rolling restart invisible.
bool _http3ReqRetryable(_In_ HttpRequest* req);

// Let go of a request's hold on its HTTP/3 stream and connection. The connection is untouched: it
// is borrowed rather than taken, and goes on serving whatever else is on it.
void _http3ReqRelease(_Inout_ HttpRequest* req);

// ---------------------------------------------------------------------------------------------
// The off-worker handoff
//
// respond() may be called from any thread, and the two paths it can take are not interchangeable.
// Writing a response also advances whatever carries it -- the parser or the frame reader, the
// receive ring, the deadlines -- and all of that belongs to one worker at a time.
//
// One of these sits on each thing that has its own ordering domain: the connection under HTTP/1.1,
// where a connection carries one request at a time, and the request itself under HTTP/3, where
// each request is a stream with a worker of its own.
// ---------------------------------------------------------------------------------------------

// A caller that finds its own Thread in the HttpDispatch is the worker and does the work in place;
// anyone else hands it over. Comparing against our own Thread is what makes a stale read harmless:
// another thread's Thread is never ours, so a reader that loses the race takes the handoff, which
// is always correct.

// Claim this thread as the one dispatching, answering the previous value so it can be put back.
// Nesting is ordinary rather than exceptional -- responding from a request handler runs the
// response path inside the pump that delivered the request -- so this saves and restores rather
// than setting and clearing.
_meta_inline Thread* _httpDispatchEnter(_Inout_ HttpDispatch* d)
{
    Thread* prev = (Thread*)atomicLoad(ptr, &d->thread, Relaxed);
    atomicStore(ptr, &d->thread, thrCurrent(), Release);
    return prev;
}

_meta_inline void _httpDispatchLeave(_Inout_ HttpDispatch* d, _In_opt_ Thread* prev)
{
    atomicStore(ptr, &d->thread, prev, Release);
}

// True when this thread is the one currently dispatching, and may therefore touch the state
// directly rather than handing it over.
_meta_inline bool _httpDispatchOwned(_In_ HttpDispatch* d)
{
    return atomicLoad(ptr, &d->thread, Acquire) == thrCurrent();
}

// Ask a worker to come and move things along. A zero-delay flow timer is the handoff: NET_Timer
// arrives on a worker, ordered behind everything already pending for that flow, so what it does
// cannot overtake an event the application has not seen yet.
//
// Whether that turns out to be writing a response or pushing more of a body is decided when it
// lands rather than here -- by then the state says which, and this side is not allowed to read it.
//
// False means the flow is already dying, so no worker is coming.
bool _httpDispatchHandoff(_Inout_ HttpDispatch* d, _In_opt_ NetFlow* flow);

// True if a handoff was pending and this call claimed it. What a timer handler calls for a timer
// id it does not otherwise recognize: nothing else arms a zero-delay timer, and a stale id simply
// finds the flag already clear.
//
// What was written off-thread became visible here through the queue's timer lock, which both
// arming and firing take.
_meta_inline bool _httpDispatchClaim(_Inout_ HttpDispatch* d)
{
    return atomicExchange(uint32, &d->pending, 0, AcqRel) != 0;
}

// Bytes between progress events when a request has not chosen an interval of its own.
#define HTTP_PROGRESS_INTERVAL 65536

// Decide whether a progress event is due for a body that has moved `done` bytes in total, and
// remember the count when it is. `seen` is the count as of the last event delivered for that
// direction. `final` forces one for anything not yet reported, which is what makes a progress bar
// reliably reach its end, and costs nothing when the last event already covered everything.
bool _httpProgressDue(uint64 done, _Inout_ uint64* seen, size_t interval, bool final);

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

// The verb a request goes on the wire as: the spelling for a named method, and whatever the
// application set for an extension one. Shared by both framing layers, so a method cannot be
// spelled one way over HTTP/1.1 and another over HTTP/3.
strref _httpMethodName(_In_ const HttpRequest* req);

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
    HTTPP_Error,          // protocol error; `err` says which

    // An interim (1xx) response arrived and the real one has not. Its status and headers are
    // readable now and are gone on the next step, which resumes at the next start line. A caller
    // with nothing to do with it just keeps looping.
    HTTPP_Interim
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
    HTTPS_Interim,      // an interim response was just reported; clear it and read the next head
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
    uint32 interim;     // interim responses seen ahead of this one, for maxInterim

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

// How many body bytes a message just framed will carry, or -1 when that is not knowable up front
// (chunked, or delimited by the connection closing). Read at the head, because the parser's own
// `length` counts down as the body arrives.
uint64 _httpBodyTotal(_In_ const HttpParser* p);

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

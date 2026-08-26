#pragma once
#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/log/log.h>
#include <cx/net/net_shared.h>
#include <cx/serialize/streambuf.h>
#include <cx/string.h>
#include <cx/stype/stype.h>

/// @file http_shared.h
/// @brief Plain-C types shared by the cxhttp classes and their consumers

CX_C_BEGIN

/// @addtogroup http_types
/// @{

// Forward declarations for the cxhttp classes, so the plain-C types below can refer to them
// without dragging in the generated headers.
typedef struct HttpConn HttpConn;
typedef struct HttpRequest HttpRequest;
typedef struct HttpClient HttpClient;
typedef struct HttpCookieJar HttpCookieJar;
typedef struct HttpServer HttpServer;
typedef struct HttpServerConn HttpServerConn;
typedef struct HttpServerRequest HttpServerRequest;

// Opaque parser state. Held by HttpConn but not part of its API: the supported way to read a
// message is through the callbacks.
typedef struct HttpParser HttpParser;

/// Request method
///
/// The methods with defined semantics in RFC 9110, plus HTTP_MethodOther for anything else -- an
/// extension method travels as a string rather than forcing this enum to grow for every WebDAV
/// verb.
typedef enum {
    HTTP_MethodOther = 0,   ///< Something not in this list; the verb is carried as a string
    HTTP_Get,
    HTTP_Head,
    HTTP_Post,
    HTTP_Put,
    HTTP_Delete,
    HTTP_Connect,
    HTTP_Options,
    HTTP_Trace,
    HTTP_Patch
} HttpMethod;

/// Protocol version seen on the wire
///
/// cxhttp only ever *sends* HTTP/1.1. This exists because a peer may answer in 1.0, which changes
/// two things that matter: chunked transfer-encoding is not available, and a connection closes by
/// default rather than persisting.
typedef enum { HTTPVER_Unknown = 0, HTTPVER_1_0 = 10, HTTPVER_1_1 = 11 } HttpVersion;

/// @brief Status codes worth naming
///
/// The status field is a plain uint16 and any code round-trips, including ones invented after this
/// was written. These constants exist so call sites read as intent rather than as numerals.
typedef enum {
    HTTP_Continue           = 100,
    HTTP_SwitchingProtocols = 101,

    HTTP_OK             = 200,
    HTTP_Created        = 201,
    HTTP_Accepted       = 202,
    HTTP_NoContent      = 204,
    HTTP_PartialContent = 206,

    HTTP_MovedPermanently  = 301,
    HTTP_Found             = 302,
    HTTP_SeeOther          = 303,
    HTTP_NotModified       = 304,
    HTTP_TemporaryRedirect = 307,
    HTTP_PermanentRedirect = 308,

    HTTP_BadRequest       = 400,
    HTTP_Unauthorized     = 401,
    HTTP_Forbidden        = 403,
    HTTP_NotFound         = 404,
    HTTP_MethodNotAllowed = 405,
    HTTP_Conflict         = 409,
    HTTP_PayloadTooLarge  = 413,
    HTTP_UnsupportedMedia = 415,
    HTTP_TooManyRequests  = 429,

    HTTP_InternalError      = 500,
    HTTP_NotImplemented     = 501,
    HTTP_BadGateway         = 502,
    HTTP_ServiceUnavailable = 503,
    HTTP_GatewayTimeout     = 504
} HttpStatus;

/// Why a request or a parse failed
typedef enum {
    HTTPERR_None = 0,           ///< No error

    HTTPERR_BadUrl,             ///< The URL could not be parsed, or its scheme is not supported
    HTTPERR_BadMessage,         ///< The peer sent something this parser will not accept
    HTTPERR_TooLarge,           ///< A header block, line, or body exceeded its configured limit
    HTTPERR_Closed,             ///< The connection ended before the message did
    HTTPERR_Timeout,            ///< A deadline expired
    HTTPERR_Network,            ///< The transport failed; see the NetErrorCode that accompanied it
    HTTPERR_TooManyRedirects,   ///< The redirect chain exceeded its limit
    HTTPERR_Aborted             ///< The request was cancelled by the application
} HttpError;

/// @brief Bounds on what a peer is allowed to send us
///
/// Every one of these exists because the parser reads untrusted bytes, and an unbounded read is a
/// denial of service. httpLimitsDefault() fills in values suited to the deployment cxhttp is
/// written for -- an API behind a reverse proxy, and a client fetching files.
///
/// A server should set `maxBodyBytes` to something its endpoints can actually handle. The default
/// leaves it unlimited, which suits a client downloading a file of unknown size and does not suit
/// anything accepting uploads from strangers.
typedef struct HttpLimits {
    uint32 maxLineLen;      ///< Longest single start line or header line, in bytes
    uint32 maxHeaderCount;  ///< Most header fields in one message
    uint32 maxHeadBytes;    ///< Total size of the start line and header block together
    uint64 maxBodyBytes;    ///< Longest body; 0 means unlimited
    uint32 maxChunkSize;    ///< Largest single chunk in a chunked body
} HttpLimits;

/// @}

/// @addtogroup http_url
/// @{

/// @brief A parsed URL, decomposed into borrowed slices of the original
///
/// A value type, not an object: a URL has no identity worth sharing and no lifetime beyond the
/// request holding it. The component strings are ordinary cx strings, so httpUrlDestroy() releases
/// them; parsing is cheap because strSubStr() produces rope references rather than copies.
///
/// Components are stored **decoded** except for `query`, which keeps its percent-encoding because
/// splitting it into key/value pairs has to happen before decoding (a `%26` in a value is data, an
/// unescaped `&` is a separator).
typedef struct HttpUrl {
    string scheme;     ///< Lowercased, without the "://" -- "http" or "https"
    string user;       ///< Userinfo before any ':', decoded; empty if absent
    string pass;       ///< Userinfo after the ':', decoded; empty if absent
    string host;       ///< Lowercased host, without brackets for an IPv6 literal
    string path;       ///< Decoded path, always starting with '/' (empty input becomes "/")
    string query;      ///< Raw query, without the '?'; still percent-encoded
    string fragment;   ///< Decoded fragment, without the '#'

    /// @brief Port in host byte order, or 0 when the URL did not name one
    ///
    /// Left at 0 rather than filled in with the scheme default, because "was a port specified" is
    /// information the Host header and the connection pool key both need.
    uint16 port;

    bool ipv6Host;   ///< The host was a bracketed IPv6 literal
} HttpUrl;

/// @}

/// @addtogroup http_headers
/// @{

/// @brief An ordered list of header field name/value pairs
///
/// Not a hashtable, and the reason is duplicate field names: `Set-Cookie` may legitimately appear
/// several times in one response and must never be folded into one comma-separated value, because a
/// cookie value can contain a comma. `Via`, `Warning` and `Link` repeat too. A hashtable would
/// silently keep one and lose the rest.
///
/// Stored as two parallel arrays rather than an array of structs so that the element type stays
/// `string` and cx's own stype machinery handles every copy and release. The two are always the
/// same length; nothing outside httpheaders.c touches them directly.
///
/// Lookup is a case-insensitive linear scan: a message carries on the order of fifteen headers, and
/// an index would have to be maintained across every mutation.
typedef struct HttpHeaders {
    sa_string names;    ///< Field names, in the order they were added
    sa_string values;   ///< Field values, index-aligned with `names`
} HttpHeaders;

/// @}

/// @addtogroup http_cookies
/// @{

/// @brief One stored cookie
///
/// A value type held by an HttpCookieJar. Public because persistence is deliberately not the jar's
/// job: a consumer that wants cookies to survive the process enumerates them, writes them
/// somewhere, and puts them back.
typedef struct HttpCookie {
    string name;     ///< Cookie name
    string value;    ///< Cookie value, as sent; never decoded
    string domain;   ///< Domain the cookie applies to, without a leading dot
    string path;     ///< Path prefix the cookie applies to

    /// @brief Expiry in cx's canonical time, or 0 for a session cookie
    ///
    /// Set from `Expires` or `Max-Age`, with `Max-Age` winning when both are present, as RFC 6265
    /// requires.
    int64 expires;

    /// @brief The response carried no `Domain`, so only an exact host match sends this back
    ///
    /// The distinction matters: a cookie set by `example.com` without a Domain attribute must not
    /// go to `www.example.com`, but one set *with* `Domain=example.com` must.
    bool hostOnly;

    bool secure;     ///< Only send over https
    bool httpOnly;   ///< Not exposed to scripts; recorded for round-tripping, unused by cxhttp
} HttpCookie;

// A shared custom stype, so a cookie can live in an sarray with cx handling every copy and
// release. The macro set is the one stype.h documents for a type visible across translation units;
// the descriptor itself is defined in httpcookie.c.
stDeclare(HttpCookie);
saDeclare(HttpCookie);
#define SType_HttpCookie                         HttpCookie*
#define STStorageType_HttpCookie                 HttpCookie
#define STypeArg_HttpCookie(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_HttpCookie(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_HttpCookie(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_HttpCookie(type, val) stType(type), stArgPtr(type, val)
#define STypeCheck_HttpCookie(type, val)         (val)
#define STypeCheckPtr_HttpCookie(type, ptr)      (ptr)

/// @}

/// @addtogroup http_forms
/// @{

/// @brief A `multipart/form-data` body under construction
///
/// A value type built up part by part and then finished, which produces both the body and the
/// Content-Type that describes it -- they cannot be separated, because the Content-Type carries the
/// boundary that delimits the body.
///
/// The body is assembled in memory. That is the right trade for what forms are actually used for
/// here -- a login, a small file, a handful of fields -- and a large upload has a better path
/// already: compose it into a StreamBuffer and use httprequestSetBodyStream(), which never makes
/// the whole thing resident.
typedef struct HttpMultipart {
    /// @brief Delimiter separating the parts
    ///
    /// Generated from the platform's random source rather than a counter or a timestamp. A boundary
    /// an attacker can predict is a boundary they can write into a field value, ending the part
    /// early and injecting one of their own.
    string boundary;

    string body;     ///< Parts assembled so far
    bool finished;   ///< The closing boundary has been written; no more parts may be added
} HttpMultipart;

/// @}

/// @addtogroup http_client
/// @{

/// Per-request behavior switches
typedef enum {
    HTTPREQ_None = 0x00,

    /// @brief Do not follow redirects; deliver the 3xx response as the answer
    ///
    /// With redirects followed (the default) a 3xx body is discarded and never reaches the
    /// application, because concatenating it ahead of the real body would be nonsense. With this
    /// set the 3xx *is* the response, body included.
    HTTPREQ_NoRedirect = 0x01,

    /// @brief Neither send nor store cookies for this request
    HTTPREQ_NoCookies = 0x02
} HttpRequestFlags;

/// @}

/// @addtogroup http_conn
/// @{

/// Which body a progress event is counting
typedef enum {
    /// @brief Bytes handed to the socket
    ///
    /// The request body on a client, the response body on a server.
    HTTPPROG_Send = 1,

    /// @brief Bytes taken off the socket
    ///
    /// The response body on a client, the request body on a server.
    HTTPPROG_Recv
} HttpProgressDir;

/// What happened, as carried on HttpEvent
typedef enum {
    /// @brief The status line was parsed; status and version are readable
    ///
    /// Ahead of the headers, so a caller that only wants to know whether to keep reading (a
    /// downloader checking for 200 before opening its output file) can decide without waiting.
    HTTPEV_Status = 1,

    /// @brief The header block is complete; headers are readable
    HTTPEV_Headers,

    /// @brief A chunk of decoded body is available in `data`
    ///
    /// Valid only for the duration of the callback. Chunk boundaries mean nothing -- they are
    /// whatever the network and the transfer coding produced, not a structure of the message.
    HTTPEV_Data,

    /// @brief Body bytes have moved; `dir`, `done` and `total` say which way and how far
    ///
    /// Throttled by HttpRequest::progressInterval, with one final event guaranteed when a body
    /// ends. Only bodies are counted -- the head is not part of `done` or `total`.
    HTTPEV_Progress,

    /// @brief The response finished successfully
    HTTPEV_Complete,

    /// @brief The exchange failed; `err` says how
    ///
    /// Terminal: no further events arrive for this request.
    HTTPEV_Error
} HttpEventType;

/// Event delivered to an HttpHandlers callback
typedef struct HttpEvent {
    HttpEventType event;    ///< Which callback this is
    HttpConn* conn;         ///< Connection it happened on
    HttpRequest* request;   ///< Request it belongs to
    void* ctx;              ///< Context registered alongside the handlers

    uint16 status;          ///< Response status, from HTTPEV_Status onward
    HttpVersion version;    ///< Version the peer answered in, from HTTPEV_Status onward

    /// @brief Response headers, from HTTPEV_Headers onward
    ///
    /// Borrowed from the parser and valid until the request completes. Copy anything that has to
    /// outlive it.
    HttpHeaders* headers;

    const uint8* data;     ///< HTTPEV_Data: decoded body bytes, valid during the callback only
    size_t len;            ///< HTTPEV_Data: length of `data`

    HttpProgressDir dir;   ///< HTTPEV_Progress: which body is being counted
    uint64 done;           ///< HTTPEV_Progress: body bytes moved so far in that direction
    int64 total;           ///< HTTPEV_Progress: body bytes expected, or -1 if not known up front

    HttpError err;         ///< HTTPEV_Error: why it failed
    NetErrorCode neterr;   ///< HTTPEV_Error: transport cause when err is HTTPERR_Network
} HttpEvent;

/// The callback type for an HTTP event handler
///
/// Runs on the flow's worker, under the same rules every netqueue callback follows: do not block,
/// and hand anything slow to a TaskQueue.
typedef void (*HttpEventCB)(_In_ HttpEvent* event);

/// @brief Callbacks for one request's response
///
/// A plain struct of function pointers, mirroring NetHandlers, so the two layers read alike:
///
/// @code
///   static const HttpHandlers handlers = {
///       .headers  = onHeaders,
///       .data     = onData,
///       .complete = onComplete,
///   };
/// @endcode
///
/// A NULL entry is simply not called. There is exactly one consumer per request, so there is no
/// fallthrough between levels the way NetHandlers has -- the request either has handlers or it
/// buffers into itself.
typedef struct HttpHandlers {
    HttpEventCB status;     ///< HTTPEV_Status: the status line is parsed
    HttpEventCB headers;    ///< HTTPEV_Headers: the header block is complete
    HttpEventCB data;       ///< HTTPEV_Data: a chunk of decoded body
    HttpEventCB progress;   ///< HTTPEV_Progress: a body moved some bytes
    HttpEventCB complete;   ///< HTTPEV_Complete: the response finished
    HttpEventCB error;      ///< HTTPEV_Error: the exchange failed
} HttpHandlers;

/// @brief Called when a connection dies, whether or not a request was running on it
///
/// The event a connection pool needs and the response callbacks cannot provide: a pooled idle
/// connection that the peer closes, or whose idle timer expires, produces no response event at all,
/// because there is no response. Without this the pool would keep handing out dead connections and
/// only discover them when a request failed on one.
///
/// @param conn Connection that died; still valid for the duration of the callback
/// @param ctx Context registered with httpconnSetClosedHandler()
typedef void (*HttpConnClosedCB)(_In_ HttpConn* conn, _In_opt_ void* ctx);

/// @}

/// @addtogroup http_server
/// @{

/// What happened, as carried on HttpServerEvent
typedef enum {
    /// @brief A complete request arrived and is waiting for a response
    ///
    /// The head and the whole body have been received. This is where an application does its work
    /// and answers.
    HTTPSRVEV_Request = 1,

    /// @brief The request head arrived; its body has not been read yet
    ///
    /// The chance to say where the body should go, before any of it has been read. Call
    /// httpsrvreqSetSink() here to stream it into a StreamBuffer, or do nothing and let it be
    /// buffered on the request. Also where an application that turned auto-continue off decides
    /// whether to accept a body announced with `Expect: 100-continue`.
    ///
    /// Answering the request from here is allowed and skips the body entirely -- what a 401 or a
    /// 413 should do rather than reading an upload it is going to throw away.
    HTTPSRVEV_Head,

    /// @brief A run of request body bytes arrived
    ///
    /// Only delivered when the request has no sink and a `data` handler is registered. Nothing is
    /// accumulated on the request in that case, so this is the only place those bytes appear.
    HTTPSRVEV_Data,

    /// @brief Body bytes have moved; `dir`, `done` and `total` say which way and how far
    ///
    /// Throttled by HttpServerRequest::progressInterval, with one final event guaranteed when a
    /// body ends. Only bodies are counted -- the head is not part of `done` or `total`.
    HTTPSRVEV_Progress,

    /// @brief The exchange failed before a response could be written; `err` says how
    ///
    /// The request is whatever had been received, and may be NULL if the failure came before a
    /// request line was even complete. cxhttp has already sent an error response where one was
    /// possible, so this is for logging rather than for answering.
    HTTPSRVEV_Error,

    /// @brief The connection ended
    ///
    /// Always the last event for a connection, error or not.
    HTTPSRVEV_Closed
} HttpServerEventType;

/// Event delivered to an HttpServerHandlers callback
typedef struct HttpServerEvent {
    HttpServerEventType event;     ///< Which callback this is
    HttpServerConn* conn;          ///< Connection it happened on
    HttpServerRequest* request;    ///< Request it belongs to; NULL if none had been received
    void* ctx;                     ///< Context registered alongside the handlers

    const uint8* data;             ///< HTTPSRVEV_Data: the bytes, valid only for this call
    size_t len;                    ///< HTTPSRVEV_Data: how many

    HttpProgressDir dir;           ///< HTTPSRVEV_Progress: which body is being counted
    uint64 done;                   ///< HTTPSRVEV_Progress: body bytes moved so far
    int64 total;                   ///< HTTPSRVEV_Progress: bytes expected, or -1 if not known

    HttpError err;                 ///< HTTPSRVEV_Error: why it failed
    NetErrorCode neterr;           ///< HTTPSRVEV_Error: transport cause when err is HTTPERR_Network
} HttpServerEvent;

/// The callback type for a server event handler
///
/// Runs on the flow's worker, under the same rules every netqueue callback follows: do not block,
/// and hand anything slow to a TaskQueue.
typedef void (*HttpServerEventCB)(_In_ HttpServerEvent* event);

/// @brief Callbacks for a server's requests
///
/// A plain struct of function pointers, mirroring HttpHandlers and NetHandlers, so the layers read
/// alike:
///
/// @code
///   static const HttpServerHandlers handlers = {
///       .request = onRequest,
///   };
/// @endcode
///
/// A NULL entry is simply not called -- except `request`, without which the server has no way to
/// answer anything and every request gets a 500.
typedef struct HttpServerHandlers {
    HttpServerEventCB head;      ///< HTTPSRVEV_Head: the head arrived; choose where the body goes
    HttpServerEventCB data;      ///< HTTPSRVEV_Data: a run of body bytes, when nothing else claims them
    HttpServerEventCB progress;  ///< HTTPSRVEV_Progress: a body moved some bytes
    HttpServerEventCB request;   ///< HTTPSRVEV_Request: a complete request is waiting for an answer
    HttpServerEventCB error;     ///< HTTPSRVEV_Error: the exchange failed
    HttpServerEventCB closed;    ///< HTTPSRVEV_Closed: the connection ended
} HttpServerHandlers;

/// @}

CX_C_END

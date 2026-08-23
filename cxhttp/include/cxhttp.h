#pragma once

#include <cx/net.h>
#include <cxtls.h>

#include <cxhttp/http_shared.h>
#include <cxhttp/httpclient.h>
#include <cxhttp/httpconn.h>
#include <cxhttp/httpcookie.h>
#include <cxhttp/httprequest.h>
#include <cxhttp/httpserver.h>
#include <cxhttp/httpserverconn.h>
#include <cxhttp/httpserverreq.h>

/// @file cxhttp.h
/// @brief HTTP/1.1 for cx: URLs, headers, and a client over NetQueue and cxtls

/// @defgroup http HTTP
/// @{
/// HTTP/1.1 over the netqueue socket layer.
///
/// cxhttp speaks the common 90% of HTTP/1.1 -- framing, headers, chunked bodies, redirects,
/// cookies, and connection reuse -- for two jobs: calling out to fetch things, and serving simple
/// APIs behind a reverse proxy. It is not an edge server and does not try to be.
///
/// @defgroup http_overview Overview
/// @ingroup http
/// @{
///
/// @section http_model Mental model
///
/// HTTP sits **above** the socket, not inside the filter chain:
///
/// @code
///   app  <->  HttpConn        (NetHandlers.recv / netsocketSend)
///                   |
///             NetSocket
///                   |
///           TlsStreamFilter   (sock->filters.a[0], only for https)
///                   |
///                 wire
/// @endcode
///
/// That is what makes `http` and `https` the same code path: cxtls makes netsocketSend() take
/// plaintext and netsocketRecv() return plaintext whether or not a filter is attached, so nothing
/// above the socket has to know which it is talking to.
///
/// @section http_async Everything is asynchronous
///
/// There is no blocking API, for the same reason netqueue has none: a convenience call that waits
/// on a remote host is how a single unresponsive server stalls an entire process. Code that wants
/// synchronous-looking control flow uses a polled queue, which stays inside the one execution
/// model:
///
/// @code
///   NetQueueConfig conf;
///   netqueuePresetClient(&conf);            // nthreads = 0 -> polled
///   NetQueue* q = netqueueCreate(&conf);
///
///   while (!done)
///       netqueueTick(q, 100);               // bounded wait, never open-ended
/// @endcode
///
/// @section http_versions Protocol versions
///
/// HTTP/1.1 only on the wire. A peer answering in HTTP/1.0 is understood and answered under 1.0
/// rules -- no chunked encoding, connection closes by default -- but cxhttp never sends `HTTP/1.0`
/// itself. HTTP/2 is out of scope permanently; HTTP/3 waits on QUIC.
///
/// @}  // end of http_overview

/// @defgroup http_types Types
/// @ingroup http
/// Plain-C types shared across the HTTP API: methods, versions, status codes, and errors. Declared
/// in cxhttp/http_shared.h.

/// @defgroup http_url URLs
/// @ingroup http
/// URL parsing and composition, percent-encoding, and query strings. Nothing in cx core provides
/// these, so they live here.

/// @defgroup http_conn Low-level Connections
/// @ingroup http
/// HTTP over a socket the caller already owns: no dialing, no redirects, no pooling, and nothing
/// buffered on the caller's behalf.

/// @defgroup http_client Client
/// @ingroup http
/// The high-level client: a URL goes in, a response comes back, with redirects, cookies, connection
/// reuse and timeouts handled in between.

/// @defgroup http_headers Headers
/// @ingroup http
/// An ordered, case-insensitive, duplicate-tolerant field list, plus parsing for the structured
/// values (dates, tokens, quality-ordered lists) that the common headers carry.

/// @defgroup http_cookies Cookies
/// @ingroup http
/// Session cookie storage: parsing `Set-Cookie`, matching by domain, path and scheme, and building
/// the `Cookie` header a request sends back.

/// @defgroup http_server Server
/// @ingroup http
/// An HTTP/1.1 server over a NetQueue listener: one callback per complete request, and a response
/// built on the request object.

/// @defgroup http_forms Form Bodies
/// @ingroup http
/// `application/x-www-form-urlencoded` and `multipart/form-data` request bodies.

/// @}  // end of http group

CX_C_BEGIN

/// @addtogroup http_headers
/// @{

/// void httpHeadersInit(HttpHeaders *h);
///
/// Initializes an empty header list
///
/// @param h Header list to initialize
#define httpHeadersInit(h)               \
    do {                                 \
        saInit(&(h)->names, string, 8);  \
        saInit(&(h)->values, string, 8); \
    } while (0)

/// void httpHeadersDestroy(HttpHeaders *h);
///
/// Releases a header list and every name and value in it
///
/// @param h Header list to release
#define httpHeadersDestroy(h)    \
    do {                         \
        saDestroy(&(h)->names);  \
        saDestroy(&(h)->values); \
    } while (0)

/// int32 httpHeadersCount(HttpHeaders *h);
///
/// Number of header fields in the list, counting each repeat of a name separately
///
/// @param h Header list
/// @return Field count
#define httpHeadersCount(h) saSize((h)->names)

/// Append a header field, keeping any existing field with the same name
///
/// This is the right call for a field that legitimately repeats -- `Set-Cookie` above all. Use
/// httpHeadersSet() for a field that should appear once.
///
/// @param h Header list
/// @param name Field name; stored as given, matched case-insensitively
/// @param value Field value
/// @return true on success
bool httpHeadersAdd(_Inout_ HttpHeaders* h, _In_opt_ strref name, _In_opt_ strref value);

/// Set a header field, replacing every existing field with the same name
///
/// The replacement keeps the position of the first existing occurrence, so setting a header that
/// was already present does not move it to the end of the block.
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @param value Field value
/// @return true on success
bool httpHeadersSet(_Inout_ HttpHeaders* h, _In_opt_ strref name, _In_opt_ strref value);

/// Find the first value for a header field
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @param out Receives a copy of the value; untouched when the field is absent
/// @return true if the field was present
_Success_(return) bool
httpHeadersGet(_In_ const HttpHeaders* h, _In_opt_ strref name, _Inout_ strhandle out);

/// Collect every value for a header field, in order
///
/// The call `Set-Cookie` needs: httpHeadersGet() would answer with the first of several and
/// silently lose the rest.
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @param out Receives the values; initialized by this call even when there are none
/// @return Number of values found
int32 httpHeadersGetAll(_In_ const HttpHeaders* h, _In_opt_ strref name, _Out_ sa_string* out);

/// Test whether a header field is present
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @return true if at least one field with that name exists
_Pure bool httpHeadersHas(_In_ const HttpHeaders* h, _In_opt_ strref name);

/// Remove every header field with a given name
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @return Number of fields removed
int32 httpHeadersRemove(_Inout_ HttpHeaders* h, _In_opt_ strref name);

/// Remove every field, leaving the list usable
///
/// @param h Header list
void httpHeadersClear(_Inout_ HttpHeaders* h);

/// Test whether a comma-separated header field contains a token
///
/// The pattern most of the small HTTP headers share -- `Connection: keep-alive, Upgrade`,
/// `Transfer-Encoding: gzip, chunked`. Splits on commas, trims whitespace, and compares each token
/// case-insensitively. Every occurrence of a repeated field is searched.
///
/// @param h Header list
/// @param name Field name; matched case-insensitively
/// @param token Token to look for; matched case-insensitively
/// @return true if any occurrence of the field lists the token
///
/// Example:
/// @code
///   if (httpHeadersHasToken(&resp, _SL("Connection"), _SL("close")))
///       // do not reuse this connection
/// @endcode
_Pure bool httpHeadersHasToken(_In_ const HttpHeaders* h, _In_opt_ strref name,
                               _In_opt_ strref token);

/// Format a header block ready for the wire
///
/// Each field as `Name: value\r\n`, in list order. Does **not** append the blank line that ends the
/// block -- the caller adds that after any fields it is contributing itself.
///
/// @param out Receives the formatted block; appended to, not replaced
/// @param h Header list
/// @return true on success
_Success_(return) bool httpHeadersFormat(_Inout_ strhandle out, _In_ const HttpHeaders* h);

/// @}

/// @addtogroup http_headers
/// @{

/// Format a timestamp as an HTTP-date
///
/// Produces the preferred RFC 9110 form, which is RFC 1123 with a hard-coded GMT zone:
/// `Sun, 06 Nov 1994 08:49:37 GMT`. This is the only form that may be *sent*.
///
/// @param out Receives the formatted date
/// @param time Timestamp in cx's canonical form (microseconds, Julian epoch, UTC)
/// @return true on success
_Success_(return) bool httpDateFormat(_Inout_ strhandle out, int64 time);

/// Parse an HTTP-date
///
/// Accepts all three forms RFC 9110 requires a recipient to understand: the preferred RFC 1123
/// form, the obsolete RFC 850 form (`Sunday, 06-Nov-94 08:49:37 GMT`) with its two-digit year, and
/// asctime (`Sun Nov  6 08:49:37 1994`). Only the first is ever generated.
///
/// @param out Receives the timestamp in cx's canonical form
/// @param s Date text
/// @return true if the date parsed
_Success_(return) bool httpDateParse(_Out_ int64* out, _In_opt_ strref s);

/// @}

/// @addtogroup http_types
/// @{

/// Fill in the default limits
///
/// Generous enough that no legitimate peer trips them, small enough that a hostile one cannot make
/// cxhttp allocate on its behalf. `maxBodyBytes` comes back as 0, meaning unlimited.
///
/// @param out Receives the defaults
///
/// Example:
/// @code
///   HttpLimits lim;
///   httpLimitsDefault(&lim);
///   lim.maxBodyBytes = 1024 * 1024;      // this server accepts at most a megabyte
///   srv->limits = lim;
/// @endcode
void httpLimitsDefault(_Out_ HttpLimits* out);

/// @}

/// @addtogroup http_url
/// @{

/// Parse a URL into its components
///
/// Accepts an absolute URL with a scheme cxhttp understands (`http` or `https`). The scheme and
/// host are lowercased; the path, userinfo and fragment are percent-decoded; the query is left
/// encoded, because splitting it into pairs has to happen before decoding. An empty path becomes
/// "/".
///
/// `out` is fully initialized on success and left untouched on failure, so a failed parse never
/// needs a destroy.
///
/// @param out Receives the parsed URL; release it with httpUrlDestroy()
/// @param url The URL to parse
/// @return true if the URL parsed and its scheme is supported
///
/// Example:
/// @code
///   HttpUrl u;
///   if (httpUrlParse(&u, _SL("https://example.com:8443/a%20b?x=1#top"))) {
///       // u.host == "example.com", u.port == 8443, u.path == "/a b", u.query == "x=1"
///       httpUrlDestroy(&u);
///   }
/// @endcode
_Success_(return) bool httpUrlParse(_Out_ HttpUrl* out, _In_opt_ strref url);

/// Parse a request target, as a server receives it on the request line
///
/// Accepts the two forms a server sees in practice:
///
/// - **origin-form** -- `/a/b?x=1`, which is what a browser sends. `scheme` and `host` come back
///   empty, so the Host header is what names the server.
/// - **absolute-form** -- `http://example.com/a/b?x=1`. Every field is filled in, and the host in
///   the target outranks the Host header.
///
/// `*`, which OPTIONS may use to mean "the server itself", comes back as a `path` of `"*"`.
///
/// The path is percent-decoded and its `.` and `..` segments are removed, so what comes back is
/// safe to compare against a prefix. Note that a `%2F` in the target decodes to a plain `/` and is
/// then indistinguishable from a real separator; a caller that needs to tell them apart should look
/// at the raw target instead.
///
/// A target carrying a `#fragment` is rejected, as is anything in a form a server may not accept.
///
/// @param out Receives the parsed target; release it with httpUrlDestroy()
/// @param target The request target, exactly as it appeared on the request line
/// @return true if the target is a form a server may accept
///
/// Example:
/// @code
///   HttpUrl u;
///   if (httpUrlParseTarget(&u, _SL("/search?q=cats"))) {
///       // u.path == "/search", u.query == "q=cats"
///       httpUrlDestroy(&u);
///   }
/// @endcode
_Success_(return) bool httpUrlParseTarget(_Out_ HttpUrl* out, _In_opt_ strref target);

/// Resolve a possibly-relative reference against a base URL
///
/// Implements the common cases of RFC 3986 reference resolution, which is what following a
/// `Location` header needs: an absolute URL replaces the base entirely, a network-path reference
/// (`//host/path`) keeps the scheme, an absolute path replaces the path, and a relative path is
/// merged against the base's directory with `.` and `..` removed.
///
/// @param out Receives the resolved URL; release it with httpUrlDestroy()
/// @param base The URL the reference was found in
/// @param ref The reference to resolve
/// @return true if the result is a usable absolute URL
_Success_(
    return) bool httpUrlResolve(_Out_ HttpUrl* out, _In_ const HttpUrl* base, _In_opt_ strref ref);

/// Reassemble a parsed URL into its string form
///
/// Re-encodes the components that were decoded by the parser, so a parse/format round trip is
/// stable (though not necessarily byte-identical to the input: the output is normalized).
///
/// @param out Receives the formatted URL
/// @param url The URL to format
/// @return true on success
_Success_(return) bool httpUrlFormat(_Inout_ strhandle out, _In_ const HttpUrl* url);

/// Build the request-target for a URL: path plus query
///
/// What goes on the request line of an origin-form request -- "/a/b?x=1", or "/" when the URL had
/// no path. The fragment is never sent.
///
/// @param out Receives the request target
/// @param url The URL being requested
/// @return true on success
_Success_(return) bool httpUrlTarget(_Inout_ strhandle out, _In_ const HttpUrl* url);

/// Build the Host header value for a URL
///
/// The host, bracketed if it is an IPv6 literal, with ":port" appended only when the URL named a
/// port that is not the scheme's default.
///
/// @param out Receives the header value
/// @param url The URL being requested
/// @return true on success
_Success_(return) bool httpUrlHostHeader(_Inout_ strhandle out, _In_ const HttpUrl* url);

/// The default port for a URL scheme
///
/// @param scheme Scheme name, case-insensitive
/// @return 80 for http, 443 for https, 0 for anything else
_Pure uint16 httpSchemeDefaultPort(_In_opt_ strref scheme);

/// The port a URL will actually be dialed on
///
/// The explicit port if the URL named one, otherwise the scheme default.
///
/// @param url The URL
/// @return Port in host byte order, or 0 if the scheme has no default
_Pure uint16 httpUrlEffectivePort(_In_ const HttpUrl* url);

/// Release every component of a parsed URL
///
/// @param url URL to release; safe to call on an already-destroyed URL
void httpUrlDestroy(_Inout_ HttpUrl* url);

/// Which characters a percent-encoder leaves alone
///
/// The unreserved set is always safe. Each component of a URL additionally tolerates some of the
/// reserved characters unescaped, and escaping them anyway produces a URL that is correct but ugly
/// and that no other implementation would have written.
typedef enum {
    /// @brief Escape everything outside the RFC 3986 unreserved set (`A-Za-z0-9-._~`)
    ///
    /// The right choice for a value going into a query string or a form body, where every reserved
    /// character is a separator that must not be mistaken for one.
    HTTPENC_Strict = 0,

    /// @brief Also leave `/` alone, for encoding a whole path
    HTTPENC_Path,

    /// @brief Also leave the sub-delimiters and `/?:@` alone, for a whole query string
    HTTPENC_Query
} HttpEncodeSet;

/// Percent-encode a string
///
/// @param out Receives the encoded text
/// @param s Text to encode (binary-safe; encodes by byte, not by code point)
/// @param set Which characters to leave unescaped
/// @return true on success
_Success_(return) bool httpUrlEncode(_Inout_ strhandle out, _In_opt_ strref s, HttpEncodeSet set);

/// Percent-decode a string
///
/// A `%` not followed by two hex digits is a malformed escape. It is passed through literally
/// rather than rejected, because that is what every browser and server does and rejecting it would
/// fail on URLs that work everywhere else.
///
/// @param out Receives the decoded text
/// @param s Text to decode
/// @param plusIsSpace Treat '+' as an encoded space, as `application/x-www-form-urlencoded`
/// requires
///                    and as a URL path does not
/// @return true on success
_Success_(return) bool httpUrlDecode(_Inout_ strhandle out, _In_opt_ strref s, bool plusIsSpace);

/// Split a query string into decoded key/value pairs
///
/// Accepts `&` and `;` as separators, decodes both halves with `+` meaning space, and keeps an
/// empty value for a key that appeared without one. Order is preserved and duplicate keys are kept,
/// because a query string is a list rather than a map.
///
/// @param out Receives the pairs as an HttpHeaders list; initialized by this call
/// @param query Raw query string, without the leading '?'
/// @return true on success
_Success_(return) bool httpQueryParse(_Out_ HttpHeaders* out, _In_opt_ strref query);

/// Build a query string from key/value pairs
///
/// The inverse of httpQueryParse(): encodes both halves with the strict set and joins with `&`.
/// Also produces an `application/x-www-form-urlencoded` request body, which has the same grammar.
///
/// @param out Receives the query string, without a leading '?'
/// @param pairs Key/value pairs to encode
/// @return true on success
_Success_(return) bool httpQueryFormat(_Inout_ strhandle out, _In_ const HttpHeaders* pairs);

/// @}

/// @addtogroup http_forms
/// @{

/// Build an `application/x-www-form-urlencoded` request body
///
/// The same grammar as a query string, and the same code: use httpFormUrlEncodedType() for the
/// Content-Type that has to accompany it.
///
/// @param out Receives the encoded body
/// @param fields Field name/value pairs, in the order they should appear
/// @return true on success
///
/// Example:
/// @code
///   HttpHeaders form;
///   httpHeadersInit(&form);
///   httpHeadersAdd(&form, _SL("user"), _SL("ada"));
///   httpHeadersAdd(&form, _SL("token"), _SL("a+b/c"));
///
///   string body = 0;
///   httpFormUrlEncoded(&body, &form);
///   httprequestSetBody(req, body, httpFormUrlEncodedType());
/// @endcode
_Success_(return) bool httpFormUrlEncoded(_Inout_ strhandle out, _In_ const HttpHeaders* fields);

/// The Content-Type for a body built by httpFormUrlEncoded()
///
/// @return `application/x-www-form-urlencoded`
_Pure strref httpFormUrlEncodedType(void);

/// Begin a `multipart/form-data` body
///
/// Generates the boundary. Every part is added afterwards, and httpMultipartFinish() produces the
/// body and the Content-Type together.
///
/// @param mp Multipart state to initialize; release it with httpMultipartDestroy()
/// @return true on success, false if no random source was available for the boundary
///
/// Example:
/// @code
///   HttpMultipart mp;
///   httpMultipartInit(&mp);
///   httpMultipartAddField(&mp, _SL("comment"), _SL("looks good"));
///   httpMultipartAddFile(&mp, _SL("upload"), _SL("notes.txt"), _SL("text/plain"), data, len);
///
///   string body = 0, ctype = 0;
///   httpMultipartFinish(&mp, &body, &ctype);
///   httprequestSetBody(req, body, ctype);
///
///   strDestroy(&body);
///   strDestroy(&ctype);
///   httpMultipartDestroy(&mp);
/// @endcode
_Success_(return) bool httpMultipartInit(_Out_ HttpMultipart* mp);

/// Add a plain field
///
/// @param mp Multipart state
/// @param name Field name
/// @param value Field value
/// @return true on success, false once the body has been finished
bool httpMultipartAddField(_Inout_ HttpMultipart* mp, _In_opt_ strref name, _In_opt_ strref value);

/// Add a file part
///
/// @param mp Multipart state
/// @param name Field name the file was submitted under
/// @param filename Name to report for the file, or empty for a generic one
/// @param contentType Media type of the content, or empty for `application/octet-stream`
/// @param data File contents
/// @param len Length of `data` in bytes
/// @return true on success, false once the body has been finished
bool httpMultipartAddFile(_Inout_ HttpMultipart* mp, _In_opt_ strref name, _In_opt_ strref filename,
                          _In_opt_ strref contentType, _In_reads_bytes_opt_(len) const uint8* data,
                          size_t len);

/// Close the body and produce it along with its Content-Type
///
/// Safe to call more than once: the closing boundary is written once, so calling this twice
/// produces the same body rather than two of them.
///
/// @param mp Multipart state
/// @param body Receives the body, or NULL if only the content type is wanted
/// @param contentType Receives the Content-Type, including the boundary, or NULL
/// @return true on success
///
/// Both outputs are optional, which is why they are spelled `string*` rather than `strhandle`:
/// that typedef carries a non-null annotation, and passing NULL through it is a diagnostic on
/// clang rather than the documented "I don't want this one".
_Success_(return) bool httpMultipartFinish(_Inout_ HttpMultipart* mp, _Inout_opt_ string* _Nullable body,
                                    _Inout_opt_ string* _Nullable contentType);

/// Release a multipart body
///
/// @param mp Multipart state to release
void httpMultipartDestroy(_Inout_ HttpMultipart* mp);

/// @}

CX_C_END

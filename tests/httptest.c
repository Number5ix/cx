// cxhttp: URLs, header lists, and HTTP-date.
//
// Everything here is pure parsing and formatting with no sockets involved, which is deliberate --
// these are the pieces the protocol machinery above them assumes are correct, so they are worth
// pinning down on their own before anything asynchronous is in the picture.

#include <cxhttp.h>
#include <cxhttp/http_private.h>
#include <cx/net.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

// The HttpConn tests below drive a real NetSocket against a raw OS socket standing in for a server,
// the same way tests/nettest.c builds a "raw peer" by hand. cx headers must precede windows.h.
#if defined(_WIN32)
#include <cx/platform/win.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(_PLATFORM_UNIX)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket    close
#endif

#define TEST_FILE  httptest
#define TEST_FUNCS httptest_funcs
#include "common.h"

// ---------------------------------------------------------------------------------------------
// URLs
// ---------------------------------------------------------------------------------------------

// Parse `instr` and require one component to equal `want`.
#define URLFIELD(instr, field, want)                                       \
    do {                                                                   \
        HttpUrl _u;                                                        \
        if (!httpUrlParse(&_u, _SL(instr))) {                              \
            ret = 1;                                                       \
        } else {                                                           \
            if (!strEq(_u.field, _SL(want)))                               \
                ret = 1;                                                   \
            httpUrlDestroy(&_u);                                           \
        }                                                                  \
    } while (0)

// Parse `instr` and require the port to be `want`.
#define URLPORT(instr, want)                                               \
    do {                                                                   \
        HttpUrl _u;                                                        \
        if (!httpUrlParse(&_u, _SL(instr))) {                              \
            ret = 1;                                                       \
        } else {                                                           \
            if (_u.port != (want))                                         \
                ret = 1;                                                   \
            httpUrlDestroy(&_u);                                           \
        }                                                                  \
    } while (0)

// Require the parser to reject a URL outright.
#define URLBAD(instr)                                                      \
    do {                                                                   \
        HttpUrl _u;                                                        \
        if (httpUrlParse(&_u, _SL(instr))) {                               \
            ret = 1;                                                       \
            httpUrlDestroy(&_u);                                           \
        }                                                                  \
    } while (0)

static int test_httptest_url(void)
{
    int ret = 0;

    URLFIELD("http://example.com/", scheme, "http");
    URLFIELD("http://example.com/", host, "example.com");
    URLFIELD("http://example.com/", path, "/");

    // A URL with no path at all still has one.
    URLFIELD("http://example.com", path, "/");
    URLFIELD("http://example.com?q=1", path, "/");
    URLFIELD("http://example.com?q=1", query, "q=1");

    // Scheme and host are lowercased; the path is not, because paths are case-sensitive.
    URLFIELD("HTTP://Example.COM/Path", scheme, "http");
    URLFIELD("HTTP://Example.COM/Path", host, "example.com");
    URLFIELD("HTTP://Example.COM/Path", path, "/Path");

    URLPORT("http://example.com/", 0);      // unspecified stays 0, not the default
    URLPORT("http://example.com:8080/", 8080);
    URLPORT("https://example.com:443/", 443);
    URLPORT("http://example.com:/", 0);     // empty port means "the default"

    // Query keeps its encoding, path and fragment do not.
    URLFIELD("http://h/a%20b?x=%26y#f%20g", path, "/a b");
    URLFIELD("http://h/a%20b?x=%26y#f%20g", query, "x=%26y");
    URLFIELD("http://h/a%20b?x=%26y#f%20g", fragment, "f g");

    // Userinfo, including a password containing an '@' -- the rightmost '@' is the separator.
    URLFIELD("http://user:pw@h/", user, "user");
    URLFIELD("http://user:pw@h/", pass, "pw");
    URLFIELD("http://user:p@ss@h/", pass, "p@ss");
    URLFIELD("http://user:p@ss@h/", host, "h");
    URLFIELD("http://user@h/", user, "user");
    URLFIELD("http://us%40er@h/", user, "us@er");

    // IPv6 literals: the brackets are structure, and the colons inside them are not a port.
    URLFIELD("http://[::1]/x", host, "::1");
    URLPORT("http://[::1]/x", 0);
    URLPORT("http://[::1]:8080/x", 8080);
    URLFIELD("http://[2001:db8::1]:99/", host, "2001:db8::1");

    HttpUrl u;
    if (httpUrlParse(&u, _SL("http://[::1]:8080/"))) {
        if (!u.ipv6Host)
            ret = 1;
        httpUrlDestroy(&u);
    } else {
        ret = 1;
    }

    URLBAD("");
    URLBAD("example.com");            // no scheme
    URLBAD("/just/a/path");           // relative; httpUrlResolve handles these
    URLBAD("ftp://example.com/");     // scheme cxhttp cannot dial
    URLBAD("mailto:a@b.com");         // not even an authority form
    URLBAD("http:///path");           // no host
    URLBAD("http://h:99999/");        // port out of range
    URLBAD("http://h:0/");            // port 0 is not dialable
    URLBAD("http://h:8o8/");          // non-numeric port
    URLBAD("http://[::1/");           // unterminated IPv6 literal

    return ret;
}

// Round-trip and target/Host derivation.
static int test_httptest_urlformat(void)
{
    int ret = 0;
    HttpUrl u;
    string s = 0;

    if (httpUrlParse(&u, _SL("https://user:pw@example.com:8443/a%20b?x=1#frag"))) {
        if (!httpUrlTarget(&s, &u) || !strEq(s, _SL("/a%20b?x=1")))
            ret = 1;

        // The port is named because 8443 is not the https default.
        if (!httpUrlHostHeader(&s, &u) || !strEq(s, _SL("example.com:8443")))
            ret = 1;

        if (!httpUrlFormat(&s, &u) ||
            !strEq(s, _SL("https://user:pw@example.com:8443/a%20b?x=1#frag")))
            ret = 1;

        httpUrlDestroy(&u);
    } else {
        ret = 1;
    }

    // A default port is not repeated in the Host header, and an unspecified one never was.
    if (httpUrlParse(&u, _SL("https://example.com:443/"))) {
        if (!httpUrlHostHeader(&s, &u) || !strEq(s, _SL("example.com")))
            ret = 1;
        httpUrlDestroy(&u);
    } else {
        ret = 1;
    }

    // An IPv6 host is bracketed again on the way out.
    if (httpUrlParse(&u, _SL("http://[::1]:8080/x"))) {
        if (!httpUrlHostHeader(&s, &u) || !strEq(s, _SL("[::1]:8080")))
            ret = 1;
        if (!httpUrlFormat(&s, &u) || !strEq(s, _SL("http://[::1]:8080/x")))
            ret = 1;
        httpUrlDestroy(&u);
    } else {
        ret = 1;
    }

    // No path and no query: the target is still "/".
    if (httpUrlParse(&u, _SL("http://example.com"))) {
        if (!httpUrlTarget(&s, &u) || !strEq(s, _SL("/")))
            ret = 1;
        httpUrlDestroy(&u);
    } else {
        ret = 1;
    }

    strDestroy(&s);
    return ret;
}

// Resolve `refstr` against `basestr` and require the formatted result to equal `want`.
#define URLRESOLVE(basestr, refstr, want)                                  \
    do {                                                                   \
        HttpUrl _b, _r;                                                    \
        string _s = 0;                                                     \
        if (!httpUrlParse(&_b, _SL(basestr))) {                            \
            ret = 1;                                                       \
        } else {                                                           \
            if (!httpUrlResolve(&_r, &_b, _SL(refstr))) {                  \
                ret = 1;                                                   \
            } else {                                                       \
                if (!httpUrlFormat(&_s, &_r) || !strEq(_s, _SL(want)))     \
                    ret = 1;                                               \
                httpUrlDestroy(&_r);                                       \
            }                                                              \
            httpUrlDestroy(&_b);                                           \
        }                                                                  \
        strDestroy(&_s);                                                   \
    } while (0)

// Reference resolution, which is what following a Location header needs.
static int test_httptest_urlresolve(void)
{
    int ret = 0;

    // An absolute reference ignores the base entirely, including its scheme.
    URLRESOLVE("http://a.com/x/y", "https://b.com/z", "https://b.com/z");

    // Network-path reference keeps only the scheme.
    URLRESOLVE("https://a.com/x/y", "//b.com/z", "https://b.com/z");

    // Absolute path replaces the path, keeps the authority.
    URLRESOLVE("http://a.com/x/y?q=1", "/z", "http://a.com/z");

    // Relative path merges against the base's directory, not the base itself.
    URLRESOLVE("http://a.com/x/y", "z", "http://a.com/x/z");
    URLRESOLVE("http://a.com/x/", "z", "http://a.com/x/z");

    // Dot segments collapse, and ".." cannot climb above the root.
    URLRESOLVE("http://a.com/x/y/z", "../q", "http://a.com/x/q");
    URLRESOLVE("http://a.com/x/y/z", "./q", "http://a.com/x/y/q");
    URLRESOLVE("http://a.com/x/y", "../../../q", "http://a.com/q");

    // Query-only and fragment-only references keep what they do not replace.
    URLRESOLVE("http://a.com/x/y?old=1", "?new=2", "http://a.com/x/y?new=2");
    URLRESOLVE("http://a.com/x/y?q=1", "#frag", "http://a.com/x/y?q=1#frag");

    // The port and userinfo survive a relative hop.
    URLRESOLVE("http://a.com:8080/x/y", "z", "http://a.com:8080/x/z");

    return ret;
}

// Percent coding, including the asymmetry between path, query and strict sets.
static int test_httptest_urlcodec(void)
{
    int ret   = 0;
    string s  = 0;

    if (!httpUrlEncode(&s, _SL("a b"), HTTPENC_Strict) || !strEq(s, _SL("a%20b")))
        ret = 1;
    if (!httpUrlEncode(&s, _SL("a/b"), HTTPENC_Strict) || !strEq(s, _SL("a%2Fb")))
        ret = 1;
    if (!httpUrlEncode(&s, _SL("a/b"), HTTPENC_Path) || !strEq(s, _SL("a/b")))
        ret = 1;

    // The unreserved set is never escaped, in any mode.
    if (!httpUrlEncode(&s, _SL("aZ0-._~"), HTTPENC_Strict) || !strEq(s, _SL("aZ0-._~")))
        ret = 1;

    // Separators must be escaped in strict mode or a value could forge a pair.
    if (!httpUrlEncode(&s, _SL("a&b=c"), HTTPENC_Strict) || !strEq(s, _SL("a%26b%3Dc")))
        ret = 1;

    // '+' is escaped, because form decoding would otherwise read it as a space.
    if (!httpUrlEncode(&s, _SL("a+b"), HTTPENC_Strict) || !strEq(s, _SL("a%2Bb")))
        ret = 1;

    // Encoding is per byte, so UTF-8 survives a round trip.
    if (!httpUrlEncode(&s, _SL("\xc3\xa9"), HTTPENC_Strict) || !strEq(s, _SL("%C3%A9")))
        ret = 1;

    if (!httpUrlDecode(&s, _SL("a%20b"), false) || !strEq(s, _SL("a b")))
        ret = 1;
    if (!httpUrlDecode(&s, _SL("a%c3%a9b"), false) || !strEq(s, _SL("a\xc3\xa9""b")))
        ret = 1;

    // '+' only means space where the caller says the grammar says so.
    if (!httpUrlDecode(&s, _SL("a+b"), false) || !strEq(s, _SL("a+b")))
        ret = 1;
    if (!httpUrlDecode(&s, _SL("a+b"), true) || !strEq(s, _SL("a b")))
        ret = 1;

    // A malformed escape passes through literally rather than failing the whole URL.
    if (!httpUrlDecode(&s, _SL("100%"), false) || !strEq(s, _SL("100%")))
        ret = 1;
    if (!httpUrlDecode(&s, _SL("a%zzb"), false) || !strEq(s, _SL("a%zzb")))
        ret = 1;
    if (!httpUrlDecode(&s, _SL("a%4"), false) || !strEq(s, _SL("a%4")))
        ret = 1;

    // A '%' that fails has to release both of the bytes it looked at, because either can start a
    // valid escape of its own. Every browser reads this as "%A".
    if (!httpUrlDecode(&s, _SL("%%41"), false) || !strEq(s, _SL("%A")))
        ret = 1;

    // The same, one byte later: the first '%' consumes "%4", and the '1' left over is a literal.
    if (!httpUrlDecode(&s, _SL("%%%41"), false) || !strEq(s, _SL("%%A")))
        ret = 1;

    // An escape straddling a rope's segment boundary. Decoding walks the string a byte at a time,
    // so a run boundary landing between the '%' and its digits is the case that would break if the
    // walk ever assumed one contiguous buffer.
    strref pre  = _S"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa%4";
    strref post = _S"1bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    string rope = 0;
    strAppend(&rope, pre);
    strAppend(&rope, post);
    if (strTestRopeDepth(rope) < 1) {
        ret = 1;   // not actually a rope; the run-crossing path would go untested
    } else {
        strref want = _S"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                       "Abbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        if (!httpUrlDecode(&s, rope, false) || !strEq(s, want))
            ret = 1;
        // Encoding walks the same way, so it gets the same treatment.
        string enc = 0;
        if (!httpUrlEncode(&enc, rope, HTTPENC_Strict) || strLen(enc) != strLen(rope) + 2)
            ret = 1;
        strDestroy(&enc);
    }
    strDestroy(&rope);

    strDestroy(&s);
    return ret;
}

// Query strings: order preserved, duplicates kept, and the encoding boundary respected.
static int test_httptest_query(void)
{
    int ret = 0;
    HttpHeaders q;
    string s = 0;

    httpQueryParse(&q, _SL("a=1&b=two&a=3"));
    if (httpHeadersCount(&q) != 3)
        ret = 1;
    if (!httpHeadersGet(&q, _SL("a"), &s) || !strEq(s, _SL("1")))
        ret = 1;   // first wins for a duplicated key

    sa_string all;
    if (httpHeadersGetAll(&q, _SL("a"), &all) != 2)
        ret = 1;
    else if (!strEq(all.a[0], _SL("1")) || !strEq(all.a[1], _SL("3")))
        ret = 1;
    saDestroy(&all);
    httpHeadersDestroy(&q);

    // A '%26' inside a value is data; a bare '&' is a separator. Getting this backwards is a
    // parameter-smuggling bug, which is why the query is decoded after splitting and not before.
    httpQueryParse(&q, _SL("x=a%26b=c"));
    if (httpHeadersCount(&q) != 1)
        ret = 1;
    if (!httpHeadersGet(&q, _SL("x"), &s) || !strEq(s, _SL("a&b=c")))
        ret = 1;
    httpHeadersDestroy(&q);

    // '+' is a space in a query, and ';' separates like '&'.
    httpQueryParse(&q, _SL("n=a+b;m=c"));
    if (httpHeadersCount(&q) != 2)
        ret = 1;
    if (!httpHeadersGet(&q, _SL("n"), &s) || !strEq(s, _SL("a b")))
        ret = 1;
    httpHeadersDestroy(&q);

    // A bare key keeps an empty value rather than being dropped.
    httpQueryParse(&q, _SL("debug&v=1"));
    if (httpHeadersCount(&q) != 2)
        ret = 1;
    if (!httpHeadersHas(&q, _SL("debug")))
        ret = 1;
    httpHeadersDestroy(&q);

    // Empty input is an empty list, not a failure.
    httpQueryParse(&q, _SL(""));
    if (httpHeadersCount(&q) != 0)
        ret = 1;
    httpHeadersDestroy(&q);

    // Format escapes everything structural, so a round trip is lossless.
    httpHeadersInit(&q);
    httpHeadersAdd(&q, _SL("a b"), _SL("c&d"));
    httpHeadersAdd(&q, _SL("e"), _SL("f=g"));
    if (!httpQueryFormat(&s, &q) || !strEq(s, _SL("a%20b=c%26d&e=f%3Dg")))
        ret = 1;
    httpHeadersDestroy(&q);

    httpQueryParse(&q, s);
    if (httpHeadersCount(&q) != 2)
        ret = 1;
    if (!httpHeadersGet(&q, _SL("a b"), &s) || !strEq(s, _SL("c&d")))
        ret = 1;
    httpHeadersDestroy(&q);

    strDestroy(&s);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Header lists
// ---------------------------------------------------------------------------------------------

static int test_httptest_headers(void)
{
    int ret = 0;
    HttpHeaders h;
    string s = 0;

    httpHeadersInit(&h);

    httpHeadersAdd(&h, _SL("Content-Type"), _SL("text/html"));
    httpHeadersAdd(&h, _SL("Content-Length"), _SL("42"));

    // Lookup is case-insensitive both ways round.
    if (!httpHeadersGet(&h, _SL("content-type"), &s) || !strEq(s, _SL("text/html")))
        ret = 1;
    if (!httpHeadersGet(&h, _SL("CONTENT-LENGTH"), &s) || !strEq(s, _SL("42")))
        ret = 1;
    if (httpHeadersGet(&h, _SL("Missing"), &s))
        ret = 1;
    if (!httpHeadersHas(&h, _SL("Content-Type")) || httpHeadersHas(&h, _SL("Nope")))
        ret = 1;

    // Duplicates are the whole reason this is not a hashtable: three Set-Cookie lines must survive
    // as three, because folding them into one comma-separated value is not reversible.
    httpHeadersAdd(&h, _SL("Set-Cookie"), _SL("a=1"));
    httpHeadersAdd(&h, _SL("Set-Cookie"), _SL("b=2"));
    httpHeadersAdd(&h, _SL("Set-Cookie"), _SL("c=3, with a comma"));

    sa_string all;
    if (httpHeadersGetAll(&h, _SL("set-cookie"), &all) != 3)
        ret = 1;
    else if (!strEq(all.a[0], _SL("a=1")) || !strEq(all.a[2], _SL("c=3, with a comma")))
        ret = 1;
    saDestroy(&all);

    if (httpHeadersCount(&h) != 5)
        ret = 1;

    // Set replaces every occurrence and keeps the position of the first.
    httpHeadersSet(&h, _SL("SET-COOKIE"), _SL("only=1"));
    if (httpHeadersGetAll(&h, _SL("Set-Cookie"), &all) != 1)
        ret = 1;
    saDestroy(&all);
    if (httpHeadersCount(&h) != 3)
        ret = 1;
    if (!strEq(h.names.a[2], _SL("Set-Cookie")))
        ret = 1;   // still third, not moved to the end

    // Set on an absent field is an add.
    httpHeadersSet(&h, _SL("Server"), _SL("cx"));
    if (httpHeadersCount(&h) != 4)
        ret = 1;

    if (httpHeadersRemove(&h, _SL("content-length")) != 1)
        ret = 1;
    if (httpHeadersRemove(&h, _SL("content-length")) != 0)
        ret = 1;
    if (httpHeadersCount(&h) != 3)
        ret = 1;

    // Order is preserved across all of that.
    if (!strEq(h.names.a[0], _SL("Content-Type")))
        ret = 1;

    httpHeadersClear(&h);
    if (httpHeadersCount(&h) != 0)
        ret = 1;

    // An empty name is refused rather than producing a field nothing can address.
    if (httpHeadersAdd(&h, _SL(""), _SL("x")))
        ret = 1;

    httpHeadersDestroy(&h);
    strDestroy(&s);
    return ret;
}

static int test_httptest_headertoken(void)
{
    int ret = 0;
    HttpHeaders h;

    httpHeadersInit(&h);
    httpHeadersAdd(&h, _SL("Connection"), _SL("keep-alive, Upgrade"));
    httpHeadersAdd(&h, _SL("Transfer-Encoding"), _SL("chunked"));

    if (!httpHeadersHasToken(&h, _SL("connection"), _SL("upgrade")))
        ret = 1;   // case-insensitive on both the field name and the token
    if (!httpHeadersHasToken(&h, _SL("Connection"), _SL("keep-alive")))
        ret = 1;
    if (httpHeadersHasToken(&h, _SL("Connection"), _SL("close")))
        ret = 1;
    if (!httpHeadersHasToken(&h, _SL("Transfer-Encoding"), _SL("chunked")))
        ret = 1;

    // A token must match whole, not as a substring of a longer one.
    if (httpHeadersHasToken(&h, _SL("Connection"), _SL("keep")))
        ret = 1;

    // A field split across two lines is searched in both, because a peer may send it either way.
    httpHeadersAdd(&h, _SL("Connection"), _SL("close"));
    if (!httpHeadersHasToken(&h, _SL("Connection"), _SL("close")))
        ret = 1;

    if (httpHeadersHasToken(&h, _SL("Absent"), _SL("x")))
        ret = 1;

    httpHeadersDestroy(&h);
    return ret;
}

static int test_httptest_headerformat(void)
{
    int ret = 0;
    HttpHeaders h;
    string s = 0;

    httpHeadersInit(&h);
    httpHeadersAdd(&h, _SL("Host"), _SL("example.com"));
    httpHeadersAdd(&h, _SL("Accept"), _SL("*/*"));

    if (!httpHeadersFormat(&s, &h) ||
        !strEq(s, _SL("Host: example.com\r\nAccept: */*\r\n")))
        ret = 1;

    // The block does not terminate itself: the caller adds the blank line after contributing
    // whatever fields it computes for itself.
    if (strEndsWith(s, _SL("\r\n\r\n")))
        ret = 1;

    httpHeadersDestroy(&h);
    strDestroy(&s);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// HTTP-date
// ---------------------------------------------------------------------------------------------

static int test_httptest_date(void)
{
    int ret  = 0;
    string s = 0;
    int64 t  = 0;

    // The canonical example from RFC 9110, in all three forms it requires a recipient to accept.
    // All three name the same instant, so they must parse to the same timestamp.
    int64 rfc1123 = 0, rfc850 = 0, asctime = 0;

    if (!httpDateParse(&rfc1123, _SL("Sun, 06 Nov 1994 08:49:37 GMT")))
        ret = 1;
    if (!httpDateParse(&rfc850, _SL("Sunday, 06-Nov-94 08:49:37 GMT")))
        ret = 1;
    if (!httpDateParse(&asctime, _SL("Sun Nov  6 08:49:37 1994")))
        ret = 1;

    if (rfc1123 != rfc850 || rfc1123 != asctime)
        ret = 1;

    // Only the preferred form is ever generated, whichever form was read.
    if (!httpDateFormat(&s, rfc1123) || !strEq(s, _SL("Sun, 06 Nov 1994 08:49:37 GMT")))
        ret = 1;

    // Round trip through a formatted date.
    if (!httpDateParse(&t, s) || t != rfc1123)
        ret = 1;

    // Two-digit years pivot at 70.
    int64 y2000 = 0, y1999 = 0;
    if (!httpDateParse(&y2000, _SL("Sat, 01-Jan-00 00:00:00 GMT")) ||
        !httpDateParse(&y1999, _SL("Fri, 01-Jan-99 00:00:00 GMT")))
        ret = 1;
    if (y2000 <= y1999)
        ret = 1;

    // A single-digit asctime day is space-padded, and the padding run must not become a field.
    if (!httpDateParse(&t, _SL("Sun Nov  6 08:49:37 1994")) || t != rfc1123)
        ret = 1;

    // Surrounding whitespace is tolerated; a header value often arrives with it.
    if (!httpDateParse(&t, _SL("  Sun, 06 Nov 1994 08:49:37 GMT  ")) || t != rfc1123)
        ret = 1;

    // Malformed dates are rejected rather than guessed at: a corrupt Expires that parses to a
    // plausible wrong answer is worse than one that fails.
    if (httpDateParse(&t, _SL("")))
        ret = 1;
    if (httpDateParse(&t, _SL("not a date")))
        ret = 1;
    if (httpDateParse(&t, _SL("Sun, 06 Nov 1994 08:49:37")))
        ret = 1;   // no zone
    if (httpDateParse(&t, _SL("Sun, 06 Nov 1994 08:49:37 EST")))
        ret = 1;   // HTTP-date is always GMT
    if (httpDateParse(&t, _SL("Sun, 32 Nov 1994 08:49:37 GMT")))
        ret = 1;   // day out of range
    if (httpDateParse(&t, _SL("Sun, 06 Xxx 1994 08:49:37 GMT")))
        ret = 1;   // unknown month
    if (httpDateParse(&t, _SL("Sun, 06 Nov 1994 25:49:37 GMT")))
        ret = 1;   // hour out of range
    if (httpDateParse(&t, _SL("Sun, 31 Feb 1994 08:49:37 GMT")))
        ret = 1;   // date that does not exist

    strDestroy(&s);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Message parsing
//
// The parser consumes a BufRing, so every test here feeds one and drains what comes out. Feeding a
// message one byte at a time is as important as feeding it whole: an incremental parser that only
// works when the whole message arrives in one read is a parser that works in tests and fails
// against a real network.
// ---------------------------------------------------------------------------------------------

// Drive the parser to a terminal state over `src`, accumulating body bytes into `body`.
// Returns the last result, so a test can assert on Complete vs Error.
static HttpParseResult drive(HttpParser* p, BufRing* src, string* body, int* headCount)
{
    for (;;) {
        HttpParseResult r = httpParserStep(p, src);

        if (r == HTTPP_Head) {
            if (headCount)
                (*headCount)++;
            continue;
        }

        if (r == HTTPP_Body) {
            uint8 buf[4096];
            size_t n;
            while ((n = bufringRead(&p->out, buf, sizeof(buf))) > 0)
                strAppendBytes(body, buf, (uint32)n);
            continue;
        }

        return r;   // NeedMore, Complete, or Error
    }
}

// Parse `text` as one message, all at once.
static HttpParseResult parseWhole(HttpParser* p, const char* text, string* body, int* headCount)
{
    BufRing src;
    bufringInit(&src, 256);
    bufringWrite(&src, (const uint8*)text, strlen(text));

    HttpParseResult r = drive(p, &src, body, headCount);

    bufringDestroy(&src);
    return r;
}

// Parse `text` one byte at a time, which is the shape a slow or fragmented connection produces.
static HttpParseResult parseDribbled(HttpParser* p, const char* text, string* body, int* headCount)
{
    BufRing src;
    bufringInit(&src, 256);

    size_t len        = strlen(text);
    HttpParseResult r = HTTPP_NeedMore;

    for (size_t i = 0; i < len; i++) {
        bufringWrite(&src, (const uint8*)text + i, 1);
        r = drive(p, &src, body, headCount);
        if (r == HTTPP_Complete || r == HTTPP_Error)
            break;
    }

    bufringDestroy(&src);
    return r;
}

// A response with a Content-Length body, parsed both whole and byte by byte.
static int test_httptest_parseresp(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0, v = 0;
    int heads   = 0;

    static const char* kResp = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: 5\r\n"
                               "\r\n"
                               "hello";

    httpParserInit(&p, false, NULL);
    if (parseWhole(&p, kResp, &body, &heads) != HTTPP_Complete)
        ret = 1;
    if (p.status != 200 || p.version != HTTPVER_1_1)
        ret = 1;
    if (!strEq(p.reason, _SL("OK")))
        ret = 1;
    if (heads != 1)
        ret = 1;
    if (!strEq(body, _SL("hello")))
        ret = 1;
    if (!httpHeadersGet(&p.headers, _SL("content-type"), &v) || !strEq(v, _SL("text/plain")))
        ret = 1;
    if (!httpParserKeepAlive(&p))
        ret = 1;   // 1.1 with no Connection header persists

    // The same bytes arriving one at a time must produce exactly the same result. This is the test
    // that catches a parser that peeks past what it has actually received.
    httpParserReset(&p);
    strDestroy(&body);
    heads = 0;
    if (parseDribbled(&p, kResp, &body, &heads) != HTTPP_Complete)
        ret = 1;
    if (p.status != 200 || heads != 1 || !strEq(body, _SL("hello")))
        ret = 1;

    httpParserDestroy(&p);
    strDestroy(&body);
    strDestroy(&v);
    return ret;
}

// Chunked decoding, including a chunk extension and a trailer field.
static int test_httptest_parsechunked(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0;

    static const char* kResp = "HTTP/1.1 200 OK\r\n"
                               "Transfer-Encoding: chunked\r\n"
                               "\r\n"
                               "5\r\nhello\r\n"
                               "6;ext=v\r\n world\r\n"
                               "0\r\n"
                               "X-Trailer: dropped\r\n"
                               "\r\n";

    httpParserInit(&p, false, NULL);
    if (parseWhole(&p, kResp, &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (!strEq(body, _SL("hello world")))
        ret = 1;
    // Trailers are read and dropped rather than merged into the header block the application has
    // already been shown.
    if (httpHeadersHas(&p.headers, _SL("X-Trailer")))
        ret = 1;

    httpParserReset(&p);
    strDestroy(&body);
    if (parseDribbled(&p, kResp, &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (!strEq(body, _SL("hello world")))
        ret = 1;

    // A zero-length chunked body is legal and is not the same as no body.
    httpParserReset(&p);
    strDestroy(&body);
    if (parseWhole(&p,
                   "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                   &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (!strEmpty(body))
        ret = 1;

    httpParserDestroy(&p);
    strDestroy(&body);
    return ret;
}

// The four body-framing rules, in the order RFC 9110 resolves them.
static int test_httptest_parseframing(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0;

    // 204 has no body however it is framed, and a Content-Length sent anyway does not create one.
    httpParserInit(&p, false, NULL);
    if (parseWhole(&p, "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n", &body, NULL) !=
        HTTPP_Complete)
        ret = 1;
    if (!strEmpty(body))
        ret = 1;

    // Neither does a 304.
    httpParserReset(&p);
    if (parseWhole(&p, "HTTP/1.1 304 Not Modified\r\nContent-Length: 9\r\n\r\n", &body, NULL) !=
        HTTPP_Complete)
        ret = 1;
    if (!strEmpty(body))
        ret = 1;

    // A response to HEAD is framed like the GET response would have been, but carries no body. The
    // parser only knows that because the caller told it which method was sent.
    httpParserReset(&p);
    p.reqMethod = HTTP_Head;
    if (parseWhole(&p, "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\n", &body, NULL) !=
        HTTPP_Complete)
        ret = 1;
    if (!strEmpty(body))
        ret = 1;

    // Close-delimited: no framing headers at all, so the body runs until the peer hangs up.
    httpParserReset(&p);
    p.reqMethod = HTTP_Get;
    strDestroy(&body);
    {
        BufRing src;
        bufringInit(&src, 256);
        static const char* r = "HTTP/1.1 200 OK\r\n\r\nsome body bytes";
        bufringWrite(&src, (const uint8*)r, strlen(r));

        if (drive(&p, &src, &body, NULL) != HTTPP_NeedMore)
            ret = 1;   // still open: without a close there is no way to know it ended
        if (httpParserEOF(&p) != HTTPP_Complete)
            ret = 1;
        if (!strEq(body, _SL("some body bytes")))
            ret = 1;
        if (httpParserKeepAlive(&p))
            ret = 1;   // a close-delimited response spends its connection by definition

        bufringDestroy(&src);
    }

    // Truncation is an error, not a short body. A response cut off mid-body must never be handed
    // up as if it were whole.
    httpParserReset(&p);
    strDestroy(&body);
    {
        BufRing src;
        bufringInit(&src, 256);
        static const char* r = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        bufringWrite(&src, (const uint8*)r, strlen(r));

        drive(&p, &src, &body, NULL);
        if (httpParserEOF(&p) != HTTPP_Error || p.err != HTTPERR_Closed)
            ret = 1;

        bufringDestroy(&src);
    }

    httpParserDestroy(&p);
    strDestroy(&body);
    return ret;
}

// Feed `text` and require the parser to reject it with `wanterr`.
#define PARSEBAD(text, wanterr)                                            \
    do {                                                                   \
        HttpParser _p;                                                     \
        string _b = 0;                                                     \
        httpParserInit(&_p, false, NULL);                                  \
        if (parseWhole(&_p, text, &_b, NULL) != HTTPP_Error)               \
            ret = 1;                                                       \
        else if (_p.err != (wanterr))                                      \
            ret = 1;                                                       \
        httpParserDestroy(&_p);                                            \
        strDestroy(&_b);                                                   \
    } while (0)

// What the parser refuses, and why. Most of these are smuggling primitives rather than merely
// malformed input, which is why they are rejected instead of being interpreted generously.
static int test_httptest_parsereject(void)
{
    int ret = 0;

    // Content-Length and Transfer-Encoding together: two implementations that resolve this
    // differently see different message boundaries. There is no safe interpretation to pick.
    PARSEBAD("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\nhello",
             HTTPERR_BadMessage);

    // Conflicting duplicate Content-Length -- the case a hashtable-backed header store would have
    // hidden by keeping only one of them.
    PARSEBAD("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 9\r\n\r\nhello",
             HTTPERR_BadMessage);

    // Obs-fold: deprecated, a smuggling vector, and sent by nothing current.
    PARSEBAD("HTTP/1.1 200 OK\r\nX-A: one\r\n  two\r\n\r\n", HTTPERR_BadMessage);

    // Whitespace before the colon, which implementations disagree about trimming.
    PARSEBAD("HTTP/1.1 200 OK\r\nContent-Length : 5\r\n\r\nhello", HTTPERR_BadMessage);

    // A header name that is not a token.
    PARSEBAD("HTTP/1.1 200 OK\r\nBad Name: x\r\n\r\n", HTTPERR_BadMessage);
    PARSEBAD("HTTP/1.1 200 OK\r\n: novalue\r\n\r\n", HTTPERR_BadMessage);

    // Start lines that are not start lines.
    PARSEBAD("HTTP/1.1\r\n\r\n", HTTPERR_BadMessage);
    PARSEBAD("HTTP/1.1 xx OK\r\n\r\n", HTTPERR_BadMessage);
    PARSEBAD("HTTP/1.1 99 Too Low\r\n\r\n", HTTPERR_BadMessage);
    PARSEBAD("HTTP/3.0 200 OK\r\n\r\n", HTTPERR_BadMessage);
    PARSEBAD("garbage\r\n\r\n", HTTPERR_BadMessage);

    // HTTP/1.0 has no chunked encoding, so a 1.0 message claiming it is malformed.
    PARSEBAD("HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
             HTTPERR_BadMessage);

    // A transfer-coding we cannot frame is not something to continue past.
    PARSEBAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", HTTPERR_BadMessage);

    // Chunk framing is strict where head parsing is lenient: a bare LF in a chunk-size line is one
    // of the classic ways to make two parsers disagree about message boundaries.
    PARSEBAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\nhello\r\n0\r\n\r\n",
             HTTPERR_BadMessage);

    // A chunk size that is not strict hex.
    PARSEBAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0x5\r\nhello\r\n0\r\n\r\n",
             HTTPERR_BadMessage);
    PARSEBAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n 5\r\nhello\r\n0\r\n\r\n",
             HTTPERR_BadMessage);

    // A chunk whose data does not end where its size said it would.
    PARSEBAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX\r\n0\r\n\r\n",
             HTTPERR_BadMessage);

    return ret;
}

// The head is lenient about line terminators where that is harmless, and the limits hold.
static int test_httptest_parselimits(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0;

    // Bare LF in the head is accepted: every implementation does, and on its own it is not a way
    // to make two parsers disagree.
    httpParserInit(&p, false, NULL);
    if (parseWhole(&p, "HTTP/1.1 200 OK\nContent-Length: 2\n\nhi", &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (!strEq(body, _SL("hi")))
        ret = 1;
    httpParserDestroy(&p);

    // A header line longer than maxLineLen is refused rather than buffered.
    {
        HttpLimits lim;
        httpLimitsDefault(&lim);
        lim.maxLineLen = 64;

        string msg = 0;
        strAppend(&msg, _SL("HTTP/1.1 200 OK\r\nX-Long: "));
        for (int i = 0; i < 200; i++)
            strAppendChar(&msg, 'a');
        strAppend(&msg, _SL("\r\n\r\n"));

        httpParserInit(&p, false, &lim);
        strDestroy(&body);
        if (parseWhole(&p, strC(msg), &body, NULL) != HTTPP_Error || p.err != HTTPERR_TooLarge)
            ret = 1;
        httpParserDestroy(&p);
        strDestroy(&msg);
    }

    // Too many header fields.
    {
        HttpLimits lim;
        httpLimitsDefault(&lim);
        lim.maxHeaderCount = 4;

        httpParserInit(&p, false, &lim);
        strDestroy(&body);
        if (parseWhole(&p,
                       "HTTP/1.1 200 OK\r\nA: 1\r\nB: 2\r\nC: 3\r\nD: 4\r\nE: 5\r\n\r\n",
                       &body, NULL) != HTTPP_Error ||
            p.err != HTTPERR_TooLarge)
            ret = 1;
        httpParserDestroy(&p);
    }

    // A body over maxBodyBytes is refused rather than accumulated.
    {
        HttpLimits lim;
        httpLimitsDefault(&lim);
        lim.maxBodyBytes = 4;

        httpParserInit(&p, false, &lim);
        strDestroy(&body);
        if (parseWhole(&p, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", &body,
                       NULL) != HTTPP_Error ||
            p.err != HTTPERR_TooLarge)
            ret = 1;
        httpParserDestroy(&p);
    }

    strDestroy(&body);
    return ret;
}

// The request side of the same state machine, and connection-reuse signalling.
static int test_httptest_parsereq(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0, v = 0;

    httpParserInit(&p, true, NULL);
    if (parseWhole(&p, "GET /a/b?x=1 HTTP/1.1\r\nHost: example.com\r\n\r\n", &body, NULL) !=
        HTTPP_Complete)
        ret = 1;
    if (p.method != HTTP_Get || p.version != HTTPVER_1_1)
        ret = 1;
    if (!strEq(p.target, _SL("/a/b?x=1")))
        ret = 1;
    if (!httpHeadersGet(&p.headers, _SL("host"), &v) || !strEq(v, _SL("example.com")))
        ret = 1;
    if (!strEmpty(body))
        ret = 1;   // a request with no framing headers has no body

    // A POST with a length.
    httpParserReset(&p);
    if (parseWhole(&p, "POST /submit HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata", &body, NULL) !=
        HTTPP_Complete)
        ret = 1;
    if (p.method != HTTP_Post || !strEq(body, _SL("data")))
        ret = 1;

    // An extension method is carried as a string rather than forcing the enum to grow.
    httpParserReset(&p);
    strDestroy(&body);
    if (parseWhole(&p, "PROPFIND / HTTP/1.1\r\n\r\n", &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (p.method != HTTP_MethodOther || !strEq(p.methodName, _SL("PROPFIND")))
        ret = 1;

    // Request lines that are not request lines.
    httpParserReset(&p);
    if (parseWhole(&p, "GET /only-two-fields\r\n\r\n", &body, NULL) != HTTPP_Error)
        ret = 1;

    httpParserReset(&p);
    if (parseWhole(&p, "GE T /x HTTP/1.1\r\n\r\n", &body, NULL) != HTTPP_Error)
        ret = 1;

    httpParserDestroy(&p);

    // Connection reuse: 1.1 persists unless told otherwise, 1.0 does the opposite.
    httpParserInit(&p, false, NULL);
    strDestroy(&body);
    parseWhole(&p, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", &body, NULL);
    if (httpParserKeepAlive(&p))
        ret = 1;

    httpParserReset(&p);
    parseWhole(&p, "HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n", &body, NULL);
    if (httpParserKeepAlive(&p))
        ret = 1;   // 1.0 closes by default

    httpParserReset(&p);
    parseWhole(&p, "HTTP/1.0 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n", &body,
               NULL);
    if (!httpParserKeepAlive(&p))
        ret = 1;   // ...unless it opts in

    httpParserDestroy(&p);
    strDestroy(&body);
    strDestroy(&v);
    return ret;
}

// Two messages back to back on one connection, which is what keep-alive actually does.
static int test_httptest_parsereuse(void)
{
    int ret = 0;
    HttpParser p;
    string body = 0;

    BufRing src;
    bufringInit(&src, 256);

    static const char* kTwo = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfirst"
                              "HTTP/1.1 404 Not Found\r\nContent-Length: 6\r\n\r\nsecond";
    bufringWrite(&src, (const uint8*)kTwo, strlen(kTwo));

    httpParserInit(&p, false, NULL);

    if (drive(&p, &src, &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (p.status != 200 || !strEq(body, _SL("first")))
        ret = 1;

    // Reset, and the bytes still in the ring are the next message -- no re-syncing needed.
    httpParserReset(&p);
    strDestroy(&body);

    if (drive(&p, &src, &body, NULL) != HTTPP_Complete)
        ret = 1;
    if (p.status != 404 || !strEq(body, _SL("second")))
        ret = 1;

    bufringDestroy(&src);
    httpParserDestroy(&p);
    strDestroy(&body);
    return ret;
}

// Chunked writing, and a round trip back through the parser.
static int test_httptest_chunkwrite(void)
{
    int ret = 0;
    string enc = 0;

    if (!httpChunkAppend(&enc, (const uint8*)"hello", 5))
        ret = 1;
    if (!strEq(enc, _SL("5\r\nhello\r\n")))
        ret = 1;

    // A zero-length chunk is the end-of-body marker, so writing one by accident would truncate the
    // body wherever the producer happened to have nothing ready.
    if (httpChunkAppend(&enc, (const uint8*)"", 0))
        ret = 1;
    if (httpChunkAppend(&enc, NULL, 0))
        ret = 1;

    // A length that needs more than one hex digit.
    strDestroy(&enc);
    {
        uint8 big[300];
        memset(big, 'x', sizeof(big));
        httpChunkAppend(&enc, big, sizeof(big));
        if (!strBeginsWith(enc, _SL("12c\r\n")))
            ret = 1;
    }

    // Encode then decode: what comes back out must be what went in.
    strDestroy(&enc);
    strAppend(&enc, _SL("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"));
    httpChunkAppend(&enc, (const uint8*)"one ", 4);
    httpChunkAppend(&enc, (const uint8*)"two ", 4);
    httpChunkAppend(&enc, (const uint8*)"three", 5);
    httpChunkFinish(&enc);

    {
        HttpParser p;
        string body = 0;
        httpParserInit(&p, false, NULL);
        if (parseWhole(&p, strC(enc), &body, NULL) != HTTPP_Complete)
            ret = 1;
        if (!strEq(body, _SL("one two three")))
            ret = 1;
        httpParserDestroy(&p);
        strDestroy(&body);
    }

    strDestroy(&enc);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// HttpConn over a real socket
//
// A raw OS socket stands in for the server: the test writes response bytes onto it by hand and
// reads the request cxhttp produced. That is deliberately not a second cxhttp -- a client and a
// server built from the same parser would agree with each other even if both were wrong about the
// protocol.
// ---------------------------------------------------------------------------------------------

#if defined(_WIN32) || defined(_PLATFORM_UNIX)

typedef struct ConnRec {
    uint32 statusCount, headerCount, dataCount, completeCount, errorCount;
    uint16 status;
    HttpError err;
    string body;
    string ctype;
} ConnRec;

static void onConnStatus(HttpEvent* ev)
{
    ConnRec* r = (ConnRec*)ev->ctx;
    r->statusCount++;
    r->status = ev->status;
}

static void onConnHeaders(HttpEvent* ev)
{
    ConnRec* r = (ConnRec*)ev->ctx;
    r->headerCount++;
    httpHeadersGet(ev->headers, _SL("Content-Type"), &r->ctype);
}

static void onConnData(HttpEvent* ev)
{
    ConnRec* r = (ConnRec*)ev->ctx;
    r->dataCount++;
    strAppendBytes(&r->body, ev->data, (uint32)ev->len);
}

static void onConnComplete(HttpEvent* ev)
{
    ConnRec* r = (ConnRec*)ev->ctx;
    r->completeCount++;
}

static void onConnError(HttpEvent* ev)
{
    ConnRec* r = (ConnRec*)ev->ctx;
    r->errorCount++;
    r->err = ev->err;
}

static const HttpHandlers kConnRecHandlers = {
    .status   = onConnStatus,
    .headers  = onConnHeaders,
    .data     = onConnData,
    .complete = onConnComplete,
    .error    = onConnError,
};

// Bind a raw loopback listener on an OS-chosen port.
static SOCKET rawListen(uint16* port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;

    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

#if defined(_WIN32)
    int alen = sizeof(a);
#else
    socklen_t alen = sizeof(a);
#endif
    getsockname(s, (struct sockaddr*)&a, &alen);
    *port = ntohs(a.sin_port);
    return s;
}

typedef struct ConnFixture {
    NetQueue* q;
    NetSocket* sock;
    SOCKET listener;
    SOCKET peer;      // the raw "server" end of the connection
    bool connected;
    bool connFailed;
} ConnFixture;

static void onFixtureConn(NetEvent* ev)
{
    ConnFixture* f = (ConnFixture*)ev->ctx;
    if (ev->conn.state == NCS_Connected)
        f->connected = true;
    else
        f->connFailed = true;
}

static const NetHandlers kFixtureHandlers = { .connection = onFixtureConn };

// Stand up a connected pair: a cx NetSocket on one end, a raw accepted socket on the other.
static bool fixtureInit(ConnFixture* f)
{
    memset(f, 0, sizeof(*f));
    f->listener = INVALID_SOCKET;
    f->peer     = INVALID_SOCKET;

    uint16 port = 0;
    f->listener = rawListen(&port);
    if (f->listener == INVALID_SOCKET)
        return false;

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    f->q = netqueueCreate(&conf);
    if (!f->q)
        return false;

    f->sock = netqueueConnect(f->q, _SL("127.0.0.1"), port, &kFixtureHandlers, f);
    if (!f->sock)
        return false;

    for (int i = 0; i < 100 && !f->connected && !f->connFailed; i++)
        netqueueTick(f->q, 20);

    if (!f->connected)
        return false;

    f->peer = accept(f->listener, NULL, NULL);
    return f->peer != INVALID_SOCKET;
}

static void fixtureDestroy(ConnFixture* f)
{
    if (f->peer != INVALID_SOCKET)
        closesocket(f->peer);
    if (f->listener != INVALID_SOCKET)
        closesocket(f->listener);
    if (f->sock) {
        netsocketClose(f->sock);
        objRelease(&f->sock);
    }
    if (f->q) {
        netqueueShutdown(f->q, 0);
        objRelease(&f->q);
    }
}

// Read whatever the client has sent so far, giving the queue a chance to flush it first.
static void readRequest(ConnFixture* f, string* out)
{
    for (int i = 0; i < 20; i++)
        netqueueTick(f->q, 5);

    char buf[4096];
#if defined(_WIN32)
    u_long nb = 1;
    ioctlsocket(f->peer, FIONBIO, &nb);
    int n = recv(f->peer, buf, sizeof(buf), 0);
#else
    int n = (int)recv(f->peer, buf, sizeof(buf), MSG_DONTWAIT);
#endif
    if (n > 0)
        strAppendBytes(out, (uint8*)buf, (size_t)n);
}

static void writeResponse(ConnFixture* f, const char* text)
{
    send(f->peer, text, (int)strlen(text), 0);
}

// Tick until the exchange reaches a terminal callback or the budget runs out.
static void tickUntilDone(ConnFixture* f, ConnRec* rec)
{
    for (int i = 0; i < 200 && !rec->completeCount && !rec->errorCount; i++)
        netqueueTick(f->q, 10);
}

// A GET with a Content-Length response: the request goes out well formed, and every callback fires
// once and in order.
static int test_httptest_conn(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec = { 0 };
    string req  = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c = httpconnCreate(f.sock, _SL("example.com"));
    if (!c) {
        fixtureDestroy(&f);
        return 1;
    }

    if (!httpconnIdle(c))
        ret = 1;

    HttpRequest* r = httprequestCreate(HTTP_Get, _SL("http://example.com/a/b?x=1"));
    if (!r) {
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }
    httprequestSetHeader(r, _SL("Accept"), _SL("*/*"));

    if (!httpconnRequest(c, r, &kConnRecHandlers, &rec))
        ret = 1;
    if (httpconnIdle(c))
        ret = 1;   // busy while a request is in flight

    readRequest(&f, &req);

    // The request line carries the target, not the whole URL, and Host comes from the connection.
    if (!strBeginsWith(req, _SL("GET /a/b?x=1 HTTP/1.1\r\n")))
        ret = 1;
    if (strFind(req, 0, _SL("Host: example.com\r\n")) < 0)
        ret = 1;
    if (strFind(req, 0, _SL("Accept: */*\r\n")) < 0)
        ret = 1;
    if (!strEndsWith(req, _SL("\r\n\r\n")))
        ret = 1;
    // A GET with no body must not claim one.
    if (strFind(req, 0, _SL("Content-Length")) >= 0)
        ret = 1;

    writeResponse(&f,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n"
                  "Content-Length: 11\r\n"
                  "\r\n"
                  "hello world");

    tickUntilDone(&f, &rec);

    if (rec.statusCount != 1 || rec.headerCount != 1 || rec.completeCount != 1)
        ret = 1;
    if (rec.errorCount != 0)
        ret = 1;
    if (rec.status != 200)
        ret = 1;
    if (!strEq(rec.body, _SL("hello world")))
        ret = 1;
    if (!strEq(rec.ctype, _SL("text/plain")))
        ret = 1;

    // The response also lands on the request object, for the buffered disposition the high-level
    // client uses.
    if (r->status != 200 || !strEq(r->reason, _SL("OK")))
        ret = 1;

    // Idle again once the response completed, so the connection could carry another request.
    if (!httpconnIdle(c))
        ret = 1;

    objRelease(&r);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    strDestroy(&req);
    return ret;
}

// A POST with a body, and a chunked response arriving in two writes -- which is the case that
// catches a parser that assumed a message arrives in one read.
static int test_httptest_connbody(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec = { 0 };
    string req  = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c    = httpconnCreate(f.sock, _SL("api.example.com"));
    HttpRequest* r = httprequestCreate(HTTP_Post, _SL("http://api.example.com/submit"));
    if (!c || !r) {
        objRelease(&r);
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }

    httprequestSetBody(r, _SL("{\"k\":1}"), _SL("application/json"));

    if (!httpconnRequest(c, r, &kConnRecHandlers, &rec))
        ret = 1;

    readRequest(&f, &req);

    if (!strBeginsWith(req, _SL("POST /submit HTTP/1.1\r\n")))
        ret = 1;
    // Content-Length is derived from the body that was actually set, not taken on trust.
    if (strFind(req, 0, _SL("Content-Length: 7\r\n")) < 0)
        ret = 1;
    if (strFind(req, 0, _SL("Content-Type: application/json\r\n")) < 0)
        ret = 1;
    if (!strEndsWith(req, _SL("{\"k\":1}")))
        ret = 1;

    // Split across two writes, mid-chunk, with a delay between them.
    writeResponse(&f,
                  "HTTP/1.1 200 OK\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n"
                  "5\r\nhel");

    for (int i = 0; i < 10; i++)
        netqueueTick(f.q, 5);

    if (rec.completeCount != 0)
        ret = 1;   // must not complete on a partial chunk

    writeResponse(&f, "lo\r\n6\r\n world\r\n0\r\n\r\n");

    tickUntilDone(&f, &rec);

    if (rec.completeCount != 1 || rec.errorCount != 0)
        ret = 1;
    if (!strEq(rec.body, _SL("hello world")))
        ret = 1;

    objRelease(&r);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    strDestroy(&req);
    return ret;
}

// Two requests on one connection, and a malformed response ending the conversation.
static int test_httptest_connreuse(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec1 = { 0 }, rec2 = { 0 };
    string req = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c     = httpconnCreate(f.sock, _SL("h"));
    HttpRequest* r1 = httprequestCreate(HTTP_Get, _SL("http://h/one"));
    HttpRequest* r2 = httprequestCreate(HTTP_Get, _SL("http://h/two"));
    if (!c || !r1 || !r2) {
        objRelease(&r2);
        objRelease(&r1);
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }

    httpconnRequest(c, r1, &kConnRecHandlers, &rec1);

    // A second request while the first is in flight is refused: pipelining is deliberately absent.
    if (httpconnRequest(c, r2, &kConnRecHandlers, &rec2))
        ret = 1;

    readRequest(&f, &req);
    writeResponse(&f, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nfirst");
    tickUntilDone(&f, &rec1);

    if (rec1.completeCount != 1 || !strEq(rec1.body, _SL("first")))
        ret = 1;

    // Now that it is idle, the same connection carries the next one.
    if (!httpconnIdle(c))
        ret = 1;
    if (!httpconnRequest(c, r2, &kConnRecHandlers, &rec2))
        ret = 1;

    strDestroy(&req);
    readRequest(&f, &req);
    if (!strBeginsWith(req, _SL("GET /two HTTP/1.1\r\n")))
        ret = 1;

    writeResponse(&f, "HTTP/1.1 404 Not Found\r\nContent-Length: 6\r\n\r\nsecond");
    tickUntilDone(&f, &rec2);

    if (rec2.completeCount != 1 || rec2.status != 404 || !strEq(rec2.body, _SL("second")))
        ret = 1;

    objRelease(&r2);
    objRelease(&r1);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&rec1.body);
    strDestroy(&rec1.ctype);
    strDestroy(&rec2.body);
    strDestroy(&rec2.ctype);
    strDestroy(&req);
    return ret;
}

// A malformed response is reported as an error rather than being half-delivered, and a peer that
// hangs up mid-body is a truncation rather than a short success.
static int test_httptest_connerror(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec = { 0 };
    string req  = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c    = httpconnCreate(f.sock, _SL("h"));
    HttpRequest* r = httprequestCreate(HTTP_Get, _SL("http://h/x"));
    if (!c || !r) {
        objRelease(&r);
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }

    httpconnRequest(c, r, &kConnRecHandlers, &rec);
    readRequest(&f, &req);

    // Content-Length and Transfer-Encoding together: a smuggling primitive, refused outright.
    writeResponse(&f,
                  "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\nhello");
    tickUntilDone(&f, &rec);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_BadMessage)
        ret = 1;
    // A protocol error ends the connection: we no longer know where the next message would start.
    if (httpconnIdle(c))
        ret = 1;

    objRelease(&r);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    strDestroy(&req);
    return ret;
}

// A peer that closes mid-body must surface as a truncation, never as a short but successful body.
static int test_httptest_conntruncated(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec = { 0 };
    string req  = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c    = httpconnCreate(f.sock, _SL("h"));
    HttpRequest* r = httprequestCreate(HTTP_Get, _SL("http://h/x"));
    if (!c || !r) {
        objRelease(&r);
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }

    httpconnRequest(c, r, &kConnRecHandlers, &rec);
    readRequest(&f, &req);

    writeResponse(&f, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\ntoo short");
    for (int i = 0; i < 10; i++)
        netqueueTick(f.q, 5);

    closesocket(f.peer);
    f.peer = INVALID_SOCKET;

    tickUntilDone(&f, &rec);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Closed)
        ret = 1;

    objRelease(&r);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    strDestroy(&req);
    return ret;
}

#endif   // _WIN32 || _PLATFORM_UNIX

// ---------------------------------------------------------------------------------------------
// Cookie jar
//
// No network: everything here is the matching rules, which are where cookies go wrong.
// ---------------------------------------------------------------------------------------------

// Feed one Set-Cookie as if it arrived from `url`.
static bool jarSet(HttpCookieJar* jar, strref url, strref field)
{
    HttpUrl u;
    if (!httpUrlParse(&u, url))
        return false;

    bool ok = httpcookiejarSetCookie(jar, &u, field);
    httpUrlDestroy(&u);
    return ok;
}

// The Cookie header the jar would send to `url`, as a plain comparison against `expect`.
static bool jarSends(HttpCookieJar* jar, strref url, strref expect)
{
    HttpUrl u;
    if (!httpUrlParse(&u, url))
        return false;

    string got = 0;
    httpcookiejarHeader(jar, &got, &u);
    bool ok = strEq(got, expect);

    strDestroy(&got);
    httpUrlDestroy(&u);
    return ok;
}

static int test_httptest_cookiejar(void)
{
    int ret            = 0;
    HttpCookieJar* jar = httpcookiejarCreate();
    if (!jar)
        return 1;

    // Plain host cookie: no Domain, so it is host-only and its default path comes from the request.
    if (!jarSet(jar, _SL("http://example.com/a/b"), _SL("sid=1")))
        ret = 1;
    if (!jarSends(jar, _SL("http://example.com/a/c"), _SL("sid=1")))
        ret = 1;
    // Default-path is "/a", so a request outside it gets nothing.
    if (!jarSends(jar, _SL("http://example.com/z"), _SL("")))
        ret = 1;
    // Host-only: a subdomain must not receive it.
    if (!jarSends(jar, _SL("http://www.example.com/a/b"), _SL("")))
        ret = 1;

    // With an explicit Domain the cookie does reach subdomains.
    if (!jarSet(jar, _SL("http://example.com/"), _SL("wide=2; Domain=example.com; Path=/")))
        ret = 1;
    if (!jarSends(jar, _SL("http://deep.sub.example.com/anything"), _SL("wide=2")))
        ret = 1;

    // ...but only real subdomains. A host that merely ends in the same letters is not one.
    if (!jarSends(jar, _SL("http://notexample.com/"), _SL("")))
        ret = 1;

    // A site may not set a cookie for a domain it is not under. This is the check that stops
    // one host writing another's session.
    if (jarSet(jar, _SL("http://evil.com/"), _SL("x=1; Domain=example.com")))
        ret = 1;
    // ...nor for a public suffix it does not own, expressed here as "not a suffix of my host".
    if (jarSet(jar, _SL("http://example.com/"), _SL("y=1; Domain=other.example.com")))
        ret = 1;

    // Secure cookies never travel in the clear, whichever host asks.
    if (!jarSet(jar, _SL("https://example.com/"), _SL("s=3; Domain=example.com; Path=/; Secure")))
        ret = 1;
    if (!jarSends(jar, _SL("https://example.com/"), _SL("wide=2; s=3")))
        ret = 1;   // equal paths, so insertion order holds
    if (!jarSends(jar, _SL("http://example.com/"), _SL("wide=2")))
        ret = 1;

    // Max-Age in the past is a deletion.
    if (!jarSet(jar, _SL("http://example.com/"), _SL("wide=2; Domain=example.com; Path=/; Max-Age=0")))
        ret = 1;
    if (!jarSends(jar, _SL("http://example.com/"), _SL("")))
        ret = 1;

    // Setting the same (name, domain, path) again replaces rather than duplicating.
    int32 before = httpcookiejarCount(jar);
    if (!jarSet(jar, _SL("http://example.com/a/b"), _SL("sid=99")))
        ret = 1;
    if (httpcookiejarCount(jar) != before)
        ret = 1;
    if (!jarSends(jar, _SL("http://example.com/a/b"), _SL("sid=99")))
        ret = 1;

    // A longer path sorts ahead of a shorter one, so a server reading only the first value gets
    // the more specific cookie.
    if (!jarSet(jar, _SL("http://example.com/"), _SL("p=root; Path=/")))
        ret = 1;
    if (!jarSet(jar, _SL("http://example.com/"), _SL("p=deep; Path=/a/b")))
        ret = 1;
    if (!jarSends(jar, _SL("http://example.com/a/b/c"), _SL("p=deep; sid=99; p=root")))
        ret = 1;

    // An expired Expires never gets stored at all.
    if (!jarSet(jar, _SL("http://example.com/"),
                _SL("old=1; Path=/; Expires=Sun, 06 Nov 1994 08:49:37 GMT")))
        ret = 1;
    if (!jarSends(jar, _SL("http://example.com/x"), _SL("p=root")))
        ret = 1;

    // Enumeration round-trips through add(), which is the whole of the persistence story.
    sa_HttpCookie all;
    int32 n = httpcookiejarEnumerate(jar, &all);
    if (n == 0)
        ret = 1;

    HttpCookieJar* jar2 = httpcookiejarCreate();
    for (int32 i = 0; i < n; i++)
        httpcookiejarAdd(jar2, &all.a[i]);
    if (httpcookiejarCount(jar2) != n)
        ret = 1;
    if (!jarSends(jar2, _SL("http://example.com/a/b/c"), _SL("p=deep; sid=99; p=root")))
        ret = 1;
    objRelease(&jar2);
    saDestroy(&all);

    httpcookiejarClear(jar);
    if (httpcookiejarCount(jar) != 0)
        ret = 1;

    objRelease(&jar);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Form bodies
// ---------------------------------------------------------------------------------------------

static int test_httptest_form(void)
{
    int ret = 0;

    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL("user"), _SL("ada l"));
    httpHeadersAdd(&fields, _SL("token"), _SL("a+b/c=d&e"));

    string body = 0;
    if (!httpFormUrlEncoded(&body, &fields))
        ret = 1;
    // Every reserved character has to be escaped, or the second field's value would parse as two
    // more fields.
    if (!strEq(body, _SL("user=ada%20l&token=a%2Bb%2Fc%3Dd%26e")))
        ret = 1;
    if (!strEq(httpFormUrlEncodedType(), _SL("application/x-www-form-urlencoded")))
        ret = 1;

    strDestroy(&body);
    httpHeadersDestroy(&fields);

    // multipart
    HttpMultipart mp;
    if (!httpMultipartInit(&mp))
        return 1;

    if (strEmpty(mp.boundary))
        ret = 1;

    httpMultipartAddField(&mp, _SL("comment"), _SL("looks good"));
    httpMultipartAddFile(&mp, _SL("upload"), _SL("notes.txt"), _SL("text/plain"),
                         (const uint8*)"line one\n", 9);

    // A field name carrying a quote or a newline must not be able to end the part header early.
    httpMultipartAddField(&mp, _SL("od\"d\r\nX-Evil: yes"), _SL("v"));

    string ctype = 0;
    if (!httpMultipartFinish(&mp, &body, &ctype))
        ret = 1;

    if (strFind(ctype, 0, _SL("multipart/form-data; boundary=")) != 0)
        ret = 1;
    if (strFind(ctype, 0, mp.boundary) < 0)
        ret = 1;

    if (strFind(body, 0, _SL("Content-Disposition: form-data; name=\"comment\"\r\n\r\nlooks good")) < 0)
        ret = 1;
    if (strFind(body, 0, _SL("name=\"upload\"; filename=\"notes.txt\"")) < 0)
        ret = 1;
    if (strFind(body, 0, _SL("Content-Type: text/plain\r\n\r\nline one\n")) < 0)
        ret = 1;
    // The quote is escaped and the CRLF is gone, so no header was injected.
    if (strFind(body, 0, _SL("X-Evil")) < 0)
        ret = 1;   // the text survives...
    if (strFind(body, 0, _SL("\r\nX-Evil")) >= 0)
        ret = 1;   // ...but not as a header of its own
    if (strFind(body, 0, _SL("name=\"od\\\"d")) < 0)
        ret = 1;

    // The closing boundary ends the body exactly once, and finishing twice is a no-op.
    string body2 = 0;
    httpMultipartFinish(&mp, &body2, NULL);
    if (!strEq(body, body2))
        ret = 1;
    if (!strEndsWith(body, _SL("--\r\n")))
        ret = 1;

    strDestroy(&body);
    strDestroy(&body2);
    strDestroy(&ctype);
    httpMultipartDestroy(&mp);
    return ret;
}

#if defined(_WIN32) || defined(_PLATFORM_UNIX)

// ---------------------------------------------------------------------------------------------
// HttpClient over loopback
//
// The client dials for itself, so unlike the HttpConn fixture there is nothing to pre-connect:
// a raw listener waits on an OS-chosen port and the request names it by address.
// ---------------------------------------------------------------------------------------------

// The same recording handlers with no data callback, so the body takes the default disposition and
// accumulates on the request instead.
static const HttpHandlers kConnRecNoDataHandlers = {
    .status   = onConnStatus,
    .headers  = onConnHeaders,
    .complete = onConnComplete,
    .error    = onConnError,
};

typedef struct ClientFixture {
    NetQueue* q;
    HttpClient* cl;
    SOCKET listener;
    SOCKET peer;
    uint16 port;
} ClientFixture;

static void rawNonBlocking(SOCKET s)
{
#if defined(_WIN32)
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static bool clientFixtureInit(ClientFixture* f)
{
    memset(f, 0, sizeof(*f));
    f->listener = INVALID_SOCKET;
    f->peer     = INVALID_SOCKET;

    f->listener = rawListen(&f->port);
    if (f->listener == INVALID_SOCKET)
        return false;
    rawNonBlocking(f->listener);

    NetQueueConfig conf;
    netqueuePresetClient(&conf);
    f->q = netqueueCreate(&conf);
    if (!f->q)
        return false;

    f->cl = httpclientCreate(f->q);
    return f->cl != NULL;
}

static void clientFixtureDestroy(ClientFixture* f)
{
    if (f->peer != INVALID_SOCKET)
        closesocket(f->peer);
    if (f->listener != INVALID_SOCKET)
        closesocket(f->listener);
    objRelease(&f->cl);
    if (f->q) {
        netqueueShutdown(f->q, 0);
        objRelease(&f->q);
    }
}

// Build "http://127.0.0.1:<port><path>".
static void clientUrl(ClientFixture* f, string* out, strref path)
{
    strClear(out);
    strAppend(out, _SL("http://127.0.0.1:"));
    string p = 0;
    strFromUInt32(&p, f->port, 10);
    strAppend(out, p);
    strDestroy(&p);
    strAppend(out, path);
}

// Drive the queue until the client's connection shows up on the listener.
static bool clientAccept(ClientFixture* f)
{
    for (int i = 0; i < 400; i++) {
        netqueueTick(f->q, 5);
        SOCKET s = accept(f->listener, NULL, NULL);
        if (s != INVALID_SOCKET) {
            f->peer = s;
            return true;
        }
    }
    return false;
}

// Read one whole request head (and any body already with it) off the peer.
static bool clientReadRequest(ClientFixture* f, string* out)
{
    strClear(out);

    for (int i = 0; i < 400; i++) {
        netqueueTick(f->q, 5);

        char buf[4096];
#if defined(_WIN32)
        u_long nb = 1;
        ioctlsocket(f->peer, FIONBIO, &nb);
        int n = recv(f->peer, buf, sizeof(buf), 0);
#else
        int n = (int)recv(f->peer, buf, sizeof(buf), MSG_DONTWAIT);
#endif
        if (n > 0)
            strAppendBytes(out, (uint8*)buf, (size_t)n);

        if (strFind(*out, 0, _SL("\r\n\r\n")) >= 0)
            return true;
    }
    return false;
}

static void clientWrite(ClientFixture* f, const char* text)
{
    send(f->peer, text, (int)strlen(text), 0);
}

static void clientTickUntil(ClientFixture* f, ConnRec* rec)
{
    for (int i = 0; i < 400 && !rec->completeCount && !rec->errorCount; i++)
        netqueueTick(f->q, 5);
}

// A plain GET: the client dials, writes a well-formed request with the headers it contributes,
// and the response lands buffered on the request object.
static int test_httptest_clientget(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec  = { 0 };
    string url   = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/thing?q=1"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    if (!httpclientSend(f.cl, r, &kConnRecNoDataHandlers, &rec))
        ret = 1;

    if (!clientAccept(&f))
        ret = 1;
    else if (!clientReadRequest(&f, &wire))
        ret = 1;

    if (!strBeginsWith(wire, _SL("GET /thing?q=1 HTTP/1.1\r\n")))
        ret = 1;
    // Host carries the port, because it is not the scheme default.
    if (strFind(wire, 0, _SL("Host: 127.0.0.1:")) < 0)
        ret = 1;
    // The client contributes a User-Agent the request never set.
    if (strFind(wire, 0, _SL("User-Agent: ")) < 0)
        ret = 1;

    clientWrite(&f,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "howdy");

    clientTickUntil(&f, &rec);

    if (rec.completeCount != 1 || rec.errorCount != 0)
        ret = 1;
    if (rec.status != 200)
        ret = 1;
    // No data handler and no sink, so the body buffered onto the request.
    if (!strEq(r->respBody, _SL("howdy")))
        ret = 1;
    if (r->status != 200 || r->err != HTTPERR_None)
        ret = 1;
    // The response headers were copied onto the request, not left borrowed from the parser.
    string ct = 0;
    if (!httpHeadersGet(&r->respHeaders, _SL("Content-Type"), &ct) ||
        !strEq(ct, _SL("text/plain")))
        ret = 1;
    strDestroy(&ct);

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A redirect followed on a reused connection. The 3xx body must never reach the application, and
// the second request must go out on the same socket -- both at once, because the interesting bug is
// a redirect that concatenates its signpost onto the answer.
static int test_httptest_clientredirect(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/old"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    if (!httpclientSend(f.cl, r, &kConnRecNoDataHandlers, &rec))
        ret = 1;

    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;
    if (!strBeginsWith(wire, _SL("GET /old HTTP/1.1\r\n")))
        ret = 1;

    // A 3xx with a body and a reusable connection.
    clientWrite(&f,
                "HTTP/1.1 302 Found\r\n"
                "Location: /new\r\n"
                "Content-Length: 20\r\n"
                "\r\n"
                "MOVED-DO-NOT-DELIVER");

    if (!clientReadRequest(&f, &wire))
        ret = 1;
    // Same connection: the listener was never asked for another.
    if (!strBeginsWith(wire, _SL("GET /new HTTP/1.1\r\n")))
        ret = 1;

    clientWrite(&f,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "final");

    clientTickUntil(&f, &rec);

    if (rec.completeCount != 1 || rec.errorCount != 0)
        ret = 1;
    if (rec.status != 200)
        ret = 1;
    // The 3xx body is gone; only the real one survives.
    if (!strEq(r->respBody, _SL("final")))
        ret = 1;
    if (r->redirects != 1)
        ret = 1;
    // The request's URL now names where the answer actually came from.
    if (!strEq(r->url.path, _SL("/new")))
        ret = 1;
    // The application never saw the intermediate status, only the final one.
    if (rec.statusCount != 1 || rec.headerCount != 1)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A redirect chain longer than the limit fails rather than looping.
static int test_httptest_clientredirectloop(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    httpclientSetMaxRedirects(f.cl, 2);

    clientUrl(&f, &url, _SL("/loop"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    if (!httpclientSend(f.cl, r, &kConnRecHandlers, &rec))
        ret = 1;
    if (!clientAccept(&f))
        ret = 1;

    // Three hops offered, two allowed.
    for (int i = 0; i < 3 && !rec.errorCount; i++) {
        if (!clientReadRequest(&f, &wire))
            break;
        clientWrite(&f,
                    "HTTP/1.1 302 Found\r\n"
                    "Location: /loop\r\n"
                    "Content-Length: 0\r\n"
                    "\r\n");
    }

    clientTickUntil(&f, &rec);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_TooManyRedirects)
        ret = 1;
    if (r->err != HTTPERR_TooManyRedirects)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// Two requests in a row reuse one pooled connection: the listener is only ever asked once.
static int test_httptest_clientpool(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec1 = { 0 }, rec2 = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/one"));
    HttpRequest* r1 = httprequestCreate(HTTP_Get, url);
    if (!r1) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r1, &kConnRecNoDataHandlers, &rec1);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");
    clientTickUntil(&f, &rec1);
    if (rec1.completeCount != 1)
        ret = 1;

    // Second request, same origin. If it dials again, accept() below succeeds and the test fails.
    clientUrl(&f, &url, _SL("/two"));
    HttpRequest* r2 = httprequestCreate(HTTP_Get, url);
    httpclientSend(f.cl, r2, &kConnRecNoDataHandlers, &rec2);

    if (!clientReadRequest(&f, &wire))
        ret = 1;
    if (!strBeginsWith(wire, _SL("GET /two HTTP/1.1\r\n")))
        ret = 1;

    SOCKET extra = accept(f.listener, NULL, NULL);
    if (extra != INVALID_SOCKET) {
        closesocket(extra);
        ret = 1;   // a second connection means the pool did not do its job
    }

    clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ndef");
    clientTickUntil(&f, &rec2);

    if (rec2.completeCount != 1 || rec2.errorCount != 0)
        ret = 1;
    if (!strEq(r2->respBody, _SL("def")))
        ret = 1;

    // Dropping the idle connections is not the same as failing anything in flight.
    httpclientCloseIdle(f.cl);

    objRelease(&r1);
    objRelease(&r2);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec1.body);
    strDestroy(&rec1.ctype);
    strDestroy(&rec2.body);
    strDestroy(&rec2.ctype);
    return ret;
}

// A cookie set by the first response comes back on the second request.
static int test_httptest_clientcookie(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec1 = { 0 }, rec2 = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    HttpCookieJar* jar = httpcookiejarCreate();
    httpclientSetCookieJar(f.cl, jar);

    clientUrl(&f, &url, _SL("/login"));
    HttpRequest* r1 = httprequestCreate(HTTP_Get, url);
    if (!r1) {
        objRelease(&jar);
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r1, &kConnRecHandlers, &rec1);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;
    // Nothing to send yet.
    if (strFind(wire, 0, _SL("Cookie:")) >= 0)
        ret = 1;

    // Two Set-Cookie fields, which is the case a hashtable-backed header list would lose.
    clientWrite(&f,
                "HTTP/1.1 200 OK\r\n"
                "Set-Cookie: sid=abc; Path=/\r\n"
                "Set-Cookie: pref=dark; Path=/\r\n"
                "Content-Length: 2\r\n"
                "\r\n"
                "ok");
    clientTickUntil(&f, &rec1);
    if (rec1.completeCount != 1)
        ret = 1;
    if (httpcookiejarCount(jar) != 2)
        ret = 1;

    clientUrl(&f, &url, _SL("/next"));
    HttpRequest* r2 = httprequestCreate(HTTP_Get, url);
    httpclientSend(f.cl, r2, &kConnRecHandlers, &rec2);

    if (!clientReadRequest(&f, &wire))
        ret = 1;
    if (strFind(wire, 0, _SL("Cookie: sid=abc; pref=dark\r\n")) < 0)
        ret = 1;

    clientWrite(&f, "HTTP/1.1 204 No Content\r\n\r\n");
    clientTickUntil(&f, &rec2);
    if (rec2.completeCount != 1 || rec2.status != 204)
        ret = 1;

    objRelease(&r1);
    objRelease(&r2);
    objRelease(&jar);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec1.body);
    strDestroy(&rec1.ctype);
    strDestroy(&rec2.body);
    strDestroy(&rec2.ctype);
    return ret;
}

// The response body streamed into a caller-supplied StreamBuffer instead of buffered, with a
// chunked transfer coding so the sink sees several writes rather than one.
static int test_httptest_clientsink(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0, sunk = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    StreamBuffer* sb = sbufCreate(4096);
    if (!sb || !sbufStrCRegisterPush(sb, &sunk)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/download"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        sbufRelease(&sb);
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    if (!httprequestSetSink(r, sb))
        ret = 1;

    httpclientSend(f.cl, r, &kConnRecHandlers, &rec);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    clientWrite(&f,
                "HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "5\r\nhello\r\n");
    for (int i = 0; i < 20; i++)
        netqueueTick(f.q, 5);
    clientWrite(&f, "6\r\n world\r\n0\r\n\r\n");

    clientTickUntil(&f, &rec);

    if (rec.completeCount != 1 || rec.errorCount != 0)
        ret = 1;
    if (!strEq(sunk, _SL("hello world")))
        ret = 1;
    // The sink took the body, so nothing accumulated on the request and no data callback ran.
    if (!strEmpty(r->respBody))
        ret = 1;
    if (rec.dataCount != 0)
        ret = 1;
    // The producer finished, so a consumer waiting on the buffer is released rather than hanging.
    if (!sbufIsPFinished(sb))
        ret = 1;

    objRelease(&r);
    sbufRelease(&sb);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&sunk);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// Nothing listening: the failure has to arrive as an error callback, not as silence.
static int test_httptest_clientrefused(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url  = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    // Close the listener so the port has nobody on it, then aim at it.
    closesocket(f.listener);
    f.listener = INVALID_SOCKET;

    clientUrl(&f, &url, _SL("/gone"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    if (!httpclientSend(f.cl, r, &kConnRecHandlers, &rec))
        ret = 1;

    for (int i = 0; i < 400 && !rec.errorCount && !rec.completeCount; i++)
        netqueueTick(f.q, 5);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Network)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A POST whose redirect is a 303: the method becomes GET and the body does not follow. This is the
// rule that stops a redirect replaying a side effect against a different endpoint.
static int test_httptest_clientseeother(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/submit"));
    HttpRequest* r = httprequestCreate(HTTP_Post, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }
    httprequestSetBody(r, _SL("a=1"), _SL("application/x-www-form-urlencoded"));
    httprequestSetHeader(r, _SL("Authorization"), _SL("Bearer secret"));

    httpclientSend(f.cl, r, &kConnRecHandlers, &rec);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;
    if (!strBeginsWith(wire, _SL("POST /submit HTTP/1.1\r\n")))
        ret = 1;
    if (strFind(wire, 0, _SL("Content-Length: 3\r\n")) < 0)
        ret = 1;

    clientWrite(&f,
                "HTTP/1.1 303 See Other\r\n"
                "Location: /result\r\n"
                "Content-Length: 0\r\n"
                "\r\n");

    if (!clientReadRequest(&f, &wire))
        ret = 1;

    if (!strBeginsWith(wire, _SL("GET /result HTTP/1.1\r\n")))
        ret = 1;
    // The body and everything describing it are gone.
    if (strFind(wire, 0, _SL("Content-Length")) >= 0)
        ret = 1;
    if (strFind(wire, 0, _SL("Content-Type")) >= 0)
        ret = 1;
    // Same origin, so credentials do still travel.
    if (strFind(wire, 0, _SL("Authorization: Bearer secret")) < 0)
        ret = 1;

    clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    clientTickUntil(&f, &rec);

    if (rec.completeCount != 1 || rec.status != 200)
        ret = 1;
    if (r->method != HTTP_Get)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A response that never arrives has to end as a timeout rather than a hang.
static int test_httptest_clienttimeout(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    httpclientSetTimeouts(f.cl, timeMS(150), 0);

    clientUrl(&f, &url, _SL("/slow"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r, &kConnRecHandlers, &rec);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    // Deliberately answer nothing.
    for (int i = 0; i < 400 && !rec.errorCount && !rec.completeCount; i++)
        netqueueTick(f.q, 5);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Timeout)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A cancel landing mid-response: the request ends as an abort rather than as whatever the closed
// socket looked like from below, and it ends exactly once.
static int test_httptest_clientcancel(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/slow"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r, &kConnRecNoDataHandlers, &rec);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    // Head and part of a body, then nothing -- the shape of a server that has gone quiet.
    clientWrite(&f,
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 100\r\n"
                "\r\n"
                "partial");
    for (int i = 0; i < 20; i++)
        netqueueTick(f.q, 5);

    if (!httprequestCancel(r))
        ret = 1;

    clientTickUntil(&f, &rec);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Aborted)
        ret = 1;
    if (r->err != HTTPERR_Aborted)
        ret = 1;

    // A second cancel has nothing left to claim.
    if (httprequestCancel(r))
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// Cancel while the exchange is still dialing, before any connection exists to carry it. The claim
// has to reach the half-open socket instead.
static int test_httptest_clientcancelearly(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url  = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/never"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r, &kConnRecNoDataHandlers, &rec);

    // Deliberately never accept, and cancel before the queue has been ticked at all.
    if (!httprequestCancel(r))
        ret = 1;

    for (int i = 0; i < 400 && !rec.errorCount && !rec.completeCount; i++)
        netqueueTick(f.q, 5);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Aborted)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// Cancelling a request that already finished is a no-op, and must not disturb the response it
// delivered. This is the race the "best effort" contract exists to describe honestly.
static int test_httptest_clientcanceldone(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/quick"));
    HttpRequest* r = httprequestCreate(HTTP_Get, url);
    if (!r) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r, &kConnRecNoDataHandlers, &rec);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\ndone");
    clientTickUntil(&f, &rec);

    if (rec.completeCount != 1 || rec.errorCount != 0)
        ret = 1;

    // Too late: the answer already arrived.
    if (httprequestCancel(r))
        ret = 1;

    if (r->err != HTTPERR_None || !strEq(r->respBody, _SL("done")))
        ret = 1;
    if (rec.errorCount != 0)
        ret = 1;

    objRelease(&r);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

// A cancelled exchange must never leave its connection in the pool: the response was abandoned at
// an unknown point, so the next request would start reading from the middle of a message. Held open
// deliberately -- a response that completed first would be the clientcanceldone case instead.
static int test_httptest_clientcancelpool(void)
{
    int ret = 0;
    ClientFixture f;
    ConnRec rec1 = { 0 }, rec2 = { 0 };
    string url = 0, wire = 0;

    if (!clientFixtureInit(&f)) {
        clientFixtureDestroy(&f);
        return 1;
    }

    clientUrl(&f, &url, _SL("/one"));
    HttpRequest* r1 = httprequestCreate(HTTP_Get, url);
    if (!r1) {
        clientFixtureDestroy(&f);
        strDestroy(&url);
        return 1;
    }

    httpclientSend(f.cl, r1, &kConnRecNoDataHandlers, &rec1);
    if (!clientAccept(&f) || !clientReadRequest(&f, &wire))
        ret = 1;

    // A response that promises far more than it delivers, so the exchange is still live -- and the
    // connection would be perfectly poolable if it ever finished.
    clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nabc");
    for (int i = 0; i < 20; i++)
        netqueueTick(f.q, 5);

    if (rec1.completeCount != 0 || rec1.errorCount != 0)
        ret = 1;   // the premise: nothing has settled yet

    if (!httprequestCancel(r1))
        ret = 1;
    clientTickUntil(&f, &rec1);

    if (rec1.errorCount != 1 || rec1.err != HTTPERR_Aborted)
        ret = 1;

    // The next request must dial rather than find that connection waiting for it.
    SOCKET first = f.peer;
    f.peer       = INVALID_SOCKET;

    clientUrl(&f, &url, _SL("/two"));
    HttpRequest* r2 = httprequestCreate(HTTP_Get, url);
    httpclientSend(f.cl, r2, &kConnRecNoDataHandlers, &rec2);

    if (!clientAccept(&f))
        ret = 1;   // no second connection means the cancelled one was handed back out
    closesocket(first);

    if (f.peer != INVALID_SOCKET) {
        if (!clientReadRequest(&f, &wire))
            ret = 1;
        if (!strBeginsWith(wire, _SL("GET /two HTTP/1.1\r\n")))
            ret = 1;

        clientWrite(&f, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\ndef");
        clientTickUntil(&f, &rec2);

        if (rec2.completeCount != 1 || !strEq(r2->respBody, _SL("def")))
            ret = 1;
    }

    objRelease(&r1);
    objRelease(&r2);
    clientFixtureDestroy(&f);
    strDestroy(&url);
    strDestroy(&wire);
    strDestroy(&rec1.body);
    strDestroy(&rec1.ctype);
    strDestroy(&rec2.body);
    strDestroy(&rec2.ctype);
    return ret;
}

// The low-level half: cancelling an HttpConn directly reports an abort rather than a close.
static int test_httptest_conncancel(void)
{
    int ret = 0;
    ConnFixture f;
    ConnRec rec = { 0 };
    string req  = 0;

    if (!fixtureInit(&f)) {
        fixtureDestroy(&f);
        return 1;
    }

    HttpConn* c = httpconnCreate(f.sock, _SL("example.com"));
    HttpRequest* r = c ? httprequestCreate(HTTP_Get, _SL("http://example.com/x")) : NULL;
    if (!c || !r) {
        objRelease(&r);
        objRelease(&c);
        fixtureDestroy(&f);
        return 1;
    }

    httpconnRequest(c, r, &kConnRecHandlers, &rec);
    readRequest(&f, &req);

    writeResponse(&f, "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nnot all of it");
    for (int i = 0; i < 20; i++)
        netqueueTick(f.q, 5);

    if (!httpconnCancel(c))
        ret = 1;

    tickUntilDone(&f, &rec);

    if (rec.errorCount != 1 || rec.completeCount != 0)
        ret = 1;
    if (rec.err != HTTPERR_Aborted)
        ret = 1;
    // A cancelled connection is spent whatever else is true.
    if (httpconnIdle(c))
        ret = 1;
    if (httpconnCancel(c))
        ret = 1;

    objRelease(&r);
    objRelease(&c);
    fixtureDestroy(&f);
    strDestroy(&req);
    strDestroy(&rec.body);
    strDestroy(&rec.ctype);
    return ret;
}

#endif   // _WIN32 || _PLATFORM_UNIX

testfunc httptest_funcs[] = {
    { "url",          test_httptest_url          },
    { "urlformat",    test_httptest_urlformat    },
    { "urlresolve",   test_httptest_urlresolve   },
    { "urlcodec",     test_httptest_urlcodec     },
    { "query",        test_httptest_query        },
    { "headers",      test_httptest_headers      },
    { "headertoken",  test_httptest_headertoken  },
    { "headerformat", test_httptest_headerformat },
    { "date",         test_httptest_date         },
    { "parseresp",    test_httptest_parseresp    },
    { "parsechunked", test_httptest_parsechunked },
    { "parseframing", test_httptest_parseframing },
    { "parsereject",  test_httptest_parsereject  },
    { "parselimits",  test_httptest_parselimits  },
    { "parsereq",     test_httptest_parsereq     },
    { "parsereuse",   test_httptest_parsereuse   },
    { "chunkwrite",   test_httptest_chunkwrite   },
    { "cookiejar",    test_httptest_cookiejar    },
    { "form",         test_httptest_form         },
#if defined(_WIN32) || defined(_PLATFORM_UNIX)
    { "conn",            test_httptest_conn              },
    { "connbody",        test_httptest_connbody          },
    { "connreuse",       test_httptest_connreuse         },
    { "connerror",       test_httptest_connerror         },
    { "conntruncated",   test_httptest_conntruncated     },
    { "clientget",       test_httptest_clientget         },
    { "clientredirect",  test_httptest_clientredirect    },
    { "clientredirloop", test_httptest_clientredirectloop },
    { "clientseeother",  test_httptest_clientseeother    },
    { "clientpool",      test_httptest_clientpool        },
    { "clientcookie",    test_httptest_clientcookie      },
    { "clientsink",      test_httptest_clientsink        },
    { "clientrefused",   test_httptest_clientrefused     },
    { "clienttimeout",   test_httptest_clienttimeout     },
    { "clientcancel",      test_httptest_clientcancel      },
    { "clientcancelearly", test_httptest_clientcancelearly },
    { "clientcanceldone",  test_httptest_clientcanceldone  },
    { "clientcancelpool",  test_httptest_clientcancelpool  },
    { "conncancel",        test_httptest_conncancel        },
#endif
    { 0,              0                          },
};

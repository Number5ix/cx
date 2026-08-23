// ---------------------------------------------------------------------------------------------
// URLs
//
// Parsing, composition, percent coding, and query strings. None of this exists in cx core, and
// nothing but cxhttp needs it yet, so it lives here.
//
// The parser is deliberately not a general RFC 3986 URI parser. It handles the absolute-URL forms
// HTTP actually uses plus the reference-resolution cases a Location header produces, and rejects
// everything else rather than half-understanding it. A URL that this refuses is one cxhttp could
// not have dialed anyway.
//
// Components come out decoded, except the query -- splitting a query into pairs has to happen
// before decoding, because a %26 inside a value is data while a bare & is a separator. Decoding
// first would make the two indistinguishable, which is a classic parameter-smuggling bug.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/container.h>
#include <cx/string.h>

STR_CONST(kSchemeHttp, "http");
STR_CONST(kSchemeHttps, "https");

// ---------------------------------------------------------------------------------------------
// Percent coding
// ---------------------------------------------------------------------------------------------

// Whether `c` may appear unescaped under `set`. The unreserved set is common to all three; each
// wider set adds the reserved characters that are structural in that position and therefore already
// mean themselves there.
static bool encodeSafe(uint8 c, HttpEncodeSet set)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    if (c == '-' || c == '.' || c == '_' || c == '~')
        return true;

    switch (set) {
    case HTTPENC_Path:
        return c == '/';
    case HTTPENC_Query:
        // sub-delims minus '&' and '=', which separate pairs, plus the characters a query is
        // allowed to carry literally. '+' is escaped because form decoding reads it as a space.
        return c == '/' || c == '?' || c == ':' || c == '@' || c == '!' || c == '$' || c == '\'' ||
            c == '(' || c == ')' || c == '*' || c == ',' || c == ';';
    case HTTPENC_Strict:
    default:
        return false;
    }
}

static uint8 hexDigit(uint8 v)
{
    return (uint8)(v < 10 ? '0' + v : 'A' + (v - 10));
}

// Value of one hex digit, or -1. Accepts either case, as every URL producer mixes them.
static int hexVal(uint8 c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

_Use_decl_annotations_
bool httpUrlEncode(strhandle out, strref s, HttpEncodeSet set)
{
    string ret = 0;

    striter it;
    striBorrow(&it, s);
    uint8 c;
    while (striChar(&it, &c)) {
        if (encodeSafe(c, set)) {
            strAppendChar(&ret, c);
        } else {
            strAppendChar(&ret, '%');
            strAppendChar(&ret, hexDigit((uint8)(c >> 4)));
            strAppendChar(&ret, hexDigit((uint8)(c & 0x0f)));
        }
    }

    strDestroy(out);
    *out = ret;
    return true;
}

// A byte source with pushback, for the one place decoding needs to look ahead. Two bytes is the
// most that can ever be handed back: a '%' whose next two bytes turn out not to be hex digits has
// to release both, because either of them may itself start a valid escape -- "%%41" decodes to
// "%A", which only comes out right if the second '%' is reconsidered from scratch.
typedef struct PctSrc {
    striter it;
    uint8 pushed[2];
    uint32 npushed;
} PctSrc;

static bool pctNext(PctSrc* src, uint8* out)
{
    if (src->npushed > 0) {
        *out = src->pushed[--src->npushed];
        return true;
    }
    return striChar(&src->it, out);
}

// Pushback is last-in first-out, so a caller handing back two bytes pushes the later one first.
static void pctPush(PctSrc* src, uint8 c)
{
    src->pushed[src->npushed++] = c;
}

_Use_decl_annotations_
bool httpUrlDecode(strhandle out, strref s, bool plusIsSpace)
{
    string ret = 0;

    PctSrc src = { 0 };
    striBorrow(&src.it, s);

    uint8 c;
    while (pctNext(&src, &c)) {
        if (c == '+' && plusIsSpace) {
            strAppendChar(&ret, ' ');
            continue;
        }

        if (c == '%') {
            uint8 h, l;
            if (pctNext(&src, &h)) {
                if (pctNext(&src, &l)) {
                    int hi = hexVal(h);
                    int lo = hexVal(l);
                    if (hi >= 0 && lo >= 0) {
                        strAppendChar(&ret, (uint8)((hi << 4) | lo));
                        continue;
                    }
                    pctPush(&src, l);
                }
                pctPush(&src, h);
            }
        }

        // A '%' that is not followed by two hex digits is malformed. Passing it through literally
        // is what every browser and server does; rejecting it would fail on URLs that work
        // everywhere else, and there is no security gain in being the only strict one.
        strAppendChar(&ret, c);
    }

    strDestroy(out);
    *out = ret;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
uint16 httpSchemeDefaultPort(strref scheme)
{
    if (strEqi(scheme, kSchemeHttp))
        return 80;
    if (strEqi(scheme, kSchemeHttps))
        return 443;
    return 0;
}

_Use_decl_annotations_
uint16 httpUrlEffectivePort(const HttpUrl* url)
{
    return url->port ? url->port : httpSchemeDefaultPort(url->scheme);
}

_Use_decl_annotations_
void httpUrlDestroy(HttpUrl* url)
{
    strDestroy(&url->scheme);
    strDestroy(&url->user);
    strDestroy(&url->pass);
    strDestroy(&url->host);
    strDestroy(&url->path);
    strDestroy(&url->query);
    strDestroy(&url->fragment);
    url->port     = 0;
    url->ipv6Host = false;
}

// Split "user:pass@host:port" into the URL's authority fields. The rightmost '@' separates userinfo
// from host, because a password may legitimately contain one; the port colon is found after the
// closing bracket of an IPv6 literal, whose own colons must not be mistaken for it.
_Success_(return) static bool parseAuthority(_Inout_ HttpUrl* url, strref auth)
{
    string hostport = 0;

    int32 at = strFindCharR(auth, strEnd, '@');
    if (at >= 0) {
        string userinfo = 0;
        strSubStr(&userinfo, auth, 0, at);
        strSubStr(&hostport, auth, at + 1, strEnd);

        int32 colon = strFindChar(userinfo, 0, ':');
        if (colon >= 0) {
            string rawUser = 0, rawPass = 0;
            strSubStr(&rawUser, userinfo, 0, colon);
            strSubStr(&rawPass, userinfo, colon + 1, strEnd);
            httpUrlDecode(&url->user, rawUser, false);
            httpUrlDecode(&url->pass, rawPass, false);
            strDestroy(&rawUser);
            strDestroy(&rawPass);
        } else {
            httpUrlDecode(&url->user, userinfo, false);
        }

        strDestroy(&userinfo);
    } else {
        strDup(&hostport, auth);
    }

    bool ok      = false;
    int32 portAt = -1;

    if (strGetChar(hostport, 0) == '[') {
        // IPv6 literal: everything up to the closing bracket is the address, and only a colon after
        // it can be the port.
        int32 close = strFindChar(hostport, 0, ']');
        if (close > 0) {
            strSubStr(&url->host, hostport, 1, close);
            url->ipv6Host = true;
            if (close + 1 < (int32)strLen(hostport) && strGetChar(hostport, close + 1) == ':')
                portAt = close + 1;
            ok = true;
        }
    } else {
        portAt = strFindChar(hostport, 0, ':');
        strSubStr(&url->host, hostport, 0, portAt >= 0 ? portAt : strEnd);
        ok = true;
    }

    if (ok && portAt >= 0) {
        string portstr = 0;
        uint64 port    = 0;
        strSubStr(&portstr, hostport, portAt + 1, strEnd);

        // An empty port ("host:") is legal and means "the scheme default", which is what leaving
        // it at 0 already expresses. Anything else has to be a number in range.
        if (strEmpty(portstr)) {
            url->port = 0;
        } else if (strToUInt64(&port, portstr, 10, true) && port > 0 && port <= UINT16_MAX) {
            url->port = (uint16)port;
        } else {
            ok = false;
        }

        strDestroy(&portstr);
    }

    if (ok && strEmpty(url->host))
        ok = false;   // a URL with no host is not something that can be dialed

    strLower(&url->host);
    strDestroy(&hostport);
    return ok;
}

// Parse the part after the authority: path, query, fragment. Shared by the absolute parser and by
// reference resolution, which reaches the same three fields by a different route.
static void parsePathQueryFragment(_Inout_ HttpUrl* url, strref rest)
{
    string work = 0;
    strDup(&work, rest);

    int32 hash = strFindChar(work, 0, '#');
    if (hash >= 0) {
        string raw = 0;
        strSubStr(&raw, work, hash + 1, strEnd);
        httpUrlDecode(&url->fragment, raw, false);
        strDestroy(&raw);
        strSubStrI(&work, 0, hash);
    }

    int32 qmark = strFindChar(work, 0, '?');
    if (qmark >= 0) {
        // The query stays encoded; see the file header.
        strSubStr(&url->query, work, qmark + 1, strEnd);
        strSubStrI(&work, 0, qmark);
    }

    if (strEmpty(work))
        strDup(&url->path, _SL("/"));
    else
        httpUrlDecode(&url->path, work, false);

    strDestroy(&work);
}

_Use_decl_annotations_
bool httpUrlParse(HttpUrl* out, strref url)
{
    _httpInit();

    HttpUrl u = { 0 };

    int32 sep = strFind(url, 0, _SL("://"));
    if (sep <= 0)
        return false;   // relative references go through httpUrlResolve, not here

    strSubStr(&u.scheme, url, 0, sep);
    strLower(&u.scheme);

    if (httpSchemeDefaultPort(u.scheme) == 0) {
        // Refusing an unknown scheme here rather than at dial time means a mailto: or file: URL in
        // a redirect chain fails at the point it can be explained, not several layers later.
        httpUrlDestroy(&u);
        return false;
    }

    string rest = 0;
    strSubStr(&rest, url, sep + 3, strEnd);

    // The authority ends at the first of '/', '?' or '#'.
    int32 end = strFindAny(rest, 0, _SL("/?#"));

    string auth = 0;
    strSubStr(&auth, rest, 0, end >= 0 ? end : strEnd);

    bool ok = parseAuthority(&u, auth);
    strDestroy(&auth);

    if (ok) {
        string tail = 0;
        if (end >= 0)
            strSubStr(&tail, rest, end, strEnd);
        parsePathQueryFragment(&u, tail);
        strDestroy(&tail);
    }

    strDestroy(&rest);

    if (!ok) {
        httpUrlDestroy(&u);
        return false;
    }

    *out = u;
    return true;
}

// Remove "." and ".." segments from an already-joined path (RFC 3986 5.2.4). Runs after merging,
// not during, so "a/b/../../c" collapses the same way whatever produced it.
static void removeDotSegments(_Inout_ strhandle path)
{
    sa_string segs = { 0 };   // strSplit clears rather than initializes; a zeroed array self-sizes
    strSplit(&segs, *path, _SL("/"), true);

    sa_string outsegs;
    saInit(&outsegs, string, saSize(segs));

    for (int32 i = 0; i < saSize(segs); i++) {
        strref s = segs.a[i];

        if (strEq(s, _SL("."))) {
            continue;
        } else if (strEq(s, _SL(".."))) {
            // Never climb above the root: a "..' that would escape is dropped, which is what stops
            // a crafted redirect from walking out of a path prefix.
            if (saSize(outsegs) > 1)
                saRemove(&outsegs, saSize(outsegs) - 1);
            continue;
        }

        saPush(&outsegs, strref, s);
    }

    // A trailing "." or ".." names a directory, so the result must keep its trailing slash.
    if (saSize(segs) > 0) {
        strref last = segs.a[saSize(segs) - 1];
        if ((strEq(last, _SL(".")) || strEq(last, _SL(".."))) &&
            (saSize(outsegs) == 0 || !strEmpty(outsegs.a[saSize(outsegs) - 1])))
            saPush(&outsegs, strref, _SL(""));
    }

    strDestroy(path);
    strJoin(path, outsegs, _SL("/"));

    if (strEmpty(*path) || strGetChar(*path, 0) != '/')
        strPrepend(_SL("/"), path);

    saDestroy(&outsegs);
    saDestroy(&segs);
}

_Use_decl_annotations_
bool httpUrlResolve(HttpUrl* out, const HttpUrl* base, strref ref)
{
    _httpInit();

    // An absolute reference ignores the base entirely.
    if (httpUrlParse(out, ref))
        return true;

    if (strEmpty(ref))
        return false;

    HttpUrl u = { 0 };
    strDup(&u.scheme, base->scheme);

    // Network-path reference: "//host/path" keeps only the scheme.
    if (strBeginsWith(ref, _SL("//"))) {
        string rest = 0;
        strSubStr(&rest, ref, 2, strEnd);

        int32 end   = strFindAny(rest, 0, _SL("/?#"));
        string auth = 0;
        strSubStr(&auth, rest, 0, end >= 0 ? end : strEnd);

        bool ok = parseAuthority(&u, auth);
        strDestroy(&auth);

        if (ok) {
            string tail = 0;
            if (end >= 0)
                strSubStr(&tail, rest, end, strEnd);
            parsePathQueryFragment(&u, tail);
            strDestroy(&tail);
            removeDotSegments(&u.path);
        }

        strDestroy(&rest);

        if (!ok) {
            httpUrlDestroy(&u);
            return false;
        }

        *out = u;
        return true;
    }

    // Everything below keeps the base's authority.
    strDup(&u.host, base->host);
    strDup(&u.user, base->user);
    strDup(&u.pass, base->pass);
    u.port     = base->port;
    u.ipv6Host = base->ipv6Host;

    uint8 first = strGetChar(ref, 0);

    if (first == '#') {
        // Fragment-only: everything else comes from the base.
        strDup(&u.path, base->path);
        strDup(&u.query, base->query);
        string raw = 0;
        strSubStr(&raw, ref, 1, strEnd);
        httpUrlDecode(&u.fragment, raw, false);
        strDestroy(&raw);
        *out = u;
        return true;
    }

    if (first == '?') {
        // Query-only: this query, the base's path. parsePathQueryFragment() cannot be used here --
        // it would see an empty path component once the query is stripped and reset the path to
        // "/", which is exactly what the base's path is supposed to survive.
        strDup(&u.path, base->path);

        string rest = 0;
        strSubStr(&rest, ref, 1, strEnd);

        int32 hash = strFindChar(rest, 0, '#');
        if (hash >= 0) {
            string raw = 0;
            strSubStr(&raw, rest, hash + 1, strEnd);
            httpUrlDecode(&u.fragment, raw, false);
            strDestroy(&raw);
            strSubStrI(&rest, 0, hash);
        }

        u.query = rest;   // stays encoded, like every other query
        *out    = u;
        return true;
    }

    if (first == '/') {
        // Absolute path: replaces the base's path outright.
        parsePathQueryFragment(&u, ref);
        removeDotSegments(&u.path);
        *out = u;
        return true;
    }

    // Relative path: merge against the base's directory, i.e. everything up to and including its
    // last slash.
    string merged = 0;
    int32 slash   = strFindCharR(base->path, strEnd, '/');
    if (slash >= 0)
        strSubStr(&merged, base->path, 0, slash + 1);
    else
        strDup(&merged, _SL("/"));

    // The reference is appended raw and decoded as one piece afterwards, so a %2F in it stays a
    // literal slash-in-a-segment rather than becoming a separator that removeDotSegments would act
    // on. Encoding the base's already-decoded path back up keeps the two halves in the same space.
    string encBase = 0;
    httpUrlEncode(&encBase, merged, HTTPENC_Path);
    strAppend(&encBase, ref);

    parsePathQueryFragment(&u, encBase);
    removeDotSegments(&u.path);

    strDestroy(&encBase);
    strDestroy(&merged);

    *out = u;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Composition
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool httpUrlHostHeader(strhandle out, const HttpUrl* url)
{
    string ret = 0;

    if (url->ipv6Host) {
        strAppendChar(&ret, '[');
        strAppend(&ret, url->host);
        strAppendChar(&ret, ']');
    } else {
        strDup(&ret, url->host);
    }

    // The port is named only when it is not the scheme's default, which is why HttpUrl keeps 0 for
    // "unspecified" instead of filling the default in at parse time.
    if (url->port != 0 && url->port != httpSchemeDefaultPort(url->scheme)) {
        string p = 0;
        strAppendChar(&ret, ':');
        strFromUInt32(&p, url->port, 10);
        strAppend(&ret, p);
        strDestroy(&p);
    }

    strDestroy(out);
    *out = ret;
    return true;
}

_Use_decl_annotations_
bool httpUrlTarget(strhandle out, const HttpUrl* url)
{
    string ret = 0;

    if (strEmpty(url->path))
        strDup(&ret, _SL("/"));
    else
        httpUrlEncode(&ret, url->path, HTTPENC_Path);

    if (!strEmpty(url->query)) {
        strAppendChar(&ret, '?');
        strAppend(&ret, url->query);
    }

    strDestroy(out);
    *out = ret;
    return true;
}

_Use_decl_annotations_
bool httpUrlFormat(strhandle out, const HttpUrl* url)
{
    string ret = 0;

    strAppend(&ret, url->scheme);
    strAppend(&ret, _SL("://"));

    if (!strEmpty(url->user)) {
        string enc = 0;
        httpUrlEncode(&enc, url->user, HTTPENC_Strict);
        strAppend(&ret, enc);
        if (!strEmpty(url->pass)) {
            strAppendChar(&ret, ':');
            httpUrlEncode(&enc, url->pass, HTTPENC_Strict);
            strAppend(&ret, enc);
        }
        strAppendChar(&ret, '@');
        strDestroy(&enc);
    }

    string hostpart = 0;
    httpUrlHostHeader(&hostpart, url);
    strAppend(&ret, hostpart);
    strDestroy(&hostpart);

    string target = 0;
    httpUrlTarget(&target, url);
    strAppend(&ret, target);
    strDestroy(&target);

    if (!strEmpty(url->fragment)) {
        string enc = 0;
        strAppendChar(&ret, '#');
        httpUrlEncode(&enc, url->fragment, HTTPENC_Query);
        strAppend(&ret, enc);
        strDestroy(&enc);
    }

    strDestroy(out);
    *out = ret;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Query strings
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool httpQueryParse(HttpHeaders* out, strref query)
{
    httpHeadersInit(out);

    string pair = 0;
    int32 pos   = 0;

    // ';' as an alternative separator is historical (it was once recommended for HTML), and still
    // turns up. Accepting it costs nothing here and is not ambiguous, since ';' is escaped on the
    // way out.
    while (strSplitNextAny(query, &pos, _SL("&;"), &pair)) {
        if (strEmpty(pair))
            continue;

        string k = 0, v = 0;
        int32 eq = strFindChar(pair, 0, '=');

        if (eq >= 0) {
            string rk = 0, rv = 0;
            strSubStr(&rk, pair, 0, eq);
            strSubStr(&rv, pair, eq + 1, strEnd);
            httpUrlDecode(&k, rk, true);
            httpUrlDecode(&v, rv, true);
            strDestroy(&rk);
            strDestroy(&rv);
        } else {
            // A key with no '=' keeps an empty value rather than being dropped: "?debug" is a flag,
            // and losing it would change what the query means.
            httpUrlDecode(&k, pair, true);
        }

        httpHeadersAdd(out, k, v);
        strDestroy(&k);
        strDestroy(&v);
    }

    strDestroy(&pair);
    return true;
}

_Use_decl_annotations_
bool httpQueryFormat(strhandle out, const HttpHeaders* pairs)
{
    string ret = 0;
    string enc = 0;

    for (int32 i = 0; i < httpHeadersCount(pairs); i++) {
        if (i > 0)
            strAppendChar(&ret, '&');

        httpUrlEncode(&enc, pairs->names.a[i], HTTPENC_Strict);
        strAppend(&ret, enc);
        strAppendChar(&ret, '=');
        httpUrlEncode(&enc, pairs->values.a[i], HTTPENC_Strict);
        strAppend(&ret, enc);
    }

    strDestroy(&enc);
    strDestroy(out);
    *out = ret;
    return true;
}

// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "httpcookie.h"
// clang-format on
// ==================== Auto-generated section ends ======================

// ---------------------------------------------------------------------------------------------
// Cookies
//
// RFC 6265 with the parts a client actually needs: name, value, Domain, Path, Expires, Max-Age and
// Secure. HttpOnly is stored so it survives a round trip through the enumeration API, but cxhttp
// has no scripting environment to withhold it from. SameSite is ignored: it governs cross-site
// request forgery in a browser, and there are no cross-site requests here to govern.
//
// The two rules worth stating outright, because both are security properties rather than
// conveniences:
//
//   - A response may only set a cookie for a domain its own host is under. Without that check,
//     one host could set a cookie for another, which is where session fixation starts.
//   - A cookie marked Secure never travels over plaintext, no matter which host asked.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/time/clock.h>
#include <cx/time/time.h>

// ---------------------------------------------------------------------------------------------
// The stored cookie type
// ---------------------------------------------------------------------------------------------

static void cookieCopy(stype st, stgeneric* gdest, stgeneric gsrc, uint32 flags)
{
    HttpCookie* dst = (HttpCookie*)gdest->st_opaque;
    HttpCookie* src = (HttpCookie*)gsrc.st_opaque;

    dst->name = dst->value = dst->domain = dst->path = 0;
    strDup(&dst->name, src->name);
    strDup(&dst->value, src->value);
    strDup(&dst->domain, src->domain);
    strDup(&dst->path, src->path);

    dst->expires  = src->expires;
    dst->hostOnly = src->hostOnly;
    dst->secure   = src->secure;
    dst->httpOnly = src->httpOnly;
}

static void cookieDtor(stype st, stgeneric* g, uint32 flags)
{
    HttpCookie* c = (HttpCookie*)g->st_opaque;
    strDestroy(&c->name);
    strDestroy(&c->value);
    strDestroy(&c->domain);
    strDestroy(&c->path);
}

// Cookie identity is the (name, domain, path) triple, per RFC 6265 5.3 step 11: a Set-Cookie for a
// name that is already stored for the same domain and path replaces it rather than adding a second.
static intptr cookieCmp(stype st, stgeneric g1, stgeneric g2, uint32 flags)
{
    HttpCookie* a = (HttpCookie*)g1.st_opaque;
    HttpCookie* b = (HttpCookie*)g2.st_opaque;

    intptr r = strCmp(a->name, b->name);
    if (r != 0)
        return r;
    r = strCmpi(a->domain, b->domain);
    if (r != 0)
        return r;
    return strCmp(a->path, b->path);
}

// Non-static: declared in http_shared.h so consumers can use `HttpCookie` as a type token too.
stDefine(HttpCookie) {
    .id    = stTypeId(opaque),
    .size  = sizeof(HttpCookie),
    .flags = stFlag(PassPtr),
    .ops   = { .cmp = cookieCmp, .copy = cookieCopy, .dtor = cookieDtor }
};

// ---------------------------------------------------------------------------------------------
// Matching rules
// ---------------------------------------------------------------------------------------------

// True if `host` is `domain` or a subdomain of it. The trailing-dot form matters: without the
// explicit '.' test, a cookie for "ample.com" would match "example.com".
static bool domainMatch(strref host, strref domain, bool hostOnly)
{
    if (strEmpty(host) || strEmpty(domain))
        return false;

    if (strEqi(host, domain))
        return true;
    if (hostOnly)
        return false;

    uint32 hl = strLen(host), dl = strLen(domain);
    if (hl <= dl)
        return false;
    if (strGetChar(host, (int32)(hl - dl - 1)) != '.')
        return false;

    return strRangeEqi(host, domain, (int32)(hl - dl), dl);
}

// RFC 6265 5.1.4: the request path equals the cookie path, or the cookie path is a prefix of it
// ending in '/', or a prefix immediately followed by '/'.
static bool pathMatch(strref reqPath, strref cookiePath)
{
    if (strEmpty(cookiePath))
        return true;
    if (strEq(reqPath, cookiePath))
        return true;

    uint32 cl = strLen(cookiePath), rl = strLen(reqPath);
    if (rl <= cl)
        return false;
    if (!strRangeEq(reqPath, cookiePath, 0, cl))
        return false;

    return strGetChar(cookiePath, -1) == '/' || strGetChar(reqPath, (int32)cl) == '/';
}

// RFC 6265 5.1.4's default-path: everything up to and including the last '/', or "/" if the path
// has no interior separator. Notably not the request path itself -- a cookie set by "/a/b" applies
// to "/a/", not only to "/a/b".
static void defaultPath(strhandle out, strref reqPath)
{
    if (strGetChar(reqPath, 0) != '/') {
        strDup(out, _SL("/"));
        return;
    }

    int32 slash = strFindCharR(reqPath, strEnd, '/');
    if (slash <= 0) {
        strDup(out, _SL("/"));
        return;
    }

    strSubStr(out, reqPath, 0, slash);
}

// A literal address is not a domain and has no subdomains: "Domain=1.2.3.4" may only ever match
// the exact host. Detected structurally rather than by parsing an address, because the only thing
// that matters here is whether a suffix match would be meaningful.
static bool looksLikeAddress(strref host)
{
    if (strEmpty(host))
        return false;
    if (strFindChar(host, 0, ':') >=
        0)   // an IPv6 literal, brackets already stripped by the parser
        return true;

    // An IPv4 literal ends in a digit; a registered name ends in a letter.
    uint8 last = strGetChar(host, -1);
    return last >= '0' && last <= '9';
}

// ---------------------------------------------------------------------------------------------
// Set-Cookie parsing
// ---------------------------------------------------------------------------------------------

// Split "name=value" at the first '=', trimming OWS from both halves. A field with no '=' at all is
// malformed for the cookie-pair and an attribute with none is a boolean flag, which is why the
// value comes back empty rather than the whole call failing.
static bool splitPair(strref s, strhandle name, strhandle value)
{
    int32 eq = strFindChar(s, 0, '=');
    if (eq < 0) {
        strDup(name, s);
        strClear(value);
    } else {
        strSubStr(name, s, 0, eq);
        strSubStr(value, s, eq + 1, strEnd);
    }

    _httpTrimOWS(name);
    _httpTrimOWS(value);
    return !strEmpty(*name);
}

static bool parseSetCookie(HttpCookie* out, strref field)
{
    sa_string parts = { 0 };
    strSplit(&parts, field, _SL(";"), false);
    if (saSize(parts) == 0) {
        saDestroy(&parts);
        return false;
    }

    memset(out, 0, sizeof(HttpCookie));

    if (!splitPair(parts.a[0], &out->name, &out->value)) {
        saDestroy(&parts);
        strDestroy(&out->name);
        strDestroy(&out->value);
        return false;
    }

    int64 maxAge      = 0;
    bool haveMaxAge   = false;
    int64 expiresTime = 0;
    bool haveExpires  = false;

    for (int32 i = 1; i < saSize(parts); i++) {
        string an = 0, av = 0;
        splitPair(parts.a[i], &an, &av);

        if (strEqi(an, _SL("Domain"))) {
            // A leading dot is legal and historically meaningful; RFC 6265 says to ignore it and
            // treat every Domain attribute as a suffix match.
            if (strGetChar(av, 0) == '.')
                strSubStr(&out->domain, av, 1, strEnd);
            else
                strDup(&out->domain, av);
            strLower(&out->domain);
        } else if (strEqi(an, _SL("Path"))) {
            if (strGetChar(av, 0) == '/')
                strDup(&out->path, av);
        } else if (strEqi(an, _SL("Expires"))) {
            haveExpires = httpDateParse(&expiresTime, av);
        } else if (strEqi(an, _SL("Max-Age"))) {
            int64 secs = 0;
            if (strToInt64(&secs, av, 10, STRNUM_NoTrailing)) {
                haveMaxAge = true;
                // A zero or negative Max-Age means "delete now", which is expressed here as an
                // expiry already in the past rather than as a special case downstream.
                maxAge     = secs;
            }
        } else if (strEqi(an, _SL("Secure"))) {
            out->secure = true;
        } else if (strEqi(an, _SL("HttpOnly"))) {
            out->httpOnly = true;
        }

        strDestroy(&an);
        strDestroy(&av);
    }

    // Max-Age wins over Expires when both are present (RFC 6265 5.3 step 3).
    if (haveMaxAge)
        out->expires = clockWall() + maxAge * (int64)1000000;
    else if (haveExpires)
        out->expires = expiresTime;

    saDestroy(&parts);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Jar
// ---------------------------------------------------------------------------------------------

// Caller holds the lock. Removes anything whose expiry has passed, so nothing downstream has to
// keep re-checking the clock.
static void sweepExpired(HttpCookieJar* self, int64 now)
{
    for (int32 i = saSize(self->cookies) - 1; i >= 0; i--) {
        if (self->cookies.a[i].expires != 0 && self->cookies.a[i].expires <= now)
            saRemove(&self->cookies, i);
    }
}

// Caller holds the lock. Replaces an existing cookie with the same identity, keeping its position
// so the order cookies were first seen in survives a refresh.
static void storeLocked(HttpCookieJar* self, HttpCookie* c)
{
    int32 idx = saFind(self->cookies, HttpCookie, *c);
    if (idx >= 0)
        saRemove(&self->cookies, idx);

    if (c->expires != 0 && c->expires <= clockWall())
        return;   // an already-expired cookie is a deletion, and the removal above was the whole
                  // job

    if (idx >= 0)
        saInsert(&self->cookies, idx, HttpCookie, *c);
    else
        saPush(&self->cookies, HttpCookie, *c);
}

_objfactory_guaranteed HttpCookieJar* HttpCookieJar_create()
{
    _httpInit();

    HttpCookieJar* self;
    self = objInstCreate(HttpCookieJar);

    objInstInit(self);

    return self;
}

_objinit_guaranteed bool HttpCookieJar_init(_In_ HttpCookieJar* self)
{
    saInit(&self->cookies, HttpCookie, 8);
    // Autogen begins -----
    mutexInit(&self->lock);
    return true;
    // Autogen ends -------
}

bool HttpCookieJar_setCookie(_In_ HttpCookieJar* self, _In_ HttpUrl* url, _In_opt_ strref field)
{
    if (!url || strEmpty(field))
        return false;

    HttpCookie c;
    if (!parseSetCookie(&c, field))
        return false;

    bool ok = false;

    if (strEmpty(c.domain)) {
        // No Domain attribute: the cookie belongs to this host and no other.
        strDup(&c.domain, url->host);
        c.hostOnly = true;
        ok         = !strEmpty(c.domain);
    } else if (looksLikeAddress(url->host)) {
        // A literal address can only ever set a cookie for itself.
        ok = strEqi(c.domain, url->host);
    } else {
        // The check that stops one host setting cookies for another.
        ok = domainMatch(url->host, c.domain, false);
    }

    if (ok) {
        if (strEmpty(c.path))
            defaultPath(&c.path, url->path);

        withMutex (&self->lock) {
            storeLocked(self, &c);
        }
    }

    // storeLocked() copies through the stype, so the local always owns its own strings.
    stDestroy(HttpCookie, &c);
    return ok;
}

int32 HttpCookieJar_storeAll(_In_ HttpCookieJar* self, _In_ HttpUrl* url, _In_ HttpHeaders* headers)
{
    if (!url || !headers)
        return 0;

    sa_string vals;
    int32 n     = httpHeadersGetAll(headers, _SL("Set-Cookie"), &vals);
    int32 count = 0;

    for (int32 i = 0; i < n; i++) {
        if (httpcookiejarSetCookie(self, url, vals.a[i]))
            count++;
    }

    saDestroy(&vals);
    return count;
}

bool HttpCookieJar_header(_In_ HttpCookieJar* self, _Out_ string* out, _In_ HttpUrl* url)
{
    strClear(out);
    if (!url)
        return false;

    bool secureXport = strEqi(url->scheme, _SL("https"));
    int64 now        = clockWall();

    sa_int32 match;
    saInit(&match, int32, 8);

    withMutex (&self->lock) {
        sweepExpired(self, now);

        for (int32 i = 0; i < saSize(self->cookies); i++) {
            HttpCookie* c = &self->cookies.a[i];
            if (c->secure && !secureXport)
                continue;
            if (!domainMatch(url->host, c->domain, c->hostOnly))
                continue;
            if (!pathMatch(url->path, c->path))
                continue;

            // Longest path first, as RFC 6265 5.4 requires: a server that reads only the first
            // value for a name must get the most specific one. Insertion sort, because a jar that
            // matched more than a handful of cookies for one request would be unusual.
            uint32 pl  = strLen(c->path);
            int32 slot = saSize(match);
            while (slot > 0 && strLen(self->cookies.a[match.a[slot - 1]].path) < pl) slot--;
            saInsert(&match, slot, int32, i);
        }

        for (int32 i = 0; i < saSize(match); i++) {
            HttpCookie* c = &self->cookies.a[match.a[i]];
            if (!strEmpty(*out))
                strAppend(out, _SL("; "));
            strAppend(out, c->name);
            strAppendChar(out, '=');
            strAppend(out, c->value);
        }
    }

    saDestroy(&match);
    return !strEmpty(*out);
}

int32 HttpCookieJar_count(_In_ HttpCookieJar* self)
{
    int32 n = 0;
    withMutex (&self->lock) {
        n = saSize(self->cookies);
    }
    return n;
}

int32 HttpCookieJar_enumerate(_In_ HttpCookieJar* self, _Out_ sa_HttpCookie* out)
{
    saInit(out, HttpCookie, 8);

    withMutex (&self->lock) {
        sweepExpired(self, clockWall());
        for (int32 i = 0; i < saSize(self->cookies); i++)
            saPush(out, HttpCookie, self->cookies.a[i]);
    }

    return saSize(*out);
}

bool HttpCookieJar_add(_In_ HttpCookieJar* self, _In_ HttpCookie* cookie)
{
    if (!cookie || strEmpty(cookie->name) || strEmpty(cookie->domain))
        return false;

    HttpCookie c = *cookie;
    if (strEmpty(c.path))
        c.path = _S "/";

    withMutex (&self->lock) {
        storeLocked(self, &c);
    }
    return true;
}

void HttpCookieJar_clear(_In_ HttpCookieJar* self)
{
    withMutex (&self->lock) {
        saClear(&self->cookies);
    }
}

void HttpCookieJar_destroy(_In_ HttpCookieJar* self)
{
    saDestroy(&self->cookies);
    // Autogen begins -----
    mutexDestroy(&self->lock);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "httpcookie.auto.inc"
// clang-format on
// Autogen ends -------

#include "http_private.h"

#include "httpclient.h"

#include <cx/time/clock.h>

// Alternative services (RFC 7838), and the memory a client keeps of what each origin turned out to
// speak.
//
// The two belong together because the header is the only reason the memory has anything in it
// before a connection has been tried: an `Alt-Svc: h3=":443"` on an ordinary HTTP/1.1 response is
// how a client that never raced finds HTTP/3 at all. Everything else that writes the table --
// a QUIC dial that worked, one that did not -- writes the same entries the same way.

// ---------------------------------------------------------------------------------------------
// Parsing
//
//   Alt-Svc       = clear / 1#alt-value
//   alt-value     = alternative *( OWS ";" OWS parameter )
//   alternative   = protocol-id "=" alt-authority
//   alt-authority = quoted-string          ; [ uri-host ] ":" port
//   parameter     = token "=" ( token / quoted-string )
// ---------------------------------------------------------------------------------------------

// A cursor over the field value. Small enough that a scanner is less code than anything general.
typedef struct AltScan {
    strref s;
    uint32 pos;
    uint32 len;
} AltScan;

static void scanInit(_Out_ AltScan* sc, _In_opt_ strref s)
{
    sc->s   = s;
    sc->pos = 0;
    sc->len = strLen(s);
}

static uint8 scanPeek(_In_ const AltScan* sc)
{
    return sc->pos < sc->len ? strGetChar(sc->s, sc->pos) : 0;
}

static void scanSkipOWS(_Inout_ AltScan* sc)
{
    while (sc->pos < sc->len && _httpIsOWS(strGetChar(sc->s, sc->pos)))
        sc->pos++;
}

// Read a `token`, which is what a protocol-id and a parameter name are made of.
static bool scanToken(_Inout_ AltScan* sc, _Inout_ strhandle out)
{
    uint32 start = sc->pos;
    while (sc->pos < sc->len && _httpIsTokenChar(strGetChar(sc->s, sc->pos)))
        sc->pos++;

    if (sc->pos == start)
        return false;

    return strSubStr(out, sc->s, start, sc->pos);
}

// Read a quoted-string, undoing its backslash escapes. The alt-authority is always one of these,
// because it contains a colon.
static bool scanQuoted(_Inout_ AltScan* sc, _Inout_ strhandle out)
{
    if (scanPeek(sc) != '"')
        return false;
    sc->pos++;

    strClear(out);
    while (sc->pos < sc->len) {
        uint8 c = strGetChar(sc->s, sc->pos++);

        if (c == '"')
            return true;

        if (c == '\\' && sc->pos < sc->len)
            c = strGetChar(sc->s, sc->pos++);

        strAppendChar(out, c);
    }

    return false;   // ran off the end with the string still open
}

// A parameter value is a token or a quoted-string, and the grammar does not say which.
static bool scanValue(_Inout_ AltScan* sc, _Inout_ strhandle out)
{
    return scanPeek(sc) == '"' ? scanQuoted(sc, out) : scanToken(sc, out);
}

// Split `[host]:port`. The host half may be empty, meaning the origin's own, and may be a
// bracketed IPv6 literal -- which is why the split is at the *last* colon rather than the first.
static bool splitAuthority(_In_opt_ strref auth, _Inout_ strhandle host, _Out_ uint16* port)
{
    *port = 0;

    int32 colon = strFindR(auth, strEnd, _SL(":"));
    if (colon < 0)
        return false;

    uint64 n = 0;
    string p = 0;
    bool ok  = strSubStr(&p, auth, colon + 1, strEnd) &&
              strToUInt64(&n, p, 10, HTTP_STRICTNUM) && n > 0 && n <= UINT16_MAX;
    strDestroy(&p);
    if (!ok)
        return false;

    *port = (uint16)n;

    if (colon == 0) {
        strClear(host);
        return true;
    }

    if (!strSubStr(host, auth, 0, colon))
        return false;

    // A bracketed IPv6 literal loses its brackets, so what comes out compares against HttpUrl::host
    // the way everything else does.
    if (strGetChar(*host, 0) == '[' && strGetChar(*host, strLen(*host) - 1) == ']')
        strSubStrI(host, 1, -1);

    return true;
}

_Use_decl_annotations_
void _httpAltSvcDestroy(HttpAltSvc* alt)
{
    strDestroy(&alt->proto);
    strDestroy(&alt->host);
}

_Use_decl_annotations_
bool _httpAltSvcFind(HttpAltSvc* out, strref value, strref proto)
{
    memset(out, 0, sizeof(*out));

    AltScan sc;
    scanInit(&sc, value);
    scanSkipOWS(&sc);

    string tok  = 0;
    string auth = 0;
    string pval = 0;
    bool found  = false;

    // `clear` is the whole field value and nothing else: the origin is asking that every
    // alternative it ever advertised be forgotten.
    {
        AltScan probe = sc;
        if (scanToken(&probe, &tok) && strEqi(tok, _SL("clear"))) {
            scanSkipOWS(&probe);
            if (probe.pos >= probe.len) {
                out->clear = true;
                found      = true;
                goto out;
            }
        }
    }

    while (sc.pos < sc.len) {
        scanSkipOWS(&sc);

        // protocol-id "=" alt-authority
        if (!scanToken(&sc, &tok))
            break;

        scanSkipOWS(&sc);
        if (scanPeek(&sc) != '=')
            break;
        sc.pos++;
        scanSkipOWS(&sc);

        if (!scanQuoted(&sc, &auth))
            break;

        // A protocol-id is percent-encoded so that it can carry an ALPN name with bytes a token
        // has no room for. `h3` needs none of that, but decoding is what makes the comparison
        // right for the ones that do.
        string decoded = 0;
        httpUrlDecode(&decoded, tok, false);
        bool wanted = strEq(decoded, proto);
        strDestroy(&decoded);

        int64 maxAge = timeS(86400);   // RFC 7838's default

        // *( OWS ";" OWS parameter )
        for (;;) {
            scanSkipOWS(&sc);
            if (scanPeek(&sc) != ';')
                break;
            sc.pos++;
            scanSkipOWS(&sc);

            if (!scanToken(&sc, &tok))
                break;
            scanSkipOWS(&sc);
            if (scanPeek(&sc) != '=')
                break;
            sc.pos++;
            scanSkipOWS(&sc);
            if (!scanValue(&sc, &pval))
                break;

            uint64 n = 0;
            if (strEqi(tok, _SL("ma")) && strToUInt64(&n, pval, 10, HTTP_STRICTNUM))
                maxAge = timeS((int64)min(n, (uint64)(HTTPORIGIN_MAXAGE / timeS(1))));
        }

        if (wanted) {
            strDup(&out->proto, proto);
            out->maxAge = maxAge;
            found       = splitAuthority(auth, &out->host, &out->port);
            if (found)
                goto out;
            strDestroy(&out->proto);
        }

        // On to the next alternative in the list.
        scanSkipOWS(&sc);
        if (scanPeek(&sc) != ',')
            break;
        sc.pos++;
    }

out:
    strDestroy(&tok);
    strDestroy(&auth);
    strDestroy(&pval);
    return found;
}

// ---------------------------------------------------------------------------------------------
// What a client remembers about an origin
// ---------------------------------------------------------------------------------------------

// Caller holds the lock. -1 when the origin is not known, or is known only from something that has
// since expired -- which is also when the entry is dropped.
static int32 originFind(_Inout_ HttpClient* cl, _In_opt_ strref key, int64 now)
{
    for (int32 i = saSize(cl->originKeys) - 1; i >= 0; i--) {
        if (!strEq(cl->originKeys.a[i], key))
            continue;

        if (now >= cl->originExpires.a[i]) {
            saRemove(&cl->originKeys, i);
            saRemove(&cl->originFlags, i);
            saRemove(&cl->originExpires, i);
            saRemove(&cl->originAltPort, i);
            return -1;
        }
        return i;
    }
    return -1;
}

_Use_decl_annotations_
void _httpOriginRemember(HttpClient* cl, strref key, uint32 flags, uint16 altPort, int64 ttl)
{
    int64 now = clockTimer();

    withMutex (&cl->lock) {
        int32 i = originFind(cl, key, now);
        if (i < 0) {
            saPush(&cl->originKeys, strref, key);
            saPush(&cl->originFlags, uint32, flags);
            saPush(&cl->originExpires, int64, now + ttl);
            saPush(&cl->originAltPort, uint16, altPort);
        } else {
            cl->originFlags.a[i]   = flags;
            cl->originExpires.a[i] = now + ttl;
            cl->originAltPort.a[i] = altPort;
        }
    }
}

_Use_decl_annotations_
uint32 _httpOriginRecall(HttpClient* cl, strref key, uint16* altPort)
{
    uint32 flags = 0;
    int64 now    = clockTimer();

    *altPort = 0;

    withMutex (&cl->lock) {
        int32 i = originFind(cl, key, now);
        if (i >= 0) {
            flags    = cl->originFlags.a[i];
            *altPort = cl->originAltPort.a[i];
        }
    }
    return flags;
}

// Forget everything about an origin. What `Alt-Svc: clear` asks for.
static void originForget(_Inout_ HttpClient* cl, _In_opt_ strref key)
{
    withMutex (&cl->lock) {
        for (int32 i = saSize(cl->originKeys) - 1; i >= 0; i--) {
            if (!strEq(cl->originKeys.a[i], key))
                continue;
            saRemove(&cl->originKeys, i);
            saRemove(&cl->originFlags, i);
            saRemove(&cl->originExpires, i);
            saRemove(&cl->originAltPort, i);
        }
    }
}

_Use_decl_annotations_
void _httpOriginLearnAltSvc(HttpClient* cl, strref key, const HttpUrl* url,
                            const HttpHeaders* resp)
{
    // Nothing to learn in a build with no HTTP/3 in it, and nothing that could act on it.
    if (!_http3Available())
        return;

    string value = 0;
    if (!httpHeadersGet(resp, _SL("Alt-Svc"), &value) || strEmpty(value)) {
        strDestroy(&value);
        return;
    }

    HttpAltSvc alt;
    bool got = _httpAltSvcFind(&alt, value, _SL(HTTP_ALPN_H3));
    strDestroy(&value);

    if (!got)
        return;

    if (alt.clear) {
        originForget(cl, key);
        _httpAltSvcDestroy(&alt);
        return;
    }

    // An alternative on a different host is a different endpoint, with its own certificate to
    // check and its own origin to be keyed under. cxhttp does not follow one: what it acts on is
    // the same host answering HTTP/3, which is what an origin advertising its own port means and
    // what every real deployment sends.
    bool sameHost = strEmpty(alt.host) || strEqi(alt.host, url->host);

    if (sameHost) {
        // The port is recorded only when it differs from the origin's own, so that the common
        // `h3=":443"` on an https origin costs nothing to act on.
        uint16 port = (alt.port == httpUrlEffectivePort(url)) ? 0 : alt.port;
        _httpOriginRemember(cl, key, HTTPORIGIN_H3Works, port, alt.maxAge);
    }

    _httpAltSvcDestroy(&alt);
}

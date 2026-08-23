// ---------------------------------------------------------------------------------------------
// Form bodies
//
// The two encodings an HTML form can produce, because an API that only speaks JSON still has to
// log in somewhere. Both build a body in memory: the consumers cxhttp targets post credentials and
// small field sets, not gigabyte uploads. A large upload has a better path already -- compose the
// body into a StreamBuffer and hand it to httprequestSetBodyStream(), which never makes it
// resident.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/platform/os.h>

STR_CONST(kUrlEncodedType, "application/x-www-form-urlencoded");
STR_CONST(kMultipartPrefix, "multipart/form-data; boundary=");

// Long enough that it cannot occur in the payload by accident, and drawn from a real random source
// rather than a counter: a boundary an attacker can predict is a boundary they can inject.
#define BOUNDARY_BYTES 18

_Use_decl_annotations_
bool httpFormUrlEncoded(strhandle out, const HttpHeaders* fields)
{
    return httpQueryFormat(out, fields);
}

_Use_decl_annotations_
strref httpFormUrlEncodedType(void)
{
    return kUrlEncodedType;
}

// ---------------------------------------------------------------------------------------------
// multipart/form-data
// ---------------------------------------------------------------------------------------------

static void makeBoundary(strhandle out)
{
    uint8 raw[BOUNDARY_BYTES];
    if (!osGenRandom(raw, sizeof(raw))) {
        strClear(out);
        return;
    }

    string b64 = 0;
    strB64Encode(&b64, raw, sizeof(raw), true);

    // The URL-safe alphabet is already inside the character set a boundary may use; only the '='
    // padding has to go.
    strClear(out);
    strAppend(out, _SL("cxhttp-"));
    striter it;
    striBorrow(&it, b64);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c != '=')
            strAppendChar(out, c);
    }
    strDestroy(&b64);
}

// A quoted-string in a Content-Disposition parameter. RFC 7578 says to send the name as-is and
// escape the two characters that would end the string early; anything cleverer (RFC 2231 encoding)
// is understood by almost nothing.
static void appendQuoted(strhandle out, strref s)
{
    strAppendChar(out, '"');
    striter it;
    striBorrow(&it, s);
    uint8 c;
    while (striChar(&it, &c)) {
        if (c == '"' || c == '\\')
            strAppendChar(out, '\\');
        // A raw CR or LF in a field name would split the part header in two, which is the
        // multipart spelling of header injection.
        if (c == '\r' || c == '\n')
            continue;
        strAppendChar(out, c);
    }
    strAppendChar(out, '"');
}

_Use_decl_annotations_
bool httpMultipartInit(HttpMultipart* mp)
{
    if (!mp)
        return false;

    memset(mp, 0, sizeof(HttpMultipart));
    makeBoundary(&mp->boundary);
    return !strEmpty(mp->boundary);
}

// Open one part: the boundary line and the Content-Disposition it carries.
static void beginPart(HttpMultipart* mp, strref name, strref filename)
{
    strAppend(&mp->body, _SL("--"));
    strAppend(&mp->body, mp->boundary);
    strAppend(&mp->body, _SL("\r\nContent-Disposition: form-data; name="));
    appendQuoted(&mp->body, name);

    if (!strEmpty(filename)) {
        strAppend(&mp->body, _SL("; filename="));
        appendQuoted(&mp->body, filename);
    }
    strAppend(&mp->body, _SL("\r\n"));
}

_Use_decl_annotations_
bool httpMultipartAddField(HttpMultipart* mp, strref name, strref value)
{
    if (!mp || strEmpty(name) || mp->finished)
        return false;

    beginPart(mp, name, NULL);
    strAppend(&mp->body, _SL("\r\n"));
    strAppend(&mp->body, value);
    strAppend(&mp->body, _SL("\r\n"));
    return true;
}

_Use_decl_annotations_
bool httpMultipartAddFile(HttpMultipart* mp, strref name, strref filename, strref contentType,
                          const uint8* data, size_t len)
{
    if (!mp || strEmpty(name) || mp->finished)
        return false;

    beginPart(mp, name, strEmpty(filename) ? _SL("file") : filename);

    strAppend(&mp->body, _SL("Content-Type: "));
    strAppend(&mp->body, strEmpty(contentType) ? _SL("application/octet-stream") : contentType);
    strAppend(&mp->body, _SL("\r\n\r\n"));

    if (data && len > 0)
        _httpAppendBytes(&mp->body, data, len);
    strAppend(&mp->body, _SL("\r\n"));
    return true;
}

_Use_decl_annotations_
bool httpMultipartFinish(HttpMultipart* mp, string* body, string* contentType)
{
    if (!mp)
        return false;

    if (!mp->finished) {
        strAppend(&mp->body, _SL("--"));
        strAppend(&mp->body, mp->boundary);
        strAppend(&mp->body, _SL("--\r\n"));
        mp->finished = true;
    }

    if (body)
        strDup(body, mp->body);

    if (contentType) {
        strClear(contentType);
        strAppend(contentType, kMultipartPrefix);
        strAppend(contentType, mp->boundary);
    }

    return true;
}

_Use_decl_annotations_
void httpMultipartDestroy(HttpMultipart* mp)
{
    if (!mp)
        return;

    strDestroy(&mp->boundary);
    strDestroy(&mp->body);
    mp->finished = false;
}

#include "http3_private.h"

#include <string.h>

// QPACK with the static table only.
//
// cxhttp advertises SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0 and SETTINGS_QPACK_BLOCKED_STREAMS = 0,
// which forbids the peer from using a dynamic table as well. Every field on the wire is therefore
// one of three things: an index into the 99-entry static table, a static name with a literal
// value, or two literals. A peer that references the dynamic table anyway is refused rather than
// mis-decoded.
//
// The cost is a few percent on a repeated header. What it buys is that a field section decodes the
// moment it arrives, with no ordering relationship to any other stream -- the head-of-line blocking
// QPACK's dynamic table introduces, and the encoder and decoder stream bookkeeping that goes with
// it, are both simply absent.

typedef struct QpackStaticEntry {
    strref name;
    strref value;
} QpackStaticEntry;

// The QPACK static table, RFC 9204 Appendix A. Each name and value is a cx string constant so
// that a lookup is a plain strEq against the field being encoded, and so the table costs no
// allocation and no startup work.
//
// QPSE declares the two constants for one entry; the array below references them with _SR().
#define QPSE(i, n, v) \
    STR_CONSTR(kQpN##i, n)   \
    STR_CONSTR(kQpV##i, v)

QPSE(0, ":authority", "")
QPSE(1, ":path", "/")
QPSE(2, "age", "0")
QPSE(3, "content-disposition", "")
QPSE(4, "content-length", "0")
QPSE(5, "cookie", "")
QPSE(6, "date", "")
QPSE(7, "etag", "")
QPSE(8, "if-modified-since", "")
QPSE(9, "if-none-match", "")
QPSE(10, "last-modified", "")
QPSE(11, "link", "")
QPSE(12, "location", "")
QPSE(13, "referer", "")
QPSE(14, "set-cookie", "")
QPSE(15, ":method", "CONNECT")
QPSE(16, ":method", "DELETE")
QPSE(17, ":method", "GET")
QPSE(18, ":method", "HEAD")
QPSE(19, ":method", "OPTIONS")
QPSE(20, ":method", "POST")
QPSE(21, ":method", "PUT")
QPSE(22, ":scheme", "http")
QPSE(23, ":scheme", "https")
QPSE(24, ":status", "103")
QPSE(25, ":status", "200")
QPSE(26, ":status", "304")
QPSE(27, ":status", "404")
QPSE(28, ":status", "503")
QPSE(29, "accept", "*/*")
QPSE(30, "accept", "application/dns-message")
QPSE(31, "accept-encoding", "gzip, deflate, br")
QPSE(32, "accept-ranges", "bytes")
QPSE(33, "access-control-allow-headers", "cache-control")
QPSE(34, "access-control-allow-headers", "content-type")
QPSE(35, "access-control-allow-origin", "*")
QPSE(36, "cache-control", "max-age=0")
QPSE(37, "cache-control", "max-age=2592000")
QPSE(38, "cache-control", "max-age=604800")
QPSE(39, "cache-control", "no-cache")
QPSE(40, "cache-control", "no-store")
QPSE(41, "cache-control", "public, max-age=31536000")
QPSE(42, "content-encoding", "br")
QPSE(43, "content-encoding", "gzip")
QPSE(44, "content-type", "application/dns-message")
QPSE(45, "content-type", "application/javascript")
QPSE(46, "content-type", "application/json")
QPSE(47, "content-type", "application/x-www-form-urlencoded")
QPSE(48, "content-type", "image/gif")
QPSE(49, "content-type", "image/jpeg")
QPSE(50, "content-type", "image/png")
QPSE(51, "content-type", "text/css")
QPSE(52, "content-type", "text/html; charset=utf-8")
QPSE(53, "content-type", "text/plain")
QPSE(54, "content-type", "text/plain;charset=utf-8")
QPSE(55, "range", "bytes=0-")
QPSE(56, "strict-transport-security", "max-age=31536000")
QPSE(57, "strict-transport-security", "max-age=31536000; includesubdomains")
QPSE(58, "strict-transport-security", "max-age=31536000; includesubdomains; preload")
QPSE(59, "vary", "accept-encoding")
QPSE(60, "vary", "origin")
QPSE(61, "x-content-type-options", "nosniff")
QPSE(62, "x-xss-protection", "1; mode=block")
QPSE(63, ":status", "100")
QPSE(64, ":status", "204")
QPSE(65, ":status", "206")
QPSE(66, ":status", "302")
QPSE(67, ":status", "400")
QPSE(68, ":status", "403")
QPSE(69, ":status", "421")
QPSE(70, ":status", "425")
QPSE(71, ":status", "500")
QPSE(72, "accept-language", "")
QPSE(73, "access-control-allow-credentials", "FALSE")
QPSE(74, "access-control-allow-credentials", "TRUE")
QPSE(75, "access-control-allow-headers", "*")
QPSE(76, "access-control-allow-methods", "get")
QPSE(77, "access-control-allow-methods", "get, post, options")
QPSE(78, "access-control-allow-methods", "options")
QPSE(79, "access-control-expose-headers", "content-length")
QPSE(80, "access-control-request-headers", "content-type")
QPSE(81, "access-control-request-method", "get")
QPSE(82, "access-control-request-method", "post")
QPSE(83, "alt-svc", "clear")
QPSE(84, "authorization", "")
QPSE(85, "content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'")
QPSE(86, "early-data", "1")
QPSE(87, "expect-ct", "")
QPSE(88, "forwarded", "")
QPSE(89, "if-range", "")
QPSE(90, "origin", "")
QPSE(91, "purpose", "prefetch")
QPSE(92, "server", "")
QPSE(93, "timing-allow-origin", "*")
QPSE(94, "upgrade-insecure-requests", "1")
QPSE(95, "user-agent", "")
QPSE(96, "x-forwarded-for", "")
QPSE(97, "x-frame-options", "deny")
QPSE(98, "x-frame-options", "sameorigin")

#undef QPSE

static const QpackStaticEntry qpackStaticTable[QPACK_STATIC_COUNT] = {
    { _SR(kQpN0), _SR(kQpV0) },
    { _SR(kQpN1), _SR(kQpV1) },
    { _SR(kQpN2), _SR(kQpV2) },
    { _SR(kQpN3), _SR(kQpV3) },
    { _SR(kQpN4), _SR(kQpV4) },
    { _SR(kQpN5), _SR(kQpV5) },
    { _SR(kQpN6), _SR(kQpV6) },
    { _SR(kQpN7), _SR(kQpV7) },
    { _SR(kQpN8), _SR(kQpV8) },
    { _SR(kQpN9), _SR(kQpV9) },
    { _SR(kQpN10), _SR(kQpV10) },
    { _SR(kQpN11), _SR(kQpV11) },
    { _SR(kQpN12), _SR(kQpV12) },
    { _SR(kQpN13), _SR(kQpV13) },
    { _SR(kQpN14), _SR(kQpV14) },
    { _SR(kQpN15), _SR(kQpV15) },
    { _SR(kQpN16), _SR(kQpV16) },
    { _SR(kQpN17), _SR(kQpV17) },
    { _SR(kQpN18), _SR(kQpV18) },
    { _SR(kQpN19), _SR(kQpV19) },
    { _SR(kQpN20), _SR(kQpV20) },
    { _SR(kQpN21), _SR(kQpV21) },
    { _SR(kQpN22), _SR(kQpV22) },
    { _SR(kQpN23), _SR(kQpV23) },
    { _SR(kQpN24), _SR(kQpV24) },
    { _SR(kQpN25), _SR(kQpV25) },
    { _SR(kQpN26), _SR(kQpV26) },
    { _SR(kQpN27), _SR(kQpV27) },
    { _SR(kQpN28), _SR(kQpV28) },
    { _SR(kQpN29), _SR(kQpV29) },
    { _SR(kQpN30), _SR(kQpV30) },
    { _SR(kQpN31), _SR(kQpV31) },
    { _SR(kQpN32), _SR(kQpV32) },
    { _SR(kQpN33), _SR(kQpV33) },
    { _SR(kQpN34), _SR(kQpV34) },
    { _SR(kQpN35), _SR(kQpV35) },
    { _SR(kQpN36), _SR(kQpV36) },
    { _SR(kQpN37), _SR(kQpV37) },
    { _SR(kQpN38), _SR(kQpV38) },
    { _SR(kQpN39), _SR(kQpV39) },
    { _SR(kQpN40), _SR(kQpV40) },
    { _SR(kQpN41), _SR(kQpV41) },
    { _SR(kQpN42), _SR(kQpV42) },
    { _SR(kQpN43), _SR(kQpV43) },
    { _SR(kQpN44), _SR(kQpV44) },
    { _SR(kQpN45), _SR(kQpV45) },
    { _SR(kQpN46), _SR(kQpV46) },
    { _SR(kQpN47), _SR(kQpV47) },
    { _SR(kQpN48), _SR(kQpV48) },
    { _SR(kQpN49), _SR(kQpV49) },
    { _SR(kQpN50), _SR(kQpV50) },
    { _SR(kQpN51), _SR(kQpV51) },
    { _SR(kQpN52), _SR(kQpV52) },
    { _SR(kQpN53), _SR(kQpV53) },
    { _SR(kQpN54), _SR(kQpV54) },
    { _SR(kQpN55), _SR(kQpV55) },
    { _SR(kQpN56), _SR(kQpV56) },
    { _SR(kQpN57), _SR(kQpV57) },
    { _SR(kQpN58), _SR(kQpV58) },
    { _SR(kQpN59), _SR(kQpV59) },
    { _SR(kQpN60), _SR(kQpV60) },
    { _SR(kQpN61), _SR(kQpV61) },
    { _SR(kQpN62), _SR(kQpV62) },
    { _SR(kQpN63), _SR(kQpV63) },
    { _SR(kQpN64), _SR(kQpV64) },
    { _SR(kQpN65), _SR(kQpV65) },
    { _SR(kQpN66), _SR(kQpV66) },
    { _SR(kQpN67), _SR(kQpV67) },
    { _SR(kQpN68), _SR(kQpV68) },
    { _SR(kQpN69), _SR(kQpV69) },
    { _SR(kQpN70), _SR(kQpV70) },
    { _SR(kQpN71), _SR(kQpV71) },
    { _SR(kQpN72), _SR(kQpV72) },
    { _SR(kQpN73), _SR(kQpV73) },
    { _SR(kQpN74), _SR(kQpV74) },
    { _SR(kQpN75), _SR(kQpV75) },
    { _SR(kQpN76), _SR(kQpV76) },
    { _SR(kQpN77), _SR(kQpV77) },
    { _SR(kQpN78), _SR(kQpV78) },
    { _SR(kQpN79), _SR(kQpV79) },
    { _SR(kQpN80), _SR(kQpV80) },
    { _SR(kQpN81), _SR(kQpV81) },
    { _SR(kQpN82), _SR(kQpV82) },
    { _SR(kQpN83), _SR(kQpV83) },
    { _SR(kQpN84), _SR(kQpV84) },
    { _SR(kQpN85), _SR(kQpV85) },
    { _SR(kQpN86), _SR(kQpV86) },
    { _SR(kQpN87), _SR(kQpV87) },
    { _SR(kQpN88), _SR(kQpV88) },
    { _SR(kQpN89), _SR(kQpV89) },
    { _SR(kQpN90), _SR(kQpV90) },
    { _SR(kQpN91), _SR(kQpV91) },
    { _SR(kQpN92), _SR(kQpV92) },
    { _SR(kQpN93), _SR(kQpV93) },
    { _SR(kQpN94), _SR(kQpV94) },
    { _SR(kQpN95), _SR(kQpV95) },
    { _SR(kQpN96), _SR(kQpV96) },
    { _SR(kQpN97), _SR(kQpV97) },
    { _SR(kQpN98), _SR(kQpV98) },
};

_Use_decl_annotations_
bool _qpackStaticGet(strref* name, strref* value, int32 idx)
{
    if (idx < 0 || idx >= QPACK_STATIC_COUNT)
        return false;

    *name  = qpackStaticTable[idx].name;
    *value = qpackStaticTable[idx].value;
    return true;
}

_Use_decl_annotations_
int32 _qpackStaticFindName(strref name)
{
    // A linear scan over 99 entries, each rejected on its length before a byte is compared. A
    // message carries on the order of fifteen fields, so an index would cost more to justify than
    // it saves.
    for (int32 i = 0; i < QPACK_STATIC_COUNT; i++) {
        if (strEq(qpackStaticTable[i].name, name))
            return i;
    }
    return -1;
}

_Use_decl_annotations_
int32 _qpackStaticFind(strref name, strref value)
{
    for (int32 i = 0; i < QPACK_STATIC_COUNT; i++) {
        if (strEq(qpackStaticTable[i].name, name) && strEq(qpackStaticTable[i].value, value))
            return i;
    }
    return -1;
}

_Use_decl_annotations_
uint64 _qpackFieldSectionSize(const HttpHeaders* h)
{
    int32 n     = httpHeadersCount(h);
    uint64 size = 0;
    for (int32 i = 0; i < n; i++)
        size += (uint64)strLen(h->names.a[i]) + strLen(h->values.a[i]) + 32;
    return size;
}

// ---------------------------------------------------------------------------------------------
// Prefixed integers and string literals (RFC 7541 sections 5.1 and 5.2)
// ---------------------------------------------------------------------------------------------

// Write `val` as an integer with an N-bit prefix. `flags` supplies the bits of the first byte
// above the prefix, which is where each representation carries its own type and switches.
static void wrInt(_Inout_ H3Wr* wr, uint64 val, uint8 prefixBits, uint8 flags)
{
    uint32 fit = (1u << prefixBits) - 1u;

    if (val < fit) {
        _h3Wr8(wr, (uint8)(flags | val));
        return;
    }

    _h3Wr8(wr, (uint8)(flags | fit));
    val -= fit;
    while (val >= 128) {
        _h3Wr8(wr, (uint8)((val & 0x7f) | 0x80));
        val >>= 7;
    }
    _h3Wr8(wr, (uint8)val);
}

// Read an integer with an N-bit prefix, whose first byte is at the cursor. The bits of that byte
// above the prefix are handed back through `flags`, still in place.
static uint64 rdInt(_Inout_ H3Rd* rd, uint8 prefixBits, _Out_ uint8* flags)
{
    uint8 b   = _h3Rd8(rd);
    uint32 fit = (1u << prefixBits) - 1u;

    *flags = (uint8)(b & ~fit);

    uint64 val = b & fit;
    if (val < fit)
        return val;

    uint32 shift = 0;
    for (;;) {
        uint8 c = _h3Rd8(rd);
        if (rd->bad)
            return 0;

        // Refuse a continuation run that could carry a value no varint can hold, rather than
        // letting it wrap. A peer sending one is either broken or trying to.
        if (shift > 62) {
            rd->bad = true;
            return 0;
        }
        val += (uint64)(c & 0x7f) << shift;
        if (val > H3_VARINT_MAX) {
            rd->bad = true;
            return 0;
        }
        if (!(c & 0x80))
            break;
        shift += 7;
    }
    return val;
}

// Write a string literal: an H flag in bit `prefixBits`, the length below it, then the octets,
// Huffman-coded when that comes out shorter. `huff` is scratch of at least `len` bytes.
static void wrStr(_Inout_ H3Wr* wr, _In_reads_bytes_(len) const uint8* data, size_t len,
                  uint8 prefixBits, uint8 flags, _Out_writes_bytes_(len) uint8* huff)
{
    uint8 hbit = (uint8)(1u << prefixBits);

    if (len > 0) {
        size_t hlen = _qpackHuffEncode(huff, len, data, len);
        if (hlen > 0 && hlen < len) {
            wrInt(wr, hlen, prefixBits, (uint8)(flags | hbit));
            _h3WrBytes(wr, huff, hlen);
            return;
        }
    }

    wrInt(wr, len, prefixBits, flags);
    _h3WrBytes(wr, data, len);
}

// Read a string literal into `out`, replacing whatever it held. `maxlen` bounds the decoded
// length; 0 means no bound.
static bool rdStr(_Inout_ H3Rd* rd, _Inout_ strhandle out, uint8 prefixBits, _Out_ uint8* flags,
                  size_t maxlen)
{
    uint8 hbit = (uint8)(1u << prefixBits);

    uint8 fl   = 0;
    uint64 len = rdInt(rd, prefixBits, &fl);
    if (rd->bad || len > _h3RdLeft(rd))
        return false;

    *flags = (uint8)(fl & ~hbit);

    const uint8* data = _h3RdBytes(rd, (size_t)len);
    if (!data)
        return false;

    if (!(fl & hbit)) {
        if (maxlen > 0 && len > maxlen)
            return false;
        return strFromBytes(out, data, (uint32)len);
    }

    size_t bufsz = _qpackHuffMaxDecoded((size_t)len);
    if (maxlen > 0 && bufsz > maxlen + 1)
        bufsz = maxlen + 1;   // one over the limit is enough to know it was exceeded

    uint8* buf  = xaAlloc(bufsz);
    size_t dlen = 0;
    bool ok     = _qpackHuffDecode(buf, bufsz, &dlen, data, (size_t)len);
    if (ok && maxlen > 0 && dlen > maxlen)
        ok = false;
    if (ok)
        ok = strFromBytes(out, buf, (uint32)dlen);
    xaFree(buf);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Field sections (RFC 9204 sections 4.5 and 5)
// ---------------------------------------------------------------------------------------------

// True for a field name HTTP/3 permits: a non-empty token, lowercase throughout, optionally with
// the leading colon that marks a pseudo-header. Uppercase is the interesting case -- HTTP/1.1
// tolerates it, HTTP/3 does not, and a peer that sends it is required to be refused rather than
// quietly corrected.
static bool validFieldName(_In_opt_ strref name)
{
    uint32 len = strLen(name);
    if (len == 0)
        return false;

    striter it;
    striBorrow(&it, name);

    bool ok    = true;
    uint32 pos = 0;
    while (it.len > 0 && ok) {
        for (uint32 i = 0; i < it.len; i++, pos++) {
            uint8 c = it.bytes[i];
            if (c == ':' && pos == 0)
                continue;
            if (c >= 'A' && c <= 'Z') {
                ok = false;
                break;
            }
            if (!_httpIsTokenChar(c)) {
                ok = false;
                break;
            }
        }
        striNext(&it);
    }

    // A bare ":" names nothing.
    return ok && !(len == 1 && strGetChar(name, 0) == ':');
}

// True for a field value HTTP/3 permits (RFC 9110 5.5, as RFC 9114 4.1.2 narrows it): no NUL, CR
// or LF anywhere, and no leading or trailing whitespace. The control characters are what make
// header injection possible in a gateway that re-serializes to HTTP/1.1.
static bool validFieldValue(_In_opt_ strref value)
{
    uint32 len = strLen(value);
    if (len == 0)
        return true;

    uint8 first = strGetChar(value, 0);
    uint8 last  = strGetChar(value, len - 1);
    if (_httpIsOWS(first) || _httpIsOWS(last))
        return false;

    striter it;
    striBorrow(&it, value);

    bool ok = true;
    while (it.len > 0 && ok) {
        for (uint32 i = 0; i < it.len; i++) {
            uint8 c = it.bytes[i];
            if (c == 0 || c == '\r' || c == '\n') {
                ok = false;
                break;
            }
        }
        striNext(&it);
    }
    return ok;
}

_Use_decl_annotations_
bool _qpackEncode(strhandle out, const HttpHeaders* h)
{
    int32 n = httpHeadersCount(h);

    // An upper bound on the encoded size. Every field costs its name and its value plus the two
    // length prefixes, and a prefixed integer never needs more than ten bytes for a length that
    // fits in a size_t. The two-byte section prefix is on top.
    size_t bound  = 2;
    size_t widest = 0;
    for (int32 i = 0; i < n; i++) {
        size_t nl = strLen(h->names.a[i]);
        size_t vl = strLen(h->values.a[i]);
        bound += nl + vl + 22;
        widest = max(widest, max(nl, vl));
    }

    uint8* buf  = xaAlloc(bound);
    uint8* raw  = widest ? xaAlloc(widest) : NULL;
    uint8* huff = widest ? xaAlloc(widest) : NULL;

    H3Wr wr;
    _h3WrInit(&wr, buf, bound);

    // The field section prefix. With no dynamic table there is nothing to wait for and nothing to
    // index against, so Required Insert Count and Delta Base are both zero on every section.
    _h3Wr8(&wr, 0x00);
    _h3Wr8(&wr, 0x00);

    string lname = 0;
    for (int32 i = 0; i < n; i++) {
        // HTTP/3 has no uppercase field names, so the name goes out lowercased whatever the
        // application asked for -- the same rule the HTTP/1.1 writer follows for Content-Length,
        // where cxhttp writes what the message actually is.
        strDup(&lname, h->names.a[i]);
        strLower(&lname);

        strref value = h->values.a[i];
        int32 idx    = _qpackStaticFind(lname, value);
        if (idx >= 0) {
            wrInt(&wr, (uint64)idx, 6, 0xc0);
            continue;
        }

        // The name is emitted before the value, so it uses the shared scratch buffer first.
        idx = _qpackStaticFindName(lname);
        if (idx >= 0) {
            wrInt(&wr, (uint64)idx, 4, 0x50);
        } else {
            uint32 nlen = strLen(lname);
            if (nlen > 0)
                strCopyRaw(lname, 0, raw, nlen);
            wrStr(&wr, raw, nlen, 3, 0x20, huff);
        }

        uint32 vlen = strLen(value);
        if (vlen > 0)
            strCopyRaw(value, 0, raw, vlen);
        wrStr(&wr, raw, vlen, 7, 0x00, huff);
    }
    strDestroy(&lname);

    bool ok = !wr.bad && _httpAppendBytes(out, buf, _h3WrLen(&wr));

    xaFree(huff);
    xaFree(raw);
    xaFree(buf);
    return ok;
}

_Use_decl_annotations_
uint64 _qpackDecode(HttpHeaders* out, const uint8* data, size_t len, uint32 maxCount,
                    uint64 maxSize)
{
    httpHeadersInit(out);

    H3Rd rd;
    _h3RdInit(&rd, data, len);

    // The field section prefix. Required Insert Count is encoded against the dynamic table's
    // capacity, which is zero here, so anything but a literal zero means the peer indexed a table
    // it was told it could not use.
    uint8 flags = 0;
    uint64 ric  = rdInt(&rd, 8, &flags);
    if (rd.bad || ric != 0)
        return H3ERR_QPACK_DECOMPRESSION_FAILED;

    // Delta Base is read for its sign bit alone: a set bit says Base is Required Insert Count
    // minus Delta Base minus one, which with a Required Insert Count of zero is negative, and no
    // encoding can mean that. The magnitude has nothing to index against either way.
    rdInt(&rd, 7, &flags);
    if (rd.bad || (flags & 0x80))
        return H3ERR_QPACK_DECOMPRESSION_FAILED;

    uint64 size   = 0;
    uint32 count  = 0;
    uint64 err    = 0;
    string name   = 0;
    string value  = 0;

    while (_h3RdLeft(&rd) > 0 && err == 0) {
        uint8 b = *rd.p;

        if (b & 0x80) {
            // Indexed Field Line. The T bit picks the table, and only the static one exists.
            uint64 idx = rdInt(&rd, 6, &flags);
            if (rd.bad || !(flags & 0x40)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
            strref sn, sv;
            if (idx > INT32_MAX || !_qpackStaticGet(&sn, &sv, (int32)idx)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
            strDup(&name, sn);
            strDup(&value, sv);
        } else if ((b & 0xc0) == 0x40) {
            // Literal Field Line with Name Reference, again static-only.
            uint64 idx = rdInt(&rd, 4, &flags);
            if (rd.bad || !(flags & 0x10)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
            strref sn, sv;
            if (idx > INT32_MAX || !_qpackStaticGet(&sn, &sv, (int32)idx)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
            strDup(&name, sn);
            if (!rdStr(&rd, &value, 7, &flags, maxSize ? (size_t)maxSize : 0)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
        } else if ((b & 0xe0) == 0x20) {
            // Two literals.
            if (!rdStr(&rd, &name, 3, &flags, maxSize ? (size_t)maxSize : 0) ||
                !rdStr(&rd, &value, 7, &flags, maxSize ? (size_t)maxSize : 0)) {
                err = H3ERR_QPACK_DECOMPRESSION_FAILED;
                break;
            }
        } else {
            // Everything left -- 0001xxxx and 0000Nxxx -- is a post-base reference, which is a
            // dynamic table reference by another name.
            err = H3ERR_QPACK_DECOMPRESSION_FAILED;
            break;
        }

        if (!validFieldName(name) || !validFieldValue(value)) {
            err = H3ERR_MESSAGE_ERROR;
            break;
        }

        count++;
        size += (uint64)strLen(name) + strLen(value) + 32;
        if ((maxCount > 0 && count > maxCount) || (maxSize > 0 && size > maxSize)) {
            err = H3ERR_MESSAGE_ERROR;
            break;
        }

        if (!httpHeadersAdd(out, name, value)) {
            err = H3ERR_INTERNAL_ERROR;
            break;
        }
    }

    strDestroy(&name);
    strDestroy(&value);
    return err;
}

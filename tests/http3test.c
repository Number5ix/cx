// cxhttp's HTTP/3 layer: the wire codec, QPACK, the session machinery, and both connection
// classes.
//
// The codec tests come first and stay away from sockets entirely, for the reason
// design/quic.md gives for the QUIC codec: RFC 7541 and RFC 9204 publish encoded bytes, so a
// field section that comes out matching them has the static indices, the section prefix and the
// Huffman codes all right at once -- which no amount of cx-against-cx round-tripping would show.

#include <cxhttp.h>
#include <cxhttp/http3_private.h>
#include <cxhttp/http3srvconn.h>

#include <cx/net.h>
#include <cx/serialize.h>
#include <cx/string.h>
#include <cx/thread.h>
#include <cx/time/time.h>
#include "tlstestcert.h"

#define TEST_FILE  http3test
#define TEST_FUNCS http3test_funcs
#include "common.h"

// bool CHECK_BYTES(const char *what, const uint8 *got, size_t gotlen, const uint8 *want, size_t wantlen);
//
// Compares a computed byte string against a vector, logging the first differing offset and both
// values there. Assigns 1 to `ret` and jumps to `out` on a mismatch. `what` is any C string, so a
// table-driven caller can name the case it is on.
#define CHECK_BYTES(what, got, gotlen, want, wantlen)                                        \
    do {                                                                                     \
        if ((size_t)(gotlen) != (size_t)(wantlen)) {                                         \
            TEST_FAILV(ret, 1, _SL("${string}: length ${int}, expected ${int}"),             \
                       stvar(strref, (strref)(what)), stvar(int64, (int64)(gotlen)),                \
                       stvar(int64, (int64)(wantlen)));                                      \
            goto out;                                                                        \
        }                                                                                    \
        for (size_t _i = 0; _i < (size_t)(wantlen); _i++) {                                  \
            if ((got)[_i] != (want)[_i]) {                                                   \
                TEST_FAILV(ret, 1, _SL("${string}: byte ${int} is ${int}, expected ${int}"), \
                           stvar(strref, (strref)(what)), stvar(int64, (int64)_i),                  \
                           stvar(int32, (int32)(got)[_i]), stvar(int32, (int32)(want)[_i])); \
                goto out;                                                                    \
            }                                                                                \
        }                                                                                    \
    } while (0)

// bool CHECK_U64(const char *what, uint64 got, uint64 want);
#define CHECK_U64(what, got, want)                                          \
    do {                                                                    \
        if ((uint64)(got) != (uint64)(want)) {                              \
            TEST_FAILV(ret, 1, _SL("${string} is ${int}, expected ${int}"), \
                       stvar(strref, (strref)(what)), stvar(int64, (int64)(got)),  \
                       stvar(int64, (int64)(want)));                        \
            goto out;                                                       \
        }                                                                   \
    } while (0)

// bool CHECK_TRUE(const char *what, bool cond);
#define CHECK_TRUE(what, cond)                                                          \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            TEST_FAILV(ret, 1, _SL("check failed: ${string}"), stvar(strref, (strref)(what))); \
            goto out;                                                                   \
        }                                                                               \
    } while (0)

// bool CHECK_STR(const char *what, strref got, strref want);
#define CHECK_STR(what, got, want)                                                     \
    do {                                                                               \
        if (!strEq((got), (want))) {                                                   \
            TEST_FAILV(ret, 1, _SL("${string} is '${string}', expected '${string}'"),  \
                       stvar(strref, (strref)(what)), stvar(strref, (got)),                   \
                       stvar(strref, (want)));                                         \
            goto out;                                                                  \
        }                                                                              \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Variable-length integers and frame headers
// ---------------------------------------------------------------------------------------------

int test_http3test_wire(void)
{
    int ret = 0;

    // RFC 9000 Appendix A.1's sample encodings, which HTTP/3 inherits along with the codec.
    static const struct {
        uint64 val;
        uint8 len;
        uint8 bytes[8];
    } tv[] = {
        { 0, 1, { 0x00 } },
        { 37, 1, { 0x25 } },
        { 63, 1, { 0x3f } },
        { 64, 2, { 0x40, 0x40 } },
        { 15293, 2, { 0x7b, 0xbd } },
        { 16383, 2, { 0x7f, 0xff } },
        { 16384, 4, { 0x80, 0x00, 0x40, 0x00 } },
        { 494878333, 4, { 0x9d, 0x7f, 0x3e, 0x7d } },
        { 1073741823, 4, { 0xbf, 0xff, 0xff, 0xff } },
        { 1073741824, 8, { 0xc0, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 } },
        { 151288809941952652ull, 8, { 0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c } },
        { H3_VARINT_MAX, 8, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        uint8 buf[8];
        H3Wr wr;
        _h3WrInit(&wr, buf, sizeof(buf));
        _h3WrVarint(&wr, tv[i].val);
        CHECK_TRUE("varint encode", !wr.bad);
        CHECK_U64("varint size", _h3VarintSize(tv[i].val), tv[i].len);
        CHECK_BYTES("varint", buf, _h3WrLen(&wr), tv[i].bytes, tv[i].len);

        H3Rd rd;
        _h3RdInit(&rd, tv[i].bytes, tv[i].len);
        CHECK_U64("varint decode", _h3RdVarint(&rd), tv[i].val);
        CHECK_TRUE("varint fully consumed", !rd.bad && _h3RdLeft(&rd) == 0);
    }

    // A value past what the encoding can carry has no size and is refused rather than truncated.
    CHECK_U64("oversized varint size", _h3VarintSize(H3_VARINT_MAX + 1), 0);
    {
        uint8 buf[8];
        H3Wr wr;
        _h3WrInit(&wr, buf, sizeof(buf));
        _h3WrVarint(&wr, H3_VARINT_MAX + 1);
        CHECK_TRUE("oversized varint refused", wr.bad);
    }

    // A varint whose declared width runs past the end of the buffer.
    {
        static const uint8 trunc[] = { 0xc0, 0x00, 0x00 };
        H3Rd rd;
        _h3RdInit(&rd, trunc, sizeof(trunc));
        _h3RdVarint(&rd);
        CHECK_TRUE("truncated varint refused", rd.bad);
    }

    // Frame headers: a type and a length, each in the shortest encoding that fits.
    {
        uint8 buf[H3_FRAME_HDR_MAX];
        static const uint8 want[] = { 0x01, 0x40, 0xc8 };   // HEADERS, 200 bytes

        size_t n = _h3WrFrameHdr(buf, sizeof(buf), H3_FRAME_HEADERS, 200);
        CHECK_BYTES("frame header", buf, n, want, sizeof(want));
        CHECK_U64("frame header size", _h3FrameHdrSize(H3_FRAME_HEADERS, 200), sizeof(want));

        H3Rd rd;
        _h3RdInit(&rd, want, sizeof(want));
        CHECK_U64("frame type", _h3RdVarint(&rd), H3_FRAME_HEADERS);
        CHECK_U64("frame length", _h3RdVarint(&rd), 200);
    }

    // A frame header that will not fit is refused whole rather than half-written.
    {
        uint8 buf[2];
        CHECK_U64("short frame header buffer", _h3WrFrameHdr(buf, sizeof(buf), 0x1f, 1000000), 0);
    }

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The incremental frame reader
// ---------------------------------------------------------------------------------------------

// Run the reader over a whole buffer fed `chunk` bytes at a time, collecting the payload of every
// frame and the sequence of frame types. Returns the number of complete frames seen, or -1 if the
// reader reported an error.
static int32 drainFrames(_In_reads_bytes_(len) const uint8* data, size_t len, size_t chunk,
                         _Inout_ strhandle payload, _Inout_ sa_uint64* types)
{
    BufRing ring;
    bufringInit(&ring, 64);

    H3FrameReader fr;
    _h3FrameReaderInit(&fr);

    int32 frames = 0;
    size_t fed   = 0;
    bool done    = false;

    while (!done) {
        if (fed < len) {
            size_t n = min(chunk, len - fed);
            bufringWrite(&ring, data + fed, n);
            fed += n;
        }

        for (;;) {
            H3FrameResult r = _h3FrameReaderStep(&fr, &ring);
            if (r == H3FR_NeedMore) {
                if (fed >= len)
                    done = true;
                break;
            }
            if (r == H3FR_Error) {
                frames = -1;
                done   = true;
                break;
            }
            if (r == H3FR_Header) {
                saPush(types, uint64, fr.type);
            } else if (r == H3FR_Payload) {
                uint8 tmp[256];
                size_t got = 0;
                while (got < fr.avail) {
                    size_t n = bufringRead(&ring, tmp, min(sizeof(tmp), fr.avail - got));
                    _httpAppendBytes(payload, tmp, n);
                    got += n;
                }
            } else if (r == H3FR_Complete) {
                frames++;
            }
        }
    }

    bufringDestroy(&ring);
    return frames;
}

int test_http3test_wirefuzz(void)
{
    int ret = 0;
    string payload = 0;
    sa_uint64 types;
    saInit(&types, uint64, 8);

    // Three frames back to back: a zero-length one, a small one, and one whose length needs a
    // two-byte varint. Fed a byte at a time, then all at once, then in odd-sized runs -- the split
    // must make no difference to what comes out.
    static const uint8 stream[] = {
        0x01, 0x00,                                     // HEADERS, empty
        0x00, 0x05, 'h', 'e', 'l', 'l', 'o',            // DATA "hello"
        0x00, 0x40, 0x41,                               // DATA, 65 bytes in a two-byte varint
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
        'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '!', '?', '.',
    };
    STR_CONST(kWant, "helloabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.");

    static const size_t chunks[] = { 1, 2, 3, 7, 64, sizeof(stream) };
    for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        strClear(&payload);
        saClear(&types);

        int32 frames = drainFrames(stream, sizeof(stream), chunks[c], &payload, &types);
        CHECK_U64("frames", frames, 3);
        CHECK_U64("frame types seen", saSize(types), 3);
        CHECK_U64("first type", types.a[0], H3_FRAME_HEADERS);
        CHECK_U64("second type", types.a[1], H3_FRAME_DATA);
        CHECK_U64("third type", types.a[2], H3_FRAME_DATA);
        CHECK_STR("payload", payload, kWant);
    }

    // A truncated frame -- a header promising more payload than ever arrives -- ends with the
    // reader still waiting rather than reading past the end of what it was given.
    {
        static const uint8 cut[] = { 0x00, 0x10, 'a', 'b', 'c' };
        strClear(&payload);
        saClear(&types);

        int32 frames = drainFrames(cut, sizeof(cut), sizeof(cut), &payload, &types);
        CHECK_U64("truncated frames completed", frames, 0);
        CHECK_U64("truncated header seen", saSize(types), 1);
        CHECK_U64("truncated payload", strLen(payload), 3);
    }

    // A frame header split so that the type arrives and the length does not must not be consumed
    // half way: the reader has to see the same frame once the rest turns up.
    {
        BufRing ring;
        bufringInit(&ring, 64);

        H3FrameReader fr;
        _h3FrameReaderInit(&fr);

        static const uint8 hdr[] = { 0x00, 0x40, 0x05 };
        bufringWrite(&ring, hdr, 2);
        CHECK_U64("split header waits", _h3FrameReaderStep(&fr, &ring), H3FR_NeedMore);
        CHECK_U64("split header not consumed", ring.total, 2);

        bufringWrite(&ring, hdr + 2, 1);
        CHECK_U64("split header completes", _h3FrameReaderStep(&fr, &ring), H3FR_Header);
        CHECK_U64("split header length", fr.len, 5);

        bufringDestroy(&ring);
    }

    // A poisoned reader stays poisoned, whatever else arrives.
    {
        BufRing ring;
        bufringInit(&ring, 64);

        H3FrameReader fr;
        _h3FrameReaderInit(&fr);
        _h3FrameReaderFail(&fr);

        static const uint8 more[] = { 0x00, 0x00 };
        bufringWrite(&ring, more, sizeof(more));
        CHECK_U64("poisoned reader", _h3FrameReaderStep(&fr, &ring), H3FR_Error);

        bufringDestroy(&ring);
    }

out:
    strDestroy(&payload);
    saDestroy(&types);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Huffman coding
// ---------------------------------------------------------------------------------------------

int test_http3test_qpackhuff(void)
{
    int ret = 0;

    // RFC 7541 Appendix C's worked examples. These pin the code table itself: every one of them
    // is wrong if a single entry's length or value is off.
    static const struct {
        const char* text;
        uint8 enc[48];
        size_t enclen;
    } tv[] = {
        { "www.example.com",
          { 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff }, 12 },
        { "no-cache", { 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf }, 6 },
        { "custom-key", { 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9, 0x7d, 0x7f }, 8 },
        { "custom-value", { 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf }, 9 },
        { "302", { 0x64, 0x02 }, 2 },
        { "private", { 0xae, 0xc3, 0x77, 0x1a, 0x4b }, 5 },
        { "gzip", { 0x9b, 0xd9, 0xab }, 3 },
        { "https://www.example.com",
          { 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae, 0x82,
            0xae, 0x43, 0xd3 },
          17 },
        { "Mon, 21 Oct 2013 20:13:21 GMT",
          { 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b,
            0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff },
          22 },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        size_t srclen = strlen(tv[i].text);
        const uint8* src = (const uint8*)tv[i].text;

        CHECK_U64("huffman length", _qpackHuffLen(src, srclen), tv[i].enclen);

        uint8 enc[64];
        size_t n = _qpackHuffEncode(enc, sizeof(enc), src, srclen);
        CHECK_BYTES("huffman encode", enc, n, tv[i].enc, tv[i].enclen);

        uint8 dec[64];
        size_t dlen = 0;
        CHECK_TRUE("huffman decode", _qpackHuffDecode(dec, sizeof(dec), &dlen, tv[i].enc,
                                                      tv[i].enclen));
        CHECK_BYTES("huffman decode", dec, dlen, src, srclen);
    }

    // Every byte value round-trips, including the ones with 28- and 30-bit codes that a
    // header field would never carry but a decoder must still handle.
    {
        uint8 all[256];
        for (int i = 0; i < 256; i++)
            all[i] = (uint8)i;

        size_t need = _qpackHuffLen(all, sizeof(all));
        uint8* enc  = xaAlloc(need);
        CHECK_U64("all-bytes encode", _qpackHuffEncode(enc, need, all, sizeof(all)), need);

        size_t bufsz = _qpackHuffMaxDecoded(need);
        uint8* dec   = xaAlloc(bufsz);
        size_t dlen  = 0;
        bool ok      = _qpackHuffDecode(dec, bufsz, &dlen, enc, need);
        if (ok)
            CHECK_BYTES("all-bytes decode", dec, dlen, all, sizeof(all));

        xaFree(dec);
        xaFree(enc);
        CHECK_TRUE("all-bytes decode", ok);
    }

    // The three ways a Huffman string can be malformed, each of which is a second encoding of
    // some string and so has to be refused rather than accepted.
    {
        uint8 dec[64];
        size_t dlen = 0;

        // Padding long enough to have held a symbol.
        static const uint8 longpad[] = { 0xff, 0xff, 0xff, 0xff };
        CHECK_TRUE("long padding refused",
                   !_qpackHuffDecode(dec, sizeof(dec), &dlen, longpad, sizeof(longpad)));

        // Padding with a zero bit in it: 0x00 decodes '0' and leaves three zero bits.
        static const uint8 zeropad[] = { 0x00 };
        CHECK_TRUE("zero padding refused",
                   !_qpackHuffDecode(dec, sizeof(dec), &dlen, zeropad, sizeof(zeropad)));

        // A symbol left incomplete at the end of the input.
        static const uint8 cut[] = { 0xfe };
        CHECK_TRUE("incomplete code refused",
                   !_qpackHuffDecode(dec, sizeof(dec), &dlen, cut, sizeof(cut)));
    }

    // An empty string encodes to nothing and decodes back to nothing.
    {
        uint8 dec[4];
        size_t dlen = 1;
        CHECK_U64("empty huffman length", _qpackHuffLen((const uint8*)"", 0), 0);
        CHECK_TRUE("empty huffman decode", _qpackHuffDecode(dec, sizeof(dec), &dlen,
                                                            (const uint8*)"", 0));
        CHECK_U64("empty huffman decoded length", dlen, 0);
    }

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// QPACK field sections
// ---------------------------------------------------------------------------------------------

// Build a header list from a NULL-terminated array of alternating names and values.
static void buildHeaders(_Out_ HttpHeaders* h, _In_ const char* const* nv)
{
    httpHeadersInit(h);
    for (size_t i = 0; nv[i]; i += 2)
        httpHeadersAdd(h, (strref)nv[i], (strref)nv[i + 1]);
}

// True if a decoded header list holds exactly the fields in the same NULL-terminated array, in
// the same order.
static bool headersMatch(_In_ const HttpHeaders* h, _In_ const char* const* nv)
{
    size_t n = 0;
    while (nv[n])
        n += 2;

    if ((size_t)httpHeadersCount(h) != n / 2)
        return false;

    for (size_t i = 0; i < n; i += 2) {
        if (!strEq(h->names.a[i / 2], (strref)nv[i]) ||
            !strEq(h->values.a[i / 2], (strref)nv[i + 1]))
            return false;
    }
    return true;
}

int test_http3test_qpackvec(void)
{
    int ret = 0;
    HttpHeaders h;
    HttpHeaders dec;
    string enc = 0;
    httpHeadersInit(&h);
    httpHeadersInit(&dec);

    // RFC 9204 Appendix B.1, the one published example that uses the static table alone. cxhttp
    // would Huffman-code the value rather than send it literally, so this is a decoder vector:
    // what matters is that the section prefix, the representation and the static index are all
    // read the way the RFC wrote them.
    {
        static const uint8 b1[] = { 0x00, 0x00, 0x51, 0x0b, '/',  'i', 'n', 'd',
                                    'e',  'x',  '.',  'h',  't',  'm', 'l' };
        static const char* const want[] = { ":path", "/index.html", NULL };

        httpHeadersDestroy(&dec);
        CHECK_U64("B.1 decode", _qpackDecode(&dec, b1, sizeof(b1), 0, 0), 0);
        CHECK_TRUE("B.1 fields", headersMatch(&dec, want));
    }

    // Encoded field sections cxhttp itself produces. The bytes were computed from RFC 7541 and
    // RFC 9204 independently of this code, so they pin the representation choice -- indexed
    // versus name-referenced versus two literals -- as well as the Huffman coding.
    static const struct {
        const char* what;
        const char* const nv[12];
        uint8 enc[48];
        size_t enclen;
    } tv[] = {
        // A name in the static table with a value that is not: literal with name reference,
        // Huffman-coded because "/index.html" is shorter that way.
        { ":path literal",
          { ":path", "/index.html", NULL },
          { 0x00, 0x00, 0x51, 0x88, 0x60, 0xd5, 0x48, 0x5f, 0x2b, 0xce, 0x9a, 0x68 },
          12 },

        // Name and value both in the table: a single indexed byte.
        { ":method GET", { ":method", "GET", NULL }, { 0x00, 0x00, 0xd1 }, 3 },
        { ":status 200", { ":status", "200", NULL }, { 0x00, 0x00, 0xd9 }, 3 },

        // An uppercase name from the application goes out lowercased, which here makes it an
        // exact static-table hit.
        { "case folded", { "Content-Type", "application/json", NULL }, { 0x00, 0x00, 0xee }, 3 },

        // A whole request head.
        { "request head",
          { ":method", "GET", ":scheme", "https", ":authority", "www.example.com", ":path", "/",
            "user-agent", "cx/1.0", NULL },
          { 0x00, 0x00, 0xd1, 0xd7, 0x50, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0,
            0xab, 0x90, 0xf4, 0xff, 0xc1, 0x5f, 0x50, 0x85, 0x27, 0x96, 0x02, 0xb8, 0x3f },
          27 },

        // A whole response head, including a field whose name is in no table at all.
        { "response head",
          { ":status", "200", "content-type", "text/plain", "content-length", "42", "server", "cx",
            "x-custom-header", "custom-value", NULL },
          { 0x00, 0x00, 0xd9, 0xf5, 0x54, 0x02, 0x34, 0x32, 0x5f, 0x4d, 0x02, 0x63, 0x78, 0x2f,
            0x04, 0xf2, 0xb1, 0x2d, 0x42, 0x4f, 0x4a, 0xd3, 0x94, 0x72, 0x16, 0xcf, 0x89, 0x25,
            0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf },
          36 },

        // An empty value, which has no Huffman form to be shorter than.
        { "empty value",
          { "x-empty", "", NULL },
          { 0x00, 0x00, 0x2e, 0xf2, 0xb1, 0x69, 0xad, 0x3e, 0xbf, 0x00 },
          10 },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        httpHeadersDestroy(&h);
        buildHeaders(&h, tv[i].nv);

        strClear(&enc);
        CHECK_TRUE("encode", _qpackEncode(&enc, &h));

        uint8 got[64];
        uint32 gotlen = strCopyRaw(enc, 0, got, sizeof(got));
        CHECK_BYTES(tv[i].what, got, gotlen, tv[i].enc, tv[i].enclen);

        // And back the other way. A decode of what we encoded has to produce what went in, with
        // the one deliberate difference that the name is now lowercase.
        httpHeadersDestroy(&dec);
        CHECK_U64("decode", _qpackDecode(&dec, tv[i].enc, tv[i].enclen, 0, 0), 0);

        CHECK_U64("field count", httpHeadersCount(&dec), httpHeadersCount(&h));
        for (int32 f = 0; f < httpHeadersCount(&h); f++) {
            string lower = 0;
            strDup(&lower, h.names.a[f]);
            strLower(&lower);
            bool same = strEq(dec.names.a[f], lower) && strEq(dec.values.a[f], h.values.a[f]);
            strDestroy(&lower);
            CHECK_TRUE("round trip field", same);
        }
    }

    // A name and a value long enough that their lengths need the continuation encoding a prefixed
    // integer falls back to past 2^N - 1.
    {
        string longname = 0;
        string longval  = 0;
        for (int i = 0; i < 300; i++) {
            strAppend(&longname, _SL("x"));
            strAppend(&longval, _SL("\x01"));   // no Huffman gain, so it goes out literally
        }

        httpHeadersDestroy(&h);
        httpHeadersInit(&h);
        httpHeadersAdd(&h, longname, longval);

        strClear(&enc);
        bool ok = _qpackEncode(&enc, &h);

        uint8* raw   = xaAlloc(strLen(enc));
        uint32 rawlen = strCopyRaw(enc, 0, raw, strLen(enc));
        httpHeadersDestroy(&dec);
        uint64 err = ok ? _qpackDecode(&dec, raw, rawlen, 0, 0) : 1;
        bool same  = err == 0 && httpHeadersCount(&dec) == 1 &&
                    strEq(dec.names.a[0], longname) && strEq(dec.values.a[0], longval);
        xaFree(raw);
        strDestroy(&longname);
        strDestroy(&longval);

        CHECK_TRUE("long field round trip", same);
    }

out:
    httpHeadersDestroy(&h);
    httpHeadersDestroy(&dec);
    strDestroy(&enc);
    return ret;
}

int test_http3test_qpackdynamic(void)
{
    int ret = 0;
    HttpHeaders dec;
    httpHeadersInit(&dec);

    // Everything a peer could send that reaches for the dynamic table after being told the
    // capacity is zero. Each is a connection error, not a field that decodes to something else:
    // a decoder that guessed here would agree with its peer about nothing afterwards.
    static const struct {
        const char* what;
        uint8 enc[8];
        size_t len;
        uint64 err;
    } tv[] = {
        // A nonzero Required Insert Count -- the peer inserted into a table it has none of.
        { "insert count", { 0x01, 0x00, 0xd1 }, 3, H3ERR_QPACK_DECOMPRESSION_FAILED },

        // A Delta Base with the sign bit set, which puts Base below zero.
        { "negative base", { 0x00, 0x80, 0xd1 }, 3, H3ERR_QPACK_DECOMPRESSION_FAILED },

        // Indexed Field Line with the T bit clear: a dynamic table index.
        { "dynamic indexed", { 0x00, 0x00, 0x91 }, 3, H3ERR_QPACK_DECOMPRESSION_FAILED },

        // Literal Field Line with Name Reference, again against the dynamic table.
        { "dynamic name ref", { 0x00, 0x00, 0x41, 0x00 }, 4,
          H3ERR_QPACK_DECOMPRESSION_FAILED },

        // Indexed Field Line with Post-Base Index.
        { "post-base indexed", { 0x00, 0x00, 0x10 }, 3, H3ERR_QPACK_DECOMPRESSION_FAILED },

        // Literal Field Line with Post-Base Name Reference.
        { "post-base name ref", { 0x00, 0x00, 0x00, 0x00 }, 4,
          H3ERR_QPACK_DECOMPRESSION_FAILED },

        // A static index past the end of the table.
        { "static overrun", { 0x00, 0x00, 0xff, 0x24 }, 4, H3ERR_QPACK_DECOMPRESSION_FAILED },

        // A truncated field section: a length that runs past the end of the section.
        { "truncated literal", { 0x00, 0x00, 0x2f, 0x02, 0x40, 'a' }, 6,
          H3ERR_QPACK_DECOMPRESSION_FAILED },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        httpHeadersDestroy(&dec);
        uint64 err = _qpackDecode(&dec, tv[i].enc, tv[i].len, 0, 0);
        if (err != tv[i].err) {
            TEST_FAILV(ret, 1, _SL("${string}: error ${int}, expected ${int}"),
                       stvar(strref, (strref)tv[i].what), stvar(int64, (int64)err),
                       stvar(int64, (int64)tv[i].err));
            goto out;
        }
    }

    // An uppercase field name is not a decoding failure -- it decodes fine -- but HTTP/3 has no
    // uppercase field names, so the message is refused. That distinction matters: it costs one
    // stream rather than the connection.
    {
        // Literal with literal name "Host", value "x".
        static const uint8 upper[] = { 0x00, 0x00, 0x24, 'H', 'o', 's', 't', 0x01, 'x' };
        httpHeadersDestroy(&dec);
        CHECK_U64("uppercase name", _qpackDecode(&dec, upper, sizeof(upper), 0, 0),
                  H3ERR_MESSAGE_ERROR);
    }

    // A field value with a newline in it, which is how a gateway re-serializing to HTTP/1.1 is
    // made to emit a header the sender never wrote.
    {
        static const uint8 inject[] = { 0x00, 0x00, 0x23, 'a', 'b', 'c', 0x03, 'x', '\n', 'y' };
        httpHeadersDestroy(&dec);
        CHECK_U64("injected newline", _qpackDecode(&dec, inject, sizeof(inject), 0, 0),
                  H3ERR_MESSAGE_ERROR);
    }

    // The two limits a receiver sets on a field section, both of which are the peer's fault but
    // only cost the one message.
    {
        static const uint8 three[] = { 0x00, 0x00, 0xd1, 0xd7, 0xd9 };

        httpHeadersDestroy(&dec);
        CHECK_U64("count limit", _qpackDecode(&dec, three, sizeof(three), 2, 0),
                  H3ERR_MESSAGE_ERROR);

        httpHeadersDestroy(&dec);
        CHECK_U64("count limit not tripped", _qpackDecode(&dec, three, sizeof(three), 3, 0), 0);

        httpHeadersDestroy(&dec);
        CHECK_U64("size limit", _qpackDecode(&dec, three, sizeof(three), 0, 64),
                  H3ERR_MESSAGE_ERROR);
    }

out:
    httpHeadersDestroy(&dec);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The connection session
//
// Driven by bytes rather than by a socket, which is the point: a peer that opens two control
// streams, or sends SETTINGS twice, or names a GOAWAY stream that moves the wrong way, is three
// lines to construct here and would be most of a test harness to provoke over a real connection.
// ---------------------------------------------------------------------------------------------

// Feed a unidirectional stream the peer opened, `chunk` bytes at a time. Returns the first error
// the session reported, or 0 if it took the lot.
static uint64 feedUni(_Inout_ Http3Session* s, _Inout_ H3UniStream* u,
                      _In_reads_bytes_(len) const uint8* data, size_t len, size_t chunk)
{
    for (size_t off = 0; off < len; off += chunk) {
        uint64 err = _h3SessionUniRecv(s, u, data + off, min(chunk, len - off));
        if (err != 0)
            return err;
    }
    return 0;
}

// The peer's control stream up to and including a SETTINGS frame carrying nothing, which is the
// least a conforming peer can open with.
static const uint8 kBareControl[] = { 0x00, 0x04, 0x00 };

int test_http3test_settings(void)
{
    int ret = 0;
    string out = 0;

    // This endpoint's own control stream preamble: the stream type, then SETTINGS, which RFC 9114
    // requires to be the first frame on it.
    {
        HttpLimits lim;
        httpLimitsDefault(&lim);
        lim.maxHeadBytes = 16384;

        Http3Session s;
        _h3SessionInit(&s, false, &lim);

        CHECK_TRUE("preamble", _h3SessionControlPreamble(&s, &out));

        static const uint8 want[] = {
            0x00,                     // unidirectional stream type: control
            0x04, 0x09,               // SETTINGS, nine bytes
            0x01, 0x00,               // QPACK_MAX_TABLE_CAPACITY = 0
            0x07, 0x00,               // QPACK_BLOCKED_STREAMS = 0
            0x06, 0x80, 0x00, 0x40, 0x00,   // MAX_FIELD_SECTION_SIZE = 16384
        };
        uint8 got[32];
        uint32 gotlen = strCopyRaw(out, 0, got, sizeof(got));
        CHECK_BYTES("control preamble", got, gotlen, want, sizeof(want));

        _h3SessionDestroy(&s);
    }

    // The peer's settings, read whole and then read a byte at a time. Splitting a frame across
    // reads is the normal case on a real connection, not the exotic one.
    static const uint8 peer[] = {
        0x00,                                 // control stream
        0x04, 0x0b,                           // SETTINGS, eleven bytes
        0x01, 0x40, 0x64,                     // QPACK_MAX_TABLE_CAPACITY = 100
        0x06, 0x80, 0x00, 0x20, 0x00,         // MAX_FIELD_SECTION_SIZE = 8192
        0x4a, 0x0b, 0x01,                     // an unknown setting, which must be ignored
    };

    static const size_t chunks[] = { 1, 3, sizeof(peer) };
    for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, true, NULL);
        _h3UniInit(&u, 3);

        uint64 err = feedUni(&s, &u, peer, sizeof(peer), chunks[c]);
        bool ok    = err == 0 && s.peerSettings && s.peerControl && s.peerControlId == 3 &&
                  s.peer.qpackMaxTableCapacity == 100 && s.peer.maxFieldSectionSize == 8192;

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);
        CHECK_TRUE("peer settings", ok);
    }

    // Every way a SETTINGS frame can be wrong.
    static const struct {
        const char* what;
        uint8 bytes[16];
        size_t len;
        uint64 err;
    } bad[] = {
        // The same identifier twice.
        { "duplicate setting", { 0x00, 0x04, 0x05, 0x06, 0x40, 0x40, 0x06, 0x10 }, 8,
          H3ERR_SETTINGS_ERROR },

        // An identifier HTTP/2 used and HTTP/3 reserved, which says the peer has the wrong
        // protocol rather than a newer one.
        { "reserved setting", { 0x00, 0x04, 0x02, 0x03, 0x0a }, 5, H3ERR_SETTINGS_ERROR },

        // An identifier with no value after it.
        { "truncated setting", { 0x00, 0x04, 0x01, 0x06 }, 4, H3ERR_FRAME_ERROR },

        // ENABLE_CONNECT_PROTOCOL is a flag, so anything but 0 or 1 is meaningless.
        { "bad flag value", { 0x00, 0x04, 0x02, 0x08, 0x07 }, 5, H3ERR_SETTINGS_ERROR },

        // A frame before SETTINGS: here a GOAWAY, which is legal later and never first.
        { "settings not first", { 0x00, 0x07, 0x01, 0x00 }, 4, H3ERR_MISSING_SETTINGS },

        // SETTINGS twice.
        { "settings twice", { 0x00, 0x04, 0x00, 0x04, 0x00 }, 5, H3ERR_FRAME_UNEXPECTED },

        // A SETTINGS frame larger than this endpoint will hold, refused from its header alone
        // rather than after the payload has been read.
        { "oversized settings", { 0x00, 0x04, 0x80, 0x01, 0x00, 0x00 }, 6, H3ERR_EXCESSIVE_LOAD },
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, true, NULL);
        _h3UniInit(&u, 3);

        uint64 err = feedUni(&s, &u, bad[i].bytes, bad[i].len, bad[i].len);

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);

        if (err != bad[i].err) {
            TEST_FAILV(ret, 1, _SL("${string}: error ${int}, expected ${int}"),
                       stvar(strref, (strref)bad[i].what), stvar(int64, (int64)err),
                       stvar(int64, (int64)bad[i].err));
            goto out;
        }
    }

out:
    strDestroy(&out);
    return ret;
}

int test_http3test_goaway(void)
{
    int ret = 0;
    string out = 0;

    // A client reading its server's GOAWAY. Stream 8 is named, so 8 and everything above it was
    // never processed and may be retried on a fresh connection whatever the method was.
    {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, false, NULL);
        _h3UniInit(&u, 3);

        static const uint8 bytes[] = { 0x00, 0x04, 0x00, 0x07, 0x01, 0x08 };
        uint64 err = feedUni(&s, &u, bytes, sizeof(bytes), 1);
        bool ok    = err == 0 && s.goawayRecvd && s.goawayId == 8 &&
                  !_h3SessionGoneAway(&s, 4) && _h3SessionGoneAway(&s, 8) &&
                  _h3SessionGoneAway(&s, 12);

        // A second GOAWAY may narrow what the server will still serve, and may not widen it.
        static const uint8 lower[] = { 0x07, 0x01, 0x04 };
        if (ok)
            ok = _h3SessionUniRecv(&s, &u, lower, sizeof(lower)) == 0 && s.goawayId == 4;

        static const uint8 higher[] = { 0x07, 0x01, 0x0c };
        if (ok)
            ok = _h3SessionUniRecv(&s, &u, higher, sizeof(higher)) == H3ERR_ID_ERROR;

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);
        CHECK_TRUE("goaway", ok);
    }

    // A server's GOAWAY names a request stream, which is client-initiated and bidirectional, so
    // its low two bits are zero. Anything else is the server naming a stream it cannot mean.
    {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, false, NULL);
        _h3UniInit(&u, 3);

        static const uint8 bytes[] = { 0x00, 0x04, 0x00, 0x07, 0x01, 0x09 };
        uint64 err = feedUni(&s, &u, bytes, sizeof(bytes), sizeof(bytes));

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);
        CHECK_U64("goaway wrong stream kind", err, H3ERR_ID_ERROR);
    }

    // Writing one. The same rule applies in this direction: an endpoint may narrow what it will
    // still serve and may never widen it, because the peer has already retried everything above
    // the first identifier it was told.
    {
        Http3Session s;
        _h3SessionInit(&s, true, NULL);

        CHECK_TRUE("goaway frame", _h3SessionGoawayFrame(&s, &out, 8));
        static const uint8 want[] = { 0x07, 0x01, 0x08 };
        uint8 got[8];
        uint32 gotlen = strCopyRaw(out, 0, got, sizeof(got));
        CHECK_BYTES("goaway frame", got, gotlen, want, sizeof(want));

        strClear(&out);
        CHECK_TRUE("goaway narrowing", _h3SessionGoawayFrame(&s, &out, 4));
        CHECK_TRUE("goaway widening refused", !_h3SessionGoawayFrame(&s, &out, 12));

        _h3SessionDestroy(&s);
    }

out:
    strDestroy(&out);
    return ret;
}

int test_http3test_badstream(void)
{
    int ret = 0;

    // Streams and frames a peer may not send, and what each costs. Every one of these is a
    // connection error: the session cannot go on having misunderstood any of them.
    static const struct {
        const char* what;
        bool server;         // which role this endpoint is playing
        uint8 first[8];      // opened on one stream
        size_t firstLen;
        uint8 second[8];     // then on another
        size_t secondLen;
        uint64 err;
    } tv[] = {
        // Two control streams. Only one per peer exists, and a second is not a duplicate to be
        // ignored but a peer whose framing this endpoint cannot follow.
        { "second control stream", true, { 0x00, 0x04, 0x00 }, 3, { 0x00, 0x04, 0x00 }, 3,
          H3ERR_STREAM_CREATION_ERROR },

        // Two QPACK encoder streams, for the same reason.
        { "second encoder stream", true, { 0x02 }, 1, { 0x02 }, 1,
          H3ERR_STREAM_CREATION_ERROR },
        { "second decoder stream", true, { 0x03 }, 1, { 0x03 }, 1,
          H3ERR_STREAM_CREATION_ERROR },

        // A push stream. A server has no business receiving one at all; a client may only receive
        // one after saying how many pushes it will take, which cxhttp never does.
        { "push stream at a server", true, { 0x01 }, 1, { 0 }, 0, H3ERR_STREAM_CREATION_ERROR },
        { "push stream at a client", false, { 0x01 }, 1, { 0 }, 0, H3ERR_ID_ERROR },

        // Request-stream frames on the control stream.
        { "DATA on control", true, { 0x00, 0x04, 0x00, 0x00, 0x00 }, 5, { 0 }, 0,
          H3ERR_FRAME_UNEXPECTED },
        { "HEADERS on control", true, { 0x00, 0x04, 0x00, 0x01, 0x00 }, 5, { 0 }, 0,
          H3ERR_FRAME_UNEXPECTED },

        // A frame type HTTP/2 used and HTTP/3 reserved -- 0x06 was PING.
        { "reserved frame type", true, { 0x00, 0x04, 0x00, 0x06, 0x00 }, 5, { 0 }, 0,
          H3ERR_FRAME_UNEXPECTED },

        // A cancel for a push that was never promised, which is every push, because cxhttp never
        // sends PUSH_PROMISE.
        { "cancel push", true, { 0x00, 0x04, 0x00, 0x03, 0x01, 0x00 }, 6, { 0 }, 0,
          H3ERR_ID_ERROR },

        // Only a client sends MAX_PUSH_ID, so a client receiving one has a confused server.
        { "max push id at a client", false, { 0x00, 0x04, 0x00, 0x0d, 0x01, 0x08 }, 6, { 0 }, 0,
          H3ERR_FRAME_UNEXPECTED },

        // The peer building dynamic table state after being told the capacity is zero. Anything
        // but a capacity-zero instruction would leave the two ends decoding differently.
        { "encoder instruction", true, { 0x02, 0x80 }, 2, { 0 }, 0,
          H3ERR_QPACK_ENCODER_STREAM_ERROR },
        { "encoder sets capacity", true, { 0x02, 0x25 }, 2, { 0 }, 0,
          H3ERR_QPACK_ENCODER_STREAM_ERROR },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        Http3Session s;
        H3UniStream a, b;
        _h3SessionInit(&s, tv[i].server, NULL);
        _h3UniInit(&a, 3);
        _h3UniInit(&b, 7);

        uint64 err = feedUni(&s, &a, tv[i].first, tv[i].firstLen, tv[i].firstLen);
        if (err == 0 && tv[i].secondLen > 0)
            err = feedUni(&s, &b, tv[i].second, tv[i].secondLen, tv[i].secondLen);

        _h3UniDestroy(&b);
        _h3UniDestroy(&a);
        _h3SessionDestroy(&s);

        if (err != tv[i].err) {
            TEST_FAILV(ret, 1, _SL("${string}: error ${int}, expected ${int}"),
                       stvar(strref, (strref)tv[i].what), stvar(int64, (int64)err),
                       stvar(int64, (int64)tv[i].err));
            goto out;
        }
    }

    // What a peer may send that this endpoint has no use for, and which must therefore be read
    // and discarded rather than refused. Getting this wrong is how an implementation stops
    // working against a peer that adds anything.
    {
        Http3Session s;
        H3UniStream ctl, enc, dec, other;
        _h3SessionInit(&s, true, NULL);
        _h3UniInit(&ctl, 3);
        _h3UniInit(&enc, 7);
        _h3UniInit(&dec, 11);
        _h3UniInit(&other, 15);

        // SETTINGS, then a frame type reserved for greasing with a payload, then a GOAWAY that
        // has to be seen despite the frame in front of it.
        static const uint8 control[] = { 0x00, 0x04, 0x00, 0x21, 0x04, 'j', 'u', 'n', 'k',
                                         0x07, 0x01, 0x00 };
        uint64 err = feedUni(&s, &ctl, control, sizeof(control), 1);
        bool ok    = err == 0 && s.peerSettings && s.goawayRecvd && s.goawayId == 0;

        // A capacity-zero instruction on the encoder stream says nothing and is allowed to.
        static const uint8 encbytes[] = { 0x02, 0x20, 0x20 };
        if (ok)
            ok = feedUni(&s, &enc, encbytes, sizeof(encbytes), 1) == 0;

        // The peer's decoder stream acknowledges field sections this endpoint's encoder never
        // sends, so whatever is on it is discarded rather than refused.
        static const uint8 decbytes[] = { 0x03, 0x80, 0x41, 0x00 };
        if (ok)
            ok = feedUni(&s, &dec, decbytes, sizeof(decbytes), 1) == 0;

        // A stream type from some later revision of the protocol.
        static const uint8 unknown[] = { 0x40, 0x40, 'x', 'y', 'z' };
        if (ok)
            ok = feedUni(&s, &other, unknown, sizeof(unknown), 1) == 0;

        // Ending an ordinary stream costs nothing; ending a critical one ends the connection.
        if (ok)
            ok = _h3SessionUniEnd(&s, &other) == 0 &&
                 _h3SessionUniEnd(&s, &ctl) == H3ERR_CLOSED_CRITICAL_STREAM &&
                 _h3SessionUniEnd(&s, &enc) == H3ERR_CLOSED_CRITICAL_STREAM &&
                 _h3SessionUniEnd(&s, &dec) == H3ERR_CLOSED_CRITICAL_STREAM;

        _h3UniDestroy(&other);
        _h3UniDestroy(&dec);
        _h3UniDestroy(&enc);
        _h3UniDestroy(&ctl);
        _h3SessionDestroy(&s);
        CHECK_TRUE("tolerated extensions", ok);
    }

    // A server reading a client's MAX_PUSH_ID, which is legal and inert: cxhttp never pushes, so
    // the permission is recorded and never used. Lowering it afterwards is not legal.
    {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, true, NULL);
        _h3UniInit(&u, 3);

        static const uint8 bytes[] = { 0x00, 0x04, 0x00, 0x0d, 0x01, 0x08 };
        bool ok = feedUni(&s, &u, bytes, sizeof(bytes), 1) == 0 && s.maxPushId == 9;

        static const uint8 lower[] = { 0x0d, 0x01, 0x04 };
        if (ok)
            ok = _h3SessionUniRecv(&s, &u, lower, sizeof(lower)) == H3ERR_ID_ERROR;

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);
        CHECK_TRUE("max push id", ok);
    }

    // The bare minimum a conforming peer opens with, which must be accepted on its own.
    {
        Http3Session s;
        H3UniStream u;
        _h3SessionInit(&s, false, NULL);
        _h3UniInit(&u, 3);

        bool ok = feedUni(&s, &u, kBareControl, sizeof(kBareControl), 1) == 0 && s.peerSettings;

        _h3UniDestroy(&u);
        _h3SessionDestroy(&s);
        CHECK_TRUE("bare control stream", ok);
    }

out:
    return ret;
}


// ---------------------------------------------------------------------------------------------
// A hand-built HTTP/3 client, for driving the server
//
// The server has to be exercised by something that is not the cx client, and at this stage the cx
// client does not exist yet. This speaks the protocol directly over cxquic: it opens its own
// control and QPACK streams, writes a field section and DATA frames onto a bidirectional stream by
// hand, and reads the answer back through the same frame reader the connections use.
//
// It is not a client -- there is no pooling, no redirects and no error recovery here. What it is
// is a way to say "send exactly these bytes and tell me exactly what came back".
// ---------------------------------------------------------------------------------------------

#define H3T_MAXREQ 24
#define H3T_MAXUNI 4

typedef struct H3TReq {
    NetFlow* flow;
    BufRing in;
    H3MsgReader msg;

    uint16 status;
    HttpHeaders headers;
    string body;
    uint32 interim;     // 1xx field sections seen ahead of the real one

    bool headDone;
    bool complete;
    bool failed;
    uint64 err;         // the stream error code, when one arrived
} H3TReq;

typedef struct H3TUni {
    NetFlow* flow;
    H3UniStream u;
} H3TUni;

typedef struct H3TClient {
    NetSocket* sock;
    NetFlow* control;
    NetFlow* enc;
    NetFlow* dec;

    Http3Session session;
    bool connected;
    bool started;
    bool connClosed;
    uint64 connErr;

    H3TReq reqs[H3T_MAXREQ];
    uint32 nreqs;
    H3TUni uni[H3T_MAXUNI];
    uint32 nuni;
} H3TClient;

typedef struct H3TFix {
    TlsTestPKI pki;
    TlsCAStore* ca;
    TlsCreds* creds;
    TlsConfig* scfg;
    TlsConfig* ccfg;
    NetQueue* q;
    HttpServer* srv;
    H3TClient cl;
} H3TFix;

static H3TReq* h3tFindReq(_Inout_ H3TClient* c, _In_opt_ NetFlow* flow)
{
    for (uint32 i = 0; i < c->nreqs; i++) {
        if (c->reqs[i].flow == flow)
            return &c->reqs[i];
    }
    return NULL;
}

static H3TUni* h3tFindUni(_Inout_ H3TClient* c, _In_opt_ NetFlow* flow)
{
    for (uint32 i = 0; i < c->nuni; i++) {
        if (c->uni[i].flow == flow)
            return &c->uni[i];
    }
    return NULL;
}

// Read whatever the peer has put on a response stream and decode it.
static void h3tReqPump(_Inout_ H3TReq* r)
{
    uint8 buf[4096];
    size_t n;
    bool fin   = false;
    bool ended = false;

    while ((n = netquicRecv(r->flow, buf, sizeof(buf), &fin)) > 0) {
        ended = ended || fin;
        bufringWrite(&r->in, buf, n);
    }
    ended = ended || fin;

    for (;;) {
        H3MsgResult res = _h3MsgStep(&r->msg, &r->in);
        if (res == H3MSG_NeedMore)
            break;

        if (res == H3MSG_Error) {
            r->failed = true;
            r->err    = r->msg.err;
            return;
        }

        if (res == H3MSG_Head) {
            uint16 st = 0;
            HttpHeaders h;
            if (_h3RespFromFields(&st, &h, &r->msg.headers) != 0) {
                httpHeadersDestroy(&h);
                r->failed = true;
                return;
            }

            // An interim response is a field section that is not the answer. The real one is still
            // to come, so the fields from this one are read and let go.
            if (st >= 100 && st < 200) {
                r->interim++;
                httpHeadersDestroy(&h);
                continue;
            }

            r->status = st;
            httpHeadersDestroy(&r->headers);
            r->headers  = h;
            r->headDone = true;
            continue;
        }

        if (res == H3MSG_Body) {
            size_t remaining = r->msg.bodyReady;
            while (remaining > 0) {
                size_t got = bufringRead(&r->in, buf, min(remaining, sizeof(buf)));
                _httpAppendBytes(&r->body, buf, got);
                remaining -= got;
            }
            continue;
        }
        // H3MSG_Trailers: read and dropped, as cxhttp itself does
    }

    if (ended)
        r->complete = true;
}

static void h3tOnFlowOpen(NetEvent* ev)
{
    H3TClient* c = (H3TClient*)ev->ctx;
    if (!ev->flow || (ev->socket && ev->flow == ev->socket->flow))
        return;

    // Only the peer's unidirectional streams arrive this way; everything this end opens it already
    // has a handle on.
    if ((ev->flow->key & 0x03) != 0x03 || c->nuni >= H3T_MAXUNI)
        return;

    H3TUni* u = &c->uni[c->nuni++];
    u->flow   = ev->flow;
    _h3UniInit(&u->u, ev->flow->key);
}

static void h3tOnRecv(NetEvent* ev)
{
    H3TClient* c = (H3TClient*)ev->ctx;

    H3TReq* r = h3tFindReq(c, ev->flow);
    if (r) {
        h3tReqPump(r);
        return;
    }

    H3TUni* u = h3tFindUni(c, ev->flow);
    if (!u)
        return;

    uint8 buf[4096];
    size_t n;
    bool fin = false;
    while ((n = netquicRecv(ev->flow, buf, sizeof(buf), &fin)) > 0) {
        uint64 err = _h3SessionUniRecv(&c->session, &u->u, buf, n);
        if (err)
            c->connErr = err;
    }
}

static void h3tOnError(NetEvent* ev)
{
    H3TClient* c = (H3TClient*)ev->ctx;

    H3TReq* r = h3tFindReq(c, ev->flow);
    if (!r)
        return;

    QuicStream* qs = objDynCast(QuicStream, ev->flow);
    r->failed      = true;
    r->err         = qs ? qs->error : 0;
}

static void h3tOnClosed(NetEvent* ev)
{
    H3TClient* c = (H3TClient*)ev->ctx;

    if (ev->socket && ev->flow == ev->socket->flow) {
        c->connClosed = true;
        return;
    }

    H3TReq* r = h3tFindReq(c, ev->flow);
    if (r)
        r->complete = true;
}

// Open this end's three unidirectional streams and send what each has to carry. The server refuses
// a connection whose control stream never appears, so this is not optional.
static bool h3tStart(_Inout_ H3TClient* c)
{
    if (c->started)
        return true;

    c->control = netquicOpen(c->sock, true);
    c->enc     = netquicOpen(c->sock, true);
    c->dec     = netquicOpen(c->sock, true);
    if (!c->control || !c->enc || !c->dec)
        return false;

    string out = 0;
    bool ok    = _h3SessionControlPreamble(&c->session, &out) &&
              netflowSend(c->control, (uint8*)strPC(&out), strLen(out), 0);
    strClear(&out);

    ok = ok && _h3SessionUniPrefix(&out, H3_STREAM_QPACK_ENCODER) &&
         netflowSend(c->enc, (uint8*)strPC(&out), strLen(out), 0);
    strClear(&out);

    ok = ok && _h3SessionUniPrefix(&out, H3_STREAM_QPACK_DECODER) &&
         netflowSend(c->dec, (uint8*)strPC(&out), strLen(out), 0);
    strDestroy(&out);

    c->started = ok;
    return ok;
}

static void h3tOnConnection(NetEvent* ev)
{
    H3TClient* c = (H3TClient*)ev->ctx;
    if (ev->conn.state == NCS_Connected) {
        c->connected = true;
        h3tStart(c);
    }
}

static const NetHandlers kH3THandlers = {
    .connection = h3tOnConnection,
    .flowOpen   = h3tOnFlowOpen,
    .recv       = h3tOnRecv,
    .error      = h3tOnError,
    .flowClosed = h3tOnClosed,
};

// Start a request on a stream of its own. `fields` is sent as the field section exactly as given,
// which is what lets a malformed one be constructed by hand; `body` follows in DATA frames.
// `fin` ends the stream, which is what says the request is complete.
_Ret_maybenull_ static H3TReq* h3tSendFields(_Inout_ H3TClient* c, _In_ const HttpHeaders* fields,
                                             _In_opt_ strref body, bool fin)
{
    if (c->nreqs >= H3T_MAXREQ)
        return NULL;

    NetFlow* flow = netquicOpen(c->sock, false);
    if (!flow)
        return NULL;

    H3TReq* r = &c->reqs[c->nreqs++];
    memset(r, 0, sizeof(*r));
    r->flow = flow;
    bufringInit(&r->in, 4096);
    _h3MsgInit(&r->msg, false, NULL);
    httpHeadersInit(&r->headers);

    if (!_h3SendFields(flow, fields, false)) {
        r->failed = true;
        return r;
    }

    uint32 blen = strLen(body);
    if (blen > 0) {
        uint8* raw = xaAlloc(blen);
        strCopyRaw(body, 0, raw, blen);
        if (!_h3SendData(flow, raw, blen))
            r->failed = true;
        xaFree(raw);
    }

    if (fin)
        netquicFinish(flow);

    return r;
}

// The ordinary case: build the pseudo-headers for a request and send it.
_Ret_maybenull_ static H3TReq* h3tRequest(_Inout_ H3TClient* c, _In_opt_ strref method,
                                          _In_opt_ strref path, _In_opt_ strref body)
{
    HttpHeaders f;
    httpHeadersInit(&f);
    httpHeadersAdd(&f, _SL(":method"), method);
    httpHeadersAdd(&f, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&f, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&f, _SL(":path"), path);
    if (strLen(body) > 0) {
        string n = 0;
        strFromUInt64(&n, strLen(body), 10);
        httpHeadersAdd(&f, _SL("content-length"), n);
        strDestroy(&n);
    }

    H3TReq* r = h3tSendFields(c, &f, body, true);
    httpHeadersDestroy(&f);
    return r;
}

static void h3tFixDestroy(_Inout_ H3TFix* f)
{
    for (uint32 i = 0; i < f->cl.nreqs; i++) {
        objRelease(&f->cl.reqs[i].flow);
        bufringDestroy(&f->cl.reqs[i].in);
        _h3MsgDestroy(&f->cl.reqs[i].msg);
        httpHeadersDestroy(&f->cl.reqs[i].headers);
        strDestroy(&f->cl.reqs[i].body);
    }
    for (uint32 i = 0; i < f->cl.nuni; i++)
        _h3UniDestroy(&f->cl.uni[i].u);

    objRelease(&f->cl.control);
    objRelease(&f->cl.enc);
    objRelease(&f->cl.dec);
    if (f->cl.sock)
        netsocketClose(f->cl.sock);
    objRelease(&f->cl.sock);
    _h3SessionDestroy(&f->cl.session);

    if (f->srv)
        httpserverShutdown(f->srv);
    objRelease(&f->srv);

    if (f->q) {
        for (int i = 0; i < 20; i++)
            netqueueTick(f->q, 0);
        netqueueShutdown(f->q, timeS(2));
    }
    objRelease(&f->q);

    objRelease(&f->ccfg);
    objRelease(&f->scfg);
    objRelease(&f->creds);
    objRelease(&f->ca);
    tlsTestPKIDestroy(&f->pki);
}

// Listener up, client connected, both ends' control streams exchanged.
static bool h3tFixInit(_Inout_ H3TFix* f, _In_ const HttpServerHandlers* handlers,
                       _In_opt_ void* ctx, _In_opt_ const HttpLimits* limits)
{
    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    f->creds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    f->scfg  = f->creds ? tlsconfigCreateServer(f->creds) : NULL;
    f->ccfg  = tlsconfigCreateClient();
    if (!f->scfg || !f->ccfg)
        return false;
    tlsconfigSetCA(f->ccfg, f->ca);

    sa_string alpn;
    saInit(&alpn, string, 1);
    saPush(&alpn, strref, _SL("h3"));
    tlsconfigSetALPN(f->ccfg, &alpn);
    saDestroy(&alpn);

    NetQueueConfig conf;
    netqueuePresetServer(&conf);
    conf.nthreads = 0;
    f->q          = netqueueCreate(&conf);
    if (!f->q)
        return false;

    f->srv = httpserverCreate(f->q);
    if (!f->srv)
        return false;
    httpserverSetHandlers(f->srv, handlers, ctx);
    if (limits)
        f->srv->limits = *limits;

    NetAddr la;
    netAddrFromStr(&la, _SL("127.0.0.1"));
    la.port = 0;

    if (!httpserverListenQuic(f->srv, &la, f->scfg))
        return false;

    _h3SessionInit(&f->cl.session, false, NULL);

    QuicConfig qcfg = { .tls = f->ccfg };
    f->cl.sock      = netquicConnect(f->q, _SL("127.0.0.1"), httpserverPort(f->srv),
                                     _S TLS_TEST_HOSTNAME, &qcfg, &kH3THandlers, &f->cl);
    if (!f->cl.sock)
        return false;

    for (int i = 0; i < 600 && !f->cl.started; i++)
        netqueueTick(f->q, timeMS(5));

    return f->cl.started;
}

// Tick until `done` answers true or the budget runs out.
static bool h3tWait(_Inout_ H3TFix* f, bool (*done)(void*), _Inout_opt_ void* ctx)
{
    for (int i = 0; i < 1500; i++) {
        if (done(ctx))
            return true;
        netqueueTick(f->q, timeMS(5));
    }
    return done(ctx);
}

static bool h3tReqDone(void* ctx)
{
    H3TReq* r = (H3TReq*)ctx;
    return r->complete || r->failed;
}

// ---------------------------------------------------------------------------------------------
// The server under test
// ---------------------------------------------------------------------------------------------

typedef struct H3SrvRec {
    int32 requests;
    int32 heads;
    string lastPath;
    string lastMethod;
    string lastBody;
    string lastHost;

    // What the handler should answer with
    strref replyBody;
    uint16 replyStatus;
    size_t bigReply;      // answer with this many generated bytes instead
    bool useSink;         // stream the request body into a buffer rather than buffering it
    bool holdBody;        // and do not drain that buffer, so the peer is held
    StreamBuffer* sink;
} H3SrvRec;

STR_CONST(kH3Hello, "hello from http/3");

// The body a large-transfer test sends and checks, generated rather than stored so that a
// mismatch shows up as a position rather than as a diff.
// The offset is a position in a stream, which outgrows size_t on a 32-bit build. Only the low
// eight bits of the result are kept, and those are the same however wide the arithmetic is.
static uint8 h3tPatternByte(uint64 off)
{
    return (uint8)(off * 31u + (off >> 8));
}

static void h3tFillPattern(_Out_writes_bytes_(len) uint8* buf, size_t off, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = h3tPatternByte(off + i);
}

static bool h3tCheckPattern(_In_ strref s, size_t len)
{
    if (strLen(s) != len)
        return false;

    uint8 want[256];
    uint8 got[256];
    for (size_t off = 0; off < len; off += sizeof(want)) {
        size_t n = min(sizeof(want), len - off);
        h3tFillPattern(want, off, n);
        if (strCopyRaw(s, (uint32)off, got, (uint32)n) != n || memcmp(want, got, n) != 0)
            return false;
    }
    return true;
}

static void h3tOnHead(HttpServerEvent* ev)
{
    H3SrvRec* rec = (H3SrvRec*)ev->ctx;
    rec->heads++;

    if (rec->useSink) {
        rec->sink = sbufCreate(4096);
        sbufSetWatermark(rec->sink, 8192, 4096);
        sbufCRegisterPush(rec->sink, NULL, NULL, NULL);
        httpsrvreqSetSink(ev->request, rec->sink);
    }
}

static void h3tOnRequest(HttpServerEvent* ev)
{
    H3SrvRec* rec          = (H3SrvRec*)ev->ctx;
    HttpServerRequest* req = ev->request;

    rec->requests++;
    strDup(&rec->lastPath, req->path);
    strDup(&rec->lastMethod, req->methodName);
    strDup(&rec->lastBody, req->body);
    httpHeadersGet(&req->headers, _SL("Host"), &rec->lastHost);

    if (rec->replyStatus)
        httpsrvreqSetStatus(req, rec->replyStatus);

    if (rec->bigReply > 0) {
        string body = 0;
        uint8* buf  = xaAlloc(rec->bigReply);
        h3tFillPattern(buf, 0, rec->bigReply);
        _httpAppendBytes(&body, buf, rec->bigReply);
        xaFree(buf);
        httpsrvreqRespond(req, body, _SL("application/octet-stream"));
        strDestroy(&body);
        return;
    }

    // The path is echoed back so that sixteen concurrent answers can be told apart.
    string body = 0;
    strNConcat(&body, rec->replyBody ? rec->replyBody : (strref)kH3Hello, _SL(" "), req->path);
    httpsrvreqRespond(req, body, _SL("text/plain"));
    strDestroy(&body);
}

static const HttpServerHandlers kH3SrvHandlers = {
    .head    = h3tOnHead,
    .request = h3tOnRequest,
};

static void h3SrvRecDestroy(_Inout_ H3SrvRec* rec)
{
    strDestroy(&rec->lastPath);
    strDestroy(&rec->lastMethod);
    strDestroy(&rec->lastBody);
    strDestroy(&rec->lastHost);
    if (rec->sink)
        sbufRelease(&rec->sink);
}

// ---------------------------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------------------------

int test_http3test_roundtrip(void)
{
    int ret       = 0;
    H3TFix f      = { 0 };
    H3SrvRec rec  = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    H3TReq* r = h3tRequest(&f.cl, _SL("GET"), _SL("/hello"), NULL);
    CHECK_TRUE("request started", r != NULL);
    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));

    CHECK_U64("status", r->status, 200);
    CHECK_U64("requests seen", rec.requests, 1);
    CHECK_STR("path", rec.lastPath, _SL("/hello"));
    CHECK_STR("method", rec.lastMethod, _SL("GET"));

    // :authority arrives as Host, so an application does not have to know which protocol carried
    // the request.
    CHECK_STR("host", rec.lastHost, _S TLS_TEST_HOSTNAME);
    CHECK_STR("body", r->body, _SL("hello from http/3 /hello"));

    // The peer's SETTINGS arrived on its control stream, which is the only thing that can have
    // opened one.
    CHECK_TRUE("peer settings", f.cl.session.peerSettings);
    CHECK_TRUE("peer control stream", f.cl.session.peerControl);

    string ctype = 0;
    httpHeadersGet(&r->headers, _SL("content-type"), &ctype);
    bool okType = strEq(ctype, _SL("text/plain"));
    strDestroy(&ctype);
    CHECK_TRUE("content-type", okType);

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_srvpost(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    H3TReq* r = h3tRequest(&f.cl, _SL("POST"), _SL("/upload"), _SL("a body in a DATA frame"));
    CHECK_TRUE("request started", r != NULL);
    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));

    CHECK_U64("status", r->status, 200);
    CHECK_STR("method", rec.lastMethod, _SL("POST"));
    CHECK_STR("request body", rec.lastBody, _SL("a body in a DATA frame"));

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

typedef struct H3TAll {
    H3TReq** reqs;
    uint32 n;
} H3TAll;

static bool h3tAllDone(void* ctx)
{
    H3TAll* a = (H3TAll*)ctx;
    for (uint32 i = 0; i < a->n; i++) {
        if (!a->reqs[i] || !(a->reqs[i]->complete || a->reqs[i]->failed))
            return false;
    }
    return true;
}

int test_http3test_concurrent(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    // Sixteen requests started before any of them is answered. This is the test that fails if the
    // streams are not really independent: the answers come back interleaved, and each carries the
    // path of the request it belongs to.
    H3TReq* reqs[16];
    string paths[16];
    for (uint32 i = 0; i < 16; i++) {
        paths[i] = 0;
        string n = 0;
        strFromUInt64(&n, i, 10);
        strNConcat(&paths[i], _SL("/n/"), n);
        strDestroy(&n);

        reqs[i] = h3tRequest(&f.cl, _SL("GET"), paths[i], NULL);
    }

    H3TAll all = { reqs, 16 };
    bool done  = h3tWait(&f, h3tAllDone, &all);

    for (uint32 i = 0; i < 16; i++) {
        if (done && reqs[i]) {
            string want = 0;
            strNConcat(&want, _SL("hello from http/3 "), paths[i]);
            if (reqs[i]->status != 200 || !strEq(reqs[i]->body, want)) {
                TEST_FAILV(ret, 1, _SL("stream ${int}: status ${int}, body '${string}'"),
                           stvar(int32, (int32)i), stvar(int32, (int32)reqs[i]->status),
                           stvar(strref, reqs[i]->body));
                done = false;
            }
            strDestroy(&want);
        }
        strDestroy(&paths[i]);
    }

    CHECK_TRUE("sixteen concurrent requests", done);
    CHECK_U64("requests seen", rec.requests, 16);

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_bodies(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };
    string big   = 0;

    // Larger than the stream's initial flow control window in both directions, so neither body
    // fits in one pass and both have to survive being stopped and started again. This is what
    // catches a DATA frame header sent without its payload.
    const size_t kBig = 400 * 1024;

    rec.bigReply = kBig;

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    {
        uint8* buf = xaAlloc(kBig);
        h3tFillPattern(buf, 0, kBig);
        _httpAppendBytes(&big, buf, kBig);
        xaFree(buf);
    }

    // The request body cannot go out in one call either: netflowSend takes what the window has
    // room for and no more, so it is pushed as the window opens.
    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL(":method"), _SL("POST"));
    httpHeadersAdd(&fields, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&fields, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&fields, _SL(":path"), _SL("/big"));
    string n = 0;
    strFromUInt64(&n, kBig, 10);
    httpHeadersAdd(&fields, _SL("content-length"), n);
    strDestroy(&n);

    H3TReq* r = h3tSendFields(&f.cl, &fields, NULL, false);
    httpHeadersDestroy(&fields);
    CHECK_TRUE("request started", r != NULL);

    uint8* raw = xaAlloc(kBig);
    h3tFillPattern(raw, 0, kBig);

    size_t sent = 0;
    for (int i = 0; i < 4000 && sent < kBig; i++) {
        // Offer a frame every tick and let the stream take it or refuse it. A refusal is not a
        // failure here: it means the window is full, and the next tick tries the same frame again.
        size_t want = min(kBig - sent, (size_t)16384);
        if (_h3SendData(r->flow, raw + sent, want))
            sent += want;
        netqueueTick(f.q, timeMS(2));
    }
    xaFree(raw);

    CHECK_U64("request body sent", sent, kBig);
    netquicFinish(r->flow);

    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));
    CHECK_U64("status", r->status, 200);
    CHECK_TRUE("request body byte for byte", h3tCheckPattern(rec.lastBody, kBig));
    CHECK_TRUE("response body byte for byte", h3tCheckPattern(r->body, kBig));

out:
    strDestroy(&big);
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_forbidden(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    // Every field section a server has to refuse, each costing one stream and not the connection.
    static const struct {
        const char* what;
        const char* nv[14];
    } tv[] = {
        // Connection-specific fields, which describe an HTTP/1.1 connection and mean nothing here.
        { "connection", { ":method", "GET", ":scheme", "https", ":path", "/", "connection",
                          "keep-alive", NULL } },
        { "transfer-encoding", { ":method", "GET", ":scheme", "https", ":path", "/",
                                 "transfer-encoding", "chunked", NULL } },
        { "upgrade", { ":method", "GET", ":scheme", "https", ":path", "/", "upgrade",
                       "websocket", NULL } },

        // TE says `trailers` or it says nothing.
        { "te", { ":method", "GET", ":scheme", "https", ":path", "/", "te", "gzip", NULL } },

        // Pseudo-headers: missing, repeated, unknown, and out of order.
        { "no method", { ":scheme", "https", ":path", "/", NULL } },
        { "no path", { ":method", "GET", ":scheme", "https", NULL } },
        { "no scheme", { ":method", "GET", ":path", "/", NULL } },
        { "duplicate path", { ":method", "GET", ":scheme", "https", ":path", "/", ":path", "/x",
                              NULL } },
        { "unknown pseudo", { ":method", "GET", ":scheme", "https", ":path", "/", ":nonsense",
                              "x", NULL } },
        { "pseudo after field", { ":method", "GET", ":scheme", "https", "x-a", "b", ":path", "/",
                                  NULL } },

        // A response's pseudo-header on a request.
        { "status on a request", { ":method", "GET", ":scheme", "https", ":path", "/", ":status",
                                   "200", NULL } },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]) && ret == 0; i++) {
        HttpHeaders fields;
        buildHeaders(&fields, tv[i].nv);

        H3TReq* r = h3tSendFields(&f.cl, &fields, NULL, true);
        httpHeadersDestroy(&fields);

        if (!r) {
            TEST_FAILV(ret, 1, _SL("${string}: no stream"), stvar(strref, (strref)tv[i].what));
            break;
        }

        h3tWait(&f, h3tReqDone, r);

        // Refused, and with nothing that could be mistaken for an answer.
        if (r->status != 0) {
            TEST_FAILV(ret, 1, _SL("${string}: answered ${int} instead of being refused"),
                       stvar(strref, (strref)tv[i].what), stvar(int32, (int32)r->status));
            break;
        }
    }

    // And the connection is still good afterwards, because none of those was a connection error.
    if (ret == 0) {
        H3TReq* r = h3tRequest(&f.cl, _SL("GET"), _SL("/after"), NULL);
        CHECK_TRUE("request after refusals", r != NULL);
        CHECK_TRUE("response after refusals", h3tWait(&f, h3tReqDone, r));
        CHECK_U64("status after refusals", r->status, 200);
    }

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_trailers(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL(":method"), _SL("POST"));
    httpHeadersAdd(&fields, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&fields, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&fields, _SL(":path"), _SL("/trailing"));

    H3TReq* r = h3tSendFields(&f.cl, &fields, _SL("body bytes"), false);
    httpHeadersDestroy(&fields);
    CHECK_TRUE("request started", r != NULL);

    // A field section after the body is trailers. They are read, checked and dropped: nothing in
    // cxhttp consumes one, and letting them through as ordinary headers would let a peer rewrite a
    // message the application has already been handed the head of.
    HttpHeaders trailers;
    httpHeadersInit(&trailers);
    httpHeadersAdd(&trailers, _SL("x-checksum"), _SL("deadbeef"));
    bool sent = _h3SendFields(r->flow, &trailers, true);
    httpHeadersDestroy(&trailers);
    CHECK_TRUE("trailers sent", sent);

    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));
    CHECK_U64("status", r->status, 200);
    CHECK_STR("body survived the trailers", rec.lastBody, _SL("body bytes"));
    CHECK_TRUE("trailers did not become headers",
               !httpHeadersHas(&r->headers, _SL("x-checksum")));

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_backpressure(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };
    uint8* raw   = NULL;

    // Larger than the stream's initial flow control window, so that the peer running out of window
    // is something this test can actually observe rather than something it has to take on trust.
    const size_t kBody = 512 * 1024;

    rec.useSink = true;

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL(":method"), _SL("POST"));
    httpHeadersAdd(&fields, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&fields, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&fields, _SL(":path"), _SL("/slow"));
    string n = 0;
    strFromUInt64(&n, kBody, 10);
    httpHeadersAdd(&fields, _SL("content-length"), n);
    strDestroy(&n);

    H3TReq* r = h3tSendFields(&f.cl, &fields, NULL, false);
    httpHeadersDestroy(&fields);
    CHECK_TRUE("request started", r != NULL);

    raw = xaAlloc(kBody);
    h3tFillPattern(raw, 0, kBody);

    // Push as hard as the window allows without ever draining the sink. The application not
    // reading is what stops cxhttp reading, which is what closes the peer's window: no threshold of
    // cxhttp's own is involved, and none is needed.
    size_t sent = 0;
    for (int i = 0; i < 600 && sent < kBody; i++) {
        size_t want = min(kBody - sent, (size_t)16384);
        if (_h3SendData(r->flow, raw + sent, want))
            sent += want;
        netqueueTick(f.q, timeMS(2));
    }

    CHECK_TRUE("the peer stopped taking the body", sent < kBody);
    CHECK_TRUE("the head still got through", rec.heads == 1);
    CHECK_TRUE("nothing was answered", rec.requests == 0);

    // Now read the sink, which releases the producer, which lets cxhttp read again, which reopens
    // the window.
    for (int i = 0; i < 4000 && sent < kBody; i++) {
        uint8 drain[8192];
        size_t got = 0;
        while (rec.sink && sbufCRead(rec.sink, drain, sizeof(drain), &got) && got > 0)
            got = 0;

        size_t want = min(kBody - sent, (size_t)16384);
        if (_h3SendData(r->flow, raw + sent, want))
            sent += want;
        netqueueTick(f.q, timeMS(2));
    }

    CHECK_U64("the whole body got through once the sink drained", sent, kBody);

    netquicFinish(r->flow);
    for (int i = 0; i < 4000 && !(r->complete || r->failed); i++) {
        uint8 drain[8192];
        size_t got = 0;
        while (rec.sink && sbufCRead(rec.sink, drain, sizeof(drain), &got) && got > 0)
            got = 0;
        netqueueTick(f.q, timeMS(2));
    }

    CHECK_U64("status", r->status, 200);
    CHECK_U64("requests seen", rec.requests, 1);

out:
    xaFree(raw);
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_reset(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    // Two streams: one abandoned halfway through its body, one that runs to completion. Resetting
    // the first must disturb nothing about the second, which is the property a connection carrying
    // many requests exists to provide.
    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL(":method"), _SL("POST"));
    httpHeadersAdd(&fields, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&fields, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&fields, _SL(":path"), _SL("/abandoned"));
    httpHeadersAdd(&fields, _SL("content-length"), _SL("1000"));

    H3TReq* doomed = h3tSendFields(&f.cl, &fields, _SL("only the first few bytes"), false);
    httpHeadersDestroy(&fields);
    CHECK_TRUE("first request started", doomed != NULL);

    for (int i = 0; i < 100 && rec.heads < 1; i++)
        netqueueTick(f.q, timeMS(2));
    CHECK_U64("the head arrived", rec.heads, 1);

    netquicReset(doomed->flow, H3ERR_REQUEST_CANCELLED);
    netquicStopSending(doomed->flow, H3ERR_REQUEST_CANCELLED);

    for (int i = 0; i < 100; i++)
        netqueueTick(f.q, timeMS(2));

    CHECK_U64("the abandoned request was never answered", rec.requests, 0);

    H3TReq* good = h3tRequest(&f.cl, _SL("GET"), _SL("/still-here"), NULL);
    CHECK_TRUE("second request started", good != NULL);
    CHECK_TRUE("second response arrived", h3tWait(&f, h3tReqDone, good));
    CHECK_U64("second status", good->status, 200);
    CHECK_STR("second body", good->body, _SL("hello from http/3 /still-here"));

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

// A request held past the end of its handler and answered from somewhere else entirely, which is
// what an application that hands its work to a TaskQueue does.
typedef struct H3Deferred {
    HttpServerRequest* held;
} H3Deferred;

static int h3tDeferredThread(Thread* self)
{
    H3Deferred* d = NULL;
    if (!stvlNext(&self->args, ptr, &d) || !d)
        return 0;

    httpsrvreqRespond(d->held, _SL("answered elsewhere"), _SL("text/plain"));
    return 0;
}

static void h3tOnRequestDeferred(HttpServerEvent* ev)
{
    H3Deferred* d = (H3Deferred*)ev->ctx;
    objRelease(&d->held);
    d->held = objAcquire(ev->request);
}

static const HttpServerHandlers kH3DeferredHandlers = {
    .request = h3tOnRequestDeferred,
};

int test_http3test_deferred(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3Deferred d = { 0 };
    Thread* th   = NULL;

    if (!h3tFixInit(&f, &kH3DeferredHandlers, &d, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    H3TReq* r = h3tRequest(&f.cl, _SL("GET"), _SL("/later"), NULL);
    CHECK_TRUE("request started", r != NULL);

    for (int i = 0; i < 300 && !d.held; i++)
        netqueueTick(f.q, timeMS(2));
    CHECK_TRUE("the request was held", d.held != NULL);

    th = thrCreate(h3tDeferredThread, _S "h3 deferred respond", stvar(ptr, &d));
    CHECK_TRUE("responder thread", th != NULL);

    // The response is composed on that thread and written by a worker here, which is the whole
    // point: nothing but a worker may touch the stream.
    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));
    CHECK_U64("status", r->status, 200);
    CHECK_STR("body", r->body, _SL("answered elsewhere"));

out:
    if (th) {
        thrWait(th, timeS(5));
        thrRelease(&th);
    }
    objRelease(&d.held);
    h3tFixDestroy(&f);
    return ret;
}

int test_http3test_interim(void)
{
    int ret      = 0;
    H3TFix f     = { 0 };
    H3SrvRec rec = { 0 };

    if (!h3tFixInit(&f, &kH3SrvHandlers, &rec, NULL)) {
        TEST_FAILV(ret, 1, _SL("the HTTP/3 fixture did not come up"), stvNone);
        goto out;
    }

    // Expect: 100-continue works the same way it does over HTTP/1.1, except that the interim
    // answer is another field section rather than a second status line.
    HttpHeaders fields;
    httpHeadersInit(&fields);
    httpHeadersAdd(&fields, _SL(":method"), _SL("POST"));
    httpHeadersAdd(&fields, _SL(":scheme"), _SL("https"));
    httpHeadersAdd(&fields, _SL(":authority"), _S TLS_TEST_HOSTNAME);
    httpHeadersAdd(&fields, _SL(":path"), _SL("/expect"));
    httpHeadersAdd(&fields, _SL("expect"), _SL("100-continue"));
    httpHeadersAdd(&fields, _SL("content-length"), _SL("7"));

    H3TReq* r = h3tSendFields(&f.cl, &fields, NULL, false);
    httpHeadersDestroy(&fields);
    CHECK_TRUE("request started", r != NULL);

    for (int i = 0; i < 300 && r->interim == 0; i++)
        netqueueTick(f.q, timeMS(2));

    CHECK_U64("a 100 Continue arrived", r->interim, 1);
    CHECK_U64("and the real answer did not", r->status, 0);

    bool sent = _h3SendData(r->flow, (const uint8*)"payload", 7);
    CHECK_TRUE("body sent", sent);
    netquicFinish(r->flow);

    CHECK_TRUE("response arrived", h3tWait(&f, h3tReqDone, r));
    CHECK_U64("status", r->status, 200);
    CHECK_STR("body", rec.lastBody, _SL("payload"));

    // The interim response did not become the answer, and did not leave its fields behind on it.
    CHECK_U64("still one interim", r->interim, 1);

out:
    h3SrvRecDestroy(&rec);
    h3tFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// The cx client against the cx server
//
// Both ends on one polled queue. This proves the two agree, which is necessary and -- as
// design/http.md section 16.9 keeps pointing out -- nowhere near sufficient: they share a codec,
// so they can agree with each other while both being wrong. What it does catch is everything
// above the codec: the pool, the multiplexing, the redirect path, cancellation.
// ---------------------------------------------------------------------------------------------

typedef struct H3CliRec {
    int32 completed;
    int32 failed;
    uint16 status;
    HttpVersion version;
    string body;
    HttpError err;
} H3CliRec;

static void h3cliOnComplete(HttpEvent* ev)
{
    H3CliRec* rec = (H3CliRec*)ev->ctx;
    rec->completed++;
    rec->status  = ev->status;
    rec->version = ev->version;
    strDup(&rec->body, ev->request->respBody);
}

static void h3cliOnError(HttpEvent* ev)
{
    H3CliRec* rec = (H3CliRec*)ev->ctx;
    rec->failed++;
    rec->err     = ev->err;
    rec->status  = ev->status;
    rec->version = ev->version;
}

static const HttpHandlers kH3CliHandlers = {
    .complete = h3cliOnComplete,
    .error    = h3cliOnError,
};

// The test certificate names cxtest.invalid and the fixture dials 127.0.0.1, so the chain is
// genuine and the name it carries is not the one being asked for. That one defect is forgiven and
// nothing else is: the CA signature and the validity dates still decide whether the handshake
// succeeds. MBEDTLS_X509_BADCERT_CN_MISMATCH is 0x04, spelled out here rather than reached for
// through <cxtls_mbed.h> for one constant.
#define H3T_BADCERT_CN_MISMATCH 0x04u

static bool h3tForgiveName(void* crt, int32 depth, uint32* flags, void* ctx)
{
    unused_noeval(crt);
    unused_noeval(depth);
    unused_noeval(ctx);

    *flags &= ~H3T_BADCERT_CN_MISMATCH;
    return true;
}

typedef struct H3Loop {
    TlsTestPKI pki;
    TlsCAStore* ca;
    TlsCreds* creds;
    TlsConfig* scfg;    // the QUIC listener's
    TlsConfig* stcp;    // the TCP listener's, when there is one
    TlsConfig* ccfg;
    NetQueue* q;
    HttpServer* srv;
    HttpClient* cl;
    bool threaded;   // the queue drives itself, so this fixture must not tick it
} H3Loop;

static void h3LoopDestroy(_Inout_ H3Loop* f)
{
    if (f->cl)
        httpclientCloseIdle(f->cl);
    objRelease(&f->cl);

    if (f->srv)
        httpserverShutdown(f->srv);
    objRelease(&f->srv);

    if (f->q) {
        // Give a polled queue the ticks it needs to carry the closes above out to the peer. A
        // threaded one has workers doing that already, and tick() is for a queue that has none.
        if (!f->threaded) {
            for (int i = 0; i < 40; i++)
                netqueueTick(f->q, 0);
        }
        netqueueShutdown(f->q, timeS(2));
    }
    objRelease(&f->q);

    objRelease(&f->ccfg);
    objRelease(&f->scfg);
    objRelease(&f->stcp);
    objRelease(&f->creds);
    objRelease(&f->ca);
    tlsTestPKIDestroy(&f->pki);
}

static bool h3LoopInitN(_Inout_ H3Loop* f, _In_ const HttpServerHandlers* handlers,
                        _In_opt_ void* ctx, int nthreads)
{
    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    f->creds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    f->scfg  = f->creds ? tlsconfigCreateServer(f->creds) : NULL;
    f->ccfg  = tlsconfigCreateClient();
    if (!f->scfg || !f->ccfg)
        return false;
    tlsconfigSetCA(f->ccfg, f->ca);
    tlsconfigSetVerifyCallback(f->ccfg, h3tForgiveName, NULL);

    NetQueueConfig conf;
    netqueuePresetServer(&conf);
    conf.nthreads = nthreads;
    f->threaded   = nthreads > 0;
    f->q          = netqueueCreate(&conf);
    if (!f->q)
        return false;

    f->srv = httpserverCreate(f->q);
    if (!f->srv)
        return false;
    httpserverSetHandlers(f->srv, handlers, ctx);

    NetAddr la;
    netAddrFromStr(&la, _SL("127.0.0.1"));
    la.port = 0;

    if (!httpserverListenQuic(f->srv, &la, f->scfg))
        return false;

    f->cl = httpclientCreate(f->q);
    if (!f->cl)
        return false;

    httpclientSetTlsConfig(f->cl, f->ccfg);
    return httpclientSetVersions(f->cl, HTTPV_Http3);
}

// Polled, which is what every test that drives the queue itself wants. A test that needs real
// workers asks for them.
static bool h3LoopInit(_Inout_ H3Loop* f, _In_ const HttpServerHandlers* handlers,
                       _In_opt_ void* ctx)
{
    return h3LoopInitN(f, handlers, ctx, 0);
}

// The URL the fixture's server answers on. Dialled by name so the certificate, which carries
// TLS_TEST_HOSTNAME and nothing else, is what proves the handshake reached the right listener --
// and the name resolves to loopback through the test host's own resolver.
static void h3LoopUrl(_Inout_ strhandle out, _In_ H3Loop* f, _In_opt_ strref path)
{
    string port = 0;
    strFromUInt32(&port, httpserverPort(f->srv), 10);
    strNConcat(out, _SL("https://127.0.0.1:"), port, path);
    strDestroy(&port);
}

int test_http3test_clientget(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    H3CliRec cli = { 0 };
    string url   = 0;
    HttpRequest* req = NULL;

    if (!h3LoopInit(&f, &kH3SrvHandlers, &rec)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    h3LoopUrl(&url, &f, _SL("/client"));
    req = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("request created", req != NULL);
    CHECK_TRUE("request sent", httpclientSend(f.cl, req, &kH3CliHandlers, &cli));

    for (int i = 0; i < 1500 && !(cli.completed || cli.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("failures", cli.failed, 0);
    CHECK_U64("completions", cli.completed, 1);
    CHECK_U64("status", cli.status, 200);
    CHECK_U64("version", cli.version, HTTPVER_3);
    CHECK_STR("body", cli.body, _SL("hello from http/3 /client"));
    CHECK_U64("server saw it", rec.requests, 1);
    CHECK_STR("path", rec.lastPath, _SL("/client"));

out:
    objRelease(&req);
    strDestroy(&url);
    strDestroy(&cli.body);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

int test_http3test_clientpool(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    string url   = 0;
    HttpRequest* reqs[8] = { 0 };
    H3CliRec recs[8]     = { 0 };

    if (!h3LoopInit(&f, &kH3SrvHandlers, &rec)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    // One request first, so that a connection for this origin exists. Eight concurrent requests
    // against a *cold* origin is a different question -- they would each dial -- and it is dial
    // coalescing that answers it.
    {
        H3CliRec warm = { 0 };
        h3LoopUrl(&url, &f, _SL("/warm"));
        HttpRequest* first = httprequestCreate(HTTP_Get, url);
        bool ok            = first && httpclientSend(f.cl, first, &kH3CliHandlers, &warm);

        for (int i = 0; i < 1500 && ok && !(warm.completed || warm.failed); i++)
            netqueueTick(f.q, timeMS(5));

        ok = ok && warm.completed == 1;
        objRelease(&first);
        strDestroy(&warm.body);

        if (!ok) {
            TEST_FAILV(ret, 1, _SL("the first request did not complete"), stvNone);
            goto out;
        }
    }

    // Now eight at once. An HTTP/1.1 pool would end with eight connections, because each carries
    // one request at a time; the HTTP/3 pool hands the same connection to all eight, which is the
    // whole reason its entry is borrowed rather than taken.
    for (uint32 i = 0; i < 8; i++) {
        string n = 0;
        strFromUInt64(&n, i, 10);
        strClear(&url);
        string path = 0;
        strNConcat(&path, _SL("/p/"), n);
        h3LoopUrl(&url, &f, path);
        strDestroy(&path);
        strDestroy(&n);

        reqs[i] = httprequestCreate(HTTP_Get, url);
        if (!reqs[i] || !httpclientSend(f.cl, reqs[i], &kH3CliHandlers, &recs[i])) {
            TEST_FAILV(ret, 1, _SL("request ${int} could not be sent"), stvar(int32, (int32)i));
            goto out;
        }
    }

    for (int i = 0; i < 2000; i++) {
        int32 done = 0;
        for (uint32 j = 0; j < 8; j++)
            done += recs[j].completed + recs[j].failed;
        if (done == 8)
            break;
        netqueueTick(f.q, timeMS(5));
    }

    for (uint32 i = 0; i < 8; i++) {
        string n = 0;
        strFromUInt64(&n, i, 10);
        string want = 0;
        strNConcat(&want, _SL("hello from http/3 /p/"), n);

        bool ok = recs[i].completed == 1 && recs[i].status == 200 && strEq(recs[i].body, want);
        strDestroy(&want);
        strDestroy(&n);

        if (!ok) {
            TEST_FAILV(ret, 1, _SL("request ${int}: completed ${int}, status ${int}, body '${string}'"),
                       stvar(int32, (int32)i), stvar(int32, recs[i].completed),
                       stvar(int32, (int32)recs[i].status), stvar(strref, recs[i].body));
            goto out;
        }
    }

    CHECK_U64("requests served", rec.requests, 9);

    // One connection carried all nine.
    CHECK_U64("connections dialled", saSize(f.cl->h3pool), 1);

out:
    for (uint32 i = 0; i < 8; i++) {
        objRelease(&reqs[i]);
        strDestroy(&recs[i].body);
    }
    strDestroy(&url);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

int test_http3test_clientbody(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    H3CliRec cli = { 0 };
    string url   = 0;
    string big   = 0;
    HttpRequest* req = NULL;

    // A request body larger than the stream's initial window, and a response the same, so both
    // directions have to survive being stopped and started again.
    const size_t kBig = 300 * 1024;
    rec.bigReply      = kBig;

    if (!h3LoopInit(&f, &kH3SrvHandlers, &rec)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    {
        uint8* buf = xaAlloc(kBig);
        h3tFillPattern(buf, 0, kBig);
        _httpAppendBytes(&big, buf, kBig);
        xaFree(buf);
    }

    h3LoopUrl(&url, &f, _SL("/upload"));
    req = httprequestCreate(HTTP_Post, url);
    CHECK_TRUE("request created", req != NULL);
    CHECK_TRUE("body set", httprequestSetBody(req, big, _SL("application/octet-stream")));

    // The default response cap is smaller than this body, so the request has to raise it.
    req->maxBody = kBig * 2;

    CHECK_TRUE("request sent", httpclientSend(f.cl, req, &kH3CliHandlers, &cli));

    for (int i = 0; i < 3000 && !(cli.completed || cli.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("failures", cli.failed, 0);
    CHECK_U64("status", cli.status, 200);
    CHECK_TRUE("request body byte for byte", h3tCheckPattern(rec.lastBody, kBig));
    CHECK_TRUE("response body byte for byte", h3tCheckPattern(cli.body, kBig));

out:
    objRelease(&req);
    strDestroy(&big);
    strDestroy(&url);
    strDestroy(&cli.body);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

int test_http3test_cancel(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3Deferred d = { 0 };
    H3CliRec one = { 0 };
    H3CliRec two = { 0 };
    string url   = 0;
    HttpRequest* held  = NULL;
    HttpRequest* other = NULL;

    // The server holds the first request without answering, so there is something to cancel.
    if (!h3LoopInit(&f, &kH3DeferredHandlers, &d)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    h3LoopUrl(&url, &f, _SL("/held"));
    held = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("first request", held != NULL);
    CHECK_TRUE("first sent", httpclientSend(f.cl, held, &kH3CliHandlers, &one));

    for (int i = 0; i < 1000 && !d.held; i++)
        netqueueTick(f.q, timeMS(5));
    CHECK_TRUE("the server has it", d.held != NULL);

    CHECK_TRUE("cancelled", httprequestCancel(held));

    for (int i = 0; i < 500 && !(one.completed || one.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("the cancelled request failed", one.failed, 1);
    CHECK_U64("and said why", one.err, HTTPERR_Aborted);

    // Let go of the abandoned one so that the next request the server holds is unambiguous.
    objRelease(&d.held);

    // Cancelling one stream disturbs no other stream on the connection, which is the difference
    // from HTTP/1.1: there, abandoning a response leaves the connection unusable.
    strClear(&url);
    h3LoopUrl(&url, &f, _SL("/after"));
    other = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("second request", other != NULL);
    CHECK_TRUE("second sent", httpclientSend(f.cl, other, &kH3CliHandlers, &two));

    for (int i = 0; i < 1000 && !(two.completed || two.failed); i++) {
        // The second request is held too, so it is answered from here rather than by a handler.
        if (d.held)
            httpsrvreqRespond(d.held, _SL("second"), _SL("text/plain"));
        netqueueTick(f.q, timeMS(5));
    }

    CHECK_U64("the second request completed", two.completed, 1);
    CHECK_U64("status", two.status, 200);
    CHECK_STR("body", two.body, _SL("second"));

out:
    objRelease(&d.held);
    objRelease(&held);
    objRelease(&other);
    strDestroy(&url);
    strDestroy(&one.body);
    strDestroy(&two.body);
    h3LoopDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Falling back
// ---------------------------------------------------------------------------------------------

// The loopback fixture with both listeners up, so a client under HTTPV_Any has two ways in.
static bool h3BothInit(_Inout_ H3Loop* f, _In_ const HttpServerHandlers* handlers,
                       _In_opt_ void* ctx, bool quic, _Inout_ uint16* port)
{
    if (!tlsTestPKIInit(&f->pki))
        return false;

    f->ca = tlscastoreCreate();
    if (!f->ca || !tlscastoreAddPEM(f->ca, f->pki.caCert))
        return false;

    f->creds = tlscredsCreatePEM(f->pki.serverCert, f->pki.serverKey, NULL);
    f->scfg  = f->creds ? tlsconfigCreateServer(f->creds) : NULL;
    f->ccfg  = tlsconfigCreateClient();
    if (!f->scfg || !f->ccfg)
        return false;
    tlsconfigSetCA(f->ccfg, f->ca);
    tlsconfigSetVerifyCallback(f->ccfg, h3tForgiveName, NULL);

    NetQueueConfig conf;
    netqueuePresetServer(&conf);
    conf.nthreads = 0;
    f->q          = netqueueCreate(&conf);
    if (!f->q)
        return false;

    f->srv = httpserverCreate(f->q);
    if (!f->srv)
        return false;
    httpserverSetHandlers(f->srv, handlers, ctx);

    NetAddr la;
    netAddrFromStr(&la, _SL("127.0.0.1"));
    la.port = 0;

    // A TlsConfig per listener, sharing the one TlsCreds: the two protocols need different ALPN
    // lists and an ALPN list belongs to a configuration.
    f->stcp = tlsconfigCreateServer(f->creds);
    if (!f->stcp)
        return false;

    // QUIC first, so its port is the one the TCP listener is asked for. A UDP socket and a TCP
    // socket on the same number are the arrangement a real deployment has.
    if (quic) {
        if (!httpserverListenQuic(f->srv, &la, f->scfg))
            return false;
        la.port = httpserverPort(f->srv);
    }

    if (!httpserverListenTls(f->srv, &la, 4, f->stcp))
        return false;

    *port = quic ? la.port : httpserverPort(f->srv);

    f->cl = httpclientCreate(f->q);
    if (!f->cl)
        return false;

    httpclientSetTlsConfig(f->cl, f->ccfg);
    return httpclientSetVersions(f->cl, HTTPV_Any);
}

static void h3PortUrl(_Inout_ strhandle out, uint16 port, _In_opt_ strref path)
{
    string p = 0;
    strFromUInt32(&p, port, 10);
    strNConcat(out, _SL("https://127.0.0.1:"), p, path);
    strDestroy(&p);
}

int test_http3test_race(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    H3CliRec one = { 0 };
    H3CliRec two = { 0 };
    string url   = 0;
    uint16 port  = 0;
    HttpRequest* a = NULL;
    HttpRequest* b = NULL;

    if (!h3BothInit(&f, &kH3SrvHandlers, &rec, true, &port)) {
        TEST_FAILV(ret, 1, _SL("the fixture did not come up"), stvNone);
        goto out;
    }

    // Both transports are dialled at once and the first to become usable wins. Over loopback that
    // is HTTP/3: QUIC's handshake is one round trip where TCP plus TLS is two.
    h3PortUrl(&url, port, _SL("/raced"));
    a = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("first request", a != NULL);
    CHECK_TRUE("first sent", httpclientSend(f.cl, a, &kH3CliHandlers, &one));

    for (int i = 0; i < 2000 && !(one.completed || one.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("failures", one.failed, 0);
    CHECK_U64("status", one.status, 200);
    CHECK_U64("HTTP/3 won", one.version, HTTPVER_3);

    // And the outcome was remembered, so the second request does not race: it goes straight to
    // HTTP/3 on the connection the first one left behind.
    CHECK_U64("one origin remembered", saSize(f.cl->originKeys), 1);
    CHECK_U64("remembered as HTTP/3", f.cl->originFlags.a[0], 1);

    strClear(&url);
    h3PortUrl(&url, port, _SL("/again"));
    b = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("second request", b != NULL);
    CHECK_TRUE("second sent", httpclientSend(f.cl, b, &kH3CliHandlers, &two));

    for (int i = 0; i < 2000 && !(two.completed || two.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("second completed", two.completed, 1);
    CHECK_U64("second was HTTP/3", two.version, HTTPVER_3);
    CHECK_U64("still one connection", saSize(f.cl->h3pool), 1);

    // Nothing was pooled over TCP, because the TCP racer never carried a request.
    CHECK_U64("no HTTP/1.1 connection", saSize(f.cl->pool), 0);

out:
    objRelease(&a);
    objRelease(&b);
    strDestroy(&url);
    strDestroy(&one.body);
    strDestroy(&two.body);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

int test_http3test_racefail(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    H3CliRec cli = { 0 };
    string url   = 0;
    uint16 port  = 0;
    HttpRequest* req = NULL;

    // No QUIC listener at all, so the UDP half of the race goes nowhere. One racer failing is not
    // a failure -- the other one is still coming, and the request is answered over HTTP/1.1
    // without waiting for the QUIC dial to give up.
    if (!h3BothInit(&f, &kH3SrvHandlers, &rec, false, &port)) {
        TEST_FAILV(ret, 1, _SL("the fixture did not come up"), stvNone);
        goto out;
    }

    h3PortUrl(&url, port, _SL("/tcponly"));
    req = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("request created", req != NULL);
    CHECK_TRUE("request sent", httpclientSend(f.cl, req, &kH3CliHandlers, &cli));

    for (int i = 0; i < 2000 && !(cli.completed || cli.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("failures", cli.failed, 0);
    CHECK_U64("completions", cli.completed, 1);
    CHECK_U64("status", cli.status, 200);
    CHECK_U64("HTTP/1.1 answered", cli.version, HTTPVER_1_1);
    CHECK_STR("body", cli.body, _SL("hello from http/3 /tcponly"));

out:
    objRelease(&req);
    strDestroy(&url);
    strDestroy(&cli.body);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

int test_http3test_coalesce(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    string url   = 0;
    HttpRequest* reqs[8] = { 0 };
    H3CliRec recs[8]     = { 0 };

    if (!h3LoopInit(&f, &kH3SrvHandlers, &rec)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    // Eight requests to an origin nothing is known about, all before any connection exists. Without
    // coalescing each would dial its own; with it, seven wait for the first and all eight end up on
    // one connection.
    for (uint32 i = 0; i < 8; i++) {
        string n = 0;
        strFromUInt64(&n, i, 10);
        string path = 0;
        strNConcat(&path, _SL("/c/"), n);
        strClear(&url);
        h3LoopUrl(&url, &f, path);
        strDestroy(&path);
        strDestroy(&n);

        reqs[i] = httprequestCreate(HTTP_Get, url);
        if (!reqs[i] || !httpclientSend(f.cl, reqs[i], &kH3CliHandlers, &recs[i])) {
            TEST_FAILV(ret, 1, _SL("request ${int} could not be sent"), stvar(int32, (int32)i));
            goto out;
        }
    }

    for (int i = 0; i < 2500; i++) {
        int32 done = 0;
        for (uint32 j = 0; j < 8; j++)
            done += recs[j].completed + recs[j].failed;
        if (done == 8)
            break;
        netqueueTick(f.q, timeMS(5));
    }

    for (uint32 i = 0; i < 8; i++) {
        if (recs[i].completed != 1 || recs[i].status != 200) {
            TEST_FAILV(ret, 1, _SL("request ${int}: completed ${int}, status ${int}"),
                       stvar(int32, (int32)i), stvar(int32, recs[i].completed),
                       stvar(int32, (int32)recs[i].status));
            goto out;
        }
    }

    CHECK_U64("requests served", rec.requests, 8);
    CHECK_U64("one connection for all eight", saSize(f.cl->h3pool), 1);

out:
    for (uint32 i = 0; i < 8; i++) {
        objRelease(&reqs[i]);
        strDestroy(&recs[i].body);
    }
    strDestroy(&url);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

// A server that says goodbye as soon as it has taken one request, then answers nothing: exactly
// what the last moments of a graceful restart look like.
typedef struct H3Goaway {
    int32 seen;
    HttpServerRequest* first;
} H3Goaway;

static void h3tOnRequestGoaway(HttpServerEvent* ev)
{
    H3Goaway* g = (H3Goaway*)ev->ctx;
    g->seen++;

    if (g->seen == 1) {
        // Held rather than answered, so the request is still in flight when the connection says it
        // will not be serving it.
        g->first = objAcquire(ev->request);
        return;
    }

    httpsrvreqRespond(ev->request, _SL("served after the retry"), _SL("text/plain"));
}

static const HttpServerHandlers kH3GoawayHandlers = {
    .request = h3tOnRequestGoaway,
};

int test_http3test_goawayretry(void)
{
    int ret       = 0;
    H3Loop f      = { 0 };
    H3Goaway g    = { 0 };
    H3CliRec cli  = { 0 };
    string url    = 0;
    HttpRequest* req = NULL;

    if (!h3LoopInit(&f, &kH3GoawayHandlers, &g)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    h3LoopUrl(&url, &f, _SL("/restarting"));
    req = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("request created", req != NULL);
    CHECK_TRUE("request sent", httpclientSend(f.cl, req, &kH3CliHandlers, &cli));

    for (int i = 0; i < 1500 && !g.first; i++)
        netqueueTick(f.q, timeMS(5));
    CHECK_TRUE("the server has it", g.first != NULL);

    // Say goodbye naming stream 0, which is the one this request is on, and reset it. The peer has
    // promised the request was not processed, so retrying it cannot repeat anything.
    {
        sa_object conns;
        saInit(&conns, object, 2);
        withMutex (&f.srv->lock) {
            htiter hti;
            htiInit(&hti, f.srv->connections);
            while (htiValid(&hti)) {
                saPush(&conns, object, htiVal(object, hti));
                htiNext(&hti);
            }
            htiFinish(&hti);
        }

        for (int32 i = 0; i < saSize(conns); i++) {
            Http3ServerConn* h3 = objDynCast(Http3ServerConn, conns.a[i]);
            if (h3) {
                h3srvconnGoaway(h3, 0);
                netquicReset(g.first->h3flow, H3ERR_REQUEST_REJECTED);
                netquicStopSending(g.first->h3flow, H3ERR_REQUEST_REJECTED);
            }
        }
        saDestroy(&conns);
    }

    for (int i = 0; i < 2500 && !(cli.completed || cli.failed); i++)
        netqueueTick(f.q, timeMS(5));

    // The request came back on a second connection and was served there. Without the retry it
    // would have failed, which is what a rolling restart would look like to every client that
    // happened to be mid-request.
    CHECK_U64("failures", cli.failed, 0);
    CHECK_U64("completions", cli.completed, 1);
    CHECK_U64("status", cli.status, 200);
    CHECK_STR("body", cli.body, _SL("served after the retry"));
    CHECK_U64("the server saw it twice", g.seen, 2);

out:
    objRelease(&g.first);
    objRelease(&req);
    strDestroy(&url);
    strDestroy(&cli.body);
    h3LoopDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Alternative services
// ---------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------
// Many streams at once, on real worker threads
// ---------------------------------------------------------------------------------------------

#define H3PAR_STREAMS 48
#define H3PAR_BODY    (64 * 1024)

typedef struct H3Par {
    Semaphore done;         // one increment per request that ended, however it ended
    atomic(uint32) bad;     // requests whose body was not what was asked for
    atomic(uint32) failed;
} H3Par;

// One request's arriving body, checked as it comes rather than kept. Each request has its own, and
// only the flow's worker ever touches it, so nothing here needs guarding.
typedef struct H3ParReq {
    H3Par* par;
    uint64 got;
    bool bad;
} H3ParReq;

// The response body is generated per request rather than shared, because several workers are
// answering at once and a string handed to respond() is consumed by the connection that took it.
static void h3parOnRequest(HttpServerEvent* ev)
{
    unused_noeval(ev->ctx);

    uint8* buf = xaAlloc(H3PAR_BODY);
    h3tFillPattern(buf, 0, H3PAR_BODY);

    string body = 0;
    _httpAppendBytes(&body, buf, H3PAR_BODY);
    xaFree(buf);

    httpsrvreqRespond(ev->request, body, _SL("application/octet-stream"));
    strDestroy(&body);
}

static const HttpServerHandlers kH3ParSrvHandlers = {
    .request = h3parOnRequest,
};

static void h3parOnData(HttpEvent* ev)
{
    H3ParReq* r = (H3ParReq*)ev->ctx;

    for (size_t i = 0; i < ev->len; i++) {
        if (ev->data[i] != h3tPatternByte(r->got + i)) {
            r->bad = true;
            break;
        }
    }
    r->got += ev->len;
}

static void h3parOnComplete(HttpEvent* ev)
{
    H3ParReq* r = (H3ParReq*)ev->ctx;

    if (r->bad || r->got != H3PAR_BODY || ev->status != 200)
        atomicFetchAdd(uint32, &r->par->bad, 1, AcqRel);
    semaInc(&r->par->done, 1);
}

static void h3parOnError(HttpEvent* ev)
{
    H3ParReq* r = (H3ParReq*)ev->ctx;
    unused_noeval(ev);

    atomicFetchAdd(uint32, &r->par->failed, 1, AcqRel);
    semaInc(&r->par->done, 1);
}

static const HttpHandlers kH3ParCliHandlers = {
    .data     = h3parOnData,
    .complete = h3parOnComplete,
    .error    = h3parOnError,
};

// Every other loopback test here runs on a polled queue, where one thread does everything in turn
// and two streams of a connection never move at the same moment. This one asks for real workers,
// because some of what a connection carries is shared by all of its streams and only a second
// worker can be holding it.
//
// What it catches: a QUIC connection's send window belongs to the connection, not to a stream, so
// a stream that measures the room it has and then writes in two parts can find the room gone
// between them. A frame written half out is not a failure the peer can see -- a DATA header
// without its payload leaves the peer reading whatever is written next as body bytes. Enough
// streams and enough bytes to keep that window contended is what makes it happen.
int test_http3test_threaded(void)
{
    int ret   = 0;
    H3Loop f  = { 0 };
    H3Par par = { 0 };
    string url = 0;
    HttpRequest* reqs[H3PAR_STREAMS] = { 0 };
    H3ParReq recs[H3PAR_STREAMS]     = { 0 };

    semaInit(&par.done, 0);

    if (!h3LoopInitN(&f, &kH3ParSrvHandlers, NULL, 4)) {
        TEST_FAILV(ret, 1, _SL("the loopback fixture did not come up"), stvNone);
        goto out;
    }

    h3LoopUrl(&url, &f, _SL("/parallel"));

    // One request first, so the rest share the connection it opens instead of each dialling one of
    // their own -- streams on one connection are what this is about.
    {
        H3ParReq warm     = { .par = &par };
        HttpRequest* req  = httprequestCreate(HTTP_Get, url);
        bool ok           = req && httpclientSend(f.cl, req, &kH3ParCliHandlers, &warm);
        ok                = ok && semaTryDecTimeout(&par.done, timeS(10));
        objRelease(&req);

        if (!ok) {
            TEST_FAILV(ret, 1, _SL("the first request did not complete"), stvNone);
            goto out;
        }
    }

    atomicStore(uint32, &par.bad, 0, Release);
    atomicStore(uint32, &par.failed, 0, Release);

    for (uint32 i = 0; i < H3PAR_STREAMS; i++) {
        recs[i].par = &par;
        reqs[i]     = httprequestCreate(HTTP_Get, url);
        if (!reqs[i] || !httpclientSend(f.cl, reqs[i], &kH3ParCliHandlers, &recs[i])) {
            TEST_FAILV(ret, 1, _SL("request ${int} would not start"), stvar(int32, (int32)i));
            goto out;
        }
    }

    for (uint32 i = 0; i < H3PAR_STREAMS; i++) {
        if (!semaTryDecTimeout(&par.done, timeS(30))) {
            TEST_FAILV(ret, 1, _SL("only ${int} of ${int} requests finished"),
                       stvar(int32, (int32)i), stvar(int32, H3PAR_STREAMS));
            goto out;
        }
    }

    CHECK_U64("requests that failed", atomicLoad(uint32, &par.failed, Acquire), 0);
    CHECK_U64("bodies that were not what was asked for", atomicLoad(uint32, &par.bad, Acquire), 0);

out:
    for (uint32 i = 0; i < H3PAR_STREAMS; i++)
        objRelease(&reqs[i]);
    strDestroy(&url);
    h3LoopDestroy(&f);
    semaDestroy(&par.done);
    return ret;
}

int test_http3test_altsvc(void)
{
    int ret = 0;

    // Field values a real server sends, and the ones a parser has to refuse without guessing.
    static const struct {
        const char* what;
        const char* value;
        bool found;
        bool clear;
        const char* host;
        uint16 port;
        int64 maxAgeSecs;
    } tv[] = {
        // The overwhelmingly common form: same host, its own port, a day.
        { "plain", "h3=\":443\"; ma=86400", true, false, "", 443, 86400 },

        // No ma at all, which RFC 7838 says means a day.
        { "no max-age", "h3=\":443\"", true, false, "", 443, 86400 },

        // A list, with the wanted protocol behind one that is not.
        { "later in the list", "h3-29=\":443\"; ma=3600, h3=\":8443\"; ma=60", true, false, "",
          8443, 60 },

        // A host of its own, which is a different endpoint rather than this one on another port.
        { "other host", "h3=\"alt.example.com:443\"", true, false, "alt.example.com", 443, 86400 },

        // A bracketed IPv6 literal, whose colons are why the split is at the last one.
        { "ipv6 literal", "h3=\"[2001:db8::1]:443\"", true, false, "2001:db8::1", 443, 86400 },

        // Parameters this endpoint does not implement are skipped rather than tripped over.
        { "unknown params", "h3=\":443\"; persist=1; ma=120; x=\"y;z\"", true, false, "", 443,
          120 },

        // Whitespace wherever the grammar allows it.
        { "spacing", "  h3 = \":443\" ;  ma = 30 ", true, false, "", 443, 30 },

        // clear asks that every alternative be forgotten, and is the whole field value.
        { "clear", "clear", true, true, "", 0, 0 },

        // Nothing here advertises HTTP/3.
        { "other protocols", "h2=\":443\"; ma=3600", false, false, "", 0, 0 },
        { "empty", "", false, false, "", 0, 0 },

        // Malformed: an unterminated quoted string, and an authority with no port.
        { "unterminated", "h3=\":443", false, false, "", 0, 0 },
        { "no port", "h3=\"example.com\"", false, false, "", 0, 0 },
        { "bad port", "h3=\":notanumber\"", false, false, "", 0, 0 },

        // `clear` only means clear on its own; as one entry of a list it is just a protocol-id
        // with no alternative, which is malformed.
        { "clear in a list", "clear, h3=\":443\"", false, false, "", 0, 0 },
    };

    for (size_t i = 0; i < sizeof(tv) / sizeof(tv[0]); i++) {
        HttpAltSvc alt;
        bool got = _httpAltSvcFind(&alt, (strref)tv[i].value, _SL("h3"));

        bool ok = got == tv[i].found;
        if (ok && got && !tv[i].clear) {
            ok = !alt.clear && alt.port == tv[i].port &&
                 strEq(alt.host, (strref)tv[i].host) &&
                 alt.maxAge == timeS(tv[i].maxAgeSecs);
        } else if (ok && got) {
            ok = alt.clear;
        }

        if (!ok) {
            TEST_FAILV(ret, 1,
                       _SL("${string}: found ${int}, clear ${int}, host '${string}', port ${int}, "
                           "ma ${int}"),
                       stvar(strref, (strref)tv[i].what), stvar(int32, (int32)got),
                       stvar(int32, (int32)alt.clear), stvar(strref, alt.host),
                       stvar(int32, (int32)alt.port),
                       stvar(int64, alt.maxAge / timeS(1)));
        }

        if (got)
            _httpAltSvcDestroy(&alt);

        if (ret != 0)
            break;
    }

    return ret;
}

int test_http3test_altsvcseed(void)
{
    int ret      = 0;
    H3Loop f     = { 0 };
    H3SrvRec rec = { 0 };
    H3CliRec one = { 0 };
    H3CliRec two = { 0 };
    string url   = 0;
    uint16 port  = 0;
    HttpRequest* a = NULL;
    HttpRequest* b = NULL;

    if (!h3BothInit(&f, &kH3SrvHandlers, &rec, true, &port)) {
        TEST_FAILV(ret, 1, _SL("the fixture did not come up"), stvNone);
        goto out;
    }

    // HTTP/1.1 only, so nothing races and nothing is remembered from a QUIC dial. The only way the
    // client can learn about HTTP/3 here is the Alt-Svc header the server sends -- which
    // httpserverListenQuic() filled in by itself from the port it bound.
    CHECK_TRUE("policy set", httpclientSetVersions(f.cl, HTTPV_Http1));

    h3PortUrl(&url, port, _SL("/first"));
    a = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("first request", a != NULL);
    CHECK_TRUE("first sent", httpclientSend(f.cl, a, &kH3CliHandlers, &one));

    for (int i = 0; i < 2000 && !(one.completed || one.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("first completed", one.completed, 1);
    CHECK_U64("over HTTP/1.1", one.version, HTTPVER_1_1);

    // The header seeded the table, so the origin is now known to speak HTTP/3.
    CHECK_U64("origin learned", saSize(f.cl->originKeys), 1);
    CHECK_U64("learned as HTTP/3", f.cl->originFlags.a[0], 1);

    // Under HTTPV_Default that is enough on its own: the second request goes over HTTP/3 without
    // racing and without anything else having told the client it could.
    CHECK_TRUE("default policy", httpclientSetVersions(f.cl, HTTPV_Default));

    strClear(&url);
    h3PortUrl(&url, port, _SL("/second"));
    b = httprequestCreate(HTTP_Get, url);
    CHECK_TRUE("second request", b != NULL);
    CHECK_TRUE("second sent", httpclientSend(f.cl, b, &kH3CliHandlers, &two));

    for (int i = 0; i < 2000 && !(two.completed || two.failed); i++)
        netqueueTick(f.q, timeMS(5));

    CHECK_U64("second completed", two.completed, 1);
    CHECK_U64("second was HTTP/3", two.version, HTTPVER_3);
    CHECK_STR("second body", two.body, _SL("hello from http/3 /second"));

out:
    objRelease(&a);
    objRelease(&b);
    strDestroy(&url);
    strDestroy(&one.body);
    strDestroy(&two.body);
    h3SrvRecDestroy(&rec);
    h3LoopDestroy(&f);
    return ret;
}

testfunc http3test_funcs[] = {
    { "wire", test_http3test_wire },
    { "wirefuzz", test_http3test_wirefuzz },
    { "qpackhuff", test_http3test_qpackhuff },
    { "qpackvec", test_http3test_qpackvec },
    { "qpackdynamic", test_http3test_qpackdynamic },
    { "settings", test_http3test_settings },
    { "goaway", test_http3test_goaway },
    { "badstream", test_http3test_badstream },
    { "roundtrip", test_http3test_roundtrip },
    { "srvpost", test_http3test_srvpost },
    { "concurrent", test_http3test_concurrent },
    { "bodies", test_http3test_bodies },
    { "forbidden", test_http3test_forbidden },
    { "trailers", test_http3test_trailers },
    { "interim", test_http3test_interim },
    { "backpressure", test_http3test_backpressure },
    { "reset", test_http3test_reset },
    { "deferred", test_http3test_deferred },
    { "clientget", test_http3test_clientget },
    { "clientpool", test_http3test_clientpool },
    { "clientbody", test_http3test_clientbody },
    { "cancel", test_http3test_cancel },
    { "race", test_http3test_race },
    { "racefail", test_http3test_racefail },
    { "coalesce", test_http3test_coalesce },
    { "goawayretry", test_http3test_goawayretry },
    { "altsvc", test_http3test_altsvc },
    { "altsvcseed", test_http3test_altsvcseed },
    { "threaded", test_http3test_threaded },
    { NULL, NULL },
};

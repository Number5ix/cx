// Known-answer tests for the QUIC wire codec and packet protection.
//
// RFC 9000 publishes sample encodings for variable-length integers and packet numbers, and
// RFC 9001 Appendix A publishes complete protected Initial packets in both directions, a Retry
// packet, and a ChaCha20 short-header packet. Everything here is checked against those bytes
// rather than against cx itself, which is what makes the two layers testable apart from a
// connection: a packet that comes out byte-identical to the RFC's has the header encoding, the
// key derivation, the AEAD nonce and the header protection mask all correct at once.

#include <cxquic.h>

#include "../cxquic/quic_private.h"

#include "quicvec.h"

#define TEST_FILE  quictest
#define TEST_FUNCS quictest_funcs
#include "common.h"

// bool CHECK_BYTES(const char *what, const uint8 *got, size_t gotlen, const uint8 *want, size_t wantlen);
//
// Compares a computed byte string against a vector, logging the first differing offset and both
// values there. Assigns 1 to `ret` and jumps to `out` on a mismatch.
#define CHECK_BYTES(what, got, gotlen, want, wantlen)                                        \
    do {                                                                                     \
        if ((size_t)(gotlen) != (size_t)(wantlen)) {                                         \
            TEST_FAILV(ret, 1, _SL("${string}: length ${int}, expected ${int}"),             \
                       stvar(strref, _S what), stvar(int64, (int64)(gotlen)),                \
                       stvar(int64, (int64)(wantlen)));                                      \
            goto out;                                                                        \
        }                                                                                    \
        for (size_t _i = 0; _i < (size_t)(wantlen); _i++) {                                  \
            if ((got)[_i] != (want)[_i]) {                                                   \
                TEST_FAILV(ret, 1, _SL("${string}: byte ${int} is ${int}, expected ${int}"), \
                           stvar(strref, _S what), stvar(int64, (int64)_i),                  \
                           stvar(int32, (int32)(got)[_i]), stvar(int32, (int32)(want)[_i])); \
                goto out;                                                                    \
            }                                                                                \
        }                                                                                    \
    } while (0)

// bool CHECK_U64(const char *what, uint64 got, uint64 want);
#define CHECK_U64(what, got, want)                                                \
    do {                                                                          \
        if ((uint64)(got) != (uint64)(want)) {                                    \
            TEST_FAILV(ret, 1, _SL("${string} is ${int}, expected ${int}"),       \
                       stvar(strref, _S what), stvar(int64, (int64)(got)),      \
                       stvar(int64, (int64)(want)));                            \
            goto out;                                                             \
        }                                                                         \
    } while (0)

// bool CHECK_TRUE(const char *what, bool cond);
#define CHECK_TRUE(what, cond)                                                       \
    do {                                                                             \
        if (!(cond)) {                                                               \
            TEST_FAILV(ret, 1, _SL("check failed: ${string}"), stvar(strref, _S what));            \
            goto out;                                                                \
        }                                                                            \
    } while (0)

// The client's chosen Destination Connection ID, which every Initial secret in RFC 9001
// Appendix A is derived from.
static const uint8 tv_dcid[8] = { 0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08 };

// ---------------------------------------------------------------------------------------------
// Wire codec
// ---------------------------------------------------------------------------------------------

// The sample encodings in RFC 9000 section 16 and Appendix A.1, plus the boundaries between the
// four widths and the padded form a sender uses to reserve room for a length it does not know yet.
static int test_quictest_varint(void)
{
    int ret = 0;

    static const struct {
        const char* enc;
        uint8 len;
        uint64 val;
    } cases[] = {
        { "\xc2\x19\x7c\x5e\xff\x14\xe8\x8c", 8, UINT64_C(151288809941952652) },
        { "\x9d\x7f\x3e\x7d", 4, 494878333 },
        { "\x7b\xbd", 2, 15293 },
        { "\x25", 1, 37 },
        { "\x40\x25", 2, 37 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        QuicRd rd;
        _quicRdInit(&rd, (const uint8*)cases[i].enc, cases[i].len);
        uint64 got = _quicRdVarint(&rd);

        if (rd.bad || got != cases[i].val) {
            TEST_FAILV(ret, 1, _SL("varint ${int} decoded to ${int}, expected ${int}"),
                       stvar(int64, (int64)i), stvar(int64, (int64)got), stvar(int64, (int64)cases[i].val));
            goto out;
        }
        CHECK_U64("varint decode consumed", (uint64)(size_t)(rd.p - (const uint8*)cases[i].enc),
                  cases[i].len);
    }

    // Every case but the last is the shortest encoding of its value, so re-encoding reproduces it.
    for (size_t i = 0; i < 4; i++) {
        uint8 buf[8];
        QuicWr wr;
        _quicWrInit(&wr, buf, sizeof(buf));
        _quicWrVarint(&wr, cases[i].val);
        CHECK_TRUE("varint encode", !wr.bad);
        CHECK_BYTES("varint encode", buf, _quicWrLen(&wr), (const uint8*)cases[i].enc,
                    cases[i].len);
    }

    // The padded form: 37 written as two bytes is the fifth case above.
    {
        uint8 buf[8];
        QuicWr wr;
        _quicWrInit(&wr, buf, sizeof(buf));
        _quicWrVarintSized(&wr, 37, 2);
        CHECK_TRUE("padded varint encode", !wr.bad);
        CHECK_BYTES("padded varint", buf, _quicWrLen(&wr), (const uint8*)"\x40\x25", 2);
    }

    CHECK_U64("size(0)", _quicVarintSize(0), 1);
    CHECK_U64("size(63)", _quicVarintSize(63), 1);
    CHECK_U64("size(64)", _quicVarintSize(64), 2);
    CHECK_U64("size(16383)", _quicVarintSize(16383), 2);
    CHECK_U64("size(16384)", _quicVarintSize(16384), 4);
    CHECK_U64("size(1073741823)", _quicVarintSize(1073741823), 4);
    CHECK_U64("size(1073741824)", _quicVarintSize(1073741824), 8);
    CHECK_U64("size(max)", _quicVarintSize(QUIC_MAX_PACKET_NUMBER - 1), 8);
    CHECK_U64("size(too big)", _quicVarintSize(QUIC_MAX_PACKET_NUMBER), 0);

    // A width too small for the value, and a width that is not a power of two, are both refused
    // rather than silently truncating.
    {
        uint8 buf[8];
        QuicWr wr;
        _quicWrInit(&wr, buf, sizeof(buf));
        _quicWrVarintSized(&wr, 16384, 2);
        CHECK_TRUE("varint too narrow refused", wr.bad);

        _quicWrInit(&wr, buf, sizeof(buf));
        _quicWrVarintSized(&wr, 1, 3);
        CHECK_TRUE("varint odd width refused", wr.bad);
    }

    // A truncated encoding is a decode failure, not a short read.
    {
        QuicRd rd;
        _quicRdInit(&rd, (const uint8*)"\xc2\x19\x7c", 3);
        _quicRdVarint(&rd);
        CHECK_TRUE("truncated varint refused", rd.bad);
    }

out:
    return ret;
}

// The worked examples in RFC 9000 Appendix A.2 and A.3.
static int test_quictest_pn(void)
{
    int ret = 0;

    CHECK_U64("pn size 0xac5c02", _quicPnSize(0xac5c02, 0xabe8b3), 2);
    CHECK_U64("pn size 0xace8fe", _quicPnSize(0xace8fe, 0xabe8b3), 3);
    CHECK_U64("pn size first packet", _quicPnSize(0, QUIC_PN_NONE), 1);
    CHECK_U64("pn size 255 unacked", _quicPnSize(255, QUIC_PN_NONE), 2);
    CHECK_U64("pn size huge gap", _quicPnSize(UINT64_C(0x100000000), QUIC_PN_NONE), 4);

    CHECK_U64("pn decode 0x9b32", _quicPnDecode(0xa82f30ea, 0x9b32, 2), 0xa82f9b32);

    // A truncated number that appears to be behind what is expected is really ahead of it, and
    // one that appears far ahead is really behind. Both are resolved to whichever candidate is
    // nearest what was expected.
    CHECK_U64("pn decode wraps forward", _quicPnDecode(0xfe, 0x01, 1), 0x101);
    CHECK_U64("pn decode wraps back", _quicPnDecode(0x101, 0xfe, 1), 0xfe);
    CHECK_U64("pn decode no wrap", _quicPnDecode(0xff, 0x80, 1), 0x180);
    CHECK_U64("pn decode first packet", _quicPnDecode(QUIC_PN_NONE, 0, 1), 0);
    CHECK_U64("pn decode second packet", _quicPnDecode(0, 1, 1), 1);

    // Every width round trips across a window boundary, which is where the candidate selection
    // has to pick between three answers. The receiver is deliberately several packets behind, so
    // the expected number and the real one land on opposite sides of the boundary.
    for (uint64 pn = 0xfff0; pn < 0x10010; pn++) {
        uint8 n = _quicPnSize(pn, pn - 5);
        uint64 truncated = pn & ((UINT64_C(1) << (n * 8)) - 1);
        uint64 got = _quicPnDecode(pn - 5, truncated, n);
        if (got != pn) {
            TEST_FAILV(ret, 1, _SL("pn ${int} round tripped as ${int} in ${int} bytes"),
                       stvar(int64, (int64)pn), stvar(int64, (int64)got), stvar(int32, (int32)n));
            goto out;
        }
    }

    // The window a receiver resolves against reaches only half the encoding's width in each
    // direction, so a width has to cover twice the gap between what is being sent and what was
    // last acknowledged -- not the gap itself. The widths change one packet before the halfway
    // point, never at it.
    CHECK_U64("pn size at half a byte", _quicPnSize(1127, 1000), 1);
    CHECK_U64("pn size past half a byte", _quicPnSize(1128, 1000), 2);
    CHECK_U64("pn size at half two bytes", _quicPnSize(132767, 100000), 2);
    CHECK_U64("pn size past half two bytes", _quicPnSize(132768, 100000), 3);

    // A gap of any size round trips, since a burst of loss can open one of any size and the
    // receiver has acknowledged nothing since. Getting this wrong is silent: the number resolves
    // to some other packet, the packet fails its AEAD and is discarded without a word, and since
    // nothing acknowledges it the gap only grows.
    for (uint64 gap = 1; gap <= 3000000; gap = gap < 70000 ? gap + 1 : gap * 3) {
        uint64 acked = 4000000;
        uint64 pn = acked + gap;
        uint8 n = _quicPnSize(pn, acked);
        uint64 truncated = pn & ((UINT64_C(1) << (n * 8)) - 1);
        uint64 got = _quicPnDecode(acked, truncated, n);
        if (got != pn) {
            TEST_FAILV(ret, 1,
                       _SL("pn ${int} at a gap of ${int} round tripped as ${int} in ${int} bytes"),
                       stvar(int64, (int64)pn), stvar(int64, (int64)gap), stvar(int64, (int64)got),
                       stvar(int32, (int32)n));
            goto out;
        }
    }

out:
    return ret;
}

// Parsing the four packet shapes out of the RFC 9001 Appendix A packets, then writing the same
// headers back out and comparing them to the unprotected header bytes the RFC prints.
static int test_quictest_header(void)
{
    int ret = 0;
    QuicPktHdr h;
    uint8 buf[64];
    QuicWr wr;

    // A.2: the client Initial. Header protection only covers the low four bits of the first byte,
    // so the packet type is readable straight off the protected packet.
    CHECK_TRUE("decode client initial", _quicHdrDecode(&h, tv_cli_packet, sizeof(tv_cli_packet), 0));
    CHECK_U64("client initial type", h.type, QUIC_PKT_INITIAL);
    CHECK_U64("client initial version", h.version, QUIC_VERSION_1);
    CHECK_U64("client initial dcid len", h.dcid.len, sizeof(tv_dcid));
    CHECK_BYTES("client initial dcid", h.dcid.id, h.dcid.len, tv_dcid, sizeof(tv_dcid));
    CHECK_U64("client initial scid len", h.scid.len, 0);
    CHECK_U64("client initial token len", h.tokenLen, 0);
    CHECK_U64("client initial length", h.len, 1182);
    CHECK_U64("client initial pn offset", h.pnOff, 18);
    CHECK_U64("client initial packet len", h.pktLen, sizeof(tv_cli_packet));

    h.pnLen = 4;
    h.pnEnc = 2;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode client initial", _quicHdrEncode(&wr, &h));
    CHECK_U64("client initial header size", _quicHdrSize(&h), _quicWrLen(&wr));
    CHECK_BYTES("client initial header", buf, _quicWrLen(&wr), tv_cli_hdr, sizeof(tv_cli_hdr));

    // A.3: the server Initial, which has an empty destination and a fresh source connection ID.
    CHECK_TRUE("decode server initial", _quicHdrDecode(&h, tv_srv_packet, sizeof(tv_srv_packet), 0));
    CHECK_U64("server initial type", h.type, QUIC_PKT_INITIAL);
    CHECK_U64("server initial dcid len", h.dcid.len, 0);
    CHECK_U64("server initial scid len", h.scid.len, 8);
    CHECK_U64("server initial length", h.len, 117);
    CHECK_U64("server initial pn offset", h.pnOff, 18);
    CHECK_U64("server initial packet len", h.pktLen, sizeof(tv_srv_packet));

    h.pnLen = 2;
    h.pnEnc = 1;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode server initial", _quicHdrEncode(&wr, &h));
    CHECK_BYTES("server initial header", buf, _quicWrLen(&wr), tv_srv_hdr, sizeof(tv_srv_hdr));

    // The Length varint can be pinned wider than the value needs, which is what lets a sender
    // reserve room for a length it will not know until the packet is full.
    h.lenSize = 4;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode with pinned length", _quicHdrEncode(&wr, &h));
    CHECK_U64("pinned length header size", _quicWrLen(&wr), sizeof(tv_srv_hdr) + 2);
    CHECK_U64("pinned length header size matches", _quicHdrSize(&h), _quicWrLen(&wr));
    CHECK_BYTES("pinned length varint", buf + 16, 4, (const uint8*)"\x80\x00\x00\x75", 4);

    h.lenSize = 1;
    CHECK_U64("pinned length too narrow refused", _quicHdrSize(&h), 0);
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode with too narrow length refused", !_quicHdrEncode(&wr, &h));
    h.lenSize = 0;

    // A.4: a Retry packet, whose token runs to sixteen bytes before the end.
    CHECK_TRUE("decode retry", _quicHdrDecode(&h, tv_retry_packet, sizeof(tv_retry_packet), 0));
    CHECK_U64("retry type", h.type, QUIC_PKT_RETRY);
    CHECK_U64("retry dcid len", h.dcid.len, 0);
    CHECK_U64("retry scid len", h.scid.len, 8);
    CHECK_U64("retry token len", h.tokenLen, 5);
    CHECK_BYTES("retry token", h.token, h.tokenLen, (const uint8*)"token", 5);
    CHECK_BYTES("retry tag", h.tag, QUIC_TAG_LEN, tv_retry_packet + 20, QUIC_TAG_LEN);

    // A.5: a short header with an empty connection ID, which is the one shape whose length the
    // receiver has to know rather than read.
    CHECK_TRUE("decode short", _quicHdrDecode(&h, tv_cc_packet, sizeof(tv_cc_packet), 0));
    CHECK_U64("short type", h.type, QUIC_PKT_SHORT);
    CHECK_U64("short version", h.version, 0);
    CHECK_U64("short pn offset", h.pnOff, 1);
    CHECK_U64("short packet len", h.pktLen, sizeof(tv_cc_packet));
    CHECK_U64("short length", h.len, sizeof(tv_cc_packet) - 1);

    memset(&h, 0, sizeof(h));
    h.type = QUIC_PKT_SHORT;
    h.pnLen = 3;
    h.pnEnc = 0xbff4;
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode short", _quicHdrEncode(&wr, &h));
    CHECK_BYTES("short header", buf, _quicWrLen(&wr), tv_cc_hdr, sizeof(tv_cc_hdr));

    // A Version Negotiation packet, which carries no packet number and whose body is a list of
    // four-byte version numbers the caller appends after the header.
    memset(&h, 0, sizeof(h));
    h.type = QUIC_PKT_VERSIONNEG;
    h.dcid.len = 4;
    memcpy(h.dcid.id, "\x01\x02\x03\x04", 4);
    h.scid.len = 2;
    memcpy(h.scid.id, "\xaa\xbb", 2);
    _quicWrInit(&wr, buf, sizeof(buf));
    CHECK_TRUE("encode vneg", _quicHdrEncode(&wr, &h));
    _quicWr32(&wr, QUIC_VERSION_1);
    CHECK_TRUE("append vneg version", !wr.bad);

    QuicPktHdr vn;
    CHECK_TRUE("decode vneg", _quicHdrDecode(&vn, buf, _quicWrLen(&wr), 0));
    CHECK_U64("vneg type", vn.type, QUIC_PKT_VERSIONNEG);
    CHECK_U64("vneg dcid len", vn.dcid.len, 4);
    CHECK_U64("vneg scid len", vn.scid.len, 2);
    CHECK_U64("vneg body len", vn.payloadLen, 4);

    // A version this build does not implement decodes only as far as the connection IDs, which is
    // all that is needed to answer it with a Version Negotiation packet.
    {
        uint8 other[32];
        memcpy(other, buf, _quicWrLen(&wr));
        other[1] = 0xaa;
        CHECK_TRUE("decode unknown version", _quicHdrDecode(&h, other, _quicWrLen(&wr), 0));
        CHECK_U64("unknown version type", h.type, QUIC_PKT_UNKNOWN);
        CHECK_U64("unknown version dcid len", h.dcid.len, 4);
    }

out:
    return ret;
}

static int test_quictest_header_malformed(void)
{
    int ret = 0;
    QuicPktHdr h;
    uint8 pkt[64];

    // Truncated at every length short of a complete client Initial header.
    for (size_t n = 1; n < 18; n++) {
        if (_quicHdrDecode(&h, tv_cli_packet, n, 0)) {
            TEST_FAILV(ret, 1, _SL("client initial header accepted at ${int} bytes"),
                       stvar(int64, (int64)n));
            goto out;
        }
    }

    // The fixed bit must be set on everything but a Version Negotiation packet.
    memcpy(pkt, tv_cli_packet, 32);
    pkt[0] &= (uint8)~0x40;
    CHECK_TRUE("long header without fixed bit refused", !_quicHdrDecode(&h, pkt, 32, 0));

    memcpy(pkt, tv_cc_packet, sizeof(tv_cc_packet));
    pkt[0] &= (uint8)~0x40;
    CHECK_TRUE("short header without fixed bit refused",
               !_quicHdrDecode(&h, pkt, sizeof(tv_cc_packet), 0));

    // A connection ID longer than version 1 allows.
    memcpy(pkt, tv_cli_packet, 32);
    pkt[5] = QUIC_MAX_CID + 1;
    CHECK_TRUE("oversized dcid refused", !_quicHdrDecode(&h, pkt, 32, 0));

    // A Length field that runs past the end of the datagram.
    memcpy(pkt, tv_cli_packet, 32);
    CHECK_TRUE("full packet still decodes",
               _quicHdrDecode(&h, tv_cli_packet, sizeof(tv_cli_packet), 0));
    CHECK_TRUE("overlong length refused", !_quicHdrDecode(&h, tv_cli_packet, 64, 0));

    // A short header too small to hold a packet number and a tag.
    CHECK_TRUE("runt short header refused", !_quicHdrDecode(&h, tv_cc_packet, 8, 0));

    // Reserved bits are only visible once header protection is removed, and a nonzero value there
    // is a protocol violation rather than something to ignore.
    memcpy(pkt, tv_cc_packet, sizeof(tv_cc_packet));
    CHECK_TRUE("decode short for reserved check",
               _quicHdrDecode(&h, pkt, sizeof(tv_cc_packet), 0));
    pkt[0] = 0x42 | 0x08;
    CHECK_TRUE("short header reserved bit refused", !_quicHdrDecodePN(&h, pkt, QUIC_PN_NONE));

    CHECK_TRUE("decode long for reserved check",
               _quicHdrDecode(&h, tv_cli_packet, sizeof(tv_cli_packet), 0));
    memcpy(pkt, tv_cli_hdr, sizeof(tv_cli_hdr));
    pkt[0] |= 0x04;
    CHECK_TRUE("long header reserved bit refused", !_quicHdrDecodePN(&h, pkt, QUIC_PN_NONE));

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------------

// Encodes one frame, decodes it back, and checks the size estimate agreed with what was written.
// `cmp` is called on the decoded frame to check the fields that survived the round trip.
static bool frameRoundTrip(const QuicFrame* in, QuicFrame* out, uint8* buf, size_t bufsz,
                           size_t* len)
{
    QuicWr wr;
    _quicWrInit(&wr, buf, bufsz);
    if (!_quicFrameEncode(&wr, in) || _quicWrLen(&wr) != _quicFrameSize(in))
        return false;

    *len = _quicWrLen(&wr);

    QuicRd rd;
    _quicRdInit(&rd, buf, *len);
    if (!_quicFrameDecode(out, &rd))
        return false;

    return _quicRdLeft(&rd) == 0 && out->type == in->type;
}

static int test_quictest_frames(void)
{
    int ret = 0;
    uint8 buf[256];
    size_t len;
    QuicFrame in, out;

    static const uint8 blob[] = { 0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33, 0x44 };

    // The server's Initial payload from RFC 9001 A.3 is an ACK and a CRYPTO frame with nothing
    // padding it out, so decoding it has to land exactly on the end.
    {
        QuicRd rd;
        _quicRdInit(&rd, tv_srv_payload, sizeof(tv_srv_payload));

        CHECK_TRUE("A.3 first frame", _quicFrameDecode(&out, &rd));
        CHECK_U64("A.3 frame 1 type", out.type, QUIC_FRAME_ACK);
        CHECK_U64("A.3 ack largest", out.ack.largest, 0);
        CHECK_U64("A.3 ack delay", out.ack.delay, 0);
        CHECK_U64("A.3 ack range count", out.ack.rangeCount, 0);
        CHECK_U64("A.3 ack first range", out.ack.firstRange, 0);

        CHECK_TRUE("A.3 second frame", _quicFrameDecode(&out, &rd));
        CHECK_U64("A.3 frame 2 type", out.type, QUIC_FRAME_CRYPTO);
        CHECK_U64("A.3 crypto offset", out.crypto.offset, 0);
        CHECK_U64("A.3 crypto len", out.crypto.len, 90);
        CHECK_BYTES("A.3 crypto data", out.crypto.data, out.crypto.len,
                    tv_srv_payload + sizeof(tv_srv_payload) - 90, 90);

        CHECK_U64("A.3 payload fully consumed", _quicRdLeft(&rd), 0);
        CHECK_TRUE("A.3 no third frame", !_quicFrameDecode(&out, &rd));
    }

    // The client's Initial payload from A.2 is a single CRYPTO frame.
    {
        QuicRd rd;
        _quicRdInit(&rd, tv_cli_crypto, sizeof(tv_cli_crypto));
        CHECK_TRUE("A.2 crypto frame", _quicFrameDecode(&out, &rd));
        CHECK_U64("A.2 frame type", out.type, QUIC_FRAME_CRYPTO);
        CHECK_U64("A.2 crypto len", out.crypto.len, 241);
        CHECK_U64("A.2 payload fully consumed", _quicRdLeft(&rd), 0);
    }

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_PING;
    CHECK_TRUE("ping", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("ping size", len, 1);

    in.type = QUIC_FRAME_HANDSHAKE_DONE;
    CHECK_TRUE("handshake done", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));

    // A run of padding decodes as one frame covering the whole run.
    {
        in.type = QUIC_FRAME_PADDING;
        in.padding = 17;
        CHECK_TRUE("padding", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
        CHECK_U64("padding written", len, 17);
        CHECK_U64("padding run", out.padding, 17);
    }

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_RESET_STREAM;
    in.resetStream.id = 8;
    in.resetStream.error = 0x1234;
    in.resetStream.finalSize = 100000;
    CHECK_TRUE("reset stream", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("reset stream id", out.resetStream.id, 8);
    CHECK_U64("reset stream error", out.resetStream.error, 0x1234);
    CHECK_U64("reset stream final size", out.resetStream.finalSize, 100000);

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_STOP_SENDING;
    in.stopSending.id = 4;
    in.stopSending.error = 7;
    CHECK_TRUE("stop sending", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("stop sending id", out.stopSending.id, 4);
    CHECK_U64("stop sending error", out.stopSending.error, 7);

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_CRYPTO;
    in.crypto.offset = 1000;
    in.crypto.len = sizeof(blob);
    in.crypto.data = blob;
    CHECK_TRUE("crypto", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("crypto offset", out.crypto.offset, 1000);
    CHECK_BYTES("crypto data", out.crypto.data, out.crypto.len, blob, sizeof(blob));

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_NEW_TOKEN;
    in.newToken.len = sizeof(blob);
    in.newToken.token = blob;
    CHECK_TRUE("new token", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_BYTES("new token", out.newToken.token, out.newToken.len, blob, sizeof(blob));

    // All eight STREAM frame shapes. The one without a length field runs to the end of the
    // packet, so it can only appear last.
    for (uint64 bits = 0; bits < 8; bits++) {
        memset(&in, 0, sizeof(in));
        in.type = QUIC_FRAME_STREAM | bits;
        in.stream.id = 12;
        in.stream.offset = (bits & QUIC_STREAM_OFF) ? 4096 : 0;
        in.stream.len = sizeof(blob);
        in.stream.data = blob;

        if (!frameRoundTrip(&in, &out, buf, sizeof(buf), &len)) {
            TEST_FAILV(ret, 1, _SL("stream frame with bits ${int} did not round trip"),
                       stvar(int64, (int64)bits));
            goto out;
        }
        CHECK_U64("stream id", out.stream.id, 12);
        CHECK_U64("stream offset", out.stream.offset, in.stream.offset);
        CHECK_BYTES("stream data", out.stream.data, out.stream.len, blob, sizeof(blob));
    }

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_MAX_DATA;
    in.maxData.max = 1 << 20;
    CHECK_TRUE("max data", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("max data", out.maxData.max, 1 << 20);

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_MAX_STREAM_DATA;
    in.maxStreamData.id = 16;
    in.maxStreamData.max = 65536;
    CHECK_TRUE("max stream data", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("max stream data id", out.maxStreamData.id, 16);
    CHECK_U64("max stream data max", out.maxStreamData.max, 65536);

    for (uint64 t = QUIC_FRAME_MAX_STREAMS_BIDI; t <= QUIC_FRAME_MAX_STREAMS_UNI; t++) {
        memset(&in, 0, sizeof(in));
        in.type = t;
        in.maxStreams.max = 100;
        CHECK_TRUE("max streams", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
        CHECK_U64("max streams", out.maxStreams.max, 100);
    }

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_DATA_BLOCKED;
    in.dataBlocked.limit = 4321;
    CHECK_TRUE("data blocked", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("data blocked", out.dataBlocked.limit, 4321);

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_STREAM_DATA_BLOCKED;
    in.streamDataBlocked.id = 20;
    in.streamDataBlocked.limit = 9;
    CHECK_TRUE("stream data blocked", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("stream data blocked id", out.streamDataBlocked.id, 20);
    CHECK_U64("stream data blocked limit", out.streamDataBlocked.limit, 9);

    for (uint64 t = QUIC_FRAME_STREAMS_BLOCKED_BIDI; t <= QUIC_FRAME_STREAMS_BLOCKED_UNI; t++) {
        memset(&in, 0, sizeof(in));
        in.type = t;
        in.streamsBlocked.limit = 3;
        CHECK_TRUE("streams blocked", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
        CHECK_U64("streams blocked", out.streamsBlocked.limit, 3);
    }

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_NEW_CONNECTION_ID;
    in.newConnId.seq = 3;
    in.newConnId.retirePrior = 1;
    in.newConnId.cid.len = 8;
    memcpy(in.newConnId.cid.id, tv_dcid, 8);
    memset(in.newConnId.token, 0x5a, QUIC_RESET_TOKEN_LEN);
    CHECK_TRUE("new connection id", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("new connection id seq", out.newConnId.seq, 3);
    CHECK_U64("new connection id retire", out.newConnId.retirePrior, 1);
    CHECK_BYTES("new connection id cid", out.newConnId.cid.id, out.newConnId.cid.len, tv_dcid, 8);
    CHECK_BYTES("new connection id token", out.newConnId.token, QUIC_RESET_TOKEN_LEN,
                in.newConnId.token, QUIC_RESET_TOKEN_LEN);

    memset(&in, 0, sizeof(in));
    in.type = QUIC_FRAME_RETIRE_CONNECTION_ID;
    in.retireConnId.seq = 2;
    CHECK_TRUE("retire connection id", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
    CHECK_U64("retire connection id", out.retireConnId.seq, 2);

    for (uint64 t = QUIC_FRAME_PATH_CHALLENGE; t <= QUIC_FRAME_PATH_RESPONSE; t++) {
        memset(&in, 0, sizeof(in));
        in.type = t;
        memcpy(in.path.data, blob, QUIC_PATH_DATA_LEN);
        CHECK_TRUE("path frame", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
        CHECK_BYTES("path data", out.path.data, QUIC_PATH_DATA_LEN, blob, QUIC_PATH_DATA_LEN);
    }

    // Only the transport form of CONNECTION_CLOSE carries the frame type that caused the close.
    for (uint64 t = QUIC_FRAME_CONNECTION_CLOSE; t <= QUIC_FRAME_CONNECTION_CLOSE_APP; t++) {
        memset(&in, 0, sizeof(in));
        in.type = t;
        in.connClose.error = QUIC_ERR_PROTOCOL_VIOLATION;
        in.connClose.frameType = (t == QUIC_FRAME_CONNECTION_CLOSE) ? QUIC_FRAME_STREAM : 0;
        in.connClose.reason = (const uint8*)"nope";
        in.connClose.reasonLen = 4;
        CHECK_TRUE("connection close", frameRoundTrip(&in, &out, buf, sizeof(buf), &len));
        CHECK_U64("connection close error", out.connClose.error, QUIC_ERR_PROTOCOL_VIOLATION);
        CHECK_U64("connection close frame type", out.connClose.frameType, in.connClose.frameType);
        CHECK_BYTES("connection close reason", out.connClose.reason, out.connClose.reasonLen,
                    (const uint8*)"nope", 4);
    }

out:
    return ret;
}

static int test_quictest_ack(void)
{
    int ret = 0;
    uint8 rangeBuf[64];
    uint8 buf[128];
    QuicFrame f, out;
    size_t len;

    // Three descending ranges with real gaps between them.
    static const QuicAckRange ranges[] = {
        { 100, 90 },
        { 80, 80 },
        { 60, 50 },
    };

    CHECK_TRUE("build ack", _quicAckBuild(&f, 1234, ranges, 3, NULL, rangeBuf, sizeof(rangeBuf)));
    CHECK_U64("ack type", f.type, QUIC_FRAME_ACK);
    CHECK_U64("ack largest", f.ack.largest, 100);
    CHECK_U64("ack delay", f.ack.delay, 1234);
    CHECK_U64("ack first range", f.ack.firstRange, 10);
    CHECK_U64("ack range count", f.ack.rangeCount, 2);

    CHECK_TRUE("ack round trip", frameRoundTrip(&f, &out, buf, sizeof(buf), &len));

    {
        QuicAckIter it;
        QuicAckRange got;
        CHECK_TRUE("ack iter init", _quicAckIterInit(&it, &out));
        for (size_t i = 0; i < 3; i++) {
            if (!_quicAckIterNext(&it, &got)) {
                TEST_FAILV(ret, 1, _SL("ack range ${int} missing"), stvar(int64, (int64)i));
                goto out;
            }
            if (got.largest != ranges[i].largest || got.smallest != ranges[i].smallest) {
                TEST_FAILV(ret, 1,
                           _SL("ack range ${int} is ${int}..${int}, expected ${int}..${int}"),
                           stvar(int64, (int64)i), stvar(int64, (int64)got.smallest),
                           stvar(int64, (int64)got.largest), stvar(int64, (int64)ranges[i].smallest),
                           stvar(int64, (int64)ranges[i].largest));
                goto out;
            }
        }
        CHECK_TRUE("ack iter ends", !_quicAckIterNext(&it, &got));
        CHECK_TRUE("ack iter clean", !it.bad);
    }

    // With ECN counts the frame changes type and grows three fields.
    {
        static const uint64 ecn[3] = { 11, 22, 33 };
        CHECK_TRUE("build ack ecn",
                   _quicAckBuild(&f, 0, ranges, 3, ecn, rangeBuf, sizeof(rangeBuf)));
        CHECK_U64("ack ecn type", f.type, QUIC_FRAME_ACK_ECN);
        CHECK_TRUE("ack ecn round trip", frameRoundTrip(&f, &out, buf, sizeof(buf), &len));
        CHECK_U64("ect0", out.ack.ect0, 11);
        CHECK_U64("ect1", out.ack.ect1, 22);
        CHECK_U64("ecnce", out.ack.ecnce, 33);
    }

    // A single range is the common case and needs no gap and length pairs at all.
    {
        static const QuicAckRange one[] = { { 7, 7 } };
        CHECK_TRUE("build single ack",
                   _quicAckBuild(&f, 0, one, 1, NULL, rangeBuf, sizeof(rangeBuf)));
        CHECK_U64("single ack range bytes", f.ack.rangeLen, 0);
        CHECK_TRUE("single ack round trip", frameRoundTrip(&f, &out, buf, sizeof(buf), &len));

        QuicAckIter it;
        QuicAckRange got;
        CHECK_TRUE("single ack iter init", _quicAckIterInit(&it, &out));
        CHECK_TRUE("single ack iter next", _quicAckIterNext(&it, &got));
        CHECK_U64("single ack largest", got.largest, 7);
        CHECK_U64("single ack smallest", got.smallest, 7);
        CHECK_TRUE("single ack iter ends", !_quicAckIterNext(&it, &got));
    }

    // Ranges that touch would have to be one range, and ranges given out of order are a caller
    // bug rather than something to silently reorder.
    {
        static const QuicAckRange touching[] = { { 100, 90 }, { 89, 80 } };
        static const QuicAckRange backwards[] = { { 50, 40 }, { 100, 90 } };
        static const QuicAckRange inverted[] = { { 10, 20 } };

        CHECK_TRUE("touching ranges refused",
                   !_quicAckBuild(&f, 0, touching, 2, NULL, rangeBuf, sizeof(rangeBuf)));
        CHECK_TRUE("out of order ranges refused",
                   !_quicAckBuild(&f, 0, backwards, 2, NULL, rangeBuf, sizeof(rangeBuf)));
        CHECK_TRUE("inverted range refused",
                   !_quicAckBuild(&f, 0, inverted, 1, NULL, rangeBuf, sizeof(rangeBuf)));
        CHECK_TRUE("empty range list refused",
                   !_quicAckBuild(&f, 0, ranges, 0, NULL, rangeBuf, sizeof(rangeBuf)));
    }

    // A first range larger than the largest acknowledged number would run off the bottom.
    {
        static const uint8 bogus[] = { 0x02, 0x05, 0x00, 0x00, 0x09 };
        QuicRd rd;
        _quicRdInit(&rd, bogus, sizeof(bogus));
        CHECK_TRUE("oversized first range refused", !_quicFrameDecode(&out, &rd));
    }

    // A second range below zero is refused by the iterator rather than wrapping. The first range
    // here is packet 1 alone, which leaves no room below it for another range at all.
    {
        static const uint8 bogus[] = { 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00 };
        QuicRd rd;
        QuicAckIter it;
        QuicAckRange got;

        _quicRdInit(&rd, bogus, sizeof(bogus));
        CHECK_TRUE("decode underflowing ack", _quicFrameDecode(&out, &rd));
        CHECK_TRUE("underflow iter init", _quicAckIterInit(&it, &out));
        CHECK_TRUE("underflow first range", _quicAckIterNext(&it, &got));
        CHECK_TRUE("underflow second range refused", !_quicAckIterNext(&it, &got));
        CHECK_TRUE("underflow flagged", it.bad);
    }

out:
    return ret;
}

static int test_quictest_frames_malformed(void)
{
    int ret = 0;
    uint8 buf[256];
    QuicFrame f;
    QuicRd rd;

    // Nothing says how long an unrecognized frame is, so it cannot be skipped over.
    {
        static const uint8 unknown[] = { 0x3f, 0x00, 0x00 };
        _quicRdInit(&rd, unknown, sizeof(unknown));
        CHECK_TRUE("unknown frame type refused", !_quicFrameDecode(&f, &rd));
    }

    // Every frame truncated one byte at a time. Building the full frame first means the test
    // covers whatever the encoder actually produces rather than a hand-written copy of it.
    {
        static const uint8 blob[] = { 1, 2, 3, 4 };
        QuicFrame in;
        memset(&in, 0, sizeof(in));
        in.type = QUIC_FRAME_CRYPTO;
        in.crypto.offset = 1000000;
        in.crypto.len = sizeof(blob);
        in.crypto.data = blob;

        QuicWr wr;
        _quicWrInit(&wr, buf, sizeof(buf));
        CHECK_TRUE("encode crypto for truncation", _quicFrameEncode(&wr, &in));

        size_t full = _quicWrLen(&wr);
        for (size_t n = 1; n < full; n++) {
            _quicRdInit(&rd, buf, n);
            if (_quicFrameDecode(&f, &rd) && f.crypto.len == sizeof(blob)) {
                TEST_FAILV(ret, 1, _SL("crypto frame decoded whole from ${int} of ${int} bytes"),
                           stvar(int64, (int64)n), stvar(int64, (int64)full));
                goto out;
            }
        }
    }

    // A NEW_CONNECTION_ID may not carry an empty or oversized connection ID, and may not ask the
    // peer to retire a sequence number it has not issued yet.
    {
        static const uint8 emptyCid[] = { 0x18, 0x01, 0x00, 0x00 };
        static const uint8 bigCid[] = { 0x18, 0x01, 0x00, 0x15 };
        static const uint8 badRetire[] = { 0x18, 0x01, 0x02, 0x01, 0xaa };

        _quicRdInit(&rd, emptyCid, sizeof(emptyCid));
        CHECK_TRUE("empty connection id refused", !_quicFrameDecode(&f, &rd));

        _quicRdInit(&rd, bigCid, sizeof(bigCid));
        CHECK_TRUE("oversized connection id refused", !_quicFrameDecode(&f, &rd));

        _quicRdInit(&rd, badRetire, sizeof(badRetire));
        CHECK_TRUE("retire past issued refused", !_quicFrameDecode(&f, &rd));
    }

    // An empty NEW_TOKEN is meaningless and a stream count past the ceiling would produce stream
    // ids that do not fit.
    {
        static const uint8 emptyToken[] = { 0x07, 0x00 };
        static const uint8 bigStreams[] = { 0x12, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

        _quicRdInit(&rd, emptyToken, sizeof(emptyToken));
        CHECK_TRUE("empty new token refused", !_quicFrameDecode(&f, &rd));

        _quicRdInit(&rd, bigStreams, sizeof(bigStreams));
        CHECK_TRUE("oversized stream count refused", !_quicFrameDecode(&f, &rd));
    }

    // A STREAM frame whose offset and length together pass the 62-bit ceiling.
    {
        static const uint8 farStream[] = { 0x0e, 0x04, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0xff, 0xfe, 0x04, 1, 2, 3, 4 };
        _quicRdInit(&rd, farStream, sizeof(farStream));
        CHECK_TRUE("stream past ceiling refused", !_quicFrameDecode(&f, &rd));
    }

    // A frame that does not fit in the space left is refused rather than truncated.
    {
        QuicFrame in;
        QuicWr wr;
        memset(&in, 0, sizeof(in));
        in.type = QUIC_FRAME_MAX_DATA;
        in.maxData.max = 1000000;

        _quicWrInit(&wr, buf, 2);
        CHECK_TRUE("frame that does not fit refused", !_quicFrameEncode(&wr, &in));
        CHECK_TRUE("writer flagged", wr.bad);
    }

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Packet protection
// ---------------------------------------------------------------------------------------------

// RFC 9001 A.1: the Initial secrets and the key, IV and header protection key each direction
// derives from them. The AEAD key itself lives inside PSA and cannot be read back, so it is
// checked by the packets the two tests below produce.
static int test_quictest_initial_keys(void)
{
    int ret = 0;
    QuicKeys client, server;

    CHECK_TRUE("derive initial keys",
               _quicKeysInitial(&client, &server, tv_dcid, sizeof(tv_dcid)));

    CHECK_BYTES("client initial secret", client.secret, client.secretLen, tv_client_initial,
                sizeof(tv_client_initial));
    CHECK_BYTES("client iv", client.iv, TLS13_IV_LEN, tv_client_iv, sizeof(tv_client_iv));
    CHECK_BYTES("client hp", client.hpKey, client.keyLen, tv_client_hp, sizeof(tv_client_hp));

    CHECK_BYTES("server initial secret", server.secret, server.secretLen, tv_server_initial,
                sizeof(tv_server_initial));
    CHECK_BYTES("server iv", server.iv, TLS13_IV_LEN, tv_server_iv, sizeof(tv_server_iv));
    CHECK_BYTES("server hp", server.hpKey, server.keyLen, tv_server_hp, sizeof(tv_server_hp));

out:
    _quicKeysDestroy(&client);
    _quicKeysDestroy(&server);
    return ret;
}

// The masks RFC 9001 prints for each of its three samples, including the ChaCha20 form, whose
// block counter comes out of the sample itself.
static int test_quictest_hpmask(void)
{
    int ret = 0;
    QuicKeys client, server, cc;
    uint8 mask[5];

    memset(&cc, 0, sizeof(cc));
    CHECK_TRUE("derive initial keys",
               _quicKeysInitial(&client, &server, tv_dcid, sizeof(tv_dcid)));

    CHECK_TRUE("client mask", _quicHpMask(&client, tv_cli_sample, mask));
    CHECK_BYTES("client mask", mask, sizeof(mask), tv_cli_mask, sizeof(tv_cli_mask));

    CHECK_TRUE("server mask", _quicHpMask(&server, tv_srv_sample, mask));
    CHECK_BYTES("server mask", mask, sizeof(mask), tv_srv_mask, sizeof(tv_srv_mask));

    CHECK_TRUE("derive chacha keys",
               _quicKeysDerive(&cc, _tls13Suite(TLS13_CHACHA20_POLY1305_SHA256), tv_cc_secret));
    CHECK_TRUE("chacha mask", _quicHpMask(&cc, tv_cc_sample, mask));
    CHECK_BYTES("chacha mask", mask, sizeof(mask), tv_cc_mask, sizeof(tv_cc_mask));

out:
    _quicKeysDestroy(&client);
    _quicKeysDestroy(&server);
    _quicKeysDestroy(&cc);
    return ret;
}

// RFC 9001 A.2: build the client's Initial packet from scratch and compare all 1200 bytes, then
// take the RFC's own copy apart again.
static int test_quictest_initial_client(void)
{
    int ret = 0;
    QuicKeys client, server;
    QuicPktHdr h;
    QuicWr wr;
    uint8 pkt[1400];
    uint8 payload[1162];
    uint8 opened[1400];
    size_t len = 0;

    CHECK_TRUE("derive initial keys",
               _quicKeysInitial(&client, &server, tv_dcid, sizeof(tv_dcid)));

    // The frames are one CRYPTO frame padded out so the datagram reaches the 1200 bytes a client
    // has to send before the server will talk back.
    memset(payload, 0, sizeof(payload));
    memcpy(payload, tv_cli_crypto, sizeof(tv_cli_crypto));

    memset(&h, 0, sizeof(h));
    h.type = QUIC_PKT_INITIAL;
    h.version = QUIC_VERSION_1;
    h.dcid.len = sizeof(tv_dcid);
    memcpy(h.dcid.id, tv_dcid, sizeof(tv_dcid));
    h.pnLen = 4;
    h.pn = 2;
    h.pnEnc = 2;
    h.len = h.pnLen + sizeof(payload) + QUIC_TAG_LEN;

    _quicWrInit(&wr, pkt, sizeof(pkt));
    CHECK_TRUE("encode header", _quicHdrEncode(&wr, &h));
    CHECK_BYTES("unprotected header", pkt, _quicWrLen(&wr), tv_cli_hdr, sizeof(tv_cli_hdr));

    CHECK_TRUE("seal", _quicPktSeal(&client, pkt, sizeof(pkt), _quicWrLen(&wr) - h.pnLen, h.pnLen,
                                    h.pn, payload, sizeof(payload), &len));
    CHECK_BYTES("protected packet", pkt, len, tv_cli_packet, sizeof(tv_cli_packet));

    // And back the other way, from the RFC's bytes.
    memcpy(pkt, tv_cli_packet, sizeof(tv_cli_packet));
    CHECK_TRUE("decode header", _quicHdrDecode(&h, pkt, sizeof(tv_cli_packet), 0));
    CHECK_TRUE("open", _quicPktOpen(&client, &h, pkt, QUIC_PN_NONE, opened, sizeof(opened), &len));
    CHECK_U64("opened packet number", h.pn, 2);
    CHECK_U64("opened packet number width", h.pnLen, 4);
    CHECK_BYTES("unprotected header after open", pkt, sizeof(tv_cli_hdr), tv_cli_hdr,
                sizeof(tv_cli_hdr));
    CHECK_BYTES("opened payload", opened, len, payload, sizeof(payload));

    // The wrong direction's keys must not open it. Header protection keys do not change, so the
    // header is already unprotected by the attempt above; only the AEAD is being tested here.
    {
        size_t bad = 0;
        CHECK_TRUE("wrong direction refused",
                   !_quicPktOpen(&server, &h, pkt, QUIC_PN_NONE, opened, sizeof(opened), &bad));
    }

out:
    _quicKeysDestroy(&client);
    _quicKeysDestroy(&server);
    return ret;
}

// RFC 9001 A.3: the server's reply, which uses a two-byte packet number and a Length field small
// enough that the RFC's own encoder padded the varint out to two bytes.
static int test_quictest_initial_server(void)
{
    int ret = 0;
    QuicKeys client, server;
    QuicPktHdr h;
    QuicWr wr;
    uint8 pkt[256];
    uint8 opened[256];
    size_t len = 0;

    static const uint8 srv_scid[8] = { 0xf0, 0x67, 0xa5, 0x50, 0x2a, 0x42, 0x62, 0xb5 };

    CHECK_TRUE("derive initial keys",
               _quicKeysInitial(&client, &server, tv_dcid, sizeof(tv_dcid)));

    memset(&h, 0, sizeof(h));
    h.type = QUIC_PKT_INITIAL;
    h.version = QUIC_VERSION_1;
    h.scid.len = sizeof(srv_scid);
    memcpy(h.scid.id, srv_scid, sizeof(srv_scid));
    h.pnLen = 2;
    h.pn = 1;
    h.pnEnc = 1;
    h.len = h.pnLen + sizeof(tv_srv_payload) + QUIC_TAG_LEN;

    _quicWrInit(&wr, pkt, sizeof(pkt));
    CHECK_TRUE("encode header", _quicHdrEncode(&wr, &h));
    CHECK_BYTES("unprotected header", pkt, _quicWrLen(&wr), tv_srv_hdr, sizeof(tv_srv_hdr));

    CHECK_TRUE("seal", _quicPktSeal(&server, pkt, sizeof(pkt), _quicWrLen(&wr) - h.pnLen, h.pnLen,
                                    h.pn, tv_srv_payload, sizeof(tv_srv_payload), &len));
    CHECK_BYTES("protected packet", pkt, len, tv_srv_packet, sizeof(tv_srv_packet));

    memcpy(pkt, tv_srv_packet, sizeof(tv_srv_packet));
    CHECK_TRUE("decode header", _quicHdrDecode(&h, pkt, sizeof(tv_srv_packet), 0));
    CHECK_TRUE("open", _quicPktOpen(&server, &h, pkt, QUIC_PN_NONE, opened, sizeof(opened), &len));
    CHECK_U64("opened packet number", h.pn, 1);
    CHECK_BYTES("opened payload", opened, len, tv_srv_payload, sizeof(tv_srv_payload));

out:
    _quicKeysDestroy(&client);
    _quicKeysDestroy(&server);
    return ret;
}

// RFC 9001 A.5: a short-header packet under ChaCha20-Poly1305, the one case where header
// protection is a raw keystream block rather than an AES block, plus the key update that derives
// the next generation of keys from the same secret.
static int test_quictest_chacha(void)
{
    int ret = 0;
    QuicKeys k, next;
    QuicPktHdr h;
    QuicWr wr;
    uint8 pkt[64];
    uint8 opened[64];
    size_t len = 0;

    static const uint8 ping = 0x01;
    static const uint64 pn = UINT64_C(654360564);

    memset(&next, 0, sizeof(next));
    CHECK_TRUE("derive chacha keys",
               _quicKeysDerive(&k, _tls13Suite(TLS13_CHACHA20_POLY1305_SHA256), tv_cc_secret));
    CHECK_BYTES("chacha iv", k.iv, TLS13_IV_LEN, tv_cc_iv, sizeof(tv_cc_iv));
    CHECK_BYTES("chacha hp", k.hpKey, k.keyLen, tv_cc_hp, sizeof(tv_cc_hp));

    memset(&h, 0, sizeof(h));
    h.type = QUIC_PKT_SHORT;
    h.pnLen = 3;
    h.pnEnc = pn & 0xffffff;

    _quicWrInit(&wr, pkt, sizeof(pkt));
    CHECK_TRUE("encode header", _quicHdrEncode(&wr, &h));
    CHECK_BYTES("unprotected header", pkt, _quicWrLen(&wr), tv_cc_hdr, sizeof(tv_cc_hdr));

    CHECK_TRUE("seal", _quicPktSeal(&k, pkt, sizeof(pkt), _quicWrLen(&wr) - h.pnLen, h.pnLen, pn,
                                    &ping, 1, &len));
    CHECK_BYTES("protected packet", pkt, len, tv_cc_packet, sizeof(tv_cc_packet));

    // The payload ciphertext and tag the RFC prints separately are the tail of that packet.
    CHECK_BYTES("payload ciphertext", pkt + sizeof(tv_cc_hdr), sizeof(tv_cc_ciphertext),
                tv_cc_ciphertext, sizeof(tv_cc_ciphertext));

    // Opening needs a plausible largest received packet number, because a three-byte field only
    // carries the low 24 bits of a number this large.
    memcpy(pkt, tv_cc_packet, sizeof(tv_cc_packet));
    CHECK_TRUE("decode header", _quicHdrDecode(&h, pkt, sizeof(tv_cc_packet), 0));
    CHECK_TRUE("open", _quicPktOpen(&k, &h, pkt, pn - 1, opened, sizeof(opened), &len));
    CHECK_U64("opened packet number", h.pn, pn);
    CHECK_U64("opened payload length", len, 1);
    CHECK_U64("opened payload", opened[0], 0x01);

    // A key update replaces the traffic secret, and with it the AEAD key and IV, but leaves the
    // header protection key alone.
    CHECK_TRUE("key update", _quicKeysNext(&next, &k));
    CHECK_BYTES("updated secret", next.secret, next.secretLen, tv_cc_ku, sizeof(tv_cc_ku));
    CHECK_BYTES("header protection key survives update", next.hpKey, next.keyLen, tv_cc_hp,
                sizeof(tv_cc_hp));

    {
        bool same = memcmp(next.iv, k.iv, TLS13_IV_LEN) == 0;
        CHECK_TRUE("updated iv differs", !same);
    }

    // Packets sealed under the new keys no longer open under the old ones.
    {
        size_t bad = 0;
        _quicWrInit(&wr, pkt, sizeof(pkt));
        CHECK_TRUE("encode header for updated keys", _quicHdrEncode(&wr, &h));
        CHECK_TRUE("seal under updated keys",
                   _quicPktSeal(&next, pkt, sizeof(pkt), _quicWrLen(&wr) - h.pnLen, h.pnLen, pn,
                                &ping, 1, &len));

        QuicPktHdr h2;
        CHECK_TRUE("decode updated header", _quicHdrDecode(&h2, pkt, len, 0));
        CHECK_TRUE("old keys refused",
                   !_quicPktOpen(&k, &h2, pkt, pn - 1, opened, sizeof(opened), &bad));
        CHECK_TRUE("new keys accepted",
                   _quicPktOpen(&next, &h2, pkt, pn - 1, opened, sizeof(opened), &len));
        CHECK_U64("reopened payload", opened[0], 0x01);
    }

out:
    _quicKeysDestroy(&k);
    _quicKeysDestroy(&next);
    return ret;
}

// RFC 9001 A.4: the integrity tag that closes a Retry packet, which proves only that whatever
// sent it could see the client's Initial.
static int test_quictest_retry(void)
{
    int ret = 0;
    uint8 tag[QUIC_TAG_LEN];

    size_t body = sizeof(tv_retry_packet) - QUIC_TAG_LEN;

    CHECK_TRUE("retry tag", _quicRetryTag(tag, tv_dcid, sizeof(tv_dcid), tv_retry_packet, body));
    CHECK_BYTES("retry tag", tag, sizeof(tag), tv_retry_packet + body, QUIC_TAG_LEN);

    // The tag covers the original connection ID even though the Retry packet does not carry it,
    // so a Retry aimed at a different Initial does not verify.
    {
        uint8 other[8];
        memcpy(other, tv_dcid, sizeof(other));
        other[0] ^= 0x01;

        CHECK_TRUE("retry tag for other cid", _quicRetryTag(tag, other, sizeof(other),
                                                            tv_retry_packet, body));
        if (memcmp(tag, tv_retry_packet + body, QUIC_TAG_LEN) == 0) {
            TEST_FAILV(ret, 1, _SL("retry tag ignored the original connection ID"), stvNone);
            goto out;
        }
    }

out:
    return ret;
}

testfunc quictest_funcs[] = {
    { "varint", test_quictest_varint },
    { "pn", test_quictest_pn },
    { "header", test_quictest_header },
    { "header_malformed", test_quictest_header_malformed },
    { "frames", test_quictest_frames },
    { "ack", test_quictest_ack },
    { "frames_malformed", test_quictest_frames_malformed },
    { "initial_keys", test_quictest_initial_keys },
    { "hpmask", test_quictest_hpmask },
    { "initial_client", test_quictest_initial_client },
    { "initial_server", test_quictest_initial_server },
    { "chacha", test_quictest_chacha },
    { "retry", test_quictest_retry },
    { NULL, NULL },
};

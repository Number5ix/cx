#include "quic_private.h"

#include <string.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// ---------------------------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicRdInit(QuicRd* rd, const uint8* data, size_t len)
{
    rd->p = data;
    rd->end = data + len;
    rd->bad = false;
}

_Use_decl_annotations_
size_t _quicRdLeft(const QuicRd* rd)
{
    return rd->bad ? 0 : (size_t)(rd->end - rd->p);
}

_Use_decl_annotations_
const uint8* _quicRdBytes(QuicRd* rd, size_t n)
{
    if (rd->bad || (size_t)(rd->end - rd->p) < n) {
        rd->bad = true;
        return NULL;
    }
    const uint8* ret = rd->p;
    rd->p += n;
    return ret;
}

_Use_decl_annotations_
uint64 _quicRdInt(QuicRd* rd, uint8 nbytes)
{
    const uint8* p = _quicRdBytes(rd, nbytes);
    if (!p)
        return 0;

    uint64 val = 0;
    for (uint8 i = 0; i < nbytes; i++)
        val = (val << 8) | p[i];
    return val;
}

_Use_decl_annotations_
uint8 _quicRd8(QuicRd* rd)
{
    return (uint8)_quicRdInt(rd, 1);
}

_Use_decl_annotations_
uint16 _quicRd16(QuicRd* rd)
{
    return (uint16)_quicRdInt(rd, 2);
}

_Use_decl_annotations_
uint32 _quicRd32(QuicRd* rd)
{
    return (uint32)_quicRdInt(rd, 4);
}

_Use_decl_annotations_
uint64 _quicRdVarint(QuicRd* rd)
{
    if (rd->bad || rd->p == rd->end) {
        rd->bad = true;
        return 0;
    }

    // The top two bits of the first byte give the total width as a power of two; the remaining
    // six are the most significant bits of the value itself.
    uint8 n = (uint8)(1u << (rd->p[0] >> 6));
    if ((size_t)(rd->end - rd->p) < n) {
        rd->bad = true;
        return 0;
    }

    uint64 val = rd->p[0] & 0x3f;
    for (uint8 i = 1; i < n; i++)
        val = (val << 8) | rd->p[i];
    rd->p += n;
    return val;
}

_Use_decl_annotations_
const uint8* _quicRdVarintBytes(QuicRd* rd, size_t* len)
{
    *len = 0;

    uint64 n = _quicRdVarint(rd);
    if (rd->bad || n > _quicRdLeft(rd)) {
        rd->bad = true;
        return NULL;
    }

    *len = (size_t)n;
    return _quicRdBytes(rd, (size_t)n);
}

// ---------------------------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicWrInit(QuicWr* wr, uint8* buf, size_t bufsz)
{
    wr->base = buf;
    wr->p = buf;
    wr->end = buf + bufsz;
    wr->bad = false;
}

_Use_decl_annotations_
size_t _quicWrLen(const QuicWr* wr)
{
    return (size_t)(wr->p - wr->base);
}

_Use_decl_annotations_
size_t _quicWrLeft(const QuicWr* wr)
{
    return wr->bad ? 0 : (size_t)(wr->end - wr->p);
}

_Use_decl_annotations_
void _quicWrBytes(QuicWr* wr, const uint8* data, size_t len)
{
    if (wr->bad || (size_t)(wr->end - wr->p) < len) {
        wr->bad = true;
        return;
    }
    if (len > 0)
        memcpy(wr->p, data, len);
    wr->p += len;
}

_Use_decl_annotations_
void _quicWrInt(QuicWr* wr, uint64 val, uint8 nbytes)
{
    if (wr->bad || (size_t)(wr->end - wr->p) < nbytes) {
        wr->bad = true;
        return;
    }
    for (uint8 i = 0; i < nbytes; i++)
        wr->p[i] = (uint8)(val >> ((nbytes - 1 - i) * 8));
    wr->p += nbytes;
}

_Use_decl_annotations_
void _quicWr8(QuicWr* wr, uint8 val)
{
    _quicWrInt(wr, val, 1);
}

_Use_decl_annotations_
void _quicWr16(QuicWr* wr, uint16 val)
{
    _quicWrInt(wr, val, 2);
}

_Use_decl_annotations_
void _quicWr32(QuicWr* wr, uint32 val)
{
    _quicWrInt(wr, val, 4);
}

_Use_decl_annotations_
uint8 _quicVarintSize(uint64 val)
{
    if (val < 0x40)
        return 1;
    if (val < 0x4000)
        return 2;
    if (val < 0x40000000)
        return 4;
    if (val < QUIC_MAX_PACKET_NUMBER)
        return 8;
    return 0;
}

_Use_decl_annotations_
void _quicWrVarintSized(QuicWr* wr, uint64 val, uint8 size)
{
    uint8 need = _quicVarintSize(val);
    if (need == 0 || size < need || (size != 1 && size != 2 && size != 4 && size != 8)) {
        wr->bad = true;
        return;
    }

    if (wr->bad || (size_t)(wr->end - wr->p) < size) {
        wr->bad = true;
        return;
    }

    for (uint8 i = 0; i < size; i++)
        wr->p[i] = (uint8)(val >> ((size - 1 - i) * 8));

    // The prefix is log2 of the width: 0 for one byte, 1 for two, 2 for four, 3 for eight.
    uint8 prefix = (size == 1) ? 0 : (size == 2) ? 1 : (size == 4) ? 2 : 3;
    wr->p[0] |= (uint8)(prefix << 6);
    wr->p += size;
}

_Use_decl_annotations_
void _quicWrVarint(QuicWr* wr, uint64 val)
{
    _quicWrVarintSized(wr, val, _quicVarintSize(val));
}

_Use_decl_annotations_
void _quicWrVarintBytes(QuicWr* wr, const uint8* data, size_t len)
{
    _quicWrVarint(wr, len);
    _quicWrBytes(wr, data, len);
}

// ---------------------------------------------------------------------------------------------
// Packet numbers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
uint8 _quicPnSize(uint64 pn, uint64 largestAcked)
{
    // The peer resolves a truncated packet number against a window centred on what it expects
    // next, so the number has to be wide enough to cover everything still outstanding.
    uint64 unacked = (largestAcked == QUIC_PN_NONE || largestAcked > pn) ? pn + 1
                                                                        : pn - largestAcked;

    // RFC 9000 section 17.1: wide enough to represent more than *twice* that difference, because
    // the window the peer resolves against reaches only half as far in each direction as the
    // encoding is wide. Sizing to the difference itself is off by one bit, and the packet that
    // first exceeds half the window decodes as some other number entirely -- which fails its AEAD,
    // since the packet number is part of the nonce. Nothing about that is visible: the packet is
    // discarded in silence, and because it never reaches the peer nothing acknowledges it, so the
    // difference only grows and every packet after it is lost the same way.
    uint64 range = unacked * 2;

    uint8 bits = 0;
    while (range > 0) {
        bits++;
        range >>= 1;
    }

    uint8 n = (uint8)((bits + 7) / 8);
    if (n < 1)
        n = 1;
    if (n > 4)
        n = 4;
    return n;
}

_Use_decl_annotations_
uint64 _quicPnDecode(uint64 largestPn, uint64 truncated, uint8 pnLen)
{
    if (pnLen < 1 || pnLen > 4)
        return truncated;

    uint64 expected = (largestPn == QUIC_PN_NONE) ? 0 : largestPn + 1;
    uint64 win = UINT64_C(1) << (pnLen * 8);
    uint64 hwin = win / 2;
    uint64 candidate = (expected & ~(win - 1)) | truncated;

    // Pick the value nearest what was expected. The additions are written on the candidate side
    // because RFC 9000's own form, expected - hwin, underflows early in a connection.
    if (candidate + hwin <= expected && candidate < QUIC_MAX_PACKET_NUMBER - win)
        return candidate + win;
    if (candidate > expected + hwin && candidate >= win)
        return candidate - win;
    return candidate;
}

// ---------------------------------------------------------------------------------------------
// Packet headers
// ---------------------------------------------------------------------------------------------

// Reads a length-prefixed connection ID. Version 1 caps them at QUIC_MAX_CID bytes.
static bool hdrRdCid(QuicRd* rd, QuicCid* cid)
{
    uint8 n = _quicRd8(rd);
    if (rd->bad || n > QUIC_MAX_CID)
        return false;

    const uint8* p = _quicRdBytes(rd, n);
    if (!p)
        return false;

    cid->len = n;
    memcpy(cid->id, p, n);
    return true;
}

_Use_decl_annotations_
bool _quicHdrDecode(QuicPktHdr* h, const uint8* pkt, size_t len, uint8 dcidLen)
{
    memset(h, 0, sizeof(*h));

    QuicRd rd;
    _quicRdInit(&rd, pkt, len);

    uint8 b0 = _quicRd8(&rd);
    if (rd.bad)
        return false;

    if (!(b0 & 0x80)) {
        if (!(b0 & 0x40) || dcidLen > QUIC_MAX_CID)
            return false;

        const uint8* p = _quicRdBytes(&rd, dcidLen);
        if (!p)
            return false;

        h->type = QUIC_PKT_SHORT;
        h->dcid.len = dcidLen;
        memcpy(h->dcid.id, p, dcidLen);
        h->spin = (b0 & 0x20) != 0;
        h->pnOff = (size_t)(rd.p - pkt);

        // A short header carries no length, so it always runs to the end of the datagram and can
        // never be followed by another packet.
        h->pktLen = len;
        h->len = len - h->pnOff;
        return h->len >= 1 + QUIC_TAG_LEN;
    }

    h->version = _quicRd32(&rd);
    if (!hdrRdCid(&rd, &h->dcid) || !hdrRdCid(&rd, &h->scid))
        return false;

    if (h->version == QUIC_VERSION_NEGOTIATE) {
        h->type = QUIC_PKT_VERSIONNEG;
        h->payload = rd.p;
        h->payloadLen = _quicRdLeft(&rd);
        h->pktLen = len;

        // The body is a list of four-byte version numbers, and a server that had nothing to offer
        // would not have sent the packet at all.
        return h->payloadLen > 0 && (h->payloadLen % 4) == 0;
    }

    if (h->version != QUIC_VERSION_1) {
        h->type = QUIC_PKT_UNKNOWN;
        h->pktLen = len;
        return true;
    }

    if (!(b0 & 0x40))
        return false;

    h->type = (uint8)((b0 >> 4) & 0x03);

    if (h->type == QUIC_PKT_RETRY) {
        size_t left = _quicRdLeft(&rd);
        if (left < QUIC_TAG_LEN)
            return false;

        h->token = rd.p;
        h->tokenLen = left - QUIC_TAG_LEN;
        h->tag = rd.p + h->tokenLen;
        h->pktLen = len;
        return true;
    }

    if (h->type == QUIC_PKT_INITIAL) {
        h->token = _quicRdVarintBytes(&rd, &h->tokenLen);
        if (rd.bad)
            return false;
    }

    uint64 plen = _quicRdVarint(&rd);
    if (rd.bad || plen > _quicRdLeft(&rd))
        return false;

    h->len = (size_t)plen;
    h->pnOff = (size_t)(rd.p - pkt);
    h->pktLen = h->pnOff + h->len;

    // The shortest possible payload is a one-byte packet number and the tag, and header
    // protection additionally needs four bytes plus a sixteen-byte sample from the packet number
    // onwards. The AEAD check that follows would catch a short packet anyway, but rejecting it
    // here keeps the sampling read in range.
    return h->len >= 1 + QUIC_TAG_LEN;
}

_Use_decl_annotations_
bool _quicHdrDecodePN(QuicPktHdr* h, const uint8* pkt, uint64 largestPn)
{
    if (h->type != QUIC_PKT_SHORT && h->type != QUIC_PKT_INITIAL && h->type != QUIC_PKT_0RTT &&
        h->type != QUIC_PKT_HANDSHAKE)
        return false;

    uint8 b0 = pkt[0];

    // The reserved bits are only visible once header protection has been removed, and RFC 9001
    // section 5.4 makes a nonzero value a protocol violation rather than something to ignore.
    uint8 reserved = (h->type == QUIC_PKT_SHORT) ? 0x18 : 0x0c;
    if (b0 & reserved)
        return false;

    // The key phase lives in the same protected byte as the packet number width, so it can only
    // be read here -- what _quicHdrDecode saw was still masked.
    if (h->type == QUIC_PKT_SHORT)
        h->keyPhase = (b0 & 0x04) != 0;

    h->pnLen = (uint8)((b0 & 0x03) + 1);
    if (h->len < (size_t)h->pnLen + QUIC_TAG_LEN)
        return false;

    QuicRd rd;
    _quicRdInit(&rd, pkt + h->pnOff, h->pnLen);
    h->pnEnc = _quicRdInt(&rd, h->pnLen);
    if (rd.bad)
        return false;

    h->pn = _quicPnDecode(largestPn, h->pnEnc, h->pnLen);
    h->payload = pkt + h->pnOff + h->pnLen;
    h->payloadLen = h->len - h->pnLen;
    return true;
}

// Resolves the width of the Length varint: whatever the sender pinned, or the shortest encoding
// that fits. A pinned width narrower than the value refuses rather than truncating.
static uint8 hdrLenSize(const QuicPktHdr* h)
{
    if (h->lenSize == 0)
        return _quicVarintSize(h->len);

    uint8 n = _quicVarintSize(h->len);
    return (n != 0 && h->lenSize >= n) ? h->lenSize : 0;
}

static uint8 hdrFirstByte(const QuicPktHdr* h)
{
    switch (h->type) {
    case QUIC_PKT_SHORT:
        return (uint8)(0x40 | (h->spin ? 0x20 : 0) | (h->keyPhase ? 0x04 : 0) | (h->pnLen - 1));
    case QUIC_PKT_VERSIONNEG:
        return 0x80;
    case QUIC_PKT_RETRY:
        return (uint8)(0xc0 | (QUIC_PKT_RETRY << 4));
    default:
        return (uint8)(0xc0 | (h->type << 4) | (h->pnLen - 1));
    }
}

_Use_decl_annotations_
size_t _quicHdrSize(const QuicPktHdr* h)
{
    if (h->dcid.len > QUIC_MAX_CID || h->scid.len > QUIC_MAX_CID)
        return 0;

    switch (h->type) {
    case QUIC_PKT_SHORT:
        if (h->pnLen < 1 || h->pnLen > 4)
            return 0;
        return 1 + h->dcid.len + h->pnLen;

    case QUIC_PKT_VERSIONNEG:
        return 1 + 4 + 1 + h->dcid.len + 1 + h->scid.len;

    case QUIC_PKT_RETRY:
        return 1 + 4 + 1 + h->dcid.len + 1 + h->scid.len + h->tokenLen + QUIC_TAG_LEN;

    case QUIC_PKT_INITIAL:
    case QUIC_PKT_0RTT:
    case QUIC_PKT_HANDSHAKE: {
        if (h->pnLen < 1 || h->pnLen > 4)
            return 0;

        uint8 ls = hdrLenSize(h);
        if (ls == 0)
            return 0;

        size_t sz = 1 + 4 + 1 + h->dcid.len + 1 + h->scid.len + ls + h->pnLen;
        if (h->type == QUIC_PKT_INITIAL) {
            uint8 ts = _quicVarintSize(h->tokenLen);
            if (ts == 0)
                return 0;
            sz += ts + h->tokenLen;
        }
        return sz;
    }

    default:
        return 0;
    }
}

_Use_decl_annotations_
bool _quicHdrEncode(QuicWr* wr, const QuicPktHdr* h)
{
    if (_quicHdrSize(h) == 0)
        return false;

    _quicWr8(wr, hdrFirstByte(h));

    if (h->type == QUIC_PKT_SHORT) {
        _quicWrBytes(wr, h->dcid.id, h->dcid.len);
        _quicWrInt(wr, h->pnEnc, h->pnLen);
        return !wr->bad;
    }

    _quicWr32(wr, (h->type == QUIC_PKT_VERSIONNEG) ? QUIC_VERSION_NEGOTIATE : h->version);
    _quicWr8(wr, h->dcid.len);
    _quicWrBytes(wr, h->dcid.id, h->dcid.len);
    _quicWr8(wr, h->scid.len);
    _quicWrBytes(wr, h->scid.id, h->scid.len);

    switch (h->type) {
    case QUIC_PKT_VERSIONNEG:
        // The version list itself is the caller's to append, since it is not part of the header.
        break;

    case QUIC_PKT_RETRY:
        if (!h->tag)
            return false;
        _quicWrBytes(wr, h->token, h->tokenLen);
        _quicWrBytes(wr, h->tag, QUIC_TAG_LEN);
        break;

    default:
        if (h->type == QUIC_PKT_INITIAL)
            _quicWrVarintBytes(wr, h->token, h->tokenLen);
        _quicWrVarintSized(wr, h->len, hdrLenSize(h));
        _quicWrInt(wr, h->pnEnc, h->pnLen);
        break;
    }

    return !wr->bad;
}

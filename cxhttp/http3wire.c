#include "http3_private.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------
// Readers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _h3RdInit(H3Rd* rd, const uint8* data, size_t len)
{
    rd->p   = data;
    rd->end = data + len;
    rd->bad = false;
}

_Use_decl_annotations_
size_t _h3RdLeft(const H3Rd* rd)
{
    return rd->bad ? 0 : (size_t)(rd->end - rd->p);
}

_Use_decl_annotations_
const uint8* _h3RdBytes(H3Rd* rd, size_t n)
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
uint8 _h3Rd8(H3Rd* rd)
{
    const uint8* p = _h3RdBytes(rd, 1);
    return p ? *p : 0;
}

_Use_decl_annotations_
uint64 _h3RdVarint(H3Rd* rd)
{
    if (rd->bad || rd->p == rd->end) {
        rd->bad = true;
        return 0;
    }

    // The top two bits of the first byte give the total width as a power of two; the remaining six
    // are the most significant bits of the value itself.
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

// ---------------------------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _h3WrInit(H3Wr* wr, uint8* buf, size_t bufsz)
{
    wr->base = buf;
    wr->p    = buf;
    wr->end  = buf + bufsz;
    wr->bad  = false;
}

_Use_decl_annotations_
size_t _h3WrLen(const H3Wr* wr)
{
    return (size_t)(wr->p - wr->base);
}

_Use_decl_annotations_
size_t _h3WrLeft(const H3Wr* wr)
{
    return wr->bad ? 0 : (size_t)(wr->end - wr->p);
}

_Use_decl_annotations_
void _h3Wr8(H3Wr* wr, uint8 val)
{
    if (wr->bad || wr->p == wr->end) {
        wr->bad = true;
        return;
    }
    *wr->p++ = val;
}

_Use_decl_annotations_
void _h3WrBytes(H3Wr* wr, const uint8* data, size_t len)
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
uint8 _h3VarintSize(uint64 val)
{
    if (val < ((uint64)1 << 6))
        return 1;
    if (val < ((uint64)1 << 14))
        return 2;
    if (val < ((uint64)1 << 30))
        return 4;
    if (val <= H3_VARINT_MAX)
        return 8;
    return 0;
}

_Use_decl_annotations_
void _h3WrVarint(H3Wr* wr, uint64 val)
{
    uint8 n = _h3VarintSize(val);
    if (wr->bad || n == 0 || (size_t)(wr->end - wr->p) < n) {
        wr->bad = true;
        return;
    }

    // The two-bit width tag rides in the top of the first byte, so the value has to be shifted
    // down out of the way before the bytes are laid out big-endian.
    static const uint8 tag[9] = { 0, 0x00, 0x40, 0, 0x80, 0, 0, 0, 0xc0 };
    for (uint8 i = 0; i < n; i++)
        wr->p[i] = (uint8)(val >> ((n - 1 - i) * 8));
    wr->p[0] |= tag[n];
    wr->p += n;
}

// ---------------------------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
uint8 _h3FrameHdrSize(uint64 type, uint64 len)
{
    uint8 t = _h3VarintSize(type);
    uint8 l = _h3VarintSize(len);
    if (t == 0 || l == 0)
        return 0;
    return (uint8)(t + l);
}

_Use_decl_annotations_
size_t _h3WrFrameHdr(uint8* buf, size_t bufsz, uint64 type, uint64 len)
{
    H3Wr wr;
    _h3WrInit(&wr, buf, bufsz);
    _h3WrVarint(&wr, type);
    _h3WrVarint(&wr, len);
    return wr.bad ? 0 : _h3WrLen(&wr);
}

// Peek the varint that starts `off` bytes into the ring, without consuming it. Returns how many
// bytes it occupies, or 0 if the ring does not hold all of them yet.
static size_t rdVarintRing(_Inout_ BufRing* src, size_t off, _Out_ uint64* out)
{
    uint8 b[8];

    if (bufringPeek(src, b, off, 1) != 1)
        return 0;

    size_t n = (size_t)1u << (b[0] >> 6);
    if (bufringPeek(src, b, off, n) != n)
        return 0;

    uint64 val = b[0] & 0x3f;
    for (size_t i = 1; i < n; i++)
        val = (val << 8) | b[i];

    *out = val;
    return n;
}

_Use_decl_annotations_
void _h3FrameReaderInit(H3FrameReader* fr)
{
    memset(fr, 0, sizeof(*fr));
}

_Use_decl_annotations_
void _h3FrameReaderFail(H3FrameReader* fr)
{
    fr->bad = true;
}

_Use_decl_annotations_
H3FrameResult _h3FrameReaderStep(H3FrameReader* fr, BufRing* src)
{
    if (fr->bad)
        return H3FR_Error;

    fr->avail = 0;

    if (!fr->inFrame) {
        // Both varints have to be present before either is consumed: a header split across two
        // reads must not leave the type consumed and the length still to come.
        uint64 type = 0, len = 0;
        size_t tn = rdVarintRing(src, 0, &type);
        if (tn == 0)
            return H3FR_NeedMore;
        size_t ln = rdVarintRing(src, tn, &len);
        if (ln == 0)
            return H3FR_NeedMore;

        bufringSkip(src, tn + ln);
        fr->type    = type;
        fr->len     = len;
        fr->remain  = len;
        fr->inFrame = true;
        return H3FR_Header;
    }

    if (fr->remain == 0) {
        fr->inFrame = false;
        return H3FR_Complete;
    }

    size_t have = src->total;
    if (have == 0)
        return H3FR_NeedMore;
    if ((uint64)have > fr->remain)
        have = (size_t)fr->remain;

    fr->avail   = have;
    fr->remain -= have;
    return H3FR_Payload;
}

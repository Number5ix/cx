#include "string_private.h"
#include "strencoding.h"

// map of all byte values to their hex digit value; 255 marks a non-hex character
static const uint8 _hex_invcharmap[256] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0,   1,   2,   3,   4,   5,   6,   7,   8,
    9,   255, 255, 255, 255, 255, 255, 255, 10,  11,  12,  13,  14,  15,  255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 10,  11,  12,  13,  14,  15,  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255
};

_Use_decl_annotations_
bool strHexEncode(strhandle out, const uint8* _Nonnull buf, uint32 sz, bool upper)
{
    if (!out || !buf)
        return false;

    const char* charmap = upper ? _strnum_udigits : _strnum_ldigits;

    // two hex digits per input byte
    uint32 elen = sz << 1;
    if (elen < sz)
        return false;   // overflowed

    _strReset(out, elen);
    uint8* c = strBuffer(out, elen);

    for (uint32 i = 0; i < sz; i++) {
        *c++ = charmap[buf[i] >> 4];
        *c++ = charmap[buf[i] & 0x0f];
    }

    return true;
}

_Use_decl_annotations_
uint32 strHexDecode(strref s, uint8* _Nullable d, uint32 bufsz)
{
    if (!STR_CHECK_VALID(s))
        return 0;

    uint32 len = _strFastLen((strref_v)s);
    // an odd number of digits can't describe whole bytes
    if (len & 1)
        return 0;

    uint32 dlen = len >> 1;
    if (!d)
        return dlen;

    if (dlen > bufsz)
        return 0;

    striter sti;
    striBorrow(&sti, s);

    uint8 ch = 0;
    for (uint32 i = 0; i < dlen; i++) {
        // return value of striChar can be ignored since we already checked length
        (void)striChar(&sti, &ch);
        uint8 hi = _hex_invcharmap[ch];
        (void)striChar(&sti, &ch);
        uint8 lo = _hex_invcharmap[ch];

        if ((hi | lo) & 0xf0)
            return 0;   // invalid character; abort decoding!

        d[i] = (uint8)((hi << 4) | lo);
    }

    if (dlen + 1 <= bufsz)
        d[dlen] = 0;   // add an extra null if there's room

    return dlen;
}

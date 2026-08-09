#include "string_private.h"
#include "strencoding.h"

// Unicode replacement character, U+FFFD, used for every byte that isn't part of a
// valid sequence. Always encodes to exactly 3 bytes.
#define U8_REPLACEMENT     0xfffd
#define U8_REPLACEMENT_LEN 3

_Use_decl_annotations_
uint32 strU8Len(strref s)
{
    if (!STR_CHECK_VALID(s))
        return 0;

    uint32 len = _strFastLen((strref_v)s);

    // ASCII is one code point per byte, and the flag may already be cached
    if (_strHdr(s) & STR_ASCII)
        return len;

    striter it;
    striBorrow(&it, s);

    uint32 count = 0;
    while (len > 0) {
        uint32 seqlen = _strUTF8Decode(&it, NULL);
        if (seqlen == 0 || seqlen > len)
            return 0;   // not valid UTF-8
        len -= seqlen;
        ++count;
    }

    return count;
}

_Use_decl_annotations_
int32 strU8Offset(strref s, int32 charIdx)
{
    if (!STR_CHECK_VALID(s))
        return (charIdx == 0 || charIdx == strEnd) ? 0 : -1;

    uint32 slen = _strFastLen((strref_v)s);

    if (charIdx == strEnd)
        return (int32)slen;

    int32 idx = charIdx;
    if (idx < 0) {
        // counting from the end needs the total, which is a scan of its own
        uint32 count = strU8Len(s);
        if (count == 0)
            return -1;   // empty or not valid UTF-8

        idx = (int32)count + charIdx;
        if (idx < 0)
            return -1;
    }

    striter it;
    striBorrow(&it, s);

    uint32 remain = slen, off = 0;
    for (int32 i = 0; i < idx; i++) {
        if (remain == 0)
            return -1;   // index is past the end of the string

        uint32 seqlen = _strUTF8Decode(&it, NULL);
        if (seqlen == 0 || seqlen > remain)
            return -1;   // not valid UTF-8

        remain -= seqlen;
        off += seqlen;
    }

    return (int32)off;
}

_Use_decl_annotations_
bool strSanitizeUTF8(strhandle o, strref s)
{
    if (!o)
        return false;

    if (!STR_CHECK_VALID(s)) {
        strDup(o, s);   // destroys any existing output; there is nothing to sanitize
        return true;
    }

    if (strValidUTF8(s)) {
        // nothing to fix, and strValidUTF8 has now cached the flag for next time
        if (*o != (string)s)
            strDup(o, s);
        return true;
    }

    uint32 slen = _strFastLen((strref_v)s);
    striter it;
    uint8 ch;

    // Pass 1: measure. Every replacement grows the string, so the output size has to
    // be known before allocating rather than resized as we go.
    uint32 outlen = 0, remain = slen;
    striBorrow(&it, s);
    while (remain > 0) {
        striter save  = it;
        uint32 seqlen = _strUTF8Decode(&it, NULL);

        if (seqlen > 0 && seqlen <= remain) {
            outlen += seqlen;
            remain -= seqlen;
        } else {
            // bad sequence; back up and drop exactly one byte
            it = save;
            (void)striChar(&it, &ch);
            outlen += U8_REPLACEMENT_LEN;
            --remain;
        }
    }

    // Pass 2: build. The source may also be the destination, so it stays untouched
    // until the new string is complete.
    string ret = 0;
    strReset(&ret, outlen);
    uint8* buf = _strBuffer(ret);

    uint32 p = 0;
    remain   = slen;
    striBorrow(&it, s);
    while (remain > 0) {
        striter save  = it;
        uint32 seqlen = _strUTF8Decode(&it, NULL);

        if (seqlen > 0 && seqlen <= remain) {
            // copy the sequence through byte for byte
            it = save;
            for (uint32 i = 0; i < seqlen; i++) {
                (void)striChar(&it, &ch);
                buf[p++] = ch;
            }
            remain -= seqlen;
        } else {
            it = save;
            (void)striChar(&it, &ch);
            p += _strUTF8Encode(&buf[p], U8_REPLACEMENT);
            --remain;
        }
    }

    buf[p] = 0;
    _strSetLen(ret, p);

    // valid UTF-8 by construction, but it contains U+FFFD so it can't be ASCII
    *_strHdrP(ret) &= ~STR_ENCODING_MASK;
    *_strHdrP(ret) |= STR_UTF8;

    strDestroy(o);
    *o = ret;

    return true;
}

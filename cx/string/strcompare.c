#include "string_private.h"

// Strings with no embedded length (plain C strings, _S"" literals, and everything
// _SL() produces on MSVC) would have to be measured with a cstrLen() scan before the
// length early-out below could be used -- and measured again by the iterator setup,
// and once more by the first seek. For the short identifier-sized strings these
// mostly are, that dwarfs the comparison itself.
//
// Instead walk the C buffer alongside the other string's runs and stop at its NUL,
// so neither string is ever measured. The helpers below all take the length-free
// side as a raw buffer; the cx side may still be a rope, hence the iterator.
//
// This is deliberately a plain byte loop rather than a word-at-a-time compare. Since
// the buffer's length is unknown, memcmp over a run could read past its terminator;
// the safe formulation needs a memchr plus a strncmp per run, and that setup costs
// more than the whole loop at the sizes this path exists to serve.

// Compares a NUL-terminated buffer against an arbitrary cx string. Stops at the first
// mismatch or at c's terminator, so c is never read past its NUL even if s contains an
// embedded one.
static bool _strEqLen0(_In_ strref_v s, _In_z_ const uint8* _Nonnull c)
{
    striter it;
    striBorrow(&it, s);

    for (;;) {
        if (it.len == 0)
            return *c == 0;   // s ran out; equal only if c did too

        for (uint32 j = 0; j < it.len; j++) {
            // the c[j] == 0 test catches c ending against an embedded NUL in s, which
            // would otherwise compare equal and run off the end of c
            if (c[j] != it.bytes[j] || c[j] == 0)
                return false;
        }

        c += it.len;
        striSeek(&it, it.len, STRI_BYTE, STRI_CUR);
    }
}

// Case-insensitive variant of _strEqLen0. tolower(0) is 0, so the explicit terminator
// test is still needed to keep c in bounds.
static bool _strEqiLen0(_In_ strref_v s, _In_z_ const uint8* _Nonnull c)
{
    striter it;
    striBorrow(&it, s);

    for (;;) {
        if (it.len == 0)
            return *c == 0;   // s ran out; equal only if c did too

        for (uint32 j = 0; j < it.len; j++) {
            if (tolower(c[j]) != tolower(it.bytes[j]) || c[j] == 0)
                return false;
        }

        c += it.len;
        striSeek(&it, it.len, STRI_BYTE, STRI_CUR);
    }
}

// Three-way compare of a NUL-terminated buffer against an arbitrary cx string; returns
// < 0 when c sorts first. Bytes compare as unsigned to match memcmp. Same NUL safety as
// _strEqLen0 above.
static int32 _strCmpLen0(_In_z_ const uint8* _Nonnull c, _In_ strref_v s)
{
    striter it;
    striBorrow(&it, s);

    for (;;) {
        if (it.len == 0)
            return *c ? 1 : 0;   // s ran out; c is longer unless it ended too

        for (uint32 j = 0; j < it.len; j++) {
            int32 ret = (int32)c[j] - (int32)it.bytes[j];
            if (ret != 0)
                return ret;   // mismatch
            if (c[j] == 0)
                return -1;    // both hit NUL, but s has bytes after it
        }

        c += it.len;
        striSeek(&it, it.len, STRI_BYTE, STRI_CUR);
    }
}

// Case-insensitive variant of _strCmpLen0
static int32 _strCmpiLen0(_In_z_ const uint8* _Nonnull c, _In_ strref_v s)
{
    striter it;
    striBorrow(&it, s);

    for (;;) {
        if (it.len == 0)
            return *c ? 1 : 0;   // s ran out; c is longer unless it ended too

        for (uint32 j = 0; j < it.len; j++) {
            int32 ret = tolower(c[j]) - tolower(it.bytes[j]);
            if (ret != 0)
                return ret;   // mismatch
            if (c[j] == 0)
                return -1;    // both hit NUL, but s has bytes after it
        }

        c += it.len;
        striSeek(&it, it.len, STRI_BYTE, STRI_CUR);
    }
}

_Use_decl_annotations_
_Pure bool strEq(strref s1, strref s2)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // avoid measuring strings that have no length field; see above. The threshold test
    // reads the other string, whose length is O(1), and falling through is always
    // correct -- it just pays the scan in exchange for memcmp, which is the better
    // trade only when the two share a very long common prefix.
    const uint8 *c1 = _strLen0Buf(s1), *c2 = _strLen0Buf(s2);
    if (c1 && c2)
        return cstrEq((const char*)c1, (const char*)c2);
    if (c1 && _strFastLen(s2) < STR_LEN0_SCAN_THRESH)
        return _strEqLen0(s2, c1);
    if (c2 && _strFastLen(s1) < STR_LEN0_SCAN_THRESH)
        return _strEqLen0(s1, c2);

    if (_strFastLen(s1) != _strFastLen(s2))
        return false;   // early out if lengths do not match

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);

    // strings are the same size, so both are guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(i1.len, i2.len);
        if (clen == 0)
            return true;    // end of strings, everything matched
        if (memcmp(i1.bytes, i2.bytes, clen))
            return false;   // mismatch
        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
    }

    return false;   // unreachable
}

_Use_decl_annotations_
_Pure int32 strCmp(strref s1, strref s2)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // see strEq above. Negating is safe here, the helper only ever returns a byte
    // difference or +/-1.
    const uint8 *c1 = _strLen0Buf(s1), *c2 = _strLen0Buf(s2);
    if (c1 && c2)
        return cstrCmp((const char*)c1, (const char*)c2);
    if (c1 && _strFastLen(s2) < STR_LEN0_SCAN_THRESH)
        return _strCmpLen0(c1, s2);
    if (c2 && _strFastLen(s1) < STR_LEN0_SCAN_THRESH)
        return -_strCmpLen0(c2, s1);

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    int ret;

    // these are NOT guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(i1.len, i2.len);
        ret         = memcmp(i1.bytes, i2.bytes, clen);
        if (ret != 0)
            return ret;   // mismatch

        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);

        if (i1.len == 0 && i2.len == 0)
            return 0;   // both hit end, match
        if (i1.len == 0)
            return -1;
        else if (i2.len == 0)
            return 1;
    }

    return 0;   // unreachable
}

_Use_decl_annotations_
_Pure bool strEqi(strref s1, strref s2)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // see strEq above
    const uint8 *c1 = _strLen0Buf(s1), *c2 = _strLen0Buf(s2);
    if (c1 && c2)
        return cstrCmpi((const char*)c1, (const char*)c2) == 0;
    if (c1 && _strFastLen(s2) < STR_LEN0_SCAN_THRESH)
        return _strEqiLen0(s2, c1);
    if (c2 && _strFastLen(s1) < STR_LEN0_SCAN_THRESH)
        return _strEqiLen0(s1, c2);

    if (_strFastLen(s1) != _strFastLen(s2))
        return false;   // early out if lengths do not match

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);

    // strings are the same size, so both are guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(i1.len, i2.len);
        if (clen == 0)
            return true;   // end of strings, everything matched

        for (uint32 j = 0; j < clen; j++) {
            if (tolower(i1.bytes[j]) != tolower(i2.bytes[j]))
                return false;
        }
        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
    }

    return false;   // unreachacle
}

_Use_decl_annotations_
_Pure int32 strCmpi(strref s1, strref s2)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // see strEq and strCmp above
    const uint8 *c1 = _strLen0Buf(s1), *c2 = _strLen0Buf(s2);
    if (c1 && c2)
        return cstrCmpi((const char*)c1, (const char*)c2);
    if (c1 && _strFastLen(s2) < STR_LEN0_SCAN_THRESH)
        return _strCmpiLen0(c1, s2);
    if (c2 && _strFastLen(s1) < STR_LEN0_SCAN_THRESH)
        return -_strCmpiLen0(c2, s1);

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    int ret = 0;

    // these are NOT guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(i1.len, i2.len);
        for (uint32 j = 0; j < clen; j++) {
            ret = tolower(i1.bytes[j]) - tolower(i2.bytes[j]);
            if (ret != 0)
                return ret;
        }

        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);

        if (i1.len == 0 && i2.len == 0)
            return 0;   // both hit end, match
        if (i1.len == 0)
            return -1;
        else if (i2.len == 0)
            return 1;
    }

    return 0;   // unreachable
}

_Use_decl_annotations_
_Pure bool strRangeEq(strref s1, strref s2, int32 off, uint32 len)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // negative offset means relative to end of string
    if (off < 0)
        off += _strFastLen(s1);
    if (off < 0)
        return false;

    if (clamphigh(_strFastLen(s1) - off, len) != clamphigh(_strFastLen(s2), len))
        return false;   // early out if lengths do not match

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    striSeek(&i1, off, STRI_BYTE, STRI_SET);

    // strings are the same size, so both are guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(min(i1.len, i2.len), len);
        if (clen == 0)
            return true;    // end of strings, everything matched
        if (memcmp(i1.bytes, i2.bytes, clen))
            return false;   // mismatch
        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
        len -= clen;
    }

    return false;   // unreachable
}

_Use_decl_annotations_
_Pure int32 strRangeCmp(strref s1, strref s2, int32 off, uint32 len)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // negative offset means relative to end of string
    if (off < 0)
        off += _strFastLen(s1);

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    striSeek(&i1, off, STRI_BYTE, STRI_SET);
    int ret;

    // these are NOT guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(min(i1.len, i2.len), len);
        ret         = memcmp(i1.bytes, i2.bytes, clen);
        if (ret != 0)
            return ret;   // mismatch

        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
        len -= clen;

        if (len == 0)
            return 0;   // both hit end, match
        if (i1.len == 0)
            return -1;
        else if (i2.len == 0)
            return 1;
    }

    return 0;   // unreachable
}

_Use_decl_annotations_
_Pure bool strRangeEqi(strref s1, strref s2, int32 off, uint32 len)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // negative offset means relative to end of string
    if (off < 0)
        off += _strFastLen(s1);

    if (clamphigh(_strFastLen(s1) - off, len) != clamphigh(_strFastLen(s2), len))
        return false;   // early out if lengths do not match

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    striSeek(&i1, off, STRI_BYTE, STRI_SET);

    // strings are the same size, so both are guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(min(i1.len, i2.len), len);
        if (clen == 0)
            return true;   // end of strings, everything matched

        for (uint32 j = 0; j < clen; j++) {
            if (tolower(i1.bytes[j]) != tolower(i2.bytes[j]))
                return false;
        }
        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
        len -= clen;
    }

    return false;   // unreachacle
}

_Use_decl_annotations_
_Pure int32 strRangeCmpi(strref s1, strref s2, int32 off, uint32 len)
{
    if (!STR_CHECK_VALID(s1))
        s1 = _strEmpty;
    if (!STR_CHECK_VALID(s2))
        s2 = _strEmpty;

    // negative offset means relative to end of string
    if (off < 0)
        off += _strFastLen(s1);

    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);
    striSeek(&i1, off, STRI_BYTE, STRI_SET);
    int ret = 0;

    // these are NOT guaranteed to hit the end at the same time
    for (;;) {
        uint32 clen = min(min(i1.len, i2.len), len);
        for (uint32 j = 0; j < clen; j++) {
            ret = tolower(i1.bytes[j]) - tolower(i2.bytes[j]);
            if (ret != 0)
                return ret;
        }

        striSeek(&i1, clen, STRI_BYTE, STRI_CUR);
        striSeek(&i2, clen, STRI_BYTE, STRI_CUR);
        len -= clen;

        if (len == 0)
            return 0;   // both hit end, match
        if (i1.len == 0)
            return -1;
        else if (i2.len == 0)
            return 1;
    }

    return 0;   // unreachable
}

_Use_decl_annotations_
_Pure bool strBeginsWith(strref s1, strref s2)
{
    return strRangeEq(s1, s2, 0, strLen(s2));
}

_Use_decl_annotations_
_Pure bool strBeginsWithi(strref s1, strref s2)
{
    return strRangeEqi(s1, s2, 0, strLen(s2));
}

_Use_decl_annotations_
_Pure bool strEndsWith(strref s1, strref s2)
{
    return strRangeEq(s1, s2, -(int32)strLen(s2), strLen(s2));
}

_Use_decl_annotations_
_Pure bool strEndsWithi(strref s1, strref s2)
{
    return strRangeEqi(s1, s2, -(int32)strLen(s2), strLen(s2));
}

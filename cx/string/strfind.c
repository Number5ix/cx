#include "string_private.h"

static _meta_inline uint8 chrFold(uint8 c, bool ci)
{
    return ci ? (uint8)tolower(c) : c;
}

// will be inlined and optimized based on the compile time ci value
static _meta_inline int32 findCharImpl(strref_v s, int32 b, char find, bool ci)
{
    uint32 slen, i;

    slen = _strFastLen(s);
    // allow negative starting index to mean from the end of the string
    if (b < 0)
        i = max(0, slen + b);
    else
        i = min((uint32)b, slen);

    uint8 fchr = chrFold((uint8)find, ci);

    striter it;
    striBorrow(&it, s);
    striSeek(&it, i, STRI_BYTE, STRI_SET);
    while (it.len > 0) {
        for (i = 0; i < it.len; i++) {
            if (chrFold(it.bytes[i], ci) == fchr)
                return (int32)(it.off + i);
        }
        striNext(&it);
    }

    return -1;
}

static _meta_inline int32 findCharRImpl(strref_v s, int32 e, char find, bool ci)
{
    // Conventional wisdom was wrong. Actually scanning backwards turns out to be
    // about 20% faster on average, probably because the conditions to check are
    // much simpler.

    uint32 slen;
    int32 i;

    slen = _strFastLen(s);
    // negative e indexes from the end of the string
    if (e < 0)
        slen = ((uint32)(-e) < slen) ? slen + e : 0;
    else if (e != strEnd)   // e == strEnd means the end of the string
        slen = min((uint32)e, slen);

    uint8 fchr = chrFold((uint8)find, ci);

    striter it;
    striBorrowRev(&it, s);
    while (it.len > 0) {
        if (it.off < slen) {
            for (i = min(it.len, slen - it.off) - 1; i >= 0; --i) {
                if (chrFold(it.bytes[i], ci) == fchr)
                    return i + it.off;
            }
        }
        striPrev(&it);
    }

    return -1;
}

_Use_decl_annotations_
int32 _strFindChar(strref_v s, int32 b, char find)
{
    return findCharImpl(s, b, find, false);
}

_Use_decl_annotations_
int32 _strFindCharR(strref_v s, int32 e, char find)
{
    return findCharRImpl(s, e, find, false);
}

// comparison helper that can handle degenerate case where
// string and substring are both ropes and don't have segments
// that line up cleanly
static _meta_inline bool striterEq(_In_ striter* _Nonnull istr_in, _In_ striter* _Nonnull isub_in,
                                   bool ci)
{
    // borrow iterator state
    striter istr = *istr_in;
    striter isub = *isub_in;
    for (;;) {
        uint32 clen = min(istr.len, isub.len);
        if (clen == 0)
            return !isub.len;   // if end of isub, everything matched

        if (!ci) {
            if (memcmp(istr.bytes, isub.bytes, clen))
                return false;   // mismatch
        } else {
            // no memcmp equivalent that folds case, so this run compares byte by byte
            for (uint32 j = 0; j < clen; j++) {
                if (chrFold(istr.bytes[j], true) != chrFold(isub.bytes[j], true))
                    return false;   // mismatch
            }
        }

        striSeek(&istr, clen, STRI_BYTE, STRI_CUR);
        striSeek(&isub, clen, STRI_BYTE, STRI_CUR);
    }

    return false;
}

static _meta_inline int32 findImpl(strref s, int32 b, strref find, bool ci)
{
    uint32 off, slen, i;

    if (!STR_CHECK_VALID(s) || strEmpty(find))
        return -1;

    if (_strFastLen((strref_v)find) == 1 && !(_strHdr(find) & STR_ROPE)) {
        // optimization for simple case
        return findCharImpl(s, b, _strBuffer(find)[0], ci);
    }

    slen = _strFastLen(s);
    // allow negative starting index to mean from the end of the string
    if (b < 0)
        off = max(0, slen + b);
    else
        off = min((uint32)b, slen);

    if (slen < off + _strFastLen(find))
        return -1;   // nonsensical, can't possibly fit

    // faster to search for first character of find string in a tight loop
    uint8 fchr = chrFold((uint8)strGetChar(find, 0), ci);

    striter istr, isub;
    striBorrow(&istr, s);
    striBorrow(&isub, find);
    striSeek(&istr, off, STRI_BYTE, STRI_SET);
    while (istr.len > 0) {
        for (i = 0; i < istr.len; i++) {
            if (chrFold(istr.bytes[i], ci) == fchr) {
                striSeek(&istr, istr.off + i, STRI_BYTE, STRI_SET);
                if (striterEq(&istr, &isub, ci))
                    return (int32)istr.off;
                i = 0;   // we reset the iterator, start at beginning
            }
        }
        striNext(&istr);
    }

    return -1;
}

static _meta_inline int32 findRImpl(strref s, int32 e, strref find, bool ci)
{
    // see findCharRImpl and findImpl for implementation notes
    uint32 slen;
    int32 i;
    if (!STR_CHECK_VALID(s) || strEmpty(find))
        return -1;

    if (_strFastLen((strref_v)find) == 1 && !(_strHdr(find) & STR_ROPE)) {
        // optimization for simple case
        return findCharRImpl(s, e, _strBuffer(find)[0], ci);
    }

    slen = _strFastLen(s);
    // negative e indexes from the end of the string
    if (e < 0)
        slen = ((uint32)(-e) < slen) ? slen + e : 0;
    else if (e != strEnd)   // e == strEnd means the end of the string
        slen = min((uint32)e, slen);

    // faster to search for first character of find string in a tight loop
    uint8 fchr = chrFold((uint8)strGetChar(find, 0), ci);

    striter istr, isub;
    striBorrowRev(&istr, s);
    striBorrow(&isub, find);
    while (istr.len > 0) {
        // complex condition is to handle "starting" at slen
        if (istr.off < slen) {
            for (i = min(istr.len, slen - istr.off) - 1; i >= 0; --i) {
                if (chrFold(istr.bytes[i], ci) == fchr) {
                    striter temp = istr;
                    striSeek(&temp, istr.off + i, STRI_BYTE, STRI_SET);
                    if (striterEq(&temp, &isub, ci))
                        return (int32)temp.off;
                }
            }
        }
        striPrev(&istr);
    }

    return -1;
}

_Use_decl_annotations_
int32 strFind(strref s, int32 b, strref find)
{
    return findImpl(s, b, find, false);
}

_Use_decl_annotations_
int32 strFindi(strref s, int32 b, strref find)
{
    return findImpl(s, b, find, true);
}

_Use_decl_annotations_
int32 strFindR(strref s, int32 e, strref find)
{
    return findRImpl(s, e, find, false);
}

_Use_decl_annotations_
int32 strFindRi(strref s, int32 e, strref find)
{
    return findRImpl(s, e, find, true);
}

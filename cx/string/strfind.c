#include "string_private.h"

// Case folding for the ci variants happens once here instead of many times later.
#define FIND_CHR_FOLD(find, ci) \
    uint8 fchr = (ci) ? (uint8)tolower((uint8)(find)) : (uint8)(find); \
    uint8 fchr2 = (ci) ? (uint8)toupper((uint8)(find)) : (uint8)(find)
#define FIND_CHR_MATCH(c, ci) ((c) == fchr || ((ci) && (c) == fchr2))

// will be inlined and optimized based on the compile time ci/invert values
static _meta_inline int32 findCharImpl(strref_v s, int32 b, char find, bool ci, bool invert)
{
    uint32 slen, i;

    slen = _strFastLen(s);
    // allow negative starting index to mean from the end of the string
    if (b < 0)
        i = max(0, slen + b);
    else
        i = min((uint32)b, slen);

    FIND_CHR_FOLD(find, ci);

    striter it;
    striBorrow(&it, s);
    striSeek(&it, i, STRI_BYTE, STRI_SET);
    while (it.len > 0) {
        for (i = 0; i < it.len; i++) {
            if (FIND_CHR_MATCH(it.bytes[i], ci) != invert)
                return (int32)(it.off + i);
        }
        striNext(&it);
    }

    return -1;
}

static _meta_inline int32 findCharRImpl(strref_v s, int32 e, char find, bool ci, bool invert)
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

    FIND_CHR_FOLD(find, ci);

    striter it;
    striBorrowRev(&it, s);
    while (it.len > 0) {
        if (it.off < slen) {
            for (i = min(it.len, slen - it.off) - 1; i >= 0; --i) {
                if (FIND_CHR_MATCH(it.bytes[i], ci) != invert)
                    return i + it.off;
            }
        }
        striPrev(&it);
    }

    return -1;
}

_Use_decl_annotations_
int32 strFindChar(strref s, int32 b, char find)
{
    if (!STR_CHECK_VALID(s))
        return -1;
    return findCharImpl((strref_v)s, b, find, false, false);
}

_Use_decl_annotations_
int32 strFindChari(strref s, int32 b, char find)
{
    if (!STR_CHECK_VALID(s))
        return -1;
    return findCharImpl((strref_v)s, b, find, true, false);
}

_Use_decl_annotations_
int32 strFindCharR(strref s, int32 e, char find)
{
    if (!STR_CHECK_VALID(s))
        return -1;
    return findCharRImpl((strref_v)s, e, find, false, false);
}

_Use_decl_annotations_
int32 strFindCharRi(strref s, int32 e, char find)
{
    if (!STR_CHECK_VALID(s))
        return -1;
    return findCharRImpl((strref_v)s, e, find, true, false);
}

// Membership table for the strFindAny family. Building it once up front turns an
// O(len * setsize) scan into a single pass over the string.
typedef bool charset[256];

static void buildCharSet(_Out_ charset set, _In_opt_ strref chars, bool ci)
{
    memset(set, 0, sizeof(charset));

    if (!STR_CHECK_VALID(chars))
        return;

    striter it;
    striBorrow(&it, chars);
    while (it.len > 0) {
        for (uint32 i = 0; i < it.len; i++) {
            uint8 c = it.bytes[i];
            set[c] = true;
            // For the ci variants, register both cases here so that the scan itself is
            // a fast table lookup. This avoids calling tolower() in the loop later, which
            // may not be a macro depending on the compiler.
            if (ci) {
                set[(uint8)tolower(c)] = true;
                set[(uint8)toupper(c)] = true;
            }
        }
        striNext(&it);
    }
}

// efficiently handle cllaers that only pass a single character in for some reason
static _meta_inline bool charSetIsChar(_In_opt_ strref chars, _Out_ char *find)
{
    if (!STR_CHECK_VALID(chars) || _strFastLen((strref_v)chars) != 1)
        return false;
    *find = (char)_strFastChar(chars, 0);
    return true;
}

// will be inlined and optimized based on the compile time ci/invert values
static _meta_inline int32 findAnyImpl(strref s, int32 b, strref chars, bool ci, bool invert)
{
    uint32 slen, i;

    if (!STR_CHECK_VALID(s))
        return -1;

    char find;
    if (charSetIsChar(chars, &find))
        return findCharImpl((strref_v)s, b, find, ci, invert);

    charset set;
    buildCharSet(set, chars, ci);

    slen = _strFastLen((strref_v)s);
    // allow negative starting index to mean from the end of the string
    if (b < 0)
        i = max(0, slen + b);
    else
        i = min((uint32)b, slen);

    striter it;
    striBorrow(&it, s);
    striSeek(&it, i, STRI_BYTE, STRI_SET);
    while (it.len > 0) {
        for (i = 0; i < it.len; i++) {
            if (set[it.bytes[i]] != invert)
                return (int32)(it.off + i);
        }
        striNext(&it);
    }

    return -1;
}

static _meta_inline int32 findAnyRImpl(strref s, int32 e, strref chars, bool ci, bool invert)
{
    // see findCharRImpl for implementation notes
    uint32 slen;
    int32 i;

    if (!STR_CHECK_VALID(s))
        return -1;

    char find;
    if (charSetIsChar(chars, &find))
        return findCharRImpl((strref_v)s, e, find, ci, invert);

    charset set;
    buildCharSet(set, chars, ci);

    slen = _strFastLen((strref_v)s);
    // negative e indexes from the end of the string
    if (e < 0)
        slen = ((uint32)(-e) < slen) ? slen + e : 0;
    else if (e != strEnd)   // e == strEnd means the end of the string
        slen = min((uint32)e, slen);

    striter it;
    striBorrowRev(&it, s);
    while (it.len > 0) {
        if (it.off < slen) {
            for (i = min(it.len, slen - it.off) - 1; i >= 0; --i) {
                if (set[it.bytes[i]] != invert)
                    return i + it.off;
            }
        }
        striPrev(&it);
    }

    return -1;
}

_Use_decl_annotations_
int32 strFindAny(strref s, int32 b, strref chars)
{
    return findAnyImpl(s, b, chars, false, false);
}

_Use_decl_annotations_
int32 strFindAnyi(strref s, int32 b, strref chars)
{
    return findAnyImpl(s, b, chars, true, false);
}

_Use_decl_annotations_
int32 strFindAnyR(strref s, int32 e, strref chars)
{
    return findAnyRImpl(s, e, chars, false, false);
}

_Use_decl_annotations_
int32 strFindAnyRi(strref s, int32 e, strref chars)
{
    return findAnyRImpl(s, e, chars, true, false);
}

_Use_decl_annotations_
int32 strFindNotAny(strref s, int32 b, strref chars)
{
    return findAnyImpl(s, b, chars, false, true);
}

_Use_decl_annotations_
int32 strFindNotAnyi(strref s, int32 b, strref chars)
{
    return findAnyImpl(s, b, chars, true, true);
}

_Use_decl_annotations_
int32 strFindNotAnyR(strref s, int32 e, strref chars)
{
    return findAnyRImpl(s, e, chars, false, true);
}

_Use_decl_annotations_
int32 strFindNotAnyRi(strref s, int32 e, strref chars)
{
    return findAnyRImpl(s, e, chars, true, true);
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
                if (_strChrFold(istr.bytes[j], true) != _strChrFold(isub.bytes[j], true))
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
        return findCharImpl(s, b, _strBuffer(find)[0], ci, false);
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
    char fc = (char)strGetChar(find, 0);
    FIND_CHR_FOLD(fc, ci);

    striter istr, isub;
    striBorrow(&istr, s);
    striBorrow(&isub, find);
    striSeek(&istr, off, STRI_BYTE, STRI_SET);
    while (istr.len > 0) {
        for (i = 0; i < istr.len; i++) {
            if (FIND_CHR_MATCH(istr.bytes[i], ci)) {
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
        return findCharRImpl(s, e, _strBuffer(find)[0], ci, false);
    }

    slen = _strFastLen(s);
    // negative e indexes from the end of the string
    if (e < 0)
        slen = ((uint32)(-e) < slen) ? slen + e : 0;
    else if (e != strEnd)   // e == strEnd means the end of the string
        slen = min((uint32)e, slen);

    // faster to search for first character of find string in a tight loop
    char fc = (char)strGetChar(find, 0);
    FIND_CHR_FOLD(fc, ci);

    striter istr, isub;
    striBorrowRev(&istr, s);
    striBorrow(&isub, find);
    while (istr.len > 0) {
        // complex condition is to handle "starting" at slen
        if (istr.off < slen) {
            for (i = min(istr.len, slen - istr.off) - 1; i >= 0; --i) {
                if (FIND_CHR_MATCH(istr.bytes[i], ci)) {
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

#include "string_private.h"
#include "cx/container/sarray.h"

// One body for all four array forms. anyset selects between a substring separator and a
// set of delimiter bytes; maxparts of 0 means unlimited. Both are compile-time constants
// at every call site, so each wrapper gets a specialized copy.
static _meta_inline int32 _strSplit(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s,
                                    _In_opt_ strref sep, bool empty, int32 maxparts, bool anyset)
{
    saClear(out);

    // for a character set every match is exactly one byte wide
    uint32 seplen = anyset ? 1 : strLen(sep);
    int32 start   = 0, next;
    string seg    = 0;

    while (!(maxparts > 0 && saSize(*out) >= maxparts - 1)) {
        next = anyset ? strFindAny(s, start, sep) : strFind(s, start, sep);
        if (next == -1)
            break;

        if (start != next || empty) {
            if (next != 0)   // degenerate case for starting with separator
                strSubStr(&seg, s, start, next);
            else
                strClear(&seg);
            saPushC(out, string, &seg);
        }
        start = next + seplen;
    }

    if (start < (int32)strLen(s) || empty) {
        strSubStr(&seg, s, start, strLen(s));
        saPushC(out, string, &seg);
    }

    strDestroy(&seg);
    return saSize(*out);
}

_Use_decl_annotations_
int32 strSplit(sa_string* _Nonnull out, strref s, strref sep, bool empty)
{
    return _strSplit(out, s, sep, empty, 0, false);
}

_Use_decl_annotations_
int32 strSplitAny(sa_string* _Nonnull out, strref s, strref chars, bool empty)
{
    return _strSplit(out, s, chars, empty, 0, true);
}

_Use_decl_annotations_
int32 strSplitMax(sa_string* _Nonnull out, strref s, strref sep, bool empty, int32 maxparts)
{
    return _strSplit(out, s, sep, empty, maxparts, false);
}

_Use_decl_annotations_
int32 strSplitAnyMax(sa_string* _Nonnull out, strref s, strref chars, bool empty, int32 maxparts)
{
    return _strSplit(out, s, chars, empty, maxparts, true);
}

// Cursor form. A cursor past the end of the string is the exhausted marker, which is
// what lets the trailing empty segment of "a,b," be produced exactly once.
static _meta_inline bool _strSplitNext(_In_opt_ strref s, _Inout_ int32* _Nonnull pos,
                                       _In_opt_ strref sep, _Inout_ strhandle out, bool anyset)
{
    if (!pos || !out)
        return false;

    int32 slen = (int32)strLen(s);
    if (*pos > slen)
        return false;

    uint32 seplen = anyset ? 1 : strLen(sep);
    int32 next    = anyset ? strFindAny(s, *pos, sep) : strFind(s, *pos, sep);

    if (next == -1) {
        // no separator left, so the rest of the string is the final segment
        strSubStr(out, s, *pos, slen);
        *pos = slen + 1;
        return true;
    }

    strSubStr(out, s, *pos, next);
    *pos = next + (int32)seplen;
    return true;
}

_Use_decl_annotations_
bool strSplitNext(strref s, int32* _Nonnull pos, strref sep, strhandle out)
{
    return _strSplitNext(s, pos, sep, out, false);
}

_Use_decl_annotations_
bool strSplitNextAny(strref s, int32* _Nonnull pos, strref chars, strhandle out)
{
    return _strSplitNext(s, pos, chars, out, true);
}

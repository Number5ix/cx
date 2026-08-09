#include "string_private.h"

// will be inlined and optimized based on the compile time ci value
static _meta_inline bool _strReplaceChar(_Inout_ strhandle o, _In_opt_ strref s, char from, char to,
                                         bool ci)
{
    if (!o)
        return false;

    if (!STR_CHECK_VALID(s)) {
        strDup(o, s);   // destroys any existing output; there is nothing to replace
        return true;
    }

    // this also handles the *o == s case
    strDup(o, s);
    _strFlatten(o, 0);

    uint32 len = _strFastLen(*o);
    uint8* buf = _strBuffer(*o);
    uint8 fchr = _strChrFold((uint8)from, ci);
    bool ascii = (uint8)from < 0x80 && (uint8)to < 0x80;

    for (uint32 i = 0; i < len; i++) {
        if (_strChrFold(buf[i], ci) == fchr)
            buf[i] = (uint8)to;
    }

    // swapping bytes outside the ASCII range can break a multi-byte sequence
    if (!ascii)
        *_strHdrP(*o) &= ~STR_ENCODING_MASK;

    return true;
}

_Use_decl_annotations_
bool strReplaceChar(strhandle o, strref s, char from, char to)
{
    return _strReplaceChar(o, s, from, to, false);
}

_Use_decl_annotations_
bool strReplaceChari(strhandle o, strref s, char from, char to)
{
    return _strReplaceChar(o, s, from, to, true);
}

static _meta_inline bool _strReplace(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref find,
                                     _In_opt_ strref repl, int32 max, bool ci)
{
    if (!o)
        return false;

    uint32 flen = strLen(find);
    int32 pos   = -1;

    if (STR_CHECK_VALID(s) && flen > 0)
        pos = ci ? strFindi(s, 0, find) : strFind(s, 0, find);

    if (pos < 0) {
        // no matches at all, this is just a copy
        strDup(o, s);
        return true;
    }

    // the source may also be the destination, so build into a new string
    string ret  = 0, seg = 0;
    uint32 slen = strLen(s);
    int32 start = 0, count = 0;

    while (pos >= 0) {
        if (pos > start) {
            strSubStr(&seg, s, start, pos);
            strAppend(&ret, seg);
        }
        strAppend(&ret, repl);

        start = pos + (int32)flen;
        ++count;
        if (max > 0 && count >= max)
            break;

        pos = ci ? strFindi(s, start, find) : strFind(s, start, find);
    }

    if (start < (int32)slen) {
        strSubStr(&seg, s, start, strEnd);
        strAppend(&ret, seg);
    }

    strDestroy(&seg);
    strDestroy(o);
    *o = ret;

    return true;
}

_Use_decl_annotations_
bool strReplace(strhandle o, strref s, strref find, strref repl, int32 max)
{
    return _strReplace(o, s, find, repl, max, false);
}

_Use_decl_annotations_
bool strReplacei(strhandle o, strref s, strref find, strref repl, int32 max)
{
    return _strReplace(o, s, find, repl, max, true);
}

_Use_decl_annotations_
bool strInsert(strhandle o, strref s, int32 off, strref ins)
{
    if (!o)
        return false;

    if (!STR_CHECK_VALID(ins)) {
        // nothing to insert, just a copy
        strDup(o, s);
        return true;
    }

    uint32 at = _strResolveOff(strLen(s), off);

    string left = 0, right = 0;
    strSubStr(&left, s, 0, (int32)at);
    strSubStr(&right, s, (int32)at, strEnd);

    // strNConcat is explicitly safe when the output is also one of the inputs
    bool ret = strNConcat(o, left, ins, right);

    strDestroy(&left);
    strDestroy(&right);

    return ret;
}

_Use_decl_annotations_
bool strErase(strhandle o, strref s, int32 b, int32 e)
{
    if (!o)
        return false;

    uint32 slen  = strLen(s);
    uint32 start = _strResolveOff(slen, b);
    uint32 end   = max(start, _strResolveOff(slen, e));

    if (start == end) {
        // empty range, just copy
        strDup(o, s);
        return true;
    }

    string left = 0, right = 0;
    strSubStr(&left, s, 0, (int32)start);
    strSubStr(&right, s, (int32)end, strEnd);

    bool ret = strConcat(o, left, right);

    strDestroy(&left);
    strDestroy(&right);

    return ret;
}

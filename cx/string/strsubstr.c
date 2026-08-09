#include "string_private.h"

static bool _strSubStr(_Inout_ strhandle o, _Inout_ strhandle ps, int32 b, int32 e, bool consume)
{
    uint32 off, len, slen;
    string s   = STR_SAFE_DEREF(ps);
    string ret = 0;

    if (!o || !s) {
        strDestroy(o);
        return false;
    }
    slen = _strFastLen(s);

    // negative indices count back from the end and strEnd is the end of the string
    off        = _strResolveOff(slen, b);
    uint32 end = _strResolveOff(slen, e);
    len        = (end > off) ? end - off : 0;

    if (len >= ROPE_SUBSTR_THRESH) {
        // this is a big substring, return a rope reference instead
        ret = _strCreateRope1(*ps, off, len);
    } else {
        if (*o == *ps && off == 0) {
            // optimization for reducing the length of a string
            _strFlatten(o, len);
            _strSetLen(*o, len);
            _strBuffer(*o)[len] = 0;
            // b and e are byte offsets, so this can slice a UTF-8 sequence in half.
            // That is deliberate -- snapping to sequence boundaries here would silently
            // change what byte-indexed callers get back. Use strSubStrU8 to cut by
            // code point instead.
            return true;
        } else if (*o != *ps) {
            ret = *o;               // steal reference
            *o  = NULL;
            _strReset(&ret, len);   // try to reuse buffer space
        } else {
            strReset(&ret, len);    // *o == *ps, destination needs to be separate
        }

        *_strHdrP(ret) &= ~STR_ENCODING_MASK;
        *_strHdrP(ret) |= _strHdr(s) & STR_ENCODING_MASK;
        _strFastCopy(s, off, _strBuffer(ret), len);
        _strBuffer(ret)[len] = 0;
        _strSetLen(ret, len);

        // may have sliced a UTF-8 sequence in half; see the note above
    }

    if (consume && *o != *ps)
        strDestroy(ps);
    strDestroy(o);
    *o = ret;

    return true;
}

_Use_decl_annotations_
bool strSubStr(strhandle o, strref s, int32 b, int32 e)
{
    return _strSubStr(o, (string*)&s, b, e, false);
}

_Use_decl_annotations_
bool strSubStrC(strhandle o, strhandle c, int32 b, int32 e)
{
    return _strSubStr(o, c, b, e, true);
}

_Use_decl_annotations_
bool strSubStrI(strhandle io, int32 b, int32 e)
{
    return _strSubStr(io, io, b, e, false);
}

_Use_decl_annotations_
bool strSubStrU8(strhandle o, strref s, int32 b, int32 e)
{
    if (!o)
        return false;

    if (!STR_CHECK_VALID(s))
        return strSubStr(o, s, b, e);

    uint32 count = strU8Len(s);
    if (count == 0) {
        // either empty or not valid UTF-8; byte and code point offsets agree on empty
        if (_strFastLen((strref_v)s) != 0)
            return false;
        return strSubStr(o, s, 0, 0);
    }

    // resolve code point indices the same way _strSubStr resolves byte offsets
    int32 bi = (int32)_strResolveOff(count, b);
    int32 ei = (int32)_strResolveOff(count, e);
    if (ei < bi)
        ei = bi;

    int32 bo = strU8Offset(s, bi);
    int32 eo = strU8Offset(s, ei);
    if (bo < 0 || eo < 0)
        return false;

    return strSubStr(o, s, bo, eo);
}

_Use_decl_annotations_
uint8 strGetChar(strref s, int32 i)
{
    if (!STR_CHECK_VALID(s))
        return 0;

    uint32 slen = _strFastLen(s);
    // negative counts back from the end; anything still past the end reads as 0
    uint32 off  = _strResolvePos(slen, i);

    if (off >= slen)
        return 0;

    return _strFastChar(s, off);
}

_Use_decl_annotations_
void strSetChar(strhandle s, int32 i, uint8 ch)
{
    if (!STR_CHECK_VALID(*s))
        strReset(s, 1);

    // negative counts back from the end and strEnd is a shortcut for appending; a
    // positive index past the end grows the string rather than being clamped
    uint32 off = _strResolvePos(_strFastLen(*s), i);

    if (off >= _strFastLen(*s))
        strSetLen(s, off + 1);

    if (!(_strHdr(*s) & STR_ROPE)) {
        _strMakeUnique(s, 0);
        _strBuffer(*s)[off] = ch;
    } else {
        string realstr;
        uint32 realoff, reallen, realstart;
        if (_strRopeRealStr(s, off, &realstr, &realoff, &reallen, &realstart, true))
            _strBuffer(realstr)[realoff] = ch;
    }
}

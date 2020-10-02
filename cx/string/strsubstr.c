#include "string_private.h"

static bool _strSubStr(string *o, string *ps, int32 b, int32 e, bool consume)
{
    uint32 off, len, slen;
    string s = STR_SAFE_DEREF(ps);
    string(ret);

    if (!o || !s) {
        strDestroy(o);
        return false;
    }
    slen = _strFastLen(s);

    // allow negative starting index to mean from the end of the string
    if (b < 0)
        off = max(0, slen + b);
    else
        off = min((uint32)b, slen);

    // similarly, negative e indexes from the end of the string as well
    if (e < 0)
        len = (slen + e > off) ? (slen + e) - off : 0;
    else if (e == strEnd)    // e == strEnd means the end of the string
        len = slen - off;
    else
        len = ((uint32)e > off) ? min((uint32)e, slen) - off : 0;

    if (len >= ROPE_SUBSTR_THRESH) {
        // this is a big substring, return a rope reference instead
        ret = _strCreateRope1(*ps, off, len);
    } else {
        if (*o == *ps && off == 0) {
            // optimization for reducing the length of a string
            _strFlatten(o, len);
            _strSetLen(*o, len);
            STR_BUFFER(*o)[len] = 0;
            // TODO: Make sure we didn't slice in the middle of a UTF-8 sequence?
            return true;
        } else if (*o != *ps) {
            ret = *o;                   // steal reference
            *o = NULL;
            _strReset(&ret, len);       // try to reuse buffer space
        } else {
            strInit(&ret, len);         // *o == *ps, destination needs to be separate
        }

        *STR_HDRP(ret) &= ~STR_ENCODING_MASK;
        *STR_HDRP(ret) |= STR_HDR(s) & STR_ENCODING_MASK;
        _strFastCopy(s, off, STR_BUFFER(ret), len);
        STR_BUFFER(ret)[len] = 0;
        _strSetLen(ret, len);

        // TODO: Make sure we didn't slice in the middle of a UTF-8 sequence?
    }

    if (consume && *o != *ps)
        strDestroy(ps);
    strDestroy(o);
    *o = ret;

    return true;
}

bool strSubStr(string *o, strref s, int32 b, int32 e)
{
    return _strSubStr(o, (string*)&s, b, e, false);
}

bool strSubStrC(string *o, string *c, int32 b, int32 e)
{
    return _strSubStr(o, c, b, e, true);
}

bool strSubStrI(string *io, int32 b, int32 e)
{
    return _strSubStr(io, io, b, e, false);
}

char strGetChar(strref s, int32 i)
{
    if (!STR_CHECK_VALID(s))
        return 0;

    uint32 off;

    // allow negative index to mean from the end of the string
    if (i < 0)
        off = max(0, _strFastLen(s) + i);
    else
        off = i;

    if (off >= _strFastLen(s))
        return 0;

    return _strFastChar(s, off);
}

void strSetChar(string *s, int32 i, char ch)
{
    if (!s || !STR_CHECK_VALID(*s))
        return;

    uint32 off;

    // allow negative index to mean from the end of the string
    if (i < 0)
        off = max(0, _strFastLen(*s) + i);
    else if (i == strEnd)
        off = _strFastLen(*s);      // shortcut for appending a character
    else
        off = i;

    if (off >= _strFastLen(*s))
        strSetLen(s, off + 1);

    if (!(STR_HDR(*s) & STR_ROPE)) {
        _strMakeUnique(s, 0);
        STR_BUFFER(*s)[off] = ch;
    } else {
        string realstr;
        uint32 realoff, reallen, realstart;
        if (_strRopeRealStr(s, off, &realstr, &realoff, &reallen, &realstart, true))
            STR_BUFFER(realstr)[realoff] = ch;
    }
}

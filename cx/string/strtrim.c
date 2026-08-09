#include "string_private.h"

// default set used when the caller passes NULL for chars
STR_CONST(_strTrimWhitespace, " \t\r\n\v\f");

// Everything was trimmed away. A NULL handle is already a valid empty string, so only
// touch the output when it actually holds something -- otherwise trimming an empty
// string would allocate one.
static void _strTrimToEmpty(_Inout_ strhandle o)
{
    if (STR_CHECK_VALID(*o))
        strClear(o);
}

// will be inlined and optimized based on the compile time left/right values
static _meta_inline bool _strTrim(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref chars,
                                  bool left, bool right)
{
    if (!o)
        return false;

    if (!chars)
        chars = _strTrimWhitespace;

    uint32 slen = strLen(s);
    int32 b     = 0;
    int32 e     = (int32)slen;

    if (left) {
        int32 f = strFindNotAny(s, 0, chars);
        if (f < 0) {
            // every byte is in the trim set, including the empty string case
            _strTrimToEmpty(o);
            return true;
        }
        b = f;
    }

    if (right) {
        int32 f = strFindNotAnyR(s, strEnd, chars);
        if (f < 0) {
            _strTrimToEmpty(o);
            return true;
        }
        e = f + 1;
    }

    if (b == 0 && e == (int32)slen) {
        // nothing to trim
        if (*o != (string)s)
            strDup(o, s);
        return true;
    }

    // strSubStr handles *o == s, and gives rope references above the threshold
    return strSubStr(o, s, b, e);
}

_Use_decl_annotations_
bool strTrim(strhandle o, strref s, strref chars)
{
    return _strTrim(o, s, chars, true, true);
}

_Use_decl_annotations_
bool strLTrim(strhandle o, strref s, strref chars)
{
    return _strTrim(o, s, chars, true, false);
}

_Use_decl_annotations_
bool strRTrim(strhandle o, strref s, strref chars)
{
    return _strTrim(o, s, chars, false, true);
}

#include "string_private.h"
#include "cx/container/sarray.h"

bool strJoin(_Inout_ string *o, _In_ sa_string arr, _In_opt_ strref sep)
{
    if (!o)
        return false;

    uint32 seplen = strLen(sep), seglen;
    int32 arrsize = saSize(arr);
    uint8 encoding = STR_ENCODING_MASK;
    int32 i;

    if (arrsize == 0) {
        _strReset(o, 0);
        return false;
    }
    if (!STR_CHECK_VALID(sep))
        sep = _strEmpty;

    uint32 sz = 0;
    for (i = 0; i < arrsize; i++) {
        sz += strLen(arr.a[i]) + (i > 0 ? seplen : 0);
        encoding &= STR_HDR(arr.a[i]) & STR_ENCODING_MASK;
    }

    _strReset(o, sz);
    uint8 *p = STR_BUFFER(*o);

    seglen = _strFastLen(arr.a[0]);
    _strFastCopy(arr.a[0], 0, p, seglen);
    p += seglen;
    for (i = 1; i < arrsize; i++) {
        if (STR_CHECK_VALID(arr.a[i])) {
            _strFastCopy(sep, 0, p, seplen);
            p += seplen;

            seglen = _strFastLen(arr.a[i]);
            _strFastCopy(arr.a[i], 0, p, seglen);
            p += seglen;
        }
    }

    *p = 0;             // null terminator

    *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
    *STR_HDRP(*o) |= encoding;
    _strSetLen(*o, sz);

    return true;
}

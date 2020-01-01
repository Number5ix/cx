#include "string_private.h"
#include "cx/container/sarray.h"

bool strJoin(string *o, string *arr, string sep)
{
    if (!o || !arr)
        return false;

    uint32 seplen = strLen(sep), seglen;
    int32 arrsize = saSize(&arr);
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
        sz += strLen(arr[i]) + (i > 0 ? seplen : 0);
        encoding &= STR_HDR(arr[i]) & STR_ENCODING_MASK;
    }

    _strReset(o, sz);
    char *p = STR_BUFFER(*o);

    seglen = _strFastLen(arr[0]);
    _strFastCopy(arr[0], 0, p, seglen);
    p += seglen;
    for (i = 1; i < arrsize; i++) {
        if (STR_CHECK_VALID(arr[i])) {
            _strFastCopy(sep, 0, p, seplen);
            p += seplen;

            seglen = _strFastLen(arr[i]);
            _strFastCopy(arr[i], 0, p, seglen);
            p += seglen;
        }
    }

    *p = 0;             // null terminator

    *STR_HDRP(*o) &= ~STR_ENCODING_MASK;
    *STR_HDRP(*o) |= encoding;
    _strSetLen(*o, sz);

    return true;
}

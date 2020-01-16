#include "sarray_private.h"

void stDtor_sarray(stype st, stgeneric *stgen, uint32 flags)
{
    _saDestroy(&stGenVal(sarray, *stgen));
}

void stCopy_sarray(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    stGenVal(sarray, *dest) = saSlice(&stGenVal(sarray, src), 0, 0);
}

uint32 stHash_sarray(stype st, stgeneric stgen, uint32 flags)
{
    void **handle = &stGenVal(sarray, stgen);
    uint32 ret = 0;
    if (!*handle)
        return ret;

    SArrayHeader *hdr = SARRAY_HDR(*handle);
    int32 i;
    for (i = 0; i < hdr->count; i++) {
        ret ^= _stHash(hdr->elemtype, HDRTYPEOPS(hdr),
                       stStored(hdr->elemtype, ELEMPTR(hdr, i)), 0);
    }

    return ret;
}

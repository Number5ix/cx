#include "sarray_private.h"

void stDtor_sarray(stype st, void *ptr, uint32 flags)
{
    _saDestroy((void**)ptr);
}

void stCopy_sarray(stype st, void *dest, const void *src, uint32 flags)
{
    *(void**)dest = saSlice(src, 0, 0);
}

uint32 stHash_sarray(stype st, const void *ptr, uint32 flags)
{
    void **handle = (void**)ptr;
    uint32 ret = 0;
    if (!*handle)
        return ret;

    SArrayHeader *hdr = SARRAY_HDR(*handle);
    int32 i;
    for (i = 0; i < hdr->count; i++) {
        ret ^= _stHash(hdr->elemtype, HDRTYPEOPS(hdr), ELEMPTR(hdr, i), 0);
    }

    return ret;
}

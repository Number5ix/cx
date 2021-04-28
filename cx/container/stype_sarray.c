#include "sarray_private.h"

void stDtor_sarray(stype st, stgeneric *gen, uint32 flags)
{
    _saDestroy(&gen->st_sarray);
}

void stCopy_sarray(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    saSlice(&dest->st_sarray, &src.st_sarray, 0, 0);
}

uint32 stHash_sarray(stype st, stgeneric gen, uint32 flags)
{
    sahandle handle = &gen.st_sarray;
    uint32 ret = 0;
    if (!handle->a)
        return ret;

    SArrayHeader *hdr = SARRAY_HDR(handle);
    int32 i;
    for (i = 0; i < hdr->count; i++) {
        ret ^= _stHash(hdr->elemtype, HDRTYPEOPS(hdr),
                       stStored(hdr->elemtype, ELEMPTR(hdr, i)), 0);
    }

    return ret;
}

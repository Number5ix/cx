#include "sarray_private.h"

void stDtor_sarray(stype st, stgeneric *gen, uint32 flags)
{
    _saRelease(&gen->st_sarray);
}

void stCopy_sarray(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    dest->st_sarray = saAcquire(src.st_sarray);
}

uint32 stHash_sarray(stype st, stgeneric gen, uint32 flags)
{
    sa_ref ref = gen.st_sarray;
    uint32 ret = 0;
    if (!ref.a)
        return ret;

    SArrayHeader *hdr = SARRAY_HDR(ref);
    int32 i;
    for (i = 0; i < hdr->count; i++) {
        ret ^= _stHash(hdr->elemtype, HDRTYPEOPS(hdr),
                       stStored(hdr->elemtype, ELEMPTR(hdr, i)), 0);
    }

    return ret;
}

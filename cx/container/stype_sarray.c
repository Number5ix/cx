#include "sarray_private.h"
#include "stype_sarray.h"

_Use_decl_annotations_
void stDtor_sarray(stype st, stgeneric* gen, flags_t flags)
{
    _saDestroy(&gen->st_sarray);
}

_Use_decl_annotations_
void stCopy_sarray(stype st, stgeneric* dest, stgeneric src, flags_t flags)
{
    saSlice(&dest->st_sarray, src.st_sarray, 0, 0);
}

_Use_decl_annotations_
intptr stCmp_sarray(stype st, stgeneric gen1, stgeneric gen2, flags_t flags)
{
    return (intptr)((char*)gen1.st_sarray.a - (char*)gen2.st_sarray.a);
}

_Use_decl_annotations_
uint32 stHash_sarray(stype st, stgeneric gen, flags_t flags)
{
    sa_ref ref = gen.st_sarray;
    uint32 ret = 0;
    if (!ref.a)
        return ret;

    SArrayHeader* hdr = SARRAY_HDR(ref);
    int32 i;
    for (i = 0; i < hdr->count; i++) {
        ret ^= _stHash(hdr->elemtype, stStored(hdr->elemtype, ELEMPTR(hdr, i)), 0);
    }

    return ret;
}

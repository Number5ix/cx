#include "stype_struct.h"
#include "cx/utils/murmur.h"
#include "struct.h"

void stDtor_struct(stype st, stgeneric* gen, uint32 flags)
{
    StructBase* b = gen->st_struct;

    _structDestroyMembersMany(b, 1);
}

static intptr structCompare(StructBase* b1, StructBase* b2)
{
    const StructInfo* i1 = b1 ? b1->structinfo : NULL;
    const StructInfo* i2 = b2 ? b2->structinfo : NULL;

    if (i1 != i2)
        return (intptr)i1 - (intptr)i2;

    if (!i1)
        return 0;   // they're both NULL

    // just compare raw bytes for speed; we shouldn't need actual deep compare for this
    return memcmp(b1, b2, i1->structsize);
}

intptr stCmp_struct(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    StructBase* b1 = gen1.st_struct;
    StructBase* b2 = gen2.st_struct;

    return structCompare(b1, b2);
}

static void structCopy(StructBase* bdest, StructBase* bsrc, flags_t flags)
{
    const StructInfo* info = bsrc->structinfo;

    for (int i = 0; i < info->nmembers; i++) {
        const StructMemberDesc* member = &info->members[i];
        void* destptr            = (char*)bdest + member->offset;
        memset(destptr, 0, info->structsize);   // ensure clean state for copy
        if (!(member->flags & STRUCT_NoCopy)) {
            void* srcptr = (char*)bsrc + member->offset;
            _stCopy(member->type, (stgeneric*)destptr, *(stgeneric*)srcptr, flags);
        }
    }
}

void stCopy_struct(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                   flags_t flags)
{
    StructBase *bsrc = src.st_struct, *bdest = dest->st_struct;
    if (!bsrc || !bdest || bsrc->structinfo != bdest->structinfo)
        return;

    structCopy(bdest, bsrc, flags);
}

uint32 stHash_struct(stype st, stgeneric gen, uint32 flags)
{
    StructBase* b = gen.st_struct;
    if (!b || !b->structinfo)
        return 0;

    return hashMurmur3((uint8*)b, b->structinfo->structsize);
}
void stDtor_structp(stype st, stgeneric* gen, uint32 flags)
{
    StructBase* b = gen->st_structp;

    _structDestroyMembersMany(b, 1);
    xaFree(b);
}

intptr stCmp_structp(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    StructBase* b1 = gen1.st_structp;
    StructBase* b2 = gen2.st_structp;

    return structCompare(b1, b2);
}

void stCopy_structp(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                    flags_t flags)
{
    StructBase* bsrc = src.st_structp;
    if (!bsrc || !bsrc->structinfo)
        return;

    dest->st_structp   = (StructBase*)xaAlloc(bsrc->structinfo->structsize);
    StructBase* bdest  = dest->st_structp;
    bdest->structinfo  = bsrc->structinfo;

    structCopy(bdest, bsrc, flags);
}

uint32 stHash_structp(stype st, stgeneric gen, uint32 flags)
{
    StructBase* b = gen.st_structp;
    if (!b || !b->structinfo)
        return 0;

    return hashMurmur3((uint8*)b, b->structinfo->structsize);
}

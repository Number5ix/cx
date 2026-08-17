#include "stype_struct.h"
#include "cx/utils/murmur.h"
#include "struct.h"

void stDtor_struct(stype st, stgeneric* gen, uint32 flags)
{
    StructBase* b = gen->st_struct;
    if (!b)
        return;

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

// Copies every member from bsrc into bdest. Caller must have zero-filled bdest and set its
// structinfo first -- zero-filling keeps padding deterministic for the byte-wise compare/hash.
static void structCopy(StructBase* bdest, StructBase* bsrc, flags_t flags)
{
    const StructInfo* info = bsrc->structinfo;

    for (int i = 0; i < info->nmembers; i++) {
        const StructMemberDesc* member = &info->members[i];
        if (member->flags & STRUCT_NoCopy)
            continue;   // already zero from the caller's fill

        char* destptr = (char*)bdest + member->offset;
        char* srcptr  = (char*)bsrc + member->offset;
        uint32 n      = member->arrsize ? member->arrsize : 1;
        size_t stride = stGetSize(member->schema->type);

        for (uint32 e = 0; e < n; e++) {
            _stCopy(member->schema->type,
                    stStoredPtr(member->schema->type, destptr + e * stride),
                    stStored(member->schema->type, srcptr + e * stride),
                    flags);
        }
    }
}

void stCopy_struct(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                   flags_t flags)
{
    StructBase *bsrc = src.st_struct, *bdest = dest->st_struct;
    if (!bsrc || !bdest || !bsrc->structinfo)
        return;

    memset(bdest, 0, bsrc->structinfo->structsize);
    bdest->structinfo = bsrc->structinfo;

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
    if (!b)
        return;

    _structDestroyMembersMany(b, 1);
    xaFree(b);
    gen->st_structp = NULL;
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

    dest->st_structp  = (StructBase*)xaAlloc(bsrc->structinfo->structsize, XA_Zero);
    StructBase* bdest = dest->st_structp;
    bdest->structinfo = bsrc->structinfo;

    structCopy(bdest, bsrc, flags);
}

uint32 stHash_structp(stype st, stgeneric gen, uint32 flags)
{
    StructBase* b = gen.st_structp;
    if (!b || !b->structinfo)
        return 0;

    return hashMurmur3((uint8*)b, b->structinfo->structsize);
}

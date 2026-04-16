#include "struct.h"

#include "cx/cx.h"

_Use_decl_annotations_
void _structInitMany(StructBase* base, const StructInfo* info, int number)
{
    bool hasdefaults = !!info->defaults;
    bool hasinit     = !!info->init;

    if (!hasdefaults)
        memset(base, 0, info->structsize * number);

    for (int i = 0; i < number; i++) {
        if (hasdefaults)
            memcpy(&base[i], info->defaults, info->structsize);
        if (hasinit)
            info->init(&base[i]);
        base[i].structinfo = info;
    }
}

_Use_decl_annotations_
StructBase* _structAlloc(StructInfo* info)
{
    StructBase* base = xaAlloc(info->structsize);
    _structInitMany(base, info, 1);
    return base;
}

_Use_decl_annotations_
void _structDestroyMembersMany(StructBase* base, int number)
{
    const StructInfo* info = base->structinfo;

    for (int i = 0; i < number; i++) {
        for (int m = 0; m < info->nmembers; m++) {
            const StructMemberDesc* member = &info->members[m];
            if (!(member->flags & STRUCT_NoDestroy)) {
                void* memberptr = (char*)&base[i] + member->offset;
                _stDestroy(member->type, (stgeneric*)memberptr, 0);
            }
        }
        if (info->destroy)
            info->destroy(&base[i]);
    }
}

_Use_decl_annotations_
void _structDestroy(StructBase** pbase)
{
    if (!*pbase)
        return;

    _structDestroyMembersMany(*pbase, 1);
    xaFree(*pbase);
    *pbase = NULL;
}

_Use_decl_annotations_
const StructInfo* structSetFind(const StructSet* ss, strref name)
{
    if (!ss || strEmpty(name))
        return NULL;
    int lo = 0, hi = ss->nentries - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strCmp(ss->entries[mid]->name, name);
        if (cmp == 0) return ss->entries[mid];
        if (cmp < 0)  lo = mid + 1;
        else          hi = mid - 1;
    }
    return NULL;
}

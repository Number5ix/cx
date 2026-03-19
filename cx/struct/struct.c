#include "struct.h"

#include "cx/cx.h"

void _structInitMany(StructBase* base, StructInfo* info, int number)
{
    memset(base, 0, info->structsize * number);
    if (info->init) {
        for (int i = 0; i < number; i++) {
            info->init(&base[i]);
        }
    }
}

StructBase* _structAlloc(StructInfo* info)
{
    StructBase* base = xaAlloc(info->structsize);
    _structInitMany(base, info, 1);
    return base;
}

void _structDestroyMembersMany(StructBase* base, int number)
{
    StructInfo* info = base->structinfo;

    for (int i = 0; i < number; i++) {
        for (int m = 0; m < info->nmembers; m++) {
            StructMemberDesc* member = &info->members[m];
            if (!(member->flags & STRUCT_NoDestroy)) {
                void* memberptr = (char*)&base[i] + member->offset;
                _stDestroy(member->type, NULL, (stgeneric*)memberptr, 0);
            }
        }
        if (info->destroy)
            info->destroy(&base[i]);
    }
}

void _structDestroy(StructBase** pbase)
{
    if (!*pbase)
        return;

    _structDestroyMembersMany(*pbase, 1);
    xaFree(*pbase);
    *pbase = NULL;
}
#include "struct.h"

#include "cx/cx.h"

// StructBase is just the common header, so index by structsize rather than sizeof(StructBase).
_meta_inline StructBase* structAt(StructBase* base, const StructInfo* info, int i)
{
    return (StructBase*)((char*)base + (size_t)i * info->structsize);
}

// Number of elements for a member: arrsize for a fixed C array, else 1.
_meta_inline uint32 structMemberCount(const StructMemberDesc* member)
{
    return member->arrsize ? member->arrsize : 1;
}

// Embedded struct members are zero-filled along with their container, so their structinfo is
// still NULL -- stamp it and run init so the member is usable. Skip members that already have
// one (e.g. from a defaults blob).
static void structInitMembers(StructBase* s, const StructInfo* info)
{
    for (int m = 0; m < info->nmembers; m++) {
        const StructMemberDesc* member = &info->members[m];
        if (member->schema->type->id != stTypeId(struct))
            continue;

        const StructInfo* sub = (const StructInfo*)member->schema->ext;
        if (!sub)
            continue;   // bare `struct` token, no identity -- nothing to stamp

        char* p       = (char*)s + member->offset;
        uint32 n      = structMemberCount(member);
        size_t stride = stGetSize(member->schema->type);

        for (uint32 e = 0; e < n; e++) {
            StructBase* es = (StructBase*)(p + e * stride);
            if (!es->structinfo)
                _structInitMany(es, sub, 1);
        }
    }
}

_Use_decl_annotations_
void _structInitMany(StructBase* base, const StructInfo* info, int number)
{
    bool hasdefaults = !!info->defaults;
    bool hasinit     = !!info->init;

    if (!hasdefaults)
        memset(base, 0, info->structsize * number);

    for (int i = 0; i < number; i++) {
        StructBase* s = structAt(base, info, i);
        if (hasdefaults)
            memcpy(s, info->defaults, info->structsize);
        s->structinfo = info;
        // before the custom init, so it sees usable members
        structInitMembers(s, info);
        if (hasinit)
            info->init(s);
    }
}

_Use_decl_annotations_
StructBase* _structAlloc(const StructInfo* info)
{
    StructBase* base = xaAlloc(info->structsize);
    _structInitMany(base, info, 1);
    return base;
}

void _structDestroyMember(const StructMemberDesc* member, StructBase* s)
{
    char* p       = (char*)s + member->offset;
    uint32 n      = structMemberCount(member);
    size_t stride = stGetSize(member->schema->type);

    for (uint32 e = 0; e < n; e++)
        _stDestroy(member->schema->type, stStoredPtr(member->schema->type, p + e * stride), 0);
}

_Use_decl_annotations_
void _structDestroyMembersMany(StructBase* base, int number)
{
    const StructInfo* info = base->structinfo;
    if (!info)
        return;   // never initialized, or already destroyed

    for (int i = 0; i < number; i++) {
        StructBase* s = structAt(base, info, i);

        // Destructor runs before members are torn down, so it still sees valid data.
        if (info->destroy)
            info->destroy(s);

        for (int m = 0; m < info->nmembers; m++) {
            const StructMemberDesc* member = &info->members[m];
            if (member->flags & STRUCT_NoDestroy)
                continue;
            _structDestroyMember(member, s);
        }
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
        if (cmp == 0)
            return ss->entries[mid];
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

#include "stype.h"

#include "cx/container/hashtable.h"
#include "cx/thread/rwlock.h"
#include "cx/utils/lazyinit.h"
#include "cx/utils/murmur.h"

// Implementation note: The hashtable is created using a static type that is already canonical. This
// prevents recursive canonicalization and the associated locking issues.

static RWLock stRegistryLock;
static hashtable stRegistry;
static LazyInitState stRegistryInit;

// Compare and hash functions below define the key that we want to use for canonical types.
// This is type ID and size (which is all it ever needed once `param` -- the last field that
// made a descriptor's identity depend on anything but its own shape -- was removed).

static intptr STypeInfoRef_cmp(stype st, stgeneric s1, stgeneric s2, flags_t flags)
{
    const STypeInfo* st1 = (const STypeInfo*)s1.st_ptr;
    const STypeInfo* st2 = (const STypeInfo*)s2.st_ptr;

    if (st1->id != st2->id)
        return ((intptr)st1->id - (intptr)st2->id);
    return ((intptr)st1->size - (intptr)st2->size);
}

static uint32 STypeInfoRef_hash(stype st, stgeneric gen, flags_t flags)
{
    STypeInfo* info = (STypeInfo*)gen.st_ptr;
    // A struct-typed key here would leave the alignment gap after a uint16 as
    // uninitialized stack padding -- fold the two fields into one integer instead of
    // relying on struct layout to not need a hole.
    uint64 hkey     = ((uint64)info->id << 32) | info->size;

    return hashMurmur3((uint8*)&hkey, sizeof(hkey));
}

static stDefine(stypeinforef) {
    .id   = stTypeId(ptr),
    .size = sizeof(STypeInfo*),
    .ops  = { .cmp = STypeInfoRef_cmp, .hash = STypeInfoRef_hash }
};

// SType harness for the custom type
#define SType_stypeinforef                         STypeInfo*
#define STStorageType_stypeinforef                 STypeInfo*
#define STypeArg_stypeinforef(type, val)           stgeneric(ptr, (void*)val)
#define STypeArgPtr_stypeinforef(type, val)        stgeneric(ptr, (void**)val)
#define STypeCheckedArg_stypeinforef(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArgPtr_stypeinforef(type, val) stType(type), stArgPtr(type, val)

static void initRegistry(void* unused)
{
    // Registry is a hashed set of dynamically-allocated STypeInfo* structures that never move or
    // are freed
    rwlockInit(&stRegistryLock);
    htInit(&stRegistry, stypeinforef, none, 16);
}

stype _stGetCanonical(stype st)
{
    lazyInit(&stRegistryInit, initRegistry, NULL);

    // The stCanonical macro should filter out non-temporary types before we get here; ensure it's
    // always being used.
    devAssert(st->flags & stFlag(Temporary));

    stype ret = NULL;
    withReadLock (&stRegistryLock) {
        htelem elem = htFind(stRegistry, stypeinforef, st, none, NULL);
        if (elem)
            ret = hteKey(stRegistry, stypeinforef, elem);
    }
    if (ret)
        return ret;   // found it!

    withWriteLock (&stRegistryLock) {
        // check again in case it was added while we were waiting for the lock
        htelem elem = htFind(stRegistry, stypeinforef, st, none, NULL);
        if (elem)
            ret = hteKey(stRegistry, stypeinforef, elem);

        if (!ret) {
            // add it
            STypeInfo* newst = xaAlloc(sizeof(STypeInfo));
            *newst           = *st;               // copy contents of the temporary type
            newst->flags &= ~stFlag(Temporary);   // it's not temporary anymore!
            htInsert(&stRegistry, stypeinforef, newst, none, NULL);
            ret = newst;
        }
    }

    return ret;
}

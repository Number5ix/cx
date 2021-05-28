#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>
#include <cx/utils/refcount.h>

typedef struct hashtable_ref {
    void *_is_hashtable;
} hashtable_ref;

typedef struct htelem_ref {
    void *_is_htelem;
} htelem_ref;

typedef struct hashtable_ref* hashtable;
typedef struct htelem_ref* htelem;

typedef struct HashTableHeader {
    // extended header, present only if HT_Extended is set
    STypeOps keytypeops;
    STypeOps valtypeops;

    // hashtable header begins here
    refcount ref;
    int32 slots;            // table size
    int32 used;             // number of used slots (including deleted)
    int32 valid;            // number of slots containing valid data

    stype keytype;
    stype valtype;
    uint32 flags;

    void *data[1];
} HashTableHeader;

typedef struct htiter {
    HashTableHeader *hdr;
    int32 slot;
    htelem elem;
} htiter;

#define HTABLE_HDRSIZE (offsetof(HashTableHeader, data))
#define HTABLE_HDR(ref) ((HashTableHeader*)(((uintptr)(&((ref)->_is_hashtable))) - HTABLE_HDRSIZE))

#define htSlots(ref) ((ref) ? HTABLE_HDR((ref))->slots : 0)
#define htUsed(ref) ((ref)) ? HTABLE_HDR((ref))->used : 0)
#define htSize(ref) ((ref) ? HTABLE_HDR((ref))->valid : 0)
#define htKeyType(ref) ((ref) ? HTABLE_HDR((ref))->keytype : 0)
#define htValType(ref) ((ref) ? HTABLE_HDR((ref))->valtype : 0)
#define hteKeyPtr(ref, elem, type) ((stStorageType(type)*)((ref) ? ((elem) && &((elem)->_is_htelem), (elem)) : 0))
#define hteValPtr(ref, elem, type) ((stStorageType(type)*)(((ref) && (elem)) ? ((uintptr)((elem) && &((elem)->_is_htelem), (elem)) + stGetSize(HTABLE_HDR((ref))->keytype)) : 0))
#define hteKey(ref, elem, type) (*hteKeyPtr(ref, elem, type))
#define hteVal(ref, elem, type) (*hteValPtr(ref, elem, type))
#define htiKeyPtr(iter, type) (((stStorageType(type)*)((iter).elem)))
#define htiValPtr(iter, type) (((stStorageType(type)*)((uintptr)((iter).elem) + stGetSize((iter).hdr->keytype))))
#define htiKey(iter, type) (*htiKeyPtr(iter, type))
#define htiVal(iter, type) (*htiValPtr(iter, type))

enum HASHTABLE_FLAGS_ENUM {
    HT_                = 0x0000,
    HT_CaseInsensitive = 0x0001,    // only for string keys
    HT_RefKeys         = 0x0002,    // use a borrowed reference for keys rather than copying them
    HT_Ref             = 0x0004,    // use a borrowed reference for values rather than copying them

    // internal use only, do not set manually
    HTINT_Quadratic    = 0x2000,    // use quadratic probing rather than linear
    HTINT_Pow2         = 0x4000,    // size forced to power of 2 to avoid division
    HTINT_Extended     = 0x8000,    // extended header is present
};

enum HASHTABLE_GROW_ENUM {
    HT_GROW_At75    = 0x00,     // Grow at 75% full (balanced)
    HT_GROW_At50    = 0x01,     // Grow at 50% full (better performance)
    HT_GROW_At90    = 0x02,     // Grow at 90% full (worse performance, memory efficient)
    HT_GROW_AT_MASK = 0x0f,

    HT_GROW_By100   = 0x00,     // Grow by 100% (balanced)
    HT_GROW_By200   = 0x10,     // Grow by 200%
    HT_GROW_By300   = 0x20,     // Grow by 300% (better performance)
    HT_GROW_By50    = 0x40,     // Grow by 50% (worse performance, memory efficient)
    HT_GROW_BY_MASK = 0xf0,

    // some presets
    HT_GROW_MaxSpeed = HT_GROW_At50 | HT_GROW_By300,
    HT_GROW_MinSize  = HT_GROW_At90 | HT_GROW_By50,
};

enum HASHTABLE_FUNC_FLAGS_ENUM {
    HTFUNC_             = 0,

    // Valid for: htInsert
    // Does not insert if a matching key already exists.
    HTFUNC_Ignore       = 0x00010000,

    // Valid for: htFind
    // If copying out an object-type variable, copy a borrowed reference rather
    // than acquiring a reference or making a copy.
    HTFUNC_Borrow       = 0x00020000,

    // internal use only
    HTFUNCINT_Consume   = 0x10000000,
};

#define HT_GROW_MASK (0xff000000)
#define htGrow(flag) (((uint32)HT_GROW_##flag) << 24)
#define HT_Grow(flag) (((uint32)HT_GROW_##flag) << 24)
#define HT_GET_GROW(flags) ((flags) >> 24)

void _htInit(hashtable *out, stype keytype, STypeOps *keyops, stype valtype, STypeOps *valops, int32 initsz, uint32 flags);
#define htInit(out, keytype, valtype, initsz, ...) _htInit(out, stFullType(keytype), stFullType(valtype), initsz, func_flags(HT, __VA_ARGS__))

_meta_inline hashtable htAcquire(hashtable htbl)
{
    refcountInc(&HTABLE_HDR(htbl)->ref);
    return htbl;
}
void htRelease(hashtable *htbl);
void htClear(hashtable *htbl);
void htSetSize(hashtable *htbl, int32 newsz);
void htClone(hashtable *out, hashtable ref);

htelem _htInsert(hashtable *htbl, stgeneric key, stgeneric val, uint32 flags);
_meta_inline htelem _htInsertChecked(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric val, uint32 flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsert(htbl, key, val, flags);
}
htelem _htInsertPtr(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags);
_meta_inline htelem _htInsertCheckedC(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, uint32 flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsertPtr(htbl, key, val, flags);
}
#define htInsert(htbl, ktype, key, vtype, val, ...) _htInsertChecked(htbl, stCheckedArg(ktype, key), stCheckedArg(vtype, val), func_flags(HTFUNC, __VA_ARGS__))
// Consumes *value*, not key
#define htInsertC(htbl, ktype, key, vtype, val, ...) _htInsertCheckedC(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val), func_flags(HTFUNC, __VA_ARGS__) | HTFUNCINT_Consume)

htelem _htFind(hashtable htbl, stgeneric key, stgeneric *val, uint32 flags);
_meta_inline htelem _htFindChecked(hashtable htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, uint32 flags)
{
    devAssert(htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    devAssert(stGetId(valtype) == stTypeId(none) || stEq(htValType(htbl), valtype));
    return _htFind(htbl, key, val, flags);
}
#define htFind(htbl, ktype, key, vtype, val_copy_out, ...) _htFindChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_copy_out), func_flags(HTFUNC, __VA_ARGS__))

// If val_copy_out is provided, the value is extracted into it rather than being destroyed
bool _htExtract(hashtable *htbl, stgeneric key, stgeneric *val);
_meta_inline bool _htExtractChecked(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype));
    devAssert(stGetId(valtype) == stTypeId(none) || stEq(htValType(*htbl), valtype));
    return _htExtract(htbl, key, val);
}
#define htExtract(htbl, ktype, key, vtype, val_copy_out) _htExtractChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_copy_out))
#define htRemove(htbl, ktype, key) _htExtractChecked(htbl, stCheckedArg(ktype, key), stType(none), NULL)

// Just checks for the existence of a key
bool _htHasKey(hashtable htbl, stgeneric key);
_meta_inline bool _htHasKeyChecked(hashtable htbl, stype keytype, stgeneric key)
{
    devAssert(htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    return _htHasKey(htbl, key);
}
#define htHasKey(htbl, ktype, key) _htHasKeyChecked(htbl, stCheckedArg(ktype, key))

// Hash table iterator
bool htiInit(htiter *iter, hashtable htbl);
bool htiNext(htiter *iter);
void htiFinish(htiter *iter);
_meta_inline bool htiValid(htiter *iter) { return iter->elem; }

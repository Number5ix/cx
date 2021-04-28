#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>

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
#define HTABLE_HDR(handle) ((HashTableHeader*)(((uintptr)(handle)) - HTABLE_HDRSIZE))

#define htSlots(handle) (*(handle) ? HTABLE_HDR(*(handle))->slots : 0)
#define htUsed(handle) (*(handle) ? HTABLE_HDR(*(handle))->used : 0)
#define htValid(handle) (*(handle) ? HTABLE_HDR(*(handle))->valid : 0)
#define htKeyType(handle) (*(handle) ? HTABLE_HDR(*(handle))->keytype : 0)
#define htValType(handle) (*(handle) ? HTABLE_HDR(*(handle))->valtype : 0)
#define hteKeyPtr(handle, elem, type) ((stStorageType(type)*)(*(handle) ? (elem) : 0))
#define hteValPtr(handle, elem, type) ((stStorageType(type)*)((*(handle) && (elem)) ? ((uintptr)(elem) + stGetSize(HTABLE_HDR(*(handle))->keytype)) : 0))
#define hteKey(handle, elem, type) (*hteKeyPtr(handle, elem, type))
#define hteVal(handle, elem, type) (*hteValPtr(handle, elem, type))
#define htiKeyPtr(iter, type) (((stStorageType(type)*)((iter).elem)))
#define htiValPtr(iter, type) (((stStorageType(type)*)((uintptr)((iter).elem) + stGetSize((iter).hdr->keytype))))
#define htiKey(iter, type) (*htiKeyPtr(iter, type))
#define htiVal(iter, type) (*htiValPtr(iter, type))

enum HASHTABLE_FLAGS_ENUM {
    HT_                = 0x0000,
    HT_CaseInsensitive = 0x0001,    // only for string keys
    HT_RefKeys         = 0x0002,    // only reference keys rather than copying them
    HT_Ref             = 0x0004,    // only reference values rather than copying them

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
    // Removes key if found.
    HTFUNC_Destroy      = 0x00020000,

    // Valid for: htFind
    // Removes key if found (does not destroy).
    HTFUNC_RemoveOnly   = 0x00040000,

    // internal use only
    HTFUNCINT_Consume   = 0x10000000,
};

#define HT_GROW_MASK (0xff000000)
#define htGrow(flag) (((uint32)HT_GROW_##flag) << 24)
#define HT_Grow(flag) (((uint32)HT_GROW_##flag) << 24)
#define HT_GET_GROW(flags) ((flags) >> 24)

void _htInit(hashtable *out, stype keytype, STypeOps *keyops, stype valtype, STypeOps *valops, int32 initsz, uint32 flags);
#define htInit(out, keytype, valtype, initsz, ...) _htInit(out, stFullType(keytype), stFullType(valtype), initsz, func_flags(HT, __VA_ARGS__))

void htDestroy(hashtable *htbl);
void htClear(hashtable *htbl);
void htSetSize(hashtable *htbl, int32 newsz);

htelem _htInsert(hashtable *htbl, stgeneric key, stgeneric val, uint32 flags);
_meta_inline htelem _htInsertChecked(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric val, uint32 flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype) && stEq(htValType(htbl), valtype));
    return _htInsert(htbl, key, val, flags);
}
htelem _htInsertPtr(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags);
_meta_inline htelem _htInsertCheckedC(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, uint32 flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype) && stEq(htValType(htbl), valtype));
    return _htInsertPtr(htbl, key, val, flags);
}
#define htInsert(htbl, ktype, key, vtype, val, ...) _htInsertChecked(htbl, stCheckedArg(ktype, key), stCheckedArg(vtype, val), func_flags(HTFUNC, __VA_ARGS__))
// Consumes *value*, not key
#define htInsertC(htbl, ktype, key, vtype, val, ...) _htInsertCheckedC(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val), func_flags(HTFUNC, __VA_ARGS__) | HTFUNCINT_Consume)

bool _htFind(hashtable *htbl, stgeneric key, stgeneric *val, uint32 flags);
_meta_inline bool _htFindChecked(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, uint32 flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype) && stEq(htValType(htbl), valtype));
    return _htFind(htbl, key, val, flags);
}
#define htFind(htbl, ktype, key, vtype, val_out, ...) _htFindChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_out), func_flags(HTFUNC, __VA_ARGS__))

// Always destroys; use htFind with HT_RemoveOnly if you want to get a reference out of a hashtable
// without destroying it.
bool _htRemove(hashtable *htbl, stgeneric key);
_meta_inline bool _htRemoveChecked(hashtable *htbl, stype keytype, stgeneric key)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    return _htRemove(htbl, key);
}
#define htRemove(htbl, ktype, key) _htRemoveChecked(htbl, stCheckedArg(ktype, key))

// Gets a pointer to the key/value data inside the hashtable rather than copying it out
htelem _htFindElem(hashtable *htbl, stgeneric key);
_meta_inline htelem _htFindElemChecked(hashtable *htbl, stype keytype, stgeneric key)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    return _htFindElem(htbl, key);
}
#define htFindElem(htbl, ktype, key) _htFindElemChecked(htbl, stCheckedArg(ktype, key))

// Just checks for the existence of a key
_meta_inline bool _htHasKeyChecked(hashtable *htbl, stype keytype, stgeneric key)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    return _htFindElem(htbl, key);
}
#define htHasKey(htbl, ktype, key) _htHasKeyChecked(htbl, stCheckedArg(ktype, key))

// Hash table iterator
bool htiInit(htiter *iter, hashtable *htbl);
bool htiNext(htiter *iter);
void htiFinish(htiter *iter);
_meta_inline bool htiValid(htiter *iter) { return iter->elem; }

#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>

typedef struct hashtable_ref {
    void *_is_hashtable;
} hashtable_ref;

typedef struct hashtable_ref *hashtable;
typedef struct HTChunkInfo HTChunkInfo;

typedef struct HashTableHeader {
    // extended header, present only if HT_Extended is set
    STypeOps keytypeops;
    STypeOps valtypeops;

    // hashtable header begins here
    uint32 idxsz;               // index size
    uint32 idxused;             // number of used index entries (including deleted)
    uint32 storsz;              // number of chunks in storage array
    uint32 storused;            // number of used storage slots (including deleted)
    uint32 valid;               // number of valid items in the hash table

    stype keytype;
    stype valtype;
    uint32 flags;

    HTChunkInfo *chunks;
    void **keystorage;
    void **valstorage;
    uint32 index[1];
} HashTableHeader;

// htelem is just the slot number (0 is reserved for "null")
typedef uint32 htelem;

typedef struct htiter {
    HashTableHeader *hdr;
    uint32 slot;
} htiter;

// HT_SLOTS_PER_CHUNK MUST BE A POWER OF TWO!
#ifdef _64BIT
#define HT_CHUNK_SHIFT 6
#else
#define HT_CHUNK_SHIFT 5
#endif
#define HT_SLOTS_PER_CHUNK (1 << HT_CHUNK_SHIFT)
#define HT_CHUNK_MASK (HT_SLOTS_PER_CHUNK - 1)
#define HT_QUARTER_CHUNK (HT_SLOTS_PER_CHUNK >> 2)
#define HT_QCHUNK_MASK (HT_QUARTER_CHUNK - 1)

#define HTABLE_HDRSIZE (offsetof(HashTableHeader, index))
#define HTABLE_HDR(ref) ((HashTableHeader*)(((uintptr)(&((ref)->_is_hashtable))) - HTABLE_HDRSIZE))

#define htSize(ref) ((ref) ? HTABLE_HDR((ref))->valid : 0)
#define htKeyType(ref) ((ref) ? HTABLE_HDR((ref))->keytype : 0)
#define htValType(ref) ((ref) ? HTABLE_HDR((ref))->valtype : 0)
#define hteKeyPtrHdr(hdr, elem, type) ((stStorageType(type)*)(((hdr) && (elem)) ? _hteElemKeyPtr(hdr, elem) : 0))
#define hteValPtrHdr(hdr, elem, type) ((stStorageType(type)*)(((hdr) && (elem)) ? _hteElemValPtr(hdr, elem) : 0))
#define hteKeyPtr(ref, elem, type) hteKeyPtrHdr(HTABLE_HDR(ref), elem, type)
#define hteValPtr(ref, elem, type) hteValPtrHdr(HTABLE_HDR(ref), elem, type)
#define hteKey(ref, elem, type) (*hteKeyPtr(ref, elem, type))
#define hteVal(ref, elem, type) (*hteValPtr(ref, elem, type))
#define htiKeyPtr(iter, type) (hteKeyPtrHdr((iter).hdr, (iter).slot, type))
#define htiValPtr(iter, type) (hteValPtrHdr((iter).hdr, (iter).slot, type))
#define htiKey(iter, type) (*htiKeyPtr(iter, type))
#define htiVal(iter, type) (*htiValPtr(iter, type))

enum HASHTABLE_FLAGS_ENUM {
    HT_CaseInsensitive = 0x0001,    // only for string keys
    HT_RefKeys         = 0x0002,    // use a borrowed reference for keys rather than copying them
    HT_Ref             = 0x0004,    // use a borrowed reference for values rather than copying them

    // By default the storage array grows by quarter-chunks to avoid wasting too much memory. High
    // traffic hash tables can set this flag to always allocate full chunks for better insert performance.
    HT_InsertOpt       = 0x0008,

    // Ultra-compact mode does not preallocate any storage memory. Inserts will be slow!
    // This is useful for tables that should be as small as possible and are used for read-mostly lookups.
    HT_Compact         = 0x0010,

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
    // Valid for: htInsert
    // Does not insert if a matching key already exists.
    HT_Ignore     = 0x00010000,

    // Valid for: htFind
    // If copying out an object-type variable, copy a borrowed reference rather
    // than acquiring a reference or making a copy.
    HT_Borrow     = 0x00020000,

    // internal use only
    HTINT_Consume = 0x10000000,
};

#define HT_GROW_MASK (0xff000000)
#define HT_Grow(flag) (((uint32)HT_GROW_##flag) << 24)
#define HT_GET_GROW(flags) ((flags) >> 24)

void _htInit(hashtable *out, stype keytype, STypeOps *keyops, stype valtype, STypeOps *valops, uint32 initsz, flags_t flags);
#define htInit(out, keytype, valtype, initsz, ...) _htInit(out, stFullType(keytype), stFullType(valtype), initsz, opt_flags(__VA_ARGS__))

void htDestroy(hashtable *htbl);
void htClear(hashtable *htbl);
void htReindex(hashtable *htbl, uint32 minsz);
void htRepack(hashtable *htbl);
void htClone(hashtable *out, hashtable ref);

htelem _htInsert(hashtable *htbl, stgeneric key, stgeneric val, flags_t flags);
_meta_inline htelem _htInsertChecked(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric val, flags_t flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsert(htbl, key, val, flags);
}
htelem _htInsertPtr(hashtable *htbl, stgeneric key, stgeneric *val, flags_t flags);
_meta_inline htelem _htInsertCheckedC(hashtable *htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, flags_t flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsertPtr(htbl, key, val, flags);
}
#define htInsert(htbl, ktype, key, vtype, val, ...) _htInsertChecked(htbl, stCheckedArg(ktype, key), stCheckedArg(vtype, val), opt_flags(__VA_ARGS__))
// Consumes *value*, not key
#define htInsertC(htbl, ktype, key, vtype, val, ...) _htInsertCheckedC(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val), opt_flags(__VA_ARGS__) | HTINT_Consume)

htelem _htFind(hashtable htbl, stgeneric key, stgeneric *val, flags_t flags);
_meta_inline htelem _htFindChecked(hashtable htbl, stype keytype, stgeneric key, stype valtype, stgeneric *val, flags_t flags)
{
    devAssert(htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    devAssert(stGetId(valtype) == stTypeId(none) || stEq(htValType(htbl), valtype));
    return _htFind(htbl, key, val, flags);
}
#define htFind(htbl, ktype, key, vtype, val_copy_out, ...) _htFindChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_copy_out), opt_flags(__VA_ARGS__))

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

void *_hteElemKeyPtr(HashTableHeader *hdr, htelem elem);
void *_hteElemValPtr(HashTableHeader *hdr, htelem elem);

// Hash table iterator
bool htiInit(htiter *iter, hashtable htbl);
bool htiNext(htiter *iter);
void htiFinish(htiter *iter);
_meta_inline bool htiValid(htiter *iter) { return iter->slot > 0; }

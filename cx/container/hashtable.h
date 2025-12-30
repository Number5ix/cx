#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>

// Hash table container with type-safe generic key-value storage
// Uses open addressing with configurable probing strategy and chunked storage allocation

typedef struct hashtable_ref {
    void* _is_hashtable;
} hashtable_ref;

// Opaque handle to a hash table
// Must be destroyed with htDestroy() when no longer needed
typedef struct hashtable_ref* hashtable;
typedef struct HTChunkInfo HTChunkInfo;

// Internal structure - do not access directly
// Use htSize(), htKeyType(), htValType() macros instead
typedef struct HashTableHeader {
    // extended header, present only if HT_Extended is set
    STypeOps keytypeops;
    STypeOps valtypeops;

    // hashtable header begins here
    uint32 idxsz;      // index size
    uint32 idxused;    // number of used index entries (including deleted)
    uint32 storsz;     // number of chunks in storage array
    uint32 storused;   // number of used storage slots (including deleted)
    uint32 valid;      // number of valid items in the hash table

    stype keytype;
    stype valtype;
    uint32 flags;

    HTChunkInfo* chunks;
    void** keystorage;
    void** valstorage;
    uint32 index[1];
} HashTableHeader;

// Element handle - represents a slot in the hash table
// Value of 0 is reserved for "null" / not found
// Can be used to retrieve key/value pointers without a second lookup
typedef uint32 htelem;

// Hash table iterator state
typedef struct htiter {
    HashTableHeader* hdr;
    uint32 slot;
} htiter;

// Storage allocation constants
// HT_SLOTS_PER_CHUNK MUST BE A POWER OF TWO!
#ifdef _64BIT
#define HT_CHUNK_SHIFT 6
#else
#define HT_CHUNK_SHIFT 5
#endif
#define HT_SLOTS_PER_CHUNK (1 << HT_CHUNK_SHIFT)
#define HT_CHUNK_MASK      (HT_SLOTS_PER_CHUNK - 1)
#define HT_QUARTER_CHUNK   (HT_SLOTS_PER_CHUNK >> 2)
#define HT_QCHUNK_MASK     (HT_QUARTER_CHUNK - 1)

// Internal macros for header access - do not use directly
#define HTABLE_HDRSIZE  (offsetof(HashTableHeader, index))
#define HTABLE_HDR(ref) ((HashTableHeader*)(((uintptr)(&((ref)->_is_hashtable))) - HTABLE_HDRSIZE))

// Internal function - use htSize(), htKeyType(), htValType() macros instead
_Ret_valid_ _meta_inline HashTableHeader* _htHdr(_In_ hashtable ref)
{
    return HTABLE_HDR(ref);
}

// uint32 htSize(hashtable ref);
// Returns the number of valid entries in the hash table
#define htSize(ref) ((ref) ? _htHdr((ref))->valid : 0)

// stype htKeyType(hashtable ref);
// Returns the runtime type descriptor for the hash table's keys
#define htKeyType(ref) ((ref) ? _htHdr((ref))->keytype : 0)

// stype htValType(hashtable ref);
// Returns the runtime type descriptor for the hash table's values
#define htValType(ref) ((ref) ? _htHdr((ref))->valtype : 0)

// [type] *hteKeyPtrHdr(HashTableHeader *hdr, stype type, htelem elem);
// Returns a typed pointer to the key stored at the given element
// Internal version that takes a header pointer directly
#define hteKeyPtrHdr(hdr, type, elem) ((stStorageType(type)*)_hteElemKeyPtr(hdr, elem))

// [type] *hteValPtrHdr(HashTableHeader *hdr, stype type, htelem elem);
// Returns a typed pointer to the value stored at the given element
// Internal version that takes a header pointer directly
#define hteValPtrHdr(hdr, type, elem) ((stStorageType(type)*)_hteElemValPtr(hdr, elem))

// [type] *hteKeyPtr(hashtable ref, stype type, htelem elem);
// Returns a typed pointer to the key stored at the given element
// The element handle must be obtained from htInsert() or htFind()
#define hteKeyPtr(ref, type, elem) hteKeyPtrHdr(_htHdr(ref), type, elem)

// [type] *hteValPtr(hashtable ref, stype type, htelem elem);
// Returns a typed pointer to the value stored at the given element
// The element handle must be obtained from htInsert() or htFind()
#define hteValPtr(ref, type, elem) hteValPtrHdr(_htHdr(ref), type, elem)

// [type] hteKey(hashtable ref, stype type, htelem elem);
// Returns the key stored at the given element (by value)
#define hteKey(ref, type, elem) (*hteKeyPtr(ref, type, elem))

// [type] hteVal(hashtable ref, stype type, htelem elem);
// Returns the value stored at the given element (by value)
#define hteVal(ref, type, elem) (*hteValPtr(ref, type, elem))

// [type] *htiKeyPtr(stype type, htiter iter);
// Returns a typed pointer to the key at the current iterator position
#define htiKeyPtr(type, iter) (hteKeyPtrHdr((iter).hdr, type, (iter).slot))

// [type] *htiValPtr(stype type, htiter iter);
// Returns a typed pointer to the value at the current iterator position
#define htiValPtr(type, iter) (hteValPtrHdr((iter).hdr, type, (iter).slot))

// [type] htiKey(stype type, htiter iter);
// Returns the key at the current iterator position (by value)
#define htiKey(type, iter) (*htiKeyPtr(type, iter))

// [type] htiVal(stype type, htiter iter);
// Returns the value at the current iterator position (by value)
#define htiVal(type, iter) (*htiValPtr(type, iter))

// Hash table configuration flags
enum HASHTABLE_FLAGS_ENUM {
    // Case-insensitive key matching - only valid for string keys
    HT_CaseInsensitive = 0x0001,

    // Use borrowed references for keys instead of copying them
    // Keys will not be destroyed when the hash table is freed
    // Only use this if keys are guaranteed to outlive the hash table
    HT_RefKeys = 0x0002,

    // Use borrowed references for values instead of copying them
    // Values will not be destroyed when the hash table is freed
    // Only use this if values are guaranteed to outlive the hash table
    HT_Ref = 0x0004,

    // Optimize for high-frequency inserts by allocating full chunks at once
    // By default the storage array grows by quarter-chunks to avoid wasting too much memory. High
    // traffic hash tables can set this flag to always allocate full chunks for better insert
    // performance.
    HT_InsertOpt = 0x0008,

    // Ultra-compact mode with minimal memory footprint
    // Does not preallocate any storage memory. Inserts will be slow!
    // This is useful for tables that should be as small as possible and are used for read-mostly
    // lookups.
    HT_Compact = 0x0010,

    // Internal use only - do not set manually
    HTINT_Quadratic = 0x2000,   // use quadratic probing rather than linear
    HTINT_Pow2      = 0x4000,   // size forced to power of 2 to avoid division
    HTINT_Extended  = 0x8000,   // extended header is present
};

// Growth rate configuration - controls how much to grow when expanding the table
enum HASHTABLE_GROWBY_ENUM {
    HT_GROW_By100   = 0x00,   // Double size (balanced default)
    HT_GROW_By200   = 0x10,   // Triple size
    HT_GROW_By300   = 0x20,   // Quadruple size (better performance, uses more memory)
    HT_GROW_By50    = 0x40,   // Grow by 50% (worse performance, memory efficient)
    HT_GROW_BY_MASK = 0xf0,
};

// Growth threshold configuration - controls when to expand the table
enum HASHTABLE_GROW_ENUM {
    HT_GROW_At75    = 0x00,   // Grow when 75% full (balanced default)
    HT_GROW_At50    = 0x01,   // Grow when 50% full (better performance, uses more memory)
    HT_GROW_At90    = 0x02,   // Grow when 90% full (worse performance, memory efficient)
    HT_GROW_AT_MASK = 0x0f,

    // Preset combinations for common use cases
    HT_GROW_MaxSpeed = (uint32)HT_GROW_At50 | (uint32)HT_GROW_By300,   // Maximum performance
    HT_GROW_MinSize  = (uint32)HT_GROW_At90 | (uint32)HT_GROW_By50,    // Minimum memory usage
};

// Function-specific flags
enum HASHTABLE_FUNC_FLAGS_ENUM {
    // Valid for: htInsert
    // Do not insert if a matching key already exists - returns existing element instead
    HT_Ignore = 0x00010000,

    // Valid for: htFind
    // If copying out an object-type variable, copy a borrowed reference rather
    // than acquiring a reference or making a deep copy
    HT_Borrow = 0x00020000,

    // Internal use only - do not use directly
    HTINT_Consume = 0x10000000,
};

// Helper macros for growth configuration

#define HT_GROW_MASK (0xff000000)

// HT_Grow(flag);
// Converts a growth flag to the format used in the flags parameter
// Example: htInit(&ht, string, int32, 16, HT_Grow(MaxSpeed))
#define HT_Grow(flag) (((uint32)HT_GROW_##flag) << 24)

// Internal macro - extracts growth settings from flags
#define HT_GET_GROW(flags) ((flags) >> 24)

void _htInit(_Outptr_ hashtable* out, stype keytype, _In_opt_ STypeOps* keyops, stype valtype,
             _In_opt_ STypeOps* valops, uint32 initsz, flags_t flags);

// void htInit(hashtable *out, keytype, valtype, uint32 initsz, [flags_t flags]);
//
// Initializes a new hash table with the specified key and value types
//
// Parameters:
//   out - Pointer to hashtable handle to receive the new table
//   keytype - Runtime type for keys (e.g., string, int32, etc.)
//   valtype - Runtime type for values, or 'none' for a hash set
//   initsz - Initial capacity (will be rounded up, 0 for default)
//   flags - Optional combination of HT_* flags
//
// Example:
//   hashtable ht;
//   htInit(&ht, string, int32, 16, HT_Grow(MaxSpeed));
//   // ... use table ...
//   htDestroy(&ht);
#define htInit(out, keytype, valtype, initsz, ...) \
    _htInit(out, stFullType(keytype), stFullType(valtype), initsz, opt_flags(__VA_ARGS__))

// Destroys a hash table and frees all associated memory
// All keys and values are properly destroyed (unless HT_Ref/HT_RefKeys was used)
// Sets *htbl to NULL after destruction
// Safe to call with NULL or a pointer to NULL
void htDestroy(_Inout_ptr_uninit_ hashtable* htbl);

// Removes all entries from the hash table but keeps the structure allocated
// All keys and values are properly destroyed (unless HT_Ref/HT_RefKeys was used)
// The table can be reused after clearing
void htClear(_Inout_ptr_ hashtable* htbl);

// Rebuilds the hash table's index with a new minimum size
// This can be used to grow or shrink the table manually
// All elements are preserved - only the internal index is rebuilt
void htReindex(_Inout_ptr_ hashtable* htbl, uint32 minsz);

// Rebuilds the hash table to eliminate deleted entries and fragmentation
// This creates a compact copy of the table with no wasted space
// Useful after many deletions to reclaim memory
void htRepack(_Inout_ptr_ hashtable* htbl);

// Creates a deep copy of the hash table
// All keys and values are properly copied according to their type semantics
// The output must be destroyed with htDestroy() when no longer needed
void htClone(_Outptr_ hashtable* out, _In_ hashtable ref);

// Internal function - do not call directly, use htInsert() or htInsertC() macro instead
htelem _htInsert(_Inout_ptr_ hashtable* htbl, _In_ stgeneric key, _In_ stgeneric val,
                 flags_t flags);

// Internal function with type checking
_meta_inline htelem _htInsertChecked(_Inout_ptr_ hashtable* htbl, stype keytype, _In_ stgeneric key,
                                     stype valtype, _In_ stgeneric val, flags_t flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsert(htbl, key, val, flags);
}

// Internal SAL annotation helper
#define _ht_Consume_Arg_                                       \
    _When_(flags& HTINT_Consume, _Pre_notnull_ _Post_invalid_) \
        _When_(!(flags & HTINT_Consume), _Inout_)

// Internal function - do not call directly, use htInsertC() macro instead
htelem _htInsertPtr(_Inout_ptr_ hashtable* htbl, _In_ stgeneric key,
                    _ht_Consume_Arg_ stgeneric* val, flags_t flags);

// Internal function with type checking
_meta_inline htelem _htInsertCheckedC(_Inout_ptr_ hashtable* htbl, stype keytype,
                                      _In_ stgeneric key, stype valtype,
                                      _ht_Consume_Arg_ stgeneric* val, flags_t flags)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype) && stEq(htValType(*htbl), valtype));
    return _htInsertPtr(htbl, key, val, flags);
}

// htelem htInsert(hashtable *htbl, ktype, key, vtype, val, [flags]);
//
// Inserts or updates a key-value pair in the hash table
//
// Parameters:
//   htbl - Pointer to the hash table handle
//   ktype - Type of the key (must match table type)
//   key - Key value to insert
//   vtype - Type of the value (must match table type)
//   val - Value to insert or update
//   flags - Optional: HT_Ignore to skip if key exists
//
// Returns:
//   Element handle (htelem) that can be used to access the key/value
//   Returns existing element if key already exists (unless HT_Ignore is set)
//
// The key and value are copied according to their type semantics. For strings and objects,
// references are properly managed. If the key already exists, the old value is destroyed
// and replaced with the new value (unless HT_Ignore is used).
//
// Example:
//   htelem elem = htInsert(&ht, string, _S"key", int32, 42);
//   int32 *valptr = hteValPtr(ht, int32, elem);
#define htInsert(htbl, ktype, key, vtype, val, ...) \
    _htInsertChecked(htbl,                          \
                     stCheckedArg(ktype, key),      \
                     stCheckedArg(vtype, val),      \
                     opt_flags(__VA_ARGS__))

// htelem htInsertC(hashtable *htbl, ktype, key, vtype, *val, [flags]);
//
// Inserts a key-value pair, consuming/stealing the value to avoid copying
//
// This is an optimized version of htInsert that takes ownership of the value instead of copying it.
// The value variable will be destroyed/cleared after this call even on failure.
// The key is still copied normally.
//
// This is useful for expensive-to-copy values like long strings or when transferring ownership.
//
// Example:
//   string longstr = 0;
//   strDup(&longstr, _S"very long string...");
//   htInsertC(&ht, string, _S"key", string, &longstr);  // longstr is now NULL
#define htInsertC(htbl, ktype, key, vtype, val, ...) \
    _htInsertCheckedC(htbl,                          \
                      stCheckedArg(ktype, key),      \
                      stCheckedPtrArg(vtype, val),   \
                      opt_flags(__VA_ARGS__) | HTINT_Consume)

// Internal function - do not call directly, use htFind() macro instead
_Success_(return != 0)
htelem _htFind(_In_ hashtable htbl, _In_ stgeneric key, _Inout_opt_ stgeneric* val, flags_t flags);

// Internal function with type checking
_Success_(return != 0) _meta_inline
htelem _htFindChecked(_In_ hashtable htbl, stype keytype, _In_ stgeneric key, stype valtype,
                      _stCopyDest_Anno_opt_(valtype) stgeneric* val, flags_t flags)
{
    devAssert(htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    devAssert(stGetId(valtype) == stTypeId(none) || stEq(htValType(htbl), valtype));
    return _htFind(htbl, key, val, flags);
}

// htelem htFind(hashtable htbl, ktype, key, vtype, *val_copy_out, [flags]);
//
// Searches for a key in the hash table and optionally copies out the value
//
// Parameters:
//   htbl - The hash table to search
//   ktype - Type of the key (must match table type)
//   key - Key to search for
//   vtype - Type of the value, or 'none' to skip copying the value
//   val_copy_out - Pointer to receive a copy of the value, or NULL
//   flags - Optional: HT_Borrow for borrowed reference on objects
//
// Returns:
//   Element handle (htelem) if found, or 0 if not found
//   The return value can be used directly in boolean context
//
// If val_copy_out is provided and not NULL, the value is copied into it.
// The caller is responsible for destroying the copied value with the appropriate destructor.
// If vtype is 'none', no value copy is performed and val_copy_out is ignored.
//
// Example:
//   int32 val;
//   if (htFind(ht, string, _S"key", int32, &val)) {
//       // found, use val
//   }
//   // -- OR --
//   htelem elem = htFind(ht, string, _S"key", none, NULL);
//   if (elem) {
//       val = hteVal(ht, int32, elem);
//   }
#define htFind(htbl, ktype, key, vtype, val_copy_out, ...) \
    _htFindChecked(htbl,                                   \
                   stCheckedArg(ktype, key),               \
                   stCheckedPtrArg(vtype, val_copy_out),   \
                   opt_flags(__VA_ARGS__))

// Internal function - do not call directly, use htExtract() or htRemove() macro instead
_Success_(return) bool
_htExtract(_Inout_ptr_ hashtable* htbl, _In_ stgeneric key, _Inout_opt_ stgeneric* val);

// Internal function with type checking
_Success_(return)
_meta_inline bool _htExtractChecked(_Inout_ptr_ hashtable* htbl, stype keytype, _In_ stgeneric key,
                                    stype valtype, _stCopyDest_Anno_opt_(valtype) stgeneric* val)
{
    devAssert(*htbl);
    devAssert(stEq(htKeyType(*htbl), keytype));
    devAssert(stGetId(valtype) == stTypeId(none) || stEq(htValType(*htbl), valtype));
    return _htExtract(htbl, key, val);
}

// bool htExtract(hashtable *htbl, ktype, key, vtype, *val_copy_out);
// Removes a key-value pair from the hash table and optionally extracts the value
//
// Parameters:
//   htbl - Pointer to the hash table
//   ktype - Type of the key
//   key - Key to remove
//   vtype - Type of the value, or 'none' to destroy it
//   val_copy_out - Pointer to receive the extracted value, or NULL to destroy it
//
// Returns:
//   true if the key was found and removed, false if not found
//
// If val_copy_out is provided, the value is extracted (ownership transferred) rather than
// destroyed. The caller becomes responsible for destroying the extracted value. The key is always
// destroyed (unless HT_RefKeys was used).
//
// Example:
//   string extracted = 0;
//   if (htExtract(&ht, string, _S"key", string, &extracted)) {
//       // use extracted
//       strDestroy(&extracted);
//   }
#define htExtract(htbl, ktype, key, vtype, val_copy_out) \
    _htExtractChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_copy_out))

// bool htRemove(hashtable *htbl, ktype, key);
// Removes a key-value pair from the hash table, destroying both key and value
//
// This is a convenience wrapper around htExtract that always destroys the value.
//
// Returns:
//   true if the key was found and removed, false if not found
//
// Example:
//   htRemove(&ht, string, _S"key");
#define htRemove(htbl, ktype, key) \
    _htExtractChecked(htbl, stCheckedArg(ktype, key), stType(none), NULL)

// Internal function - do not call directly, use htHasKey() macro instead
bool _htHasKey(_In_ hashtable htbl, _In_ stgeneric key);

// Internal function with type checking
_meta_inline bool _htHasKeyChecked(_In_ hashtable htbl, stype keytype, _In_ stgeneric key)
{
    devAssert(htbl);
    devAssert(stEq(htKeyType(htbl), keytype));
    return _htHasKey(htbl, key);
}

// bool htHasKey(hashtable htbl, ktype, key);
// Checks if a key exists in the hash table without retrieving its value
//
// Returns:
//   true if the key exists, false otherwise
//
// This is more efficient than htFind when you only need to check for existence.
//
// Example:
//   if (htHasKey(ht, string, _S"key")) {
//       // key exists
//   }
#define htHasKey(htbl, ktype, key) _htHasKeyChecked(htbl, stCheckedArg(ktype, key))

// Internal function - gets pointer to key storage for an element
// Do not call directly - use hteKeyPtr() or hteKey() macros instead
_Pre_satisfies_(elem > 0) void* _hteElemKeyPtr(_Inout_ HashTableHeader* hdr, htelem elem);

// Internal function - gets pointer to value storage for an element
// Do not call directly - use hteValPtr() or hteVal() macros instead
_Pre_satisfies_(elem > 0) void* _hteElemValPtr(_Inout_ HashTableHeader* hdr, htelem elem);

// ========================================
// Hash table iteration
// ========================================
//
// Iteration allows traversing all key-value pairs in the hash table.
// The order of iteration is the same as the order that the elements were inserted into the table.
//
// Usage pattern:
//   htiter iter;
//   htiInit(&iter, ht);
//   while (htiValid(&iter)) {
//       string key = htiKey(string, iter);
//       int32 val = htiVal(int32, iter);
//       // process key and val
//       htiNext(&iter)
//   }
//   htiFinish(&iter);

// define separately so the the prototype is all on one line and the tooltips in vscode work
// properly...
#define _htiInitAnno                                   \
    _Success_(return) _Post_satisfies_(iter->slot > 0) \
        _On_failure_(_Post_satisfies_(iter->slot == 0))

// Initializes an iterator and positions it at the first element
//
// Returns:
//   true if the table has at least one element (iterator is valid)
//   false if the table is empty or NULL (iterator is invalid)
//
// If this returns false, do not call htiNext(), but htiValid() can be used
// regardless and is often more convenient. htiFinish() is also safe to call regardless of the
// return value.
_htiInitAnno bool htiInit(_Out_ htiter* iter, _In_ hashtable htbl);

#define _htiNextAnno                                   \
    _Success_(return) _Post_satisfies_(iter->slot > 0) \
        _On_failure_(_Post_satisfies_(iter->slot == 0))

// Advances the iterator to the next element
//
// Returns:
//   true if advanced to a valid element
//   false if there are no more elements (iteration complete)
//
// After this returns false, the iterator is invalid and htiFinish() should be called.
_htiNextAnno bool htiNext(_Inout_ htiter* iter);

// Finalizes an iterator after iteration is complete
//
// This must be called after iteration is complete (when htiNext returns false)
// to properly clean up the iterator state.
void htiFinish(_Pre_notnull_ _Post_invalid_ htiter* iter);

// Checks if an iterator is currently positioned at a valid element
//
// Returns:
//   true if the iterator is valid and can be used to access data
//   false if the iterator is invalid (uninitialized or past the end)
_Post_equal_to_(iter->slot > 0) _meta_inline bool htiValid(_In_ htiter* iter)
{
    return iter->slot > 0;
}

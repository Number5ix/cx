#pragma once
/// @file sarray.h
/// @brief Dynamic arrays with type-safe generic programming
///
/// SArray (Sorted Array) provides dynamic arrays with runtime type system integration,
/// optional sorting, and copy-on-write semantics for efficient memory management.

/// @defgroup array SArray (Dynamic Arrays)
/// @ingroup containers
/// @{
///
/// Generic dynamic arrays with type safety, optional sorting, and automatic growth.
///
/// **Key Features:**
/// - Type-safe generic programming through runtime type system
/// - Optional sorted mode with O(log n) binary search
/// - Configurable growth strategies for performance tuning
/// - Reference or value semantics for pointer types
/// - Automatic memory management with proper element destruction
///
/// **Basic Usage:**
/// @code
///   sa_int32 arr = { 0 };
///   saInit(&arr, int32, 16);
///   saPush(&arr, int32, 42);
///   saPush(&arr, int32, 17);
///   int32 idx = saFind(arr, int32, 42);
///   saDestroy(&arr);
/// @endcode
///
/// **Sorted Arrays:**
/// @code
///   sa_int32 sorted = { 0 };
///   saInit(&sorted, int32, 0, SA_Sorted);
///   saPush(&sorted, int32, 30);  // Inserted in sorted position
///   saPush(&sorted, int32, 10);
///   saPush(&sorted, int32, 20);
///   // Array is now [10, 20, 30]
///   int32 idx = saFind(sorted, int32, 20);  // O(log n) binary search
/// @endcode

#include <cx/cx.h>
#include <cx/debug/assert.h>
#include <cx/debug/dbgtypes.h>
#include <cx/utils/macros/unused.h>

#define sarrayref(typ) sa_##typ
#define sarrayhdl(typ) sa_##typ*

/// @defgroup array_core Core Types & Information
/// @ingroup array
/// @{
/// Array type declarations, pre-defined types, and information queries
///
/// **Type System Overview:**
///
/// Each distinct element type requires its own sarray type declaration (e.g., `sa_int32`,
/// `sa_string`). These types are declared using the `saDeclareType()` or convenience macros like
/// `saDeclare()`. Common types are pre-declared below.
///
/// The `.a` member provides direct typed access to the array's data:
/// @code
///   sa_int32 arr;
///   saInit(&arr, int32, 16);
///   arr.a[0] = 42;           // Direct access - arr.a is int32*
///   int32 val = arr.a[5];    // Type-safe element access
/// @endcode
///
/// This design enables compile-time type checking while maintaining the flexibility
/// of generic operations through the runtime type system.

/// saDeclareType(name, typ)
///
/// Declares a new named sarray type for passing between functions
///
/// @param name The name for the array type (will create sa_name)
/// @param typ The element type stored in the array
#define saDeclareType(name, typ) \
    typedef union sa_##name {    \
        _nv_sarray* _debug;      \
        void* _is_sarray;        \
        void* _is_sarray_##name; \
        typ* a;                  \
    } sa_##name

/// saDeclare(name)
///
/// Declares a named sarray type where the element type matches the name
///
/// @param name The type name (both for array and element)
#define saDeclare(name) saDeclareType(name, name)

/// saDeclarePtr(name)
///
/// Declares a named sarray type for pointer-to-name elements
///
/// @param name The base type name (creates array of name*)
#define saDeclarePtr(name) saDeclareType(name, name*)

/// saInitNone
///
/// Static initializer for an empty/uninitialized array
#define saInitNone { .a = 0 }

// Pre-declared common sarray types
typedef sa_ref sa_opaque;
saDeclare(int8);
saDeclare(int16);
saDeclare(int32);
saDeclare(int64);
saDeclare(intptr);
saDeclare(uint8);
saDeclare(uint16);
saDeclare(uint32);
saDeclare(uint64);
saDeclare(uintptr);
saDeclare(bool);
saDeclareType(size, size_t);
saDeclare(float32);
saDeclare(float64);
saDeclareType(ptr, void*);
saDeclare(string);
// strref doesn't make sense in an sarray
saDeclareType(object, ObjInst*);
saDeclareType(suid, SUID);
saDeclare(stvar);
saDeclareType(sarray, sa_ref);
saDeclare(hashtable);

// SArray header structure
//
// Internal structure containing array metadata. Access through helper macros.
typedef struct SArrayHeader {
    // sarray extended header begins here (only valid if SAINT_Extended is set)
    STypeOps typeops;
    // sarray header begins here
    stype elemtype;
    int32 count;
    int32 capacity;
    uint32 flags;   // high 8 bits = growth
    void* data[1];
} SArrayHeader;

/// Creation flags for sarray initialization
enum SARRAY_CREATE_FLAGS_ENUM {
    SA_Ref    = 0x0010,   ///< Array references data without copying/destroying (pointer types only)
    SA_Sorted = 0x0020,   ///< Maintain sorted order with O(log n) search and O(n) insert
    SA_AutoShrink = 0x0040,    ///< Automatically release memory when array shrinks

    SAINT_Extended = 0x8000,   // includes extended header
};

// Growth rate strategies for array expansion
enum SARRAY_GROW_ENUM {
    SA_GROW_Auto,

    // Dynamic growth rates (transition from high to lower growth as size increases)
    SA_GROW_Normal,
    SA_GROW_Aggressive,
    SA_GROW_Slow,

    // Fixed growth rates
    SA_GROW_100,
    SA_GROW_50,
    SA_GROW_25,

    SA_GROW_Minimal,
};

/// Operation flags for sarray functions
enum SARRAY_FUNC_FLAGS_ENUM {
    /// Don't insert/merge duplicates
    /// @note Valid for `saPush()`, `saMerge()`
    SA_Unique = 0x00010000,

    /// Fast removal by swapping with last element (disrupts order, not valid for sorted arrays)
    /// @note Valid for `saExtract()`, `saRemove()`, `saFindRemove()`
    SA_Fast = 0x00020000,

    /// Return insertion point for not found in sorted arrays
    /// @note Valid for `saFind()`
    SA_Inexact = 0x00100000,

    // Internal use only - do not use directly
    SAINT_Consume = 0x10000000,
};

#define SA_GROW_MASK (0xff000000)

/// SA_Grow(rate)
///
/// Converts a growth rate to the format used in the flags parameter
///
/// Controls how the array capacity expands when more space is needed. Dynamic rates
/// automatically adjust growth as the array gets larger to balance performance with
/// memory efficiency.
///
/// Available rates:
///
/// **Dynamic Growth (Recommended):**
/// | Rate | Initial | Mid-size | Large | Thresholds | Use Case |
/// |------|---------|----------|-------|------------|----------|
/// | Auto | Varies | Varies | Varies | Element-size based | Automatic selection (default) |
/// | Normal | 100% | 50% | 25% | 16→128 elements | Balanced default for most cases |
/// | Aggressive | 100% | 50% | 25% | 32→256 elements | Better performance, more memory |
/// | Slow | 100% | 50% | 25% | 8→64 elements | Memory efficient, slower growth |
///
/// **Fixed Growth Rates:**
/// | Rate | Multiplier | Use Case |
/// |------|------------|----------|
/// | 100 | 2x | Always double capacity (fast growth) |
/// | 50 | 1.5x | Moderate growth |
/// | 25 | 1.25x | Conservative growth |
/// | Minimal | Exact | No over-allocation (slowest, minimal memory) |
///
/// Dynamic rates start with high growth for small arrays, then reduce the rate as the array
/// grows larger. This provides good performance for typical usage while avoiding excessive
/// memory consumption for large arrays.
///
/// @param rate Growth rate (e.g., Auto, Normal, Aggressive, 100, 50, Minimal)
/// @return Formatted flags value for use with saInit()
/// Example:
/// @code
///   // Use automatic growth selection
///   saInit(&arr, int32, 16, SA_Grow(Auto));
///
///   // Or specify a fixed rate
///   saInit(&arr, string, 0, SA_Sorted | SA_Grow(100));
/// @endcode
#define SA_Grow(rate) (((uint32)SA_GROW_##rate) << 24)

// Internal macro - extracts growth settings from flags
#define SA_GET_GROW(flags) ((flags) >> 24)

#define SARRAY_HDRSIZE  (offsetof(SArrayHeader, data))
#define SARRAY_HDR(ref) ((SArrayHeader*)(((uintptr)((ref).a)) - SARRAY_HDRSIZE))
#define SAREF(r)        (unused_noeval(&((r)._is_sarray)), *(sa_ref*)(&(r)))
#define SAHANDLE(h)     ((sahandle)(unused_noeval((h != NULL) && &((h)->_is_sarray)), (h)))

_Ret_notnull_ _meta_inline SArrayHeader* _saHdr(_In_ sa_ref ref)
{
    return SARRAY_HDR(ref);
}

/// saSize(ref)
///
/// Returns the number of elements in the array
///
/// @param ref The array (passed by value)
/// @return Number of elements, or 0 if array is NULL
#define saSize(ref) ((ref)._is_sarray ? _saHdr(SAREF(ref))->count : 0)

/// saCapacity(ref)
///
/// Returns the allocated capacity of the array
///
/// @param ref The array (passed by value)
/// @return Allocated capacity, or 0 if array is NULL
#define saCapacity(ref) ((ref)._is_sarray ? _saHdr(SAREF(ref))->capacity : 0)

/// saElemSize(ref)
///
/// Returns the size in bytes of each array element
///
/// @param ref The array (passed by value)
/// @return Element size in bytes, or 0 if array is NULL
#define saElemSize(ref) ((ref)._is_sarray ? stGetSize(_saHdr(SAREF(ref))->elemtype) : 0)

/// saElemType(ref)
///
/// Returns the runtime type descriptor for array elements
///
/// @param ref The array (passed by value)
/// @return stype descriptor, or 0 if array is NULL
#define saElemType(ref) ((ref)._is_sarray ? _saHdr(SAREF(ref))->elemtype : 0)

/// saValid(ref)
///
/// Checks if the array is initialized (non-NULL)
///
/// @param ref The array (passed by value)
/// @return true if array is initialized, false otherwise
#define saValid(ref) ((ref).a)

/// @}  // end of array_core group

/// @defgroup array_lifecycle Array Lifecycle & Size Management
/// @ingroup array
/// @{
/// Array creation, destruction, and capacity/size management operations

_Success_(!canfail || return)
_When_(canfail, _Check_return_)
_At_(out->a, _Post_notnull_) bool _saInit(_Out_ sahandle out, stype elemtype, _In_opt_ STypeOps* ops, int32 capacity,
                        bool canfail, flags_t flags);

/// bool saInit(sa_type *out, type, int32 capacity, [flags])
///
/// Initializes a new dynamic array with the specified element type
///
/// @param out Pointer to the sarray to initialize
/// @param type Runtime type for elements (e.g., int32, string, ptr, object, etc.)
/// @param capacity Initial capacity (will be clamped to at least 1, use 0 for default of 8)
/// @param ... (flags) Optional combination of SA_* flags:
///              - SA_Ref - Array references data without copying/destroying (pointer types only)
///              - SA_Sorted - Maintain sorted order with O(log n) search, O(n) insert
///              - SA_AutoShrink - Release memory when array shrinks
///              - SA_Grow(rate) - Growth rate: Normal, Aggressive, Slow, 100, 50, 25, Minimal
/// @return true (this version cannot fail and will runtime assert on allocation failure)
///
/// Example:
/// @code
///   sa_int32 arr;
///   saInit(&arr, int32, 16);
///   saPush(&arr, int32, 42);
///   saDestroy(&arr);
/// @endcode
#define saInit(out, type, capacity, ...) \
    _saInit(SAHANDLE(out), stFullType(type), capacity, false, opt_flags(__VA_ARGS__))

/// bool saTryInit(sa_type *out, type, int32 capacity, [flags])
///
/// Same as saInit but can fail and return false if memory allocation fails
///
/// @param out Pointer to the sarray to initialize
/// @param type Runtime type for elements
/// @param capacity Initial capacity
/// @param ... (flags) Optional combination of SA_* flags
/// @return true on success, false on allocation failure
#define saTryInit(out, type, capacity, ...) \
    _saInit(SAHANDLE(out), stFullType(type), capacity, true, opt_flags(__VA_ARGS__))

_At_(handle->a, _Pre_maybenull_ _Post_null_) void _saDestroy(_Inout_ sahandle handle);

/// void saDestroy(sa_type *handle)
///
/// Destroys a dynamic array and frees all associated memory
///
/// All elements are properly destroyed according to their type (unless SA_Ref was used).
/// Sets handle->a to NULL after destruction. Safe to call with NULL or uninitialized arrays.
///
/// @param handle Pointer to the array
///
/// Example:
/// @code
///   saDestroy(&arr);
/// @endcode
#define saDestroy(handle) _saDestroy(SAHANDLE(handle))

_When_(canfail, _Check_return_)
    _At_(handle->a,
         _Pre_notnull_
             _Post_notnull_) bool _saReserve(_Inout_ sahandle handle, int32 capacity, bool canfail);

/// bool saReserve(sa_type *handle, int32 capacity)
///
/// Reserves space for at least the specified capacity
///
/// If the current capacity is less than requested, the array is expanded.
/// The array size (number of elements) is not changed, only the allocated capacity.
///
/// @param handle Pointer to the array
/// @param capacity Minimum capacity to ensure (use 0 for at least 1)
/// @return true on success, false on allocation failure
///
/// Example:
/// @code
///   saReserve(&arr, 100);  // Ensure space for at least 100 elements
/// @endcode
#define saReserve(handle, capacity) _saReserve(SAHANDLE(handle), capacity, true)

_At_(handle->a,
     _Pre_notnull_ _Post_notnull_) void _saShrink(_Inout_ sahandle handle, int32 capacity);

/// void saShrink(sa_type *handle, int32 capacity)
///
/// Reduces the array capacity to free unused memory
///
/// Shrinks the allocated capacity to the specified size, but never below the current
/// element count. This is useful for reclaiming memory after removing many elements.
/// Unlike saSetSize, this does not affect the number of elements in the array.
///
/// @param handle Pointer to the array
/// @param capacity Desired capacity (use 0 for minimum of 1)
///
/// Example:
/// @code
///   // After removing many elements:
///   saShrink(&arr, 15);           // Shrink but leave room for 15 elements
///   saShrink(&arr, 0);            // Shrink to minimum (current count or 1)
/// @endcode
#define saShrink(handle, capacity) _saShrink(SAHANDLE(handle), capacity)

_At_(handle->a, _Pre_notnull_ _Post_notnull_) void _saSetSize(_Inout_ sahandle handle, int32 size);

/// void saSetSize(sa_type *handle, int32 size)
///
/// Sets the array to exactly the specified size
///
/// If growing, new elements are zero-initialized. If shrinking, excess elements
/// are properly destroyed. The capacity is automatically adjusted if needed.
///
/// @param handle Pointer to the array
/// @param size New size for the array
///
/// Example:
/// @code
///   saSetSize(&arr, 50);  // Array now has exactly 50 elements
/// @endcode
#define saSetSize(handle, size) _saSetSize(SAHANDLE(handle), size)

_At_(handle->a, _Pre_maybenull_) void _saClear(_Inout_ sahandle handle);

/// void saClear(sa_type *handle)
///
/// Removes all elements from the array and sets size to 0
///
/// All elements are properly destroyed (unless SA_Ref was used). The allocated
/// capacity is preserved. Safe to call with NULL or uninitialized arrays.
///
/// @param handle Pointer to the array
///
/// Example:
/// @code
///   saClear(&arr);  // Remove all elements but keep capacity
/// @endcode
#define saClear(handle) _saClear(SAHANDLE(handle))

/// @}  // end of array_lifecycle group

/// @defgroup array_ops Array Operations
/// @ingroup array
/// @{
/// Operations for adding, searching, inserting, removing, and sorting elements

#define _sa_Consume_Arg_                                       \
    _When_(flags& SAINT_Consume, _Pre_notnull_ _Post_invalid_) \
        _When_(!(flags & SAINT_Consume), _Inout_)
_At_(handle->a, _Pre_maybenull_ _Post_notnull_) int32 _saPush(_Inout_ sahandle handle, stype elemtype, _In_ stgeneric elem,
                         flags_t flags);
_At_(handle->a, _Pre_maybenull_ _Post_notnull_) int32 _saPushPtr(_Inout_ sahandle handle, stype elemtype,
                            _sa_Consume_Arg_ stgeneric* elem, flags_t flags);

/// int32 saPush(sa_type *handle, type, elem, [flags])
///
/// Appends an element to the end of the array (or inserts in sorted position)
///
/// The element is copied according to its type semantics. For strings and objects,
/// references are properly managed. The array is automatically initialized if NULL
/// and grown if needed.
///
/// @param handle Pointer to the array (can be a valid pointer to a NULL array)
/// @param type Runtime type of the element (must match array type if already initialized)
/// @param elem Element value to push
/// @param ... (flags) Optional: SA_Unique - Don't insert if element already exists (requires
/// equality check)
/// @return Index where the element was inserted, or -1 if SA_Unique prevented insertion
///
/// For sorted arrays, the element is inserted at the correct position to maintain order.
///
/// Example:
/// @code
///   sa_int32 arr = {0};
///   saPush(&arr, int32, 42);
///   saPush(&arr, int32, 17);
/// @endcode
#define saPush(handle, type, elem, ...) \
    _saPush(SAHANDLE(handle), stCheckedArg(type, elem), opt_flags(__VA_ARGS__))

/// int32 saPushC(sa_type *handle, type, *elem, [flags])
///
/// Appends an element to the array, consuming/stealing it to avoid copying
///
/// This is an optimized version of saPush that takes ownership of the element instead
/// of copying it. The source variable will be destroyed/cleared after this call.
/// Requires that elem be a pointer.
///
/// This is useful for expensive-to-copy elements like long strings or when transferring
/// ownership.
///
/// @param handle Pointer to the array
/// @param type Runtime type of the element
/// @param elem Pointer to element to consume (will be destroyed/cleared)
/// @param ... (flags) Optional: SA_Unique flag
/// @return Index where the element was inserted, or -1 if SA_Unique prevented insertion
///
/// Example:
/// @code
///   string str = 0;
///   strDup(&str, _S"long string...");
///   saPushC(&arr, string, &str);  // str is now NULL/invalid
/// @endcode
#define saPushC(handle, type, elem, ...)    \
    _saPushPtr(SAHANDLE(handle),            \
               stCheckedPtrArg(type, elem), \
               opt_flags(__VA_ARGS__) | SAINT_Consume)

_Ret_opt_valid_ _At_(handle->a,
                     _Pre_maybenull_) void* _saPopPtr(_Inout_ sahandle handle, int32 idx);

/// void *saPopPtr(sa_type *handle)
///
/// Removes and returns the last element without calling its destructor
///
/// Transfers ownership of the element to the caller. The destructor is NOT called,
/// so the caller is responsible for managing the returned value. Only valid for
/// pointer-type or object-type arrays.
///
/// @param handle Pointer to the array
/// @return Pointer to the removed element, or NULL if array is empty/NULL
///
/// Example:
/// @code
///   SomeObj *obj = saPopPtr(&arr);  // Ownership transferred
///   // use obj...
///   objRelease(&obj);  // Caller must destroy
/// @endcode
#define saPopPtr(handle) _saPopPtr(SAHANDLE(handle), -1)

/// void *saPopPtrI(sa_type *handle, int32 idx)
///
/// Removes and returns the element at the specified index without calling its destructor
///
/// Like saPopPtr but for an arbitrary index. Negative indices count from the end (-1 is last).
/// Transfers ownership to the caller.
///
/// @param handle Pointer to the array
/// @param idx Index of element to remove
/// @return Pointer to the removed element, or NULL if array is empty/NULL
///
/// Example:
/// @code
///   void *elem = saPopPtrI(&arr, 5);  // Remove element at index 5
/// @endcode
#define saPopPtrI(handle, idx) _saPopPtr(SAHANDLE(handle), idx)

_At_(ref.a, _Pre_notnull_) int32 _saFind(_In_ sa_ref ref, _In_ stgeneric elem, flags_t flags);
_At_(ref.a, _Pre_maybenull_) _meta_inline int32
_saFindChecked(_In_ sa_ref ref, stype elemtype, _In_ stgeneric elem, flags_t flags)
{
    if (!ref.a)
        return -1;
    devAssert(stEq(saElemType(ref), elemtype));
    return _saFind(ref, elem, flags);
}

/// int32 saFind(sa_type ref, type, elem, [flags])
///
/// Searches for an element in the array
///
/// Uses linear search for unsorted arrays (O(n)) or binary search for sorted arrays (O(log n)).
/// Comparison is performed according to the element type's comparison semantics.
///
/// @param ref The array to search (passed by value)
/// @param type Runtime type of the element
/// @param elem Element value to search for
/// @param ... (flags) Optional: SA_Inexact - For sorted arrays, return insertion point if not found
/// @return Index of the found element, or -1 if not found.
///         If SA_Inexact is set on a sorted array, returns the index where the element
///         would be inserted even if not found (never returns -1)
///
/// Example:
/// @code
///   int32 idx = saFind(arr, int32, 42);
///   if (idx >= 0) {
///       // found at arr.a[idx]
///   }
/// @endcode
#define saFind(ref, type, elem, ...) \
    _saFindChecked(SAREF(ref), stCheckedArg(type, elem), opt_flags(__VA_ARGS__))

_At_(
    handle->a,
    _Pre_notnull_
        _Post_notnull_) bool _saFindRemove(_Inout_ sahandle handle, _In_ stgeneric elem, flags_t flags);
_At_(handle->a, _Pre_maybenull_) _meta_inline bool
_saFindRemoveChecked(_Inout_ sahandle handle, stype elemtype, _In_ stgeneric elem, flags_t flags)
{
    if (!handle->a)
        return false;
    devAssert(stEq(saElemType(*handle), elemtype));
    return _saFindRemove(handle, elem, flags);
}

/// bool saFindRemove(sa_type *handle, type, elem, [flags])
///
/// Searches for an element and removes it if found
///
/// Combines find and remove operations into a single call. The element is properly
/// destroyed (unless SA_Ref was used).
///
/// @param handle Pointer to the array
/// @param type Runtime type of the element
/// @param elem Element value to search for and remove
/// @param ... (flags) Optional: SA_Fast - Use fast removal (swap with last element, disrupts order)
///              Not valid for sorted arrays
/// @return true if the element was found and removed, false if not found or array is NULL
///
/// Example:
/// @code
///   if (saFindRemove(&arr, int32, 42)) {
///       // Element was found and removed
///   }
/// @endcode
#define saFindRemove(handle, type, elem, ...) \
    _saFindRemoveChecked(SAHANDLE(handle), stCheckedArg(type, elem), opt_flags(__VA_ARGS__))

_At_(handle->a,
     _Pre_notnull_
         _Post_notnull_) int32 _saInsert(_Inout_ sahandle handle, int32 idx, _In_ stgeneric elem);
_At_(handle->a, _Pre_notnull_ _Post_notnull_) _meta_inline int32
_saInsertChecked(_Inout_ sahandle handle, int32 idx, stype elemtype, _In_ stgeneric elem)
{
    devAssert(handle->_is_sarray);
    devAssert(stEq(saElemType(*handle), elemtype));
    return _saInsert(handle, idx, elem);
}

/// int32 saInsert(sa_type *handle, int32 idx, type, elem)
///
/// Inserts an element at the specified index
///
/// All elements at or after the index are shifted right to make room. The element
/// is copied according to its type semantics. Negative indices count from the end
/// (-1 is after the last element).
///
/// @param handle Pointer to the array (must already be initialized)
/// @param idx Index where element should be inserted (0 to count inclusive)
/// @param type Runtime type of the element
/// @param elem Element value to insert
/// @return The index where the element was inserted
///
/// Note: Inserting into a sorted array clears the SA_Sorted flag as it may violate
/// sort order. Use saPush for sorted arrays to maintain order.
///
/// Example:
/// @code
///   saInsert(&arr, 0, int32, 42);  // Insert at beginning
///   saInsert(&arr, -1, int32, 17); // Insert at end
/// @endcode
#define saInsert(handle, idx, type, elem) \
    _saInsertChecked(SAHANDLE(handle), idx, stCheckedArg(type, elem))

_At_(handle->a, _Pre_notnull_ _Post_notnull_) bool _saExtract(_Inout_ sahandle handle, int32 idx, _Inout_opt_ stgeneric* elem,
                           flags_t flags);

_At_(handle->a, _Pre_maybenull_) _meta_inline bool
_saExtractChecked(_Inout_ sahandle handle, int32 idx, stype elemtype,
                  _stCopyDest_Anno_opt_(elemtype) stgeneric* elem, flags_t flags)
{
    if (!handle->a)
        return false;
    devAssert(stGetId(elemtype) == stTypeId(none) || stEq(saElemType(*handle), elemtype));
    return _saExtract(handle, idx, elem, flags);
}

/// bool saExtract(sa_type *handle, int32 idx, type, *elem_copy_out, [flags])
///
/// Removes an element at the specified index and optionally extracts its value
///
/// The element is removed from the array. If elem_copy_out is provided, the value
/// is copied out before removal. Otherwise the element is destroyed. Negative indices
/// count from the end (-1 is the last element).
///
/// @param handle Pointer to the array
/// @param idx Index of element to remove
/// @param type Runtime type of the element, or 'none' to just destroy it
/// @param elem_copy_out Pointer to receive a copy of the element, or NULL to destroy it
/// @param ... (flags) Optional: SA_Fast - Swap with last element for O(1) removal (disrupts order)
///              Not valid for sorted arrays
/// @return true if the element was removed, false if array is NULL or index is invalid
///
/// Example:
/// @code
///   int32 val;
///   if (saExtract(&arr, 5, int32, &val)) {
///       // val contains the removed element
///   }
/// @endcode
#define saExtract(handle, idx, type, elem_copy_out, ...)    \
    _saExtractChecked(SAHANDLE(handle),                     \
                      idx,                                  \
                      stCheckedPtrArg(type, elem_copy_out), \
                      opt_flags(__VA_ARGS__))

/// bool saRemove(sa_type *handle, int32 idx, [flags])
///
/// Removes and destroys the element at the specified index
///
/// This is a convenience wrapper around saExtract that always destroys the element.
/// Negative indices count from the end (-1 is the last element).
///
/// @param handle Pointer to the array
/// @param idx Index of element to remove
/// @param ... (flags) Optional: SA_Fast for O(1) removal by swapping with last element
/// @return true if the element was removed, false if array is NULL or index is invalid
///
/// Example:
/// @code
///   saRemove(&arr, 5);  // Remove element at index 5
///   saRemove(&arr, -1, SA_Fast);  // Remove last element (fast flag has no effect here)
/// @endcode
#define saRemove(handle, idx, ...) \
    _saExtractChecked(SAHANDLE(handle), idx, stType(none), NULL, opt_flags(__VA_ARGS__))

_At_(handle->a, _Pre_maybenull_) void _saSort(_Inout_ sahandle handle, bool keep);

/// void saSort(sa_type *handle, bool keep)
///
/// Sorts the array in-place using the element type's comparison function
///
/// Uses an optimized quicksort implementation with specializations for built-in types.
/// After sorting, you can optionally set the SA_Sorted flag to enable binary search
/// and maintain sort order on future insertions.
///
/// @param handle Pointer to the array to sort
/// @param keep If true, sets SA_Sorted flag to maintain sort order on future operations.
///             If false, just sorts without changing flags
///
/// Safe to call with NULL or empty arrays.
///
/// Example:
/// @code
///   saSort(&arr, true);   // Sort and maintain sorted order
///   saSort(&arr, false);  // Just sort once
/// @endcode
#define saSort(handle, keep) _saSort(SAHANDLE(handle), keep)

/// @}  // end of array_ops group

/// @defgroup array_copy Slicing, Cloning & Merging
/// @ingroup array
/// @{

_At_(out->a, _When_(ref.a, _Post_notnull_) ) void _saSlice(_Out_ sahandle out, _In_ sa_ref ref, int32 start, int32 end);

/// void saSlice(sa_type *out, sa_type src, int32 start, int32 end)
///
/// Creates a new array containing a slice (subrange) of the source array
///
/// All elements in the range are deep copied into the new array. The source array
/// is unmodified. Negative indices count from the end of the array.
///
/// @param out Pointer to uninitialized array to receive the slice
/// @param src Source array to slice from (passed by value)
/// @param start Starting index (inclusive), negative counts from end
/// @param end Ending index (exclusive), 0 means to end of array, negative counts from end
///
/// Example:
/// @code
///   sa_int32 slice;
///   saSlice(&slice, arr, 5, 10);   // Elements 5-9
///   saSlice(&slice, arr, -5, 0);   // Last 5 elements
///   saSlice(&slice, arr, 0, -5);   // All but last 5 elements
///   saDestroy(&slice);
/// @endcode
#define saSlice(out, src, start, end) _saSlice(SAHANDLE(out), SAREF(src), start, end)

/// void saClone(sa_type *out, sa_type src)
///
/// Creates a deep copy of the entire array
///
/// This is a convenience wrapper around saSlice that copies all elements.
/// All elements are deep copied into the new array.
///
/// @param out Pointer to uninitialized array to receive the clone
/// @param src Source array to clone (passed by value)
///
/// Example:
/// @code
///   sa_string clone;
///   saClone(&clone, original);
///   // modify clone without affecting original
///   saDestroy(&clone);
/// @endcode
#define saClone(out, src) _saSlice(SAHANDLE(out), SAREF(src), 0, 0)

_At_(out->a,
     _Post_maybenull_) void _saMerge(_Out_ sahandle out, int n, _In_ sa_ref* refs, flags_t flags);

/// void saMerge(sa_type *out, sa_type array1, sa_type array2, ...)
///
/// Creates a new array by concatenating multiple source arrays
///
/// All elements from all source arrays are deep copied into the new array in order.
/// Source arrays must all be the same element type. The output inherits flags from
/// the first source array.
///
/// @param out Pointer to uninitialized array to receive the merged result
/// @param ... Variable number of source arrays to merge (passed by value)
///
/// Example:
/// @code
///   sa_int32 merged;
///   saMerge(&merged, arr1, arr2, arr3);  // Concatenate three arrays
///   saDestroy(&merged);
/// @endcode
#define saMerge(out, ...)                                         \
    _saMerge(SAHANDLE(out),                                       \
             sizeof((sa_ref[]) { __VA_ARGS__ }) / sizeof(sa_ref), \
             (sa_ref[]) { __VA_ARGS__ },                          \
             0)

/// void saMergeF(sa_type *out, flags_t flags, sa_type array1, sa_type array2, ...)
///
/// Creates a new array by merging multiple source arrays with specified flags
///
/// Like saMerge but allows specifying flags. If SA_Unique is set, duplicate elements
/// are not inserted (requires element type to support equality comparison).
///
/// @param out Pointer to uninitialized array to receive the merged result
/// @param flags Merge flags (e.g., SA_Unique to exclude duplicates)
/// @param ... Variable number of source arrays to merge (passed by value)
///
/// Example:
/// @code
///   sa_int32 unique;
///   saMergeF(&unique, SA_Unique, arr1, arr2);  // Merge without duplicates
///   saDestroy(&unique);
/// @endcode
#define saMergeF(out, flags, ...)                                 \
    _saMerge(SAHANDLE(out),                                       \
             sizeof((sa_ref[]) { __VA_ARGS__ }) / sizeof(sa_ref), \
             (sa_ref[]) { __VA_ARGS__ },                          \
             flags)

/// @}
/// @}

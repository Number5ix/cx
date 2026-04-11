#pragma once

/// @file stype.h
/// @brief Runtime type system and type descriptor infrastructure

/// @defgroup stype SType (Runtime Type System)
/// @{
/// Compile-time safe, runtime-inspectable type system enabling generic programming
/// and type-safe containers in pure C without template bloat.

/// @defgroup stype_overview Overview
/// @ingroup stype
/// @{
///
/// @section stype_concept Core Concept
///
/// The stype system provides runtime type information through pointers to canonical
/// `STypeInfo` descriptors. This enables generic programming patterns similar to C++
/// templates while maintaining C compatibility and avoiding code bloat from template
/// instantiation.
///
/// Each type has exactly one canonical `const STypeInfo` in static storage. An `stype`
/// value is simply a pointer to that descriptor - equality is pointer equality (via
/// `stEq`, which canonicalizes before comparing). Type names like `int32`, `string`,
/// and `opaque(MyStruct)` expand to pointers to their canonical descriptors through the
/// `stType()` macro.
///
/// @section stype_descriptor Type Descriptor
///
/// `stype` is a `const STypeInfo*`. The `STypeInfo` struct carries all information
/// about a type in one place:
///
/// @code
///   typedef struct STypeInfo {
///       uint32           id;       // hierarchical type class | subtype | discriminant
///       uint16           size;     // storage size in bytes
///       uint16           flags;    // STypeFlag_Object, STypeFlag_PassPtr, STypeFlag_Temporary
///       const char*      name;     // type name for debug and serialization
///       const STypeInfo* param[2]; // for parameterized types (must be canonical):
///                                  //   sarray:    param[0] = element type
///                                  //   hashtable: param[0] = key, param[1] = value
///       const void*      ext;      // type-specific extension data (e.g. StructInfo*)
///       STypeOps         ops;      // embedded operations: dtor, cmp, hash, copy, convert
///   } STypeInfo;
/// @endcode
///
/// @section stype_typeid Type ID Structure
///
/// The `id` field uses a hierarchical bit-field:
/// - **Bits 31–16**: Class - `STCLASS_BASIC`, `STCLASS_CX`, `STCLASS_DYNAMIC`, `STCLASS_USER`
/// - **Bits 15–8**: Subtype - for BASIC: `STST_INT`, `STST_UINT`, `STST_FLOAT`, `STST_PTR`;
///   for CX: `STST_OPAQUE`, `STST_MISC`, `STST_OBJ`, `STST_CONTAINER`
/// - **Bits 7–0**: Discriminant - often `sizeof(type)` for primitives, sequential for CX types
///
/// Use `st->id` to inspect the type class. Compare against `stTypeId(name)` constants
/// (e.g., `stTypeId(int32)`, `stTypeId(sarray)`) for type-specific dispatch.
///
/// @section stype_types Supported Types
///
/// **Primitive Types:**
/// - Integers: int8, int16, int32, int64, intptr
/// - Unsigned: uint8, uint16, uint32, uint64, uintptr, bool, size
/// - Floating point: float32, float64
/// - Pointer: ptr (generic void*)
/// - Special: opaque (POD blob), none (empty/void sentinel)
///
/// **CX Framework Types:**
/// - string, strref - Copy-on-write strings
/// - object, weakref - Object system handles
/// - suid - 128-bit unique identifiers
/// - stvar - Type-tagged variant containers
/// - sarray - Dynamic arrays
/// - hashtable - Hash tables
/// - closure, cchain - Function closures
///
/// @section stype_canonicalization Canonical Descriptors and the Type Registry
///
/// All built-in type descriptors are `static const STypeInfo` with `STypeFlag_Temporary`
/// clear. For dynamic types - plain `opaque(T)` with no custom ops, or parameterized
/// container types like an sarray of a specific element - the descriptor is constructed
/// as a compound literal with `STypeFlag_Temporary` set. When such a descriptor is
/// passed to a container init function, `stCanonical()` is called:
///
/// - If `Temporary` is clear, the descriptor is already canonical - returned as-is.
/// - If `Temporary` is set, `_stGetCanonical()` looks up or inserts the descriptor
///   in the global type registry (keyed by id + size + param pointers), returns the
///   canonical heap-allocated copy with `Temporary` cleared.
///
/// After the first use, all subsequent calls with equivalent parameters resolve to the
/// same pointer, so type identity is pointer equality everywhere.
///
/// For custom types with user-defined ops, the correct pattern is a **global or file-static
/// canonical descriptor** - see @ref stype_custom.
///
/// @section stype_usage Usage Patterns
///
/// **Generic function parameters** use macros that expand to type-value pairs:
/// @code
///   htInsert(&ht, string, _SL("key"), int32, 42);
///   // Expands to stType(string)/stArg(string,...) + stType(int32)/stArg(int32,...)
/// @endcode
///
/// **Stored values** in containers use `stStored()` to load from raw memory:
/// @code
///   stgeneric val = stStored(hdr->elemtype, ELEMPTR(hdr, i));
/// @endcode
///
/// **Type identity checks** use `stEq()` or direct pointer comparison on known-canonical
/// types:
/// @code
///   if (stEq(saElemType(arr), stType(int32))) { ... }
///   if (v.type == stType(string)) { ... }   // safe when v.type came from stCanonical
/// @endcode
///
/// **Plain opaque** (POD blob, no custom ops):
/// @code
///   sa_MyPOD arr;
///   saInit(&arr, opaque(MyPOD), 16);    // Temporary; type registry provides canonical pointer
///   saPush(&arr, opaque, val);          // opaque push: size checked against container's elem type
/// @endcode
///
/// @section stype_operations Type Operations
///
/// `STypeInfo` embeds an `STypeOps` struct with five optional function pointers:
/// - **dtor** - Destructor (cleanup/release resources). NULL = trivial (no-op).
/// - **cmp** - Comparison (for sorting, equality). NULL = memcmp fallback.
/// - **hash** - Hash function. NULL = generic byte-hash fallback.
/// - **copy** - Deep copy (ref-count bump for managed types). NULL = memcpy fallback.
/// - **convert** - Type conversion with range/precision checking. NULL = cannot convert.
///
/// @section stype_safety Type Safety
///
/// Compile-time checking is enforced through macro expansion:
/// - Type names must match expected patterns (verified at preprocessing)
/// - Values are checked against type structure members (compile-time type validation)
/// - Containers verify element types on access
///
/// Runtime checking occurs when:
/// - Converting between types (with range/precision validation)
/// - Accessing heterogeneous containers
/// - Comparing types for compatibility
///
/// @section stype_variants Variant Types (stvar)
///
/// The `stvar` structure combines a value with its type descriptor for runtime
/// polymorphism. The `type` field is an `stype` pointer.
/// @code
///   stvar v = stvar(int32, 42);
///   if (stEq(v.type, stType(int32))) {
///       int32 val = v.data.st_int32;
///   }
/// @endcode
///
/// This is the foundation for type-safe variadic arguments and heterogeneous
/// collections throughout the framework.
///
/// @}

#include <cx/debug/assert.h>
#include <cx/platform/base.h>
#include <cx/platform/cpp.h>
#include <cx/utils/macros/optarg.h>
#include <cx/utils/macros/salieri.h>
#include <cx/utils/macros/unused.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// extra files that can be included for specific functions
#define STYPE_FOREACH_ALL <cx/stype/alltypes.inc>

CX_C_BEGIN

// do this as a typedef instead of a #define
#ifdef bool
#undef bool
#endif

// IMPORTANT NOTE!
// Always initialize string to NULL or 0 first!
typedef struct str_ref* _Nullable string;
typedef const struct str_ref* _Nullable strref;
typedef struct hashtable_ref* hashtable;
typedef struct closure_ref* closure;
typedef struct cchain_ref* cchain;
typedef struct BufferHeader* Buffer;
typedef struct ObjInst ObjInst;
typedef struct ObjInst_WeakRef ObjInst_WeakRef;
typedef struct StructBase StructBase;
typedef struct SUID SUID;
typedef struct stvar stvar;

enum STYPE_CLASS_ID {
    STCLASS_BASIC   = 0x00000000,   // basic discrete types
    STCLASS_CX      = 0x00010000,   // "CX framework" types (strings, objects, containers)
    STCLASS_DYNAMIC = 0x00020000,   // dynamically defined types created at runtime
    STCLASS_USER    = 0x00100000,   // start of user-defined static type range
};

// subtypes for STCLASS_BASIC types
enum STYPE_BASIC_SUBTYPE_ID {
    STST_INT   = 0x0100,
    STST_UINT  = 0x0200,
    STST_FLOAT = 0x0300,
    STST_PTR   = 0x0400,
};

// subtypes for STCLASS_CX types
// these intentionally don't overlap with the basic subtypes, to allow for easier checks
enum STYPE_CX_SUBTYPE_ID {
    STST_OPAQUE    = 0x1000,   // opaque byte blocks, often with custom ops
    STST_MISC      = 0x1100,   // miscallenous CX-related types like SUIDs
    STST_OBJ       = 0x1200,   // object-like types that use pointer handles
    STST_CONTAINER = 0x1300,   // container types
};

#define STYPE_CLASS_MASK 0xffff0000
#define STYPE_CLASS(v)   ((v) & STYPE_CLASS_MASK)

// subtypes are only valid for STCLASS_BASIC and STCLASS_CX
#define STYPE_HAS_SUBTYPE(v) (STYPE_CLASS(v) == STCLASS_BASIC || STYPE_CLASS(v) == STCLASS_CX)
#define STYPE_SUBTYPE_MASK   0x0000ff00
#define STYPE_SUBTYPE(v)     ((v) & STYPE_SUBTYPE_MASK)

typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;
typedef intptr_t intptr;
typedef uintptr_t uintptr;
#ifndef __cplusplus
typedef _Bool bool;
#else
typedef bool _Bool;
#endif

// limits for integer types

#define MIN_INT8   (-0x7f - 1)
#define MAX_INT8   0x7f
#define MAX_UINT8  0xff
#define MIN_INT16  (-0x7fff - 1)
#define MAX_INT16  0x7fff
#define MAX_UINT16 0xffff
#define MIN_INT32  (-0x7fffffffL - 1)
#define MAX_INT32  0x7fffffffL
#define MAX_UINT32 0xffffffffUL
#define MAX_INT64  0x7fffffffffffffffLL
#define MIN_INT64  (-0x7fffffffffffffffLL - 1)
#define MAX_UINT64 0xffffffffffffffffULL

#if defined(_64BIT)
#define MIN_INTPTR  MIN_INT64
#define MAX_INTPTR  MAX_INT64
#define MAX_UINTPTR MAX_UINT64
#elif defined(_32BIT)
#define MIN_INTPTR  MIN_INT32
#define MAX_INTPTR  MAX_INT32
#define MAX_UINTPTR MAX_UINT32
#endif

typedef float float32;
typedef double float64;

typedef struct STypeOps STypeOps;
// stype is a pointer to the canonical STypeInfo structure for the type
typedef struct STypeInfo STypeInfo;
typedef const STypeInfo* stype;

// standardize on uint32 for function call flags
typedef uint32 flags_t;

// sarrays are special because of the pointer union
typedef union sa_ref {
    void* _is_sarray;
    void* a;
} sa_ref;
typedef union sa_ref* sahandle;

// This is the type that is used for passing as a parameter by-value, as well as variants. Should be
// no larger than 64 bits wide, but can be smaller.
#define SType_none      void*
#define SType_opaque    void*
#define SType_int8      int8
#define SType_int16     int16
#define SType_int32     int32
#define SType_int64     int64
#define SType_uint8     uint8
#define SType_uint16    uint16
#define SType_uint32    uint32
#define SType_uint64    uint64
#define SType_intptr    intptr
#define SType_uintptr   uintptr
#define SType_bool      _Bool
#define SType_size      size_t
#define SType_float32   float32
#define SType_float64   float64
#define SType_ptr       void*
#define SType_string    string
#define SType_strref    strref
#define SType_object    ObjInst*
#define SType_weakref   ObjInst_WeakRef*
#define SType_suid      SUID*
#define SType_stvar     stvar*
#define SType_sarray    sa_ref
#define SType_hashtable hashtable
#define SType_closure   closure
#define SType_cchain    cchain
#define SType_buffer    Buffer
#define SType_struct    StructBase*
#define SType_structptr StructBase*
#define stTypeDef(name) SType_##name

/// @defgroup stype_utils Type Utilities
/// @ingroup stype
/// @{
///
/// Utility macros for type casting, checking, and manipulation.

/// <type> stTypeCast(type, value)
///
/// Cast a value to the appropriate C type for the given stype name.
///
/// @param name Type name (e.g., int32, string, object)
/// @param v Value to cast
/// @return Value cast to the appropriate type
///
/// Example:
/// @code
///   void* ptr = ...;
///   string s = stTypeCast(string, ptr);
/// @endcode
#define stTypeCast(name, v) ((SType_##name)(v))

/// <type>* stPtrCast(type, value)
///
/// Cast a pointer to a pointer-to-type for the given stype name.
///
/// @param name Type name
/// @param v Pointer to cast
/// @return Pointer cast to pointer-to-type
///
/// Example:
/// @code
///   void* ptr = ...;
///   int32* iptr = stPtrCast(int32, ptr);
/// @endcode
#define stPtrCast(name, v) ((SType_##name*)(v))

// container that can be aliased for any type
#define CONTAINER_TYPE(type) stTypeDef(type) st_##type
typedef union stgeneric {
    uint64 st_generic;
    CONTAINER_TYPE(none);
    CONTAINER_TYPE(opaque);
    CONTAINER_TYPE(int8);
    CONTAINER_TYPE(int16);
    CONTAINER_TYPE(int32);
    CONTAINER_TYPE(int64);
    CONTAINER_TYPE(uint8);
    CONTAINER_TYPE(uint16);
    CONTAINER_TYPE(uint32);
    CONTAINER_TYPE(uint64);
    CONTAINER_TYPE(intptr);
    CONTAINER_TYPE(uintptr);
    CONTAINER_TYPE(bool);
    CONTAINER_TYPE(size);
    CONTAINER_TYPE(float32);
    CONTAINER_TYPE(float64);
    CONTAINER_TYPE(ptr);
    CONTAINER_TYPE(string);
    CONTAINER_TYPE(strref);
    CONTAINER_TYPE(object);
    CONTAINER_TYPE(weakref);
    CONTAINER_TYPE(suid);
    CONTAINER_TYPE(stvar);
    CONTAINER_TYPE(sarray);
    CONTAINER_TYPE(hashtable);
    CONTAINER_TYPE(closure);
    CONTAINER_TYPE(cchain);
    CONTAINER_TYPE(buffer);
    CONTAINER_TYPE(struct);
    CONTAINER_TYPE(structptr);
} stgeneric;

_Static_assert(sizeof(stgeneric) == sizeof(uint64), "stype container too large");

#ifndef __cplusplus
#define stgeneric(type, val)           ((stgeneric) { .st_##type = stCheck(type, val) })
#define stgeneric_unchecked(type, val) ((stgeneric) { .st_##type = (val) })
#define stgensarray(val)               stgeneric(ptr, (val)._is_sarray)
#else
#define stgeneric(type, val)           ((stgeneric)stCheck(type, val))
#define stgeneric_unchecked(type, val) ((stgeneric)(val))
#define stgensarray(val)               stgeneric(ptr, (val)._is_sarray)
#endif

// Compact variant structure. This is most often used for passing arrays of values that
// the type is not known at compile time, as part of the type-safe varargs replacement
// mechanism.

typedef struct stvar {
    stgeneric data;
    stype type;
} stvar;

// The type that's actually used for storage in containers, etc.
#define STStorageType_none      void
#define STStorageType_opaque    void
#define STStorageType_int8      int8
#define STStorageType_int16     int16
#define STStorageType_int32     int32
#define STStorageType_int64     int64
#define STStorageType_uint8     uint8
#define STStorageType_uint16    uint16
#define STStorageType_uint32    uint32
#define STStorageType_uint64    uint64
#define STStorageType_intptr    intptr
#define STStorageType_uintptr   uintptr
#define STStorageType_bool      _Bool
#define STStorageType_size      size_t
#define STStorageType_float32   float32
#define STStorageType_float64   float64
#define STStorageType_ptr       void*
#define STStorageType_string    string
#define STStorageType_strref    string
#define STStorageType_object    ObjInst*
#define STStorageType_weakref   ObjInst_WeakRef*
#define STStorageType_suid      SUID
#define STStorageType_stvar     stvar
#define STStorageType_sarray    sa_ref
#define STStorageType_hashtable hashtable
#define STStorageType_closure   closure
#define STStorageType_cchain    cchain
#define STStorageType_buffer    Buffer
#define STStorageType_struct    StructBase
#define STStorageType_structptr StructBase*
#define stStorageType(name)     STStorageType_##name

enum STYPE_ID {
    // none is a special type for empty argument lists, variants, etc
    STypeId_none   = STCLASS_BASIC | 0,
    // opaque is a magic catch-all type for custom structures and such
    STypeId_opaque = STCLASS_CX | STST_OPAQUE | 1,
    // generic scalar types
    STypeId_int8   = STCLASS_BASIC | STST_INT | 1,
    STypeId_int16  = STCLASS_BASIC | STST_INT | 2,
    STypeId_int32  = STCLASS_BASIC | STST_INT | 4,
    STypeId_int64  = STCLASS_BASIC | STST_INT | 8,
    STypeId_intptr = STCLASS_BASIC | STST_INT |
        sizeof(intptr),   // alias for one of the other int types
    STypeId_uint8   = STCLASS_BASIC | STST_UINT | 1,
    STypeId_uint16  = STCLASS_BASIC | STST_UINT | 2,
    STypeId_uint32  = STCLASS_BASIC | STST_UINT | 4,
    STypeId_uint64  = STCLASS_BASIC | STST_UINT | 8,
    STypeId_uintptr = STCLASS_BASIC | STST_UINT | sizeof(intptr),
    STypeId_bool    = STCLASS_BASIC | STST_UINT | 3,             // fill in a gap in the ID sequence
    STypeId_size = STCLASS_BASIC | STST_UINT | sizeof(size_t),   // alias for one of the uint types
    STypeId_float32   = STCLASS_BASIC | STST_FLOAT | 4,
    STypeId_float64   = STCLASS_BASIC | STST_FLOAT | 8,
    STypeId_ptr       = STCLASS_BASIC | STST_PTR | sizeof(void*),
    // most of the CX class are "object-like" types and use pointers as handles
    STypeId_suid      = STCLASS_CX | STST_MISC | 0,   // notable exception
    STypeId_string    = STCLASS_CX | STST_OBJ | 0,
    STypeId_strref    = STCLASS_CX | STST_OBJ | 0,
    STypeId_object    = STCLASS_CX | STST_OBJ | 1,
    STypeId_weakref   = STCLASS_CX | STST_OBJ | 2,
    STypeId_stvar     = STCLASS_CX | STST_OBJ | 3,
    STypeId_closure   = STCLASS_CX | STST_OBJ | 4,
    STypeId_buffer    = STCLASS_CX | STST_OBJ | 5,
    STypeId_struct    = STCLASS_CX | STST_OBJ | 6,
    STypeId_structptr = STCLASS_CX | STST_OBJ | 7,
    STypeId_sarray    = STCLASS_CX | STST_CONTAINER | 0,
    STypeId_hashtable = STCLASS_CX | STST_CONTAINER | 1,
    STypeId_cchain    = STCLASS_CX | STST_CONTAINER | 2,
};

/// uint32 stTypeId(type)
///
/// Get the compile-time type ID constant for a type name.
///
/// @param name Type name
/// @return Type ID enum value (STypeId_xxx)
///
/// Example:
/// @code
///   stype st = ...;
///   if (st->id == stTypeId(int32)) { ... }
/// @endcode
#define stTypeId(name) STypeId_##name

// The actual storage size of the type
enum STYPE_SIZE {
    STypeSize_none      = 0,
    // opaque is not really size 0, but filled by by macros later
    STypeSize_opaque    = 0,
    STypeSize_int8      = sizeof(int8),
    STypeSize_int16     = sizeof(int16),
    STypeSize_int32     = sizeof(int32),
    STypeSize_int64     = sizeof(int64),
    STypeSize_intptr    = sizeof(intptr),
    STypeSize_uint8     = sizeof(int8),
    STypeSize_uint16    = sizeof(int16),
    STypeSize_uint32    = sizeof(int32),
    STypeSize_uint64    = sizeof(int64),
    STypeSize_uintptr   = sizeof(uintptr),
    STypeSize_bool      = sizeof(_Bool),
    STypeSize_size      = sizeof(size_t),
    STypeSize_float32   = sizeof(float32),
    STypeSize_float64   = sizeof(float64),
    STypeSize_ptr       = sizeof(void*),
    STypeSize_string    = sizeof(void*),
    STypeSize_strref    = sizeof(void*),
    STypeSize_object    = sizeof(ObjInst*),
    STypeSize_weakref   = sizeof(ObjInst_WeakRef*),
    // SUID is special because it's always passed by reference, but stored as the full 16 bytes
    STypeSize_suid      = 16,
    STypeSize_stvar     = sizeof(stvar),
    STypeSize_sarray    = sizeof(sa_ref),
    STypeSize_hashtable = sizeof(hashtable),
    STypeSize_closure   = sizeof(closure),
    STypeSize_cchain    = sizeof(cchain),
    STypeSize_buffer    = sizeof(Buffer),
    // similar to opaque, not known at compile time
    STypeSize_struct    = 0,
    STypeSize_structptr = sizeof(StructBase*),
};

/// size_t stTypeSize(type)
///
/// Get the compile-time storage size for a type name.
///
/// @param name Type name
/// @return Size in bytes
///
/// Example:
/// @code
///   void* buffer = xaAlloc(stTypeSize(MyStruct) * count);
/// @endcode
#define stTypeSize(name) STypeSize_##name

enum STYPE_FLAGS {
    STypeFlag_Object    = (1 << 0),   // "object-like" type -- pointer to managed object
    STypeFlag_PassPtr   = (1 << 1),   // type is passed by pointer rather than by value,
                                      // does not apply to 'handle' style objects
    STypeFlag_Temporary = (1 << 2),   // type info is temporary, i.e. constructed as a compound
                                      // literal or otherwise dynamic. must be registered into the
                                      // global type registry before use
};
#define stFlag(name) STypeFlag_##name

/// bool stHasFlag(stype st, flagname)
///
/// Check if a type descriptor has a specific flag set.
///
/// @param st Type descriptor
/// @param fname Flag name (Object, Custom, or PassPtr)
/// @return true if flag is set
///
/// Example:
/// @code
///   if (stHasFlag(st, Object)) {
///       // This is an object-like type
///   }
/// @endcode
#define stHasFlag(st, fname) (((st)->flags & stFlag(fname)) != 0)

/// uint16 stGetSize(stype st)
///
/// Extract the storage size from a type descriptor. Equivalent to `st->size`.
///
/// @param st Type descriptor
/// @return Size in bytes
#define stGetSize(st) ((st)->size)

// Static type checks
// These types all include the marker as either the first member, or as part of
// a union that coincides with the first member. Therefore we can take their address
// and get the address of the parent structure that we already had (essentially a no-op)
// while checking that they exist, thus verifying the type is correct.
// Note the magic of the comma operator -- these prevent compilation if something is wrong,
// but get optimized away entirely and do not emit any code.
// The use of unused_noeval further improves this by hiding it in a conditional branch that
// is guaranteed to not be evaluated, preventing any side effects such as function calls or
// postfix operators.
// The seemingly redundant (h) && portions of the expression are there solely to suppress
// warnings from stupid compilers that don't understand it can never actually be dereferenced.
#define saCheckType(name, h)  ((sa_##name*)(unused_noeval((h) && &((h)->is_sarray_##name)), (h)))
#define saCheck(s)            (unused_noeval(&((s)._is_sarray)), (s.a))
#define saCheckPtr(h)         (unused_noeval((h != NULL) && &((h)->_is_sarray)), (h))
#define htCheck(h)            (unused_noeval((h) && &((h)->_is_hashtable)), (h))
#define htCheckPtr(h)         (unused_noeval((h) && (*h) && &((*h)->_is_hashtable)), (h))
#define objInstCheck(o)       (unused_noeval((o) && &((o)->_is_ObjInst)), (o))
#define objInstCheckPtr(o)    (unused_noeval((o != NULL) && (*o) && &((*o)->_is_ObjInst)), (o))
#define objWeakRefCheck(o)    (unused_noeval((o) && &((o)->_is_ObjInst_WeakRef)), (o))
#define objWeakRefCheckPtr(o) (unused_noeval((o) && (*o) && &((*o)->_is_ObjInst_WeakRef)), (o))
#define strCheck(s)           (unused_noeval((s) && &((s)->_is_string)), (s))
#define strCheckPtr(s)        (unused_noeval((s != NULL) && (*s) && &((*s)->_is_string)), (s))
#define closureCheck(c)       (unused_noeval((c) && &((c)->_is_closure)), (c))
#define closureCheckPtr(c)    (unused_noeval((c != NULL) && (*c) && &((*c)->_is_closure)), (c))
#define cchainCheck(c)        (unused_noeval((c) && &((c)->_is_closure_chain)), (c))
#define cchainCheckPtr(c)    (unused_noeval((c != NULL) && (*c) && &((*c)->_is_closure_chain)), (c))
#define bufferCheck(c)       (unused_noeval((c) && &((c)->_is_buffer)), (c))
#define bufferCheckPtr(c)    (unused_noeval((c != NULL) && (*c) && &((*c)->_is_buffer)), (c))
#define structCheck(s)       (unused_noeval(&((s)._is_struct)), (s))
#define structCheckPtr(s)    (unused_noeval((s != NULL) && &((s)->_is_struct)), (s))
#define structCheckPtrPtr(s) (unused_noeval((s != NULL) && (*s) && &((*s)->_is_struct)), (s))

// most of these are no-ops, but some can do extra type checking
#define STypeCheck_opaque(type, val)    (val)
#define STypeCheck_int8(type, val)      (val)
#define STypeCheck_int16(type, val)     (val)
#define STypeCheck_int32(type, val)     (val)
#define STypeCheck_int64(type, val)     (val)
#define STypeCheck_intptr(type, val)    (val)
#define STypeCheck_uint8(type, val)     (val)
#define STypeCheck_uint16(type, val)    (val)
#define STypeCheck_uint32(type, val)    (val)
#define STypeCheck_uint64(type, val)    (val)
#define STypeCheck_uintptr(type, val)   (val)
#define STypeCheck_bool(type, val)      (val)
#define STypeCheck_size(type, val)      (val)
#define STypeCheck_float32(type, val)   (val)
#define STypeCheck_float64(type, val)   (val)
#define STypeCheck_ptr(type, val)       (val)
#define STypeCheck_string(type, val)    strCheck(val)
#define STypeCheck_strref(type, val)    strCheck(val)
#define STypeCheck_object(type, val)    objInstCheck(val)
#define STypeCheck_weakref(type, val)   objWeakRefCheck(val)
#define STypeCheck_suid(type, val)      (unused_noeval((((val).low), ((val).high))), (val))
#define STypeCheck_stvar(_type, val)    (unused_noeval((((val).data), ((val).type))), (val))
#define STypeCheck_sarray(type, val)    saCheck(val)
#define STypeCheck_hashtable(type, val) htCheck(val)
#define STypeCheck_closure(type, val)   closureCheck(val)
#define STypeCheck_cchain(type, val)    cchainCheck(val)
#define STypeCheck_buffer(type, val)    bufferCheck(val)
#define STypeCheck_struct(type, val)    structCheck(val)
#define STypeCheck_structptr(type, val) structCheckPtr(val)

/// value stCheck(type, value)
///
/// Perform compile-time type checking on a value.
///
/// For primitive types, this is a no-op that returns the value unchanged.
/// For object types (strings, objects, containers), verifies the value has
/// the expected structure marker, providing compile-time type safety.
///
/// This is used internally by the argument passing macros but can also be
/// used directly for type validation.
///
/// @param type Type name
/// @param val Value to check
/// @return The value, unchanged (with compile-time validation)
///
/// Example:
/// @code
///   string s = ...;
///   string checked = stCheck(string, s);  // Validates s is a string
/// @endcode
#define stCheck(type, val) STypeCheck_##type(type, val)

#define STypeCheckPtr_gen(type, ptr)       (unused_noeval((stTypeDef(type)*)(ptr)), ptr)
#define STypeCheckPtr_opaque(type, ptr)    (ptr)
#define STypeCheckPtr_int8(type, ptr)      (ptr)
#define STypeCheckPtr_int16(type, ptr)     (ptr)
#define STypeCheckPtr_int32(type, ptr)     (ptr)
#define STypeCheckPtr_int64(type, ptr)     (ptr)
#define STypeCheckPtr_intptr(type, ptr)    (ptr)
#define STypeCheckPtr_uint8(type, ptr)     (ptr)
#define STypeCheckPtr_uint16(type, ptr)    (ptr)
#define STypeCheckPtr_uint32(type, ptr)    (ptr)
#define STypeCheckPtr_uint64(type, ptr)    (ptr)
#define STypeCheckPtr_uintptr(type, ptr)   (ptr)
#define STypeCheckPtr_bool(type, ptr)      (ptr)
#define STypeCheckPtr_size(type, ptr)      (ptr)
#define STypeCheckPtr_float32(type, ptr)   (ptr)
#define STypeCheckPtr_float64(type, ptr)   (ptr)
#define STypeCheckPtr_ptr(type, ptr)       (ptr)
#define STypeCheckPtr_string(type, ptr)    strCheckPtr(ptr)
#define STypeCheckPtr_strref(type, ptr)    strCheckPtr(ptr)
#define STypeCheckPtr_object(type, ptr)    objInstCheckPtr(ptr)
#define STypeCheckPtr_weakref(type, ptr)   objWeakRefCheckPtr(ptr)
#define STypeCheckPtr_suid(type, ptr)      (unused_noeval((((ptr)->low), ((ptr)->high))), (ptr))
#define STypeCheckPtr_stvar(_type, ptr)    (unused_noeval((((ptr)->data), ((ptr)->type))), (ptr))
#define STypeCheckPtr_sarray(type, ptr)    saCheckPtr(ptr)
#define STypeCheckPtr_hashtable(type, ptr) htCheckPtr(ptr)
#define STypeCheckPtr_closure(type, ptr)   closureCheckPtr(ptr)
#define STypeCheckPtr_cchain(type, ptr)    cchainCheckPtr(ptr)
#define STypeCheckPtr_buffer(type, ptr)    bufferCheckPtr(ptr)
#define STypeCheckPtr_struct(type, ptr)    structCheckPtr(ptr)
#define STypeCheckPtr_structptr(type, ptr) structCheckPtrPtr(ptr)

/// pointer stCheckPtr(type, pointer)
///
/// Perform compile-time type checking on a pointer to a value.
///
/// Similar to stCheck() but for pointers. Validates that the pointer points
/// to the expected type.
///
/// @param type Type name
/// @param ptr Pointer to check
/// @return The pointer, unchanged (with compile-time validation)
#define stCheckPtr(type, ptr) STypeCheckPtr_##type(type, ptr)

/// stStorageType(type)* stRvalAddr(type, rvalue)
///
/// Create an lvalue from an rvalue expression.
///
/// Uses C99 compound literals to create a temporary on the stack, allowing
/// pointers to be taken of arbitrary expressions. This is primarily used
/// internally for pass-by-pointer types (SUID, stvar, opaque) that need
/// to pass rvalues where a pointer is required.
///
/// @param type Type name
/// @param rval Rvalue expression
/// @return Pointer to a stack-allocated temporary containing the value
///
/// Example (internal use):
/// @code
///   // Allows passing SUID literals where SUID* is expected
///   stRvalAddr(suid, suidMake())
/// @endcode
// C99 compound literals are lvalues and can force the compiler to create a temporary
// on the stack if necessary, so that we can pass pointers to arbitrary expressions
#define stRvalAddr(type, rval) ((stStorageType(type)[1]) { rval })

/// @}

// Canonical type information for builtin types
extern const STypeInfo _sti_none;
extern const STypeInfo _sti_int8;
extern const STypeInfo _sti_int16;
extern const STypeInfo _sti_int32;
extern const STypeInfo _sti_int64;
extern const STypeInfo _sti_uint8;
extern const STypeInfo _sti_uint16;
extern const STypeInfo _sti_uint32;
extern const STypeInfo _sti_uint64;
extern const STypeInfo _sti_bool;
extern const STypeInfo _sti_float32;
extern const STypeInfo _sti_float64;
extern const STypeInfo _sti_ptr;
extern const STypeInfo _sti_string;
extern const STypeInfo _sti_object;
extern const STypeInfo _sti_weakref;
extern const STypeInfo _sti_suid;
extern const STypeInfo _sti_stvar;
extern const STypeInfo _sti_sarray;
extern const STypeInfo _sti_hashtable;
extern const STypeInfo _sti_closure;
extern const STypeInfo _sti_cchain;
extern const STypeInfo _sti_buffer;
extern const STypeInfo _sti_structptr;

extern const STypeOps _stops_opaque;
extern const STypeOps _stops_struct;

// canonical type info name for static types
#define stTypeInfo(name) _sti_##name

// MEGA PREPROCESSOR HACKS INCOMING
// this enables the use of opaque(realtype) as type name in functions like saCreate
#ifndef _cplusplus
#define _sti_opaque(realtype)                                          \
    ((const STypeInfo) { .id    = stTypeId(opaque),                    \
                         .flags = stFlag(PassPtr) | stFlag(Temporary), \
                         .size  = (uint16)sizeof(realtype),            \
                         .ops   = _stops_opaque })
#define _sti_struct(realtype)                                                           \
    ((const STypeInfo) { .id    = stTypeId(struct),                                     \
                         .flags = stFlag(PassPtr) | stFlag(Object) | stFlag(Temporary), \
                         .size  = (uint16)sizeof(realtype),                             \
                         .ops   = _stops_struct })
#else
// C++ doesn't allow compound literals in the same way
// We probably don't actually need to define anything since this code shouldn't be used in C++ mode.
// Just need to make sure that the header doesn't cause compiler errors if included.
#endif

// aliases for types that don't have their own info

#define _sti_strref _sti_string

// these are architecture-dependant alises
#if defined(_64BIT)
#define _sti_intptr  _sti_int64
#define _sti_uintptr _sti_uint64
#define _sti_size    _sti_uint64
#else
#define _sti_intptr  _sti_int32
#define _sti_uintptr _sti_uint32
#define _sti_size    _sti_uint32
#endif

/// @defgroup stype_args Argument Passing System
/// @ingroup stype
/// @{
///
/// Macros for passing typed values through generic function interfaces.
///
/// The CX framework uses a macro-based system for type-safe generic functions.
/// Instead of traditional C varargs or void pointers, functions take type-value
/// pairs that expand to type descriptors and type-checked containers.
///
/// **Four levels of argument passing:**
///
/// 1. **stType(type)** - Type descriptor only (stype value)
/// 2. **stFullType(type)** - Type descriptor + operations pointer (stype, STypeOps*)
/// 3. **stArg(type, val)** - Type-checked value in generic container (stgeneric)
/// 4. **stCheckedArg(type, val)** - Complete: descriptor + checked value (stype, stgeneric)
///
/// **Usage patterns:**
///
/// @code
///   // Simple type descriptor
///   stype t = stType(int32);
///
///   // Generic container operations
///   htInsert(&ht, string, _SL("key"), int32, 42);
///   // Expands to:
///   _htInsert(&ht, stType(string), stArg(string, _SL("key")),
///                  stType(int32), stArg(int32, 42));
///
///   // Low-level operations with full type info
///   stDestroy(string, &s);
///   // Expands to:
///   _stDestroy(stFullType(string), stArgPtr(string, &s));
/// @endcode
///
/// The pointer variants (stArgPtr, stCheckedPtrArg) are used for output parameters
/// or when the function needs to modify/consume the value.

/// stype stType(type)
///
/// Get a type descriptor for a type name.
///
/// For opaque types, can specify the real structure type: stType(opaque(MyStruct))
///
/// @param name Type name or opaque(RealType)
/// @return Type descriptor
///
/// Example:
/// @code
///   stype st = stType(int32);
///   stype st_custom = stType(opaque(MyStruct));
/// @endcode
#define stType(name) (&stTypeInfo(name))

// Macros for passing arguments by value

// none ignores the value
#define STypeArg_none(type, val)    ((stgeneric) { 0 })
// opqaue is a special case that must always be passed by pointer, and
// the caller must supply an lvalue
#define STypeArg_opaque(type, val)  stgeneric(type, &(val))
// but everything else gets put into a container
#define STypeArg_int8(type, val)    stgeneric(type, val)
#define STypeArg_int16(type, val)   stgeneric(type, val)
#define STypeArg_int32(type, val)   stgeneric(type, val)
#define STypeArg_int64(type, val)   stgeneric(type, val)
#define STypeArg_intptr(type, val)  stgeneric(type, val)
#define STypeArg_uint8(type, val)   stgeneric(type, val)
#define STypeArg_uint16(type, val)  stgeneric(type, val)
#define STypeArg_uint32(type, val)  stgeneric(type, val)
#define STypeArg_uint64(type, val)  stgeneric(type, val)
#define STypeArg_uintptr(type, val) stgeneric(type, val)
#define STypeArg_bool(type, val)    stgeneric(type, val)
#define STypeArg_size(type, val)    stgeneric(type, val)
#define STypeArg_float32(type, val) stgeneric(type, val)
#define STypeArg_float64(type, val) stgeneric(type, val)
#define STypeArg_ptr(type, val)     stgeneric(type, val)
#define STypeArg_string(type, val)  stgeneric(type, val)
#define STypeArg_strref(type, val)  stgeneric(type, val)
#define STypeArg_object(type, val)  stgeneric(type, objInstBase(val))
#define STypeArg_weakref(type, val) stgeneric(type, objWeakRefBase(val))
// SUID and stvar are too big, so make a copy and pass a pointer
#define STypeArg_suid(type, val)    stgeneric_unchecked(type, stRvalAddr(type, stCheck(type, val)))
#define STypeArg_stvar(type, val)   stgeneric_unchecked(type, stRvalAddr(type, stCheck(type, val)))
#define STypeArg_sarray(type, val)  stgensarray(val)
#define STypeArg_hashtable(type, val) stgeneric(type, val)
#define STypeArg_closure(type, val)   stgeneric(type, val)
#define STypeArg_cchain(type, val)    stgeneric(type, val)
#define STypeArg_buffer(type, val)    stgeneric(type, val)
// struct works like opaque
#define STypeArg_struct(type, val)    stgeneric(type, &(val))
#define STypeArg_structptr(type, val) stgeneric(type, val)

/// stgeneric stArg(type, value)
///
/// Pack a typed value into a generic container with compile-time type checking.
///
/// Converts a value of any supported type into an stgeneric union that can be
/// passed through generic function interfaces. Performs type checking to ensure
/// the value is compatible with the specified type.
///
/// Special handling:
/// - Primitives: Stored directly in the union
/// - Objects/handles: Stored as pointers
/// - opaque: Requires an lvalue, takes its address
/// - SUID/stvar: Too large for union, creates stack temporary and passes pointer
///
/// @param type Type name
/// @param val Value to pack
/// @return Generic container holding the value
///
/// Example:
/// @code
///   stgeneric gen = stArg(int32, 42);
///   stgeneric gen_str = stArg(string, s);
/// @endcode
#define stArg(type, val) STypeArg_##type(type, val)

// And for passing a pointer-to-pointer, mostly for functions that want to
// consume or reallocate the object

#define STypeArgPtr_none(type, val)      NULL
// opaque is already passed in as a pointer
#define STypeArgPtr_opaque(type, val)    &stgeneric(type, val)
#define STypeArgPtr_int8(type, val)      (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int16(type, val)     (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int32(type, val)     (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int64(type, val)     (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_intptr(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint8(type, val)     (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint16(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint32(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint64(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uintptr(type, val)   (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_bool(type, val)      (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_size(type, val)      (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_float32(type, val)   (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_float64(type, val)   (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_ptr(type, val)       (stgeneric*)stCheckPtr(type, (void**)(val))
#define STypeArgPtr_string(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_strref(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_object(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_weakref(type, val)   (stgeneric*)stCheckPtr(type, val)
// same for the other pass-by-pointer cases (SUID, stvar)
#define STypeArgPtr_suid(type, val)      &stgeneric_unchecked(type, stCheckPtr(type, val))
#define STypeArgPtr_stvar(type, val)     &stgeneric_unchecked(type, stCheckPtr(type, val))
#define STypeArgPtr_sarray(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_hashtable(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_closure(type, val)   (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_cchain(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_buffer(type, val)    (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_struct(type, val)    &stgeneric(type, val)
#define STypeArgPtr_structptr(type, val) (stgeneric*)stCheckPtr(type, val)

/// stgeneric* stArgPtr(type, pointer)
///
/// Create a pointer to a generic container from a pointer to a typed value.
///
/// Used for output parameters or when a function needs to modify or consume
/// the value. For most types, casts the pointer to stgeneric*. For opaque
/// types, wraps the pointer in a generic container.
///
/// @param type Type name
/// @param val Pointer to the value
/// @return Pointer to generic container
///
/// Example:
/// @code
///   int32 i;
///   stgeneric* gen_ptr = stArgPtr(int32, &i);
/// @endcode
#define stArgPtr(type, val) STypeArgPtr_##type(type, val)

// Macros for type-checked inline metafunctions.
// These expand to a pair of parameters for type, followed by a pointer.

#define STypeCheckedArg_none(type, val)      stType(type), stArg(type, val)
#define STypeCheckedArg_opaque(type, val)    (&_sti_opaque(val)), stArg(type, val)
#define STypeCheckedArg_int8(type, val)      stType(type), stArg(type, val)
#define STypeCheckedArg_int16(type, val)     stType(type), stArg(type, val)
#define STypeCheckedArg_int32(type, val)     stType(type), stArg(type, val)
#define STypeCheckedArg_int64(type, val)     stType(type), stArg(type, val)
#define STypeCheckedArg_intptr(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_uint8(type, val)     stType(type), stArg(type, val)
#define STypeCheckedArg_uint16(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_uint32(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_uint64(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_uintptr(type, val)   stType(type), stArg(type, val)
#define STypeCheckedArg_bool(type, val)      stType(type), stArg(type, val)
#define STypeCheckedArg_size(type, val)      stType(type), stArg(type, val)
#define STypeCheckedArg_float32(type, val)   stType(type), stArg(type, val)
#define STypeCheckedArg_float64(type, val)   stType(type), stArg(type, val)
#define STypeCheckedArg_ptr(type, val)       stType(type), stArg(type, val)
#define STypeCheckedArg_string(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_strref(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_object(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_weakref(type, val)   stType(type), stArg(type, val)
#define STypeCheckedArg_suid(type, val)      stType(type), stArg(type, val)
#define STypeCheckedArg_stvar(type, val)     stType(type), stArg(type, val)
#define STypeCheckedArg_sarray(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_hashtable(type, val) stType(type), stArg(type, val)
#define STypeCheckedArg_closure(type, val)   stType(type), stArg(type, val)
#define STypeCheckedArg_cchain(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_buffer(type, val)    stType(type), stArg(type, val)
#define STypeCheckedArg_struct(type, val)    (&_sti_struct(val)), stArg(type, val)
#define STypeCheckedArg_structptr(type, val) stType(type), stArg(type, val)

/// (stype, stgeneric) stCheckedArg(type, value)
///
/// Complete argument package: type descriptor + type-checked value.
///
/// Expands to TWO comma-separated values for passing to functions that take
/// runtime type parameters. Combines stType() and stArg() into a single call.
/// This is the most common form for generic function parameters.
///
/// @param type Type name
/// @param val Value to pass
/// @return Type descriptor, generic container (TWO values)
///
/// Example:
/// @code
///   _saPush(handle, stCheckedArg(int32, 42));
///   // Expands to: _saPush(handle, stType(int32), stArg(int32, 42))
///
///   htInsert(&ht, string, _SL("key"), int32, 42);
///   // Each type-value pair uses stCheckedArg internally
/// @endcode
#define stCheckedArg(type, val) STypeCheckedArg_##type(type, val)

// Type checking of pointers to types, mostly for functions that want to
// consume object-like variables and destroy them

#define STypeCheckedPtrArg_none(type, val)      stType(type), stArgPtr(type, val)
// go the opposite direction for opaque since it's already passed in as a pointer
#define STypeCheckedPtrArg_opaque(type, val)    (&_sti_opaque(*val)), stArgPtr(type, val)
#define STypeCheckedPtrArg_int8(type, val)      stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_int16(type, val)     stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_int32(type, val)     stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_int64(type, val)     stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_intptr(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_uint8(type, val)     stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_uint16(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_uint32(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_uint64(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_uintptr(type, val)   stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_bool(type, val)      stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_size(type, val)      stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_float32(type, val)   stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_float64(type, val)   stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_ptr(type, val)       stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_string(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_strref(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_object(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_weakref(type, val)   stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_suid(type, val)      stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_stvar(type, val)     stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_sarray(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_hashtable(type, val) stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_closure(type, val)   stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_cchain(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_buffer(type, val)    stType(type), stArgPtr(type, val)
#define STypeCheckedPtrArg_struct(type, val)    (&_sti_struct(*val)), stArgPtr(type, val)
#define STypeCheckedPtrArg_structptr(type, val) stType(type), stArgPtr(type, val)

/// (stype, stgeneric*) stCheckedPtrArg(type, pointer)
///
/// Complete argument package for pointer parameters: type descriptor + pointer.
///
/// Expands to TWO comma-separated values for passing to functions that need
/// to modify or consume the value. Combines stType() and stArgPtr() into a
/// single call. Used for output parameters and value consumption.
///
/// @param type Type name
/// @param val Pointer to the value
/// @return Type descriptor, pointer to generic container (TWO values)
///
/// Example:
/// @code
///   int32 result;
///   _someFunc(stCheckedPtrArg(int32, &result));
///   // Expands to: _someFunc(stType(int32), stArgPtr(int32, &result))
///
///   ssdCopyOut(root, path, int32, &val);
///   // Uses stCheckedPtrArg internally for the output parameter
/// @endcode
#define stCheckedPtrArg(type, val) STypeCheckedPtrArg_##type(type, val)

/// @}

/// @defgroup stype_flags Operation Flags
/// @ingroup stype
/// @{

/// Flags controlling behavior of type operations (comparison, hashing, conversion).
/// Multiple flags can be combined with bitwise OR.
enum STYPE_OPS_FLAGS {
    /// Perform case-insensitive operation (valid for: cmp, hash)
    ///
    /// When used with comparison or hashing, treats strings and string-like types
    /// in a case-insensitive manner. Has no effect on types that don't support
    /// case-insensitive operations.
    ST_CaseInsensitive = 0x00000001,

    /// Only check equality, not ordering (valid for: cmp)
    ///
    /// Indicates that the caller only needs to know if values are equal or not equal.
    /// The comparison function may return 0 for equal and any nonzero value for not equal,
    /// without establishing a proper ordering relationship. This is important for objects
    /// that don't implement the Sortable interface but still need equality testing,
    /// especially when stored in stvar or heterogeneous containers.
    ST_Equality = 0x00000002,

    /// Allow numeric overflow/underflow (valid for: convert)
    ///
    /// Disables range checking during type conversion. Allows values outside the
    /// destination type's range to wrap around. For example, converting int32(-1)
    /// to uint32 with this flag produces 0xffffffff. Without this flag, the
    /// conversion would fail. May result in sign flips for signed integers.
    ST_Overflow = 0x00000004,

    /// Require lossless conversion (valid for: convert)
    ///
    /// Enables extra precision checks during conversion. The conversion will fail
    /// if the exact value cannot be represented in the destination type. For example,
    /// converting int32(16777217) to float32 fails with this flag because float32
    /// cannot represent all integers in that range with full precision, even though
    /// the value is within the numeric range.
    ST_Lossless = 0x00000008,
};

/// @}

/// @defgroup stype_ops Type Operations
/// @ingroup stype
/// @{
///
/// Function pointers for runtime-dispatched operations on typed values.
/// These enable generic algorithms to work with any type that provides
/// the required operations.
///
/// **IMPORTANT**: These are LOW LEVEL operations. In particular, stCopy and
/// stConvert **OVERWRITE** the destination without checking. This allows them
/// to efficiently initialize uninitialized memory, but caution is required with
/// object types to avoid leaking memory.
///
/// Example leak scenario:
/// @code
///   string s1 = _SL("original");
///   string s2 = _SL("new");
///   stCopy(string, &s1, s2);  // LEAKS "original" - should use strDup!
/// @endcode
///
/// For managed types (strings, objects, containers), use their specific APIs
/// for assignment operations. Use stype operations primarily for:
/// - Initializing uninitialized memory
/// - Container implementation internals
/// - Generic code that doesn't know the specific type

#define _stCopyDest_Anno_(typvar)     _Post_valid_ _When_(stHasFlag(typvar, PassPtr), _Pre_valid_)
#define _stCopyDest_Anno_opt_(typvar) _Out_opt_ _When_(stHasFlag(typvar, PassPtr), _Pre_opt_valid_)

/// void stDtorFunc(stype st, stgeneric* gen, [flags])
///
/// Destructor function for releasing resources owned by a typed value.
///
/// Called when a value is being removed from a container or otherwise destroyed.
/// Responsible for freeing memory, decrementing reference counts, and any other
/// cleanup required by the type.
///
/// @param st Type descriptor for the value
/// @param gen Pointer to the value container (invalidated after call)
/// @param flags Optional operation flags
typedef void (*stDtorFunc)(stype st, _Pre_notnull_ _Post_invalid_ stgeneric* gen, flags_t flags);

/// intptr stCmpFunc(stype st, stgeneric gen1, stgeneric gen2, [flags])
///
/// Comparison function returning ordering of two values.
///
/// @param st Type descriptor for both values
/// @param gen1 First value
/// @param gen2 Second value
/// @param flags Operation flags:
///   - ST_CaseInsensitive: Perform case-insensitive comparison (if applicable)
///   - ST_Equality: Caller only needs equality (0/non-zero), not ordering
/// @return Negative if gen1 < gen2, zero if equal, positive if gen1 > gen2
typedef intptr (*stCmpFunc)(stype st, _In_ stgeneric gen1, _In_ stgeneric gen2, flags_t flags);

/// uint32 stHashFunc(stype st, stgeneric gen, [flags])
///
/// Hash function computing a 32-bit hash value for use in hash tables.
///
/// Must satisfy: if stCmp(a,b)==0, then stHash(a)==stHash(b)
///
/// @param st Type descriptor
/// @param gen Value to hash
/// @param flags Operation flags:
///   - ST_CaseInsensitive: Case-insensitive hash (if applicable)
/// @return 32-bit hash value
typedef uint32 (*stHashFunc)(stype st, _In_ stgeneric gen, flags_t flags);

/// void stCopyFunc(stype st, stgeneric* dest, stgeneric src, [flags])
///
/// Deep copy function that duplicates a value.
///
/// **WARNING**: Overwrites destination without destroying existing value.
/// Use only on uninitialized memory or after calling stDestroy on dest.
///
/// For reference-counted types (strings, objects), increments the reference count.
/// For value types, performs a bitwise copy. For complex types, may perform
/// deep duplication.
///
/// @param st Type descriptor
/// @param dest Pointer to destination (must be valid for PassPtr types)
/// @param src Source value to copy
/// @param flags Optional operation flags
typedef void (*stCopyFunc)(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                           flags_t flags);

/// bool stConvertFunc(stype destst, stgeneric* dest, stype srcst, stgeneric src, [flags])
///
/// Type conversion function attempting to convert from one type to another.
///
/// **WARNING**: Overwrites destination without destroying existing value.
///
/// May fail if conversion is not possible, out of range, or would lose precision
/// (depending on flags). The source type is responsible for implementing
/// conversions to other types.
///
/// @param destst Destination type descriptor
/// @param dest Pointer to destination
/// @param srcst Source type descriptor
/// @param src Source value
/// @param flags Conversion flags:
///   - ST_Overflow: Allow overflow/underflow (no range checking)
///   - ST_Lossless: Fail if conversion loses precision
/// @return true if conversion succeeded, false otherwise
typedef _Success_(return)
_Check_return_ bool (*stConvertFunc)(stype destst, _stCopyDest_Anno_(destst) stgeneric* dest,
                                     stype srcst, _In_ stgeneric src, flags_t flags);

typedef struct STypeOps {
    stDtorFunc dtor;
    stCmpFunc cmp;
    stHashFunc hash;
    stCopyFunc copy;
    stConvertFunc convert;
} STypeOps;

/// Type descriptor for all runtime types in the stype system.
///
/// Each type has exactly one canonical `const STypeInfo` in static storage. `stype` is
/// `const STypeInfo*` - two type values are equal if and only if they point to the same
/// canonical descriptor (use `stEq()` to compare, which handles Temporary canonicalization).
///
/// Built-in type descriptors are declared as `extern const STypeInfo _sti_name` and
/// accessed via `stType(name)` / `stTypeInfo(name)`. Custom types are defined at file
/// scope with `stDefine(name) { ... }` and declared in headers with
/// `stDeclare(name)`.
typedef struct STypeInfo {
    uint32 id;          ///< hierarchical type ID: STCLASS | STST_subtype | discriminant
    uint16 size;        ///< storage size in bytes
    uint16 flags;       ///< STypeFlag_Object, STypeFlag_PassPtr, STypeFlag_Temporary
    const char* name;   ///< name for debug, serialization, etc

    /// Parameterized sub-type descriptors (must point to canonical types):
    ///   sarray:    param[0] = element type, param[1] = NULL
    ///   hashtable: param[0] = key type,     param[1] = value type
    ///   all others: both NULL
    const STypeInfo* param[2];

    const void* ext;   ///< type-specific extension data (e.g. StructInfo*); ignored by stype core
    STypeOps ops;      ///< operations embedded directly - no separate allocation
} STypeInfo;

/// @}

/// @defgroup stype_storage Storage Access
/// @ingroup stype
/// @{
///
/// Macros for loading and storing typed values from raw memory.
/// These handle the distinction between pass-by-value types (stored inline)
/// and pass-by-pointer types (stored as pointers).

_meta_inline stgeneric _stStoredVal(stype st, _In_ const void* storage)
{
    stgeneric ret;
    switch (st->size) {
    case 1:
        ret.st_uint8 = *(uint8*)storage;
        break;
    case 2:
        ret.st_uint16 = *(uint16*)storage;
        break;
    case 4:
        ret.st_uint32 = *(uint32*)storage;
        break;
    case 8:
        ret.st_uint64 = *(uint64*)storage;
        break;
    default:
        devFatalError("Invalid small stype size");
        ret.st_uint64 = 0;
    }
    return ret;
}

/// stgeneric stStored(stype st, void* storage)
///
/// Load a typed value from raw storage into a generic container.
///
/// Handles both inline storage (primitives) and pointer-based storage (objects, opaque).
/// For pass-by-value types, reads the value directly from memory. For pass-by-pointer
/// types, treats storage as containing a pointer to the actual value.
///
/// @param st Type descriptor
/// @param storage Pointer to the stored value
/// @return Generic container holding the value
///
/// Example:
/// @code
///   int32* array = ...;
///   stgeneric val = stStored(stType(int32), &array[5]);
/// @endcode
#define stStored(st, storage) \
    (stHasFlag(st, PassPtr) ? stgeneric(ptr, (void*)(storage)) : _stStoredVal(st, storage))

/// stgeneric* stStoredPtr(stype st, void* storage)
///
/// Get a pointer to a stored value suitable for modification.
///
/// For pass-by-value types, returns a pointer directly to the storage location.
/// For pass-by-pointer types, wraps the pointer in a generic container.
///
/// @param st Type descriptor
/// @param storage Pointer to storage location
/// @return Pointer to generic container that can be used with type operations
#define stStoredPtr(st, storage) \
    (stHasFlag(st, PassPtr) ? &stgeneric(ptr, ((void*)(storage))) : (stgeneric*)((void*)(storage)))

/// void* stGenPtr(stype st, stgeneric gen)
///
/// Get a pointer to the actual data from a generic container.
///
/// For pass-by-pointer types, extracts the pointer stored in the container.
/// For pass-by-value types, returns the address of the container itself.
///
/// @param st Type descriptor
/// @param gen Generic container with value
/// @return Pointer to the actual data
#define stGenPtr(st, gen) (stHasFlag(st, PassPtr) ? (gen).st_ptr : &(gen))

/// @}

/// @defgroup stype_api High-Level Operations
/// @ingroup stype
/// @{
///
/// Type-safe wrappers for common operations on typed values.
/// These macros expand to the appropriate function calls with compile-time
/// type checking.

// inlining these lets most of it get optimized out and specialized if the type is known at
// compile-time

/// void stDestroy(type, pobj, [flags])
///
/// Destroy a typed value, releasing any resources it owns.
///
/// Calls the appropriate destructor based on the type. For reference-counted
/// types, decrements the reference count. For containers, recursively destroys
/// elements. For primitives, this is typically a no-op.
///
/// @param type Type name (e.g., string, int32, object)
/// @param pobj Pointer to the value to destroy
/// @param ... (flags) Optional operation flags
///
/// Example:
/// @code
///   string s = _SL("hello");
///   stDestroy(string, &s);  // s is now invalid
/// @endcode
#define stDestroy(type, pobj, ...) \
    _stDestroy(stType(type), stArgPtr(type, pobj), opt_flags(__VA_ARGS__))
_meta_inline void _stDestroy(stype st, _Pre_notnull_ _Post_invalid_ stgeneric* gen, flags_t flags)
{
    if (!st)
        return;

    if (st->ops.dtor)
        st->ops.dtor(st, gen, flags);
}

#ifdef __GNUC__
// GCC incorrectly emits an array bounds warning on the second memcpy in _stCopy; and we use -Werror
// which turns it into an error. The warning is provably a false positive because it's triggering on
// the int8 case where the size is 1, but the branch that accesses st_ptr is never reached because
// int8 does not have the PassPtr flag.
// The same line also triggers a maybe-uninitialized warning.
// This section also suppresses an incorrect nonnull warning in _stCmp that follows the same pattern
// of the compiler thinking we're accessing the pointer version when we are not.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wnonnull"
#endif

/// intptr stCmp(type, obj1, obj2, [flags])
///
/// Compare two typed values for ordering.
///
/// @param type Type name
/// @param obj1 First value
/// @param obj2 Second value
/// @param ... (flags) Optional operation flags:
///   - ST_CaseInsensitive: Case-insensitive comparison
///   - ST_Equality: Only check equality, not ordering
/// @return Negative if obj1 < obj2, zero if equal, positive if obj1 > obj2
///
/// Example:
/// @code
///   if (stCmp(string, s1, s2) == 0) {
///       // strings are equal
///   }
/// @endcode
#define stCmp(type, obj1, obj2, ...) \
    _stCmp(stType(type), stArg(type, obj1), stArg(type, obj2), opt_flags(__VA_ARGS__))
_meta_inline intptr _stCmp(stype st, _In_ stgeneric gen1, _In_ stgeneric gen2, flags_t flags)
{
    if (!st)
        return 1;   // treat null type as always greater (sorts last), mirrors stCmp_none

    if (st->ops.cmp)
        return st->ops.cmp(st, gen1, gen2, flags);

    if (!stHasFlag(st, PassPtr))
        return memcmp(&gen1, &gen2, st->size);
    else
        return memcmp(gen1.st_ptr, gen2.st_ptr, st->size);
}

/// void stCopy(type, pdest, src, [flags])
///
/// Copy a typed value from source to destination.
///
/// **WARNING**: Overwrites destination without destroying existing value.
/// For managed types, the destination must be uninitialized or already destroyed.
///
/// @param type Type name
/// @param pdest Pointer to destination
/// @param src Source value
/// @param ... (flags) Optional operation flags
///
/// Example:
/// @code
///   int32 dest;  // uninitialized
///   stCopy(int32, &dest, 42);
/// @endcode
#define stCopy(type, pdest, src, ...) \
    _stCopy(stType(type), stArgPtr(type, pdest), stArg(type, src), opt_flags(__VA_ARGS__))
_meta_inline void _stCopy(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                          flags_t flags)
{
    if (!st)
        return;

    if (st->ops.copy)
        st->ops.copy(st, dest, src, flags);
    else if (!stHasFlag(st, PassPtr))
        memcpy(dest, &src, st->size);
    else
        memcpy(dest->st_ptr, src.st_ptr, st->size);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

uint32 stHash_gen(stype st, _In_ stgeneric stgen, flags_t flags);

/// uint32 stHash(type, obj, [flags])
///
/// Compute a hash value for a typed value.
///
/// @param type Type name
/// @param obj Value to hash
/// @param ... (flags) Optional operation flags:
///   - ST_CaseInsensitive: Case-insensitive hash
/// @return 32-bit hash value
///
/// Example:
/// @code
///   uint32 h = stHash(string, s);
/// @endcode
#define stHash(type, obj, ...) _stHash(stType(type), stArg(type, obj), opt_flags(__VA_ARGS__))
_meta_inline uint32 _stHash(stype st, _In_ stgeneric gen, flags_t flags)
{
    if (!st)
        return 0;

    if (st->ops.hash)
        return st->ops.hash(st, gen, flags);
    else
        return stHash_gen(st, gen, flags);
}

/// bool stConvert(desttype, pdest, srctype, src, [flags])
///
/// Convert a value from one type to another.
///
/// **WARNING**: Overwrites destination without destroying existing value.
///
/// @param desttype Destination type name
/// @param pdest Pointer to destination
/// @param srctype Source type name
/// @param src Source value
/// @param ... (flags) Conversion flags:
///   - ST_Overflow: Allow overflow without error
///   - ST_Lossless: Fail if conversion loses precision
/// @return true if conversion succeeded
///
/// Example:
/// @code
///   float32 f;
///   if (stConvert(float32, &f, int32, 42)) {
///       // f now contains 42.0
///   }
/// @endcode
#define stConvert(desttype, pdest, srctype, src, ...) \
    _stConvert(stType(desttype),                      \
               stArgPtr(desttype, pdest),             \
               stType(srctype),                       \
               stArg(srctype, src),                   \
               opt_flags(__VA_ARGS__))
_Success_(return) _Check_return_ _meta_inline bool
_stConvert(stype destst, _stCopyDest_Anno_(destst) stgeneric* dest, stype srcst, _In_ stgeneric src,
           flags_t flags)
{
    if (!srcst || !destst)
        return false;

    // The *source* stype is responsible for handling conversions to other types
    if (srcst == destst)
        return _stCopy(destst, dest, src, flags), true;
    if (srcst->ops.convert)
        return srcst->ops.convert(destst, dest, srcst, src, flags);

    return false;   // can't convert it if we don't know how!
}

// Internal: look up or insert a Temporary descriptor in the global type registry.
// Recursively canonicalizes param[0]/param[1] before keying. Thread-safe via RWLock.
// Always returns a non-Temporary canonical pointer.
stype _stGetCanonical(stype st);

/// stype stCanonical(stype st)
///
/// Return the canonical pointer for a type descriptor.
///
/// For static built-in types (`STypeFlag_Temporary` clear), returns `st` immediately -
/// zero overhead. For dynamically constructed types (`STypeFlag_Temporary` set, e.g.
/// `opaque(MyStruct)` compound literals or parameterized container types), looks up or
/// inserts the descriptor in the global type registry and returns the stable canonical
/// pointer. After the first call, all equivalent descriptors resolve to the same address.
///
/// Containers call `stCanonical()` automatically at init time. User code rarely needs
/// to call this directly.
///
/// @param st Type descriptor (may be Temporary)
/// @return Canonical, non-Temporary type descriptor pointer
_meta_inline stype stCanonical(stype st)
{
    if (!st)
        return stType(none);

    if (!stHasFlag(st, Temporary))
        return st;

    return _stGetCanonical(st);
}

/// bool stEq(stype s1, stype s2)
///
/// Test whether two type descriptors refer to the same type.
///
/// Calls `stCanonical()` on both operands before comparing pointers. For static types
/// this reduces to a single pointer comparison. Handles the case where one or both
/// descriptors are Temporary compound literals that have not yet been registered.
///
/// @param s1 First type descriptor
/// @param s2 Second type descriptor
/// @return true if both descriptors represent the same type
_meta_inline bool stEq(stype s1, stype s2)
{
    return stCanonical(s1) == stCanonical(s2);
}

/// @defgroup stype_custom Custom Type Integration
/// @ingroup stype
/// @{
///
/// Macros for defining user types that integrate fully with the stype system.
///
/// A custom type is defined with `stDefine` followed by a brace-enclosed
/// `STypeInfo` initializer, paired with a standard block of harness `#define`s that
/// hook the type name into `stType`, `stArg`, `stCheckedArg`, and the container APIs
/// (`saInit`, `saPush`, `htInsert`, `stCmp`, etc.).
///
/// **File-local type** (no cross-TU sharing needed):
/// @code
///   // In myfile.c:
///   static stDefine(mykey) {
///       .id    = STypeId_opaque,
///       .size  = sizeof(MyKey),
///       .flags = stFlag(PassPtr),
///       .name  = "MyKey",
///       .ops   = { .dtor = myKeyDtor, .cmp = myKeyCmp, .hash = myKeyHash },
///   };
///   #define SType_mykey                         MyKey*
///   #define STStorageType_mykey                 MyKey
///   #define STypeArg_mykey(type, val)           stgeneric(opaque, &(val))
///   #define STypeArgPtr_mykey(type, val)        &stgeneric(opaque, (val))
///   #define STypeCheckedArg_mykey(type, val)    stType(type), stArg(type, val)
///   #define STypeCheckedPtrArg_mykey(type, val) stType(type), stArgPtr(type, val)
///
///   hashtable tbl;
///   htInit(&tbl, mykey, int32, 32);
///   htInsert(&tbl, mykey, k, int32, 500);
/// @endcode
///
/// **Shared type** (visible across translation units):
/// @code
///   // mylib.h:
///   stDeclare(vec3);
///   saDeclare(vec3);
///   #define SType_vec3                         Vec3*
///   #define STStorageType_vec3                 Vec3
///   #define STypeArg_vec3(type, val)           stgeneric(opaque, &(val))
///   #define STypeArgPtr_vec3(type, val)        &stgeneric(opaque, (val))
///   #define STypeCheckedArg_vec3(type, val)    stType(type), stArg(type, val)
///   #define STypeCheckedPtrArg_vec3(type, val) stType(type), stArgPtr(type, val)
///
///   // mylib.c:
///   stDefine(vec3) {
///       .id    = STypeId_opaque,
///       .size  = sizeof(Vec3),
///       .flags = stFlag(PassPtr),
///       .name  = "Vec3",
///       .ops   = { .cmp = vec3Cmp, .hash = vec3Hash },
///   };
///
///   // any .c file that includes mylib.h:
///   sa_Vec3 points;
///   saInit(&points, vec3, 64);
///   Vec3 v = { 1.0f, 2.0f, 3.0f };
///   saPush(&points, vec3, v);
///   htInsert(&ht, string, _SL("origin"), vec3, v);
/// @endcode
///
/// `stType(vec3)` resolves to `&_sti_vec3` — a static, canonical pointer with no
/// `STypeFlag_Temporary` set, so no type registry lookup occurs at init or push time.

/// stDeclare(name)
///
/// Declare a custom type descriptor defined in another translation unit.
///
/// Places `extern const STypeInfo _sti_##name` in the current scope.
/// Typically used in a header file alongside a `#define stType_name (&_sti_name)`
/// so that `stType(name)` resolves cleanly.
///
/// @param name Type name (token)
#define stDeclare(name) extern const STypeInfo _sti_##name

/// stDefine(name)
///
/// Begin the definition of a custom type descriptor.
///
/// Must be followed immediately by a brace-enclosed `STypeInfo` struct initializer.
/// Place at file scope in a `.c` file; use `static` storage class for file-local types.
///
/// @param name Type name (token)
///
/// Example:
/// @code
///   stDefine(vec3) {
///       .id    = STypeId_opaque,
///       .size  = sizeof(Vec3),
///       .flags = stFlag(PassPtr),
///       .name  = "Vec3",
///       .ops   = { .cmp = vec3Cmp, .hash = vec3Hash },
///   };
/// @endcode
#define stDefine(name) const STypeInfo _sti_##name =

/// @}  // end of stype_custom group

/// @}  // end of stype_api group

/// @}  // end of stype group

CX_C_END

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <cx/debug/assert.h>
#include <cx/core/cpp.h>
#include <cx/utils/macros.h>

// extra files that can be included for specific functions
#define STYPE_FOREACH_ALL <cx/core/alltypes.inc>

CX_C_BEGIN

// IMPORTANT NOTE!
// Always initialize string to NULL or 0 first!
typedef struct str_impl* string;
typedef struct hashtable_impl* hashtable;
typedef struct SUID SUID;
typedef struct stvar stvar;

enum STYPE_CLASS_ID {
    STCLASS_OPAQUE = 0x00,
    STCLASS_INT = 0x10,
    STCLASS_UINT = 0x20,
    STCLASS_FLOAT = 0x30,
    STCLASS_PTR = 0x40,
    STCLASS_CX = 0xe0,
    STCLASS_CX_CONTAINER = 0xf0,
};

#define STYPE_CLASS_MASK 0xf0
#define STYPE_CLASS(v) ((v) & STYPE_CLASS_MASK)

typedef char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;
typedef intptr_t intptr;
typedef uintptr_t uintptr;

typedef float float32;
typedef double float64;

typedef uint32 stype;

// This is the type that is used for passing as a parameter by-value, containers, and
// variants. Should be no larger than 64 bits wide, but can be smaller.
#define SType_opaque void*
#define SType_int8 int8
#define SType_int16 int16
#define SType_int32 int32
#define SType_int64 int64
#define SType_uint8 uint8
#define SType_uint16 uint16
#define SType_uint32 uint32
#define SType_uint64 uint64
#define SType_intptr intptr
#define SType_uintptr uintptr
#define SType_size size_t
#define SType_float32 float32
#define SType_float64 float64
#define SType_ptr void*
#define SType_string string
#define SType_object void*
#define SType_suid SUID*
#define SType_stvar stvar*
#define SType_sarray void*
#define SType_hashtable hashtable
#define stTypeDef(name) SType_##name

#define stTypeCast(name, v) ((SType_##name)(v))
#define stPtrCast(name, v) ((SType_##name*)(v))

// container that can be aliased for any type
#define CONTAINER_TYPE(type) stTypeDef(type) st_##type
typedef union stgeneric_u {
    uint64 st_generic;
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
    CONTAINER_TYPE(size);
    CONTAINER_TYPE(float32);
    CONTAINER_TYPE(float64);
    CONTAINER_TYPE(ptr);
    CONTAINER_TYPE(string);
    CONTAINER_TYPE(object);
    CONTAINER_TYPE(suid);
    CONTAINER_TYPE(stvar);
    CONTAINER_TYPE(sarray);
    CONTAINER_TYPE(hashtable);
} stgeneric_u;

// Some compilers *cough* were generating very suboptimal code for copying the union,
// like "rep movsb" with ecx=4 and all associated register setup at the _saPush call
// site. So we have to treat it as a plain integer and do a pointer alias dance with
// macros, which is a bit less convenient but does the right thing on all supported
// compilers.

typedef uint64 stgeneric;

_Static_assert(sizeof(stgeneric_u) == sizeof(stgeneric), "stype container too large");

#ifndef __cplusplus
#define stgeneric(type, val) (((stgeneric_u){ .st_##type = stCheck(type, val) }).st_generic)
#else
#define stgeneric(type, val) ((stgeneric)stCheck(type, val))
#endif
#define stGenVal(type, gen) (((stgeneric_u*)(&(gen)))->st_##type)

// Compact variant structure. This is most often used for passing arrays of values that
// the type is not known at compile time, as part of the type-safe varargs replacement
// mechanism.

typedef struct stvar {
    stgeneric data;
    stype type;
} stvar;

// Note that this uses a temporary, so the variant's lifetime is equal to the function
// that it was created in. It should be used for passing varargs by-value.
#ifndef __cplusplus
#define stvar(typen, val) ((stvar){ .data = stArg(typen, val), .type = stType(typen) })
#else
_meta_inline stvar _stvar(stype st, stgeneric val) {
    stvar ret;
    ret.data = val;
    ret.type = st;
    return ret;
}
#define stvar(typen, val) _stvar(stType(typen), stArg(typen, val))
#endif

// The type that's actually used for storage in containers, etc.
#define STStorageType_opaque void
#define STStorageType_int8 int8
#define STStorageType_int16 int16
#define STStorageType_int32 int32
#define STStorageType_int64 int64
#define STStorageType_uint8 uint8
#define STStorageType_uint16 uint16
#define STStorageType_uint32 uint32
#define STStorageType_uint64 uint64
#define STStorageType_intptr intptr
#define STStorageType_uintptr uintptr
#define STStorageType_size size_t
#define STStorageType_float32 float32
#define STStorageType_float64 float64
#define STStorageType_ptr void*
#define STStorageType_string string
#define STStorageType_object void*
#define STStorageType_suid SUID
#define STStorageType_stvar stvar
#define STStorageType_sarray void*
#define STStorageType_hashtable hashtable
#define stStorageType(name) STStorageType_##name

enum STYPE_ID {
    // opaque is a magic catch-all type for custom structures and such
    STypeId_opaque = STCLASS_OPAQUE,
    // generic scalar types
    STypeId_int8 = STCLASS_INT | 1,
    STypeId_int16 = STCLASS_INT | 2,
    STypeId_int32 = STCLASS_INT | 4,
    STypeId_int64 = STCLASS_INT | 8,
    STypeId_intptr = STCLASS_INT | sizeof(intptr),  // alias for one of the other int types
    STypeId_uint8 = STCLASS_UINT | 1,
    STypeId_uint16 = STCLASS_UINT | 2,
    STypeId_uint32 = STCLASS_UINT | 4,
    STypeId_uint64 = STCLASS_UINT | 8,
    STypeId_uintptr = STCLASS_UINT | sizeof(intptr),
    STypeId_size = STCLASS_UINT | sizeof(size_t),   // alias for one of the uint types
    STypeId_float32 = STCLASS_FLOAT | 4,
    STypeId_float64 = STCLASS_FLOAT | 8,
    // ptr is stored similar to (u)intptr, but automatically dereferenced in ops functions
    STypeId_ptr = STCLASS_PTR | sizeof(void*),
    // most of the CX class are "object-like" types and use pointers as handles
    STypeId_string = STCLASS_CX | 0,
    STypeId_object = STCLASS_CX | 1,
    STypeId_suid = STCLASS_CX | 2,        // notable exception
    STypeId_stvar = STCLASS_CX | 3,
    STypeId_sarray = STCLASS_CX_CONTAINER | 0,
    STypeId_hashtable = STCLASS_CX_CONTAINER | 1,
};
#define stTypeId(name) STypeId_##name

// The actual storage size of the type
enum STYPE_SIZE {
    // opaque is not really size 0, but filled by by macros later
    STypeSize_opaque = 0,
    STypeSize_int8 = sizeof(int8),
    STypeSize_int16 = sizeof(int16),
    STypeSize_int32 = sizeof(int32),
    STypeSize_int64 = sizeof(int64),
    STypeSize_intptr = sizeof(intptr),
    STypeSize_uint8 = sizeof(int8),
    STypeSize_uint16 = sizeof(int16),
    STypeSize_uint32 = sizeof(int32),
    STypeSize_uint64 = sizeof(int64),
    STypeSize_uintptr = sizeof(uintptr),
    STypeSize_size = sizeof(size_t),
    STypeSize_float32 = sizeof(float32),
    STypeSize_float64 = sizeof(float64),
    STypeSize_ptr = sizeof(void*),
    STypeSize_string = sizeof(void*),
    STypeSize_object = sizeof(void*),
    // SUID is special because it's always passed by reference, but stored as the full 16 bytes
    STypeSize_suid = 16,
    STypeSize_stvar = sizeof(stvar),
    STypeSize_sarray = sizeof(void*),
    STypeSize_hashtable = sizeof(void*),
};
#define stTypeSize(name) STypeSize_##name

enum STYPE_FLAGS {
    STypeFlag_Object =   (1 << 0),      // "object-like" type -- pointer to managed object
    STypeFlag_Custom =   (1 << 1),      // type uses custom ops
    STypeFlag_PassPtr =  (1 << 2),      // type is passed by pointer rather than by value,
                                        // does not apply to 'handle' style objects
};
#define stFlag(name) STypeFlag_##name
#define stHasFlag(st, fname) ((st >> 8) & stFlag(fname))

/*typedef union stype {
    uint32 spec;
    struct {
        uint8 id;
        uint8 flags;
        uint16 size;
    };
} stype; */
#define stGetId(st) (st & 0xff)
#define stGetFlags(st) ((st >> 8) & 0xff)
#define stGetSize(st) (st >> 16)

#define _stype_mktype(tid, tflags, tsz) ((tid & 0xff) | (tflags & 0xff) << 8 | (tsz & 0xffff) << 16)

// ignore custom flag when comparing types
_meta_inline bool stEq(stype sta, stype stb) {
    return (sta & ~_stype_mktype(0, STypeFlag_Custom, 0)) == (stb & ~_stype_mktype(0, STypeFlag_Custom, 0));
}

enum STYPE_DEFAULT_FLAGS {
    STypeFlags_opaque = stFlag(PassPtr),
    STypeFlags_int8 = 0,
    STypeFlags_int16 = 0,
    STypeFlags_int32 = 0,
    STypeFlags_int64 = 0,
    STypeFlags_intptr = 0,
    STypeFlags_uint8 = 0,
    STypeFlags_uint16 = 0,
    STypeFlags_uint32 = 0,
    STypeFlags_uint64 = 0,
    STypeFlags_uintptr = 0,
    STypeFlags_size = 0,
    STypeFlags_float32 = 0,
    STypeFlags_float64 = 0,
    STypeFlags_ptr = 0,
    STypeFlags_string = stFlag(Object),
    STypeFlags_object = stFlag(Object),
    STypeFlags_suid = stFlag(PassPtr),
    STypeFlags_stvar = stFlag(PassPtr) | stFlag(Object),
    STypeFlags_sarray = stFlag(Object),
    STypeFlags_hashtable = stFlag(Object),
};
#define stTypeFlags(name) STypeFlags_##name

#define stTypeInternal(name) _stype_mktype(stTypeId(name), stTypeFlags(name), stTypeSize(name))
#define stFullTypeInternal(name) stTypeInternal(name), 0
_meta_inline stype _stype_mkcustom(stype base)
{
    base |= _stype_mktype(0, stFlag(Custom), 0);
    return base;
}

// Static type checks
// These get completely optimized away by any sane compiler
#define stCheck(type, val) ((stTypeDef(type))(val), val)
#define stCheckPtr(type, ptr) ((stTypeDef(type)*)(ptr), ptr)

// C99 compound literals are lvalues and can force the compiler to create a temporary
// on the stack if necessary, so that we can pass pointers to arbitrary expressions
#define stRvalAddr(type, rval) ((stStorageType(type)[1]){rval})

// MEGA PREPROCESSOR HACKS INCOMING
// this enables the use of opaque(realtype) as type name in functions like saCreate
#define stType_opaque(realtype) _stype_mktype(stTypeId(opaque), stTypeFlags(opaque), (uint16)sizeof(realtype))
#define stType_int8 stTypeInternal(int8)
#define stType_int16 stTypeInternal(int16)
#define stType_int32 stTypeInternal(int32)
#define stType_int64 stTypeInternal(int64)
#define stType_intptr stTypeInternal(intptr)
#define stType_uint8 stTypeInternal(uint8)
#define stType_uint16 stTypeInternal(uint16)
#define stType_uint32 stTypeInternal(uint32)
#define stType_uint64 stTypeInternal(uint64)
#define stType_uintptr stTypeInternal(uintptr)
#define stType_size stTypeInternal(size)
#define stType_float32 stTypeInternal(float32)
#define stType_float64 stTypeInternal(float64)
#define stType_ptr stTypeInternal(ptr)
#define stType_string stTypeInternal(string)
#define stType_object stTypeInternal(object)
#define stType_suid stTypeInternal(suid)
#define stType_stvar stTypeInternal(stvar)
#define stType_sarray stTypeInternal(sarray)
#define stType_hashtable stTypeInternal(hashtable)
#define stType_custom(basetype) _stype_mkcustom(stType_##basetype)
#define stType(name) stType_##name

// and the hack for custom(basetype, ops)
#define stFullType_opaque(realtype) _stype_mktype(stTypeId(opaque), stTypeFlags(opaque), (uint16)sizeof(realtype)), 0
#define stFullType_int8 stFullTypeInternal(int8)
#define stFullType_int16 stFullTypeInternal(int16)
#define stFullType_int32 stFullTypeInternal(int32)
#define stFullType_int64 stFullTypeInternal(int64)
#define stFullType_intptr stFullTypeInternal(intptr)
#define stFullType_uint8 stFullTypeInternal(uint8)
#define stFullType_uint16 stFullTypeInternal(uint16)
#define stFullType_uint32 stFullTypeInternal(uint32)
#define stFullType_uint64 stFullTypeInternal(uint64)
#define stFullType_uintptr stFullTypeInternal(uintptr)
#define stFullType_size stFullTypeInternal(size)
#define stFullType_float32 stFullTypeInternal(float32)
#define stFullType_float64 stFullTypeInternal(float64)
#define stFullType_ptr stFullTypeInternal(ptr)
#define stFullType_string stFullTypeInternal(string)
#define stFullType_object stFullTypeInternal(object)
#define stFullType_suid stFullTypeInternal(suid)
#define stFullType_stvar stFullTypeInternal(stvar)
#define stFullType_sarray stFullTypeInternal(sarray)
#define stFullType_hashtable stFullTypeInternal(hashtable)
// this will chain evaluate macros for stuff like custom(opaque(realtype), ops)
// it gets token pasted as _stype_mkcustom(stType_opaque(realtype), ops)
#define stFullType_custom(basetype, ops) _stype_mkcustom(stType_##basetype), (&ops)
#define stFullType(name) stFullType_##name

// Macros for passing arguments by value

// opqaue is a special case that must always be passed by pointer, and
// the caller must supply an lvalue
#define STypeArg_opaque(type, val) stgeneric(type, &(val))
// but everything else gets put into a container
#define STypeArg_int8(type, val) stgeneric(type, val)
#define STypeArg_int16(type, val) stgeneric(type, val)
#define STypeArg_int32(type, val) stgeneric(type, val)
#define STypeArg_int64(type, val) stgeneric(type, val)
#define STypeArg_intptr(type, val) stgeneric(type, val)
#define STypeArg_uint8(type, val) stgeneric(type, val)
#define STypeArg_uint16(type, val) stgeneric(type, val)
#define STypeArg_uint32(type, val) stgeneric(type, val)
#define STypeArg_uint64(type, val) stgeneric(type, val)
#define STypeArg_uintptr(type, val) stgeneric(type, val)
#define STypeArg_size(type, val) stgeneric(type, val)
#define STypeArg_float32(type, val) stgeneric(type, val)
#define STypeArg_float64(type, val) stgeneric(type, val)
#define STypeArg_ptr(type, val) stgeneric(type, val)
#define STypeArg_string(type, val) stgeneric(type, val)
#define STypeArg_object(type, val) stgeneric(type, val)
// SUID and stvar are too big, so make a copy and pass a pointer
#define STypeArg_suid(type, val) stgeneric(type, stRvalAddr(type, val))
#define STypeArg_stvar(type, val) stgeneric(type, stRvalAddr(type, val))
#define STypeArg_sarray(type, val) stgeneric(type, val)
#define STypeArg_hashtable(type, val) stgeneric(type, val)
#define stArg(type, val) STypeArg_##type(type, val)

// And for passing a pointer-to-pointer, mostly for functions that want to
// consume or reallocate the object

// opaque is already passed in as a pointer
#define STypeArgPtr_opaque(type, val) &stgeneric(type, val)
#define STypeArgPtr_int8(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int16(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int32(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_int64(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_intptr(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint8(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint16(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint32(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uint64(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_uintptr(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_size(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_float32(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_float64(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_ptr(type, val) (stgeneric*)stCheckPtr(type, (void**)(val))
#define STypeArgPtr_string(type, val) (stgeneric*)stCheckPtr(type, val)
#define STypeArgPtr_object(type, val) (stgeneric*)stCheckPtr(type, (void**)val)
// same for the other pass-by-pointer cases (SUID, stvar)
#define STypeArgPtr_suid(type, val) &stgeneric(type, val)
#define STypeArgPtr_stvar(type, val) &stgeneric(type, val)
#define STypeArgPtr_sarray(type, val) (stgeneric*)stCheckPtr(type, (void**)(val))
#define STypeArgPtr_hashtable(type, val) (stgeneric*)stCheckPtr(type, val)
#define stArgPtr(type, val) STypeArgPtr_##type(type, val)

// Macros for type-checked inline metafunctions.
// These expand to a pair of parameters for type, followed by a pointer.

#define STypeChecked_opaque(type, val) stType_opaque(val), stArg(type, val)
#define STypeChecked_int8(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_int16(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_int32(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_int64(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_intptr(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_uint8(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_uint16(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_uint32(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_uint64(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_uintptr(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_size(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_float32(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_float64(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_ptr(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_string(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_object(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_suid(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_stvar(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_sarray(type, val) stTypeInternal(type), stArg(type, val)
#define STypeChecked_hashtable(type, val) stTypeInternal(type), stArg(type, val)
#define stChecked(type, val) STypeChecked_##type(type, val)

// Type checking of pointers to types, mostly for functions that want to
// consume object-like variables and destroy them

// go the opposite direction for opaque since it's already passed in as a pointer
#define STypeCheckedPtr_opaque(type, val) stType_opaque(*val), stArgPtr(type, val)
#define STypeCheckedPtr_int8(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_int16(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_int32(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_int64(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_intptr(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_uint8(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_uint16(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_uint32(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_uint64(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_uintptr(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_size(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_float32(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_float64(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_ptr(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_string(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_object(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_suid(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_stvar(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_sarray(type, val) stTypeInternal(type), stArgPtr(type, val)
#define STypeCheckedPtr_hashtable(type, val) stTypeInternal(type), stArgPtr(type, val)
#define stCheckedPtr(type, val) STypeCheckedPtr_##type(type, val)

enum STYPE_OPS_FLAGS {
    STOPS_ = 0,

    // Valid for: cmp, hash
    // Perform a case-insensitive hash or compare operation if the
    // underlying type supports it
    STOPS_CaseInsensitive = 0x00000001,
};

typedef void (*stDtorFunc)(stype st, stgeneric*, uint32 flags);
typedef intptr (*stCmpFunc)(stype st, stgeneric, stgeneric, uint32 flags);
typedef uint32 (*stHashFunc)(stype st, stgeneric, uint32 flags);
typedef void (*stCopyFunc)(stype st, stgeneric*, stgeneric, uint32 flags);

extern stDtorFunc _stDefaultDtor[256];
extern stCmpFunc _stDefaultCmp[256];
extern stHashFunc _stDefaultHash[256];
extern stCopyFunc _stDefaultCopy[256];

typedef struct STypeOps {
    stDtorFunc dtor;
    stCmpFunc cmp;
    stHashFunc hash;
    stCopyFunc copy;
} STypeOps;

#define stStored(st, storage) (stHasFlag(st, PassPtr) ? stgeneric(ptr, (void*)(storage)) : *(stgeneric*)((void*)(storage)))

#define stStoredPtr(st, storage) (stHasFlag(st, PassPtr) ? &stgeneric(ptr, ((void*)(storage))) : (stgeneric*)((void*)(storage)))

#define stGenPtr(st, gen) (stHasFlag(st, PassPtr) ? stGenVal(ptr, gen) : &(gen))

// inlining these lets most of it get optimized out and specialized if the type is known at compile-time

_meta_inline void _stDestroy(stype st, STypeOps *ops, stgeneric *stgen, uint32 flags)
{
    // ops is mandatory for custom type
    devAssert(!stHasFlag(st, Custom) || ops);

    if (ops && ops->dtor)
        ops->dtor(st, stgen, flags);
    else if (_stDefaultDtor[stGetId(st)])
        _stDefaultDtor[stGetId(st)](st, stgen, flags);
}
#define stDestroy(type, pobj, ...) _stDestroy(stFullType(type), stArgPtr(type, pobj), func_flags(STOPS, __VA_ARGS__))

_meta_inline intptr _stCmp(stype st, STypeOps *ops, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    // ops is mandatory for custom type
    devAssert(!stHasFlag(st, Custom) || ops);

    if (ops && ops->cmp)
        return ops->cmp(st, stgen1, stgen2, flags);

    if (_stDefaultCmp[stGetId(st)])
        return _stDefaultCmp[stGetId(st)](st, stgen1, stgen2, flags);

    if (!stHasFlag(st, PassPtr))
        return memcmp(&stgen1, &stgen2, stGetSize(st));
    else
        return memcmp(stGenVal(ptr, stgen1), stGenVal(ptr, stgen2), stGetSize(st));
}
#define stCmp(type, obj1, obj2, ...) _stCmp(stFullType(type), stArg(type, obj1), stArg(type, obj2), func_flags(STOPS, __VA_ARGS__))

_meta_inline void _stCopy(stype st, STypeOps *ops, stgeneric *dest, stgeneric src, uint32 flags)
{
    // ops is mandatory for custom type
    devAssert(!stHasFlag(st, Custom) || ops);

    if (ops && ops->copy)
        ops->copy(st, dest, src, flags);
    else if (_stDefaultCopy[stGetId(st)])
        _stDefaultCopy[stGetId(st)](st, dest, src, flags);
    else if (!stHasFlag(st, PassPtr))
        memcpy(dest, &src, stGetSize(st));
    else
        memcpy(stGenVal(ptr, *dest), stGenVal(ptr, src), stGetSize(st));
}
#define stCopy(type, pdest, src, ...) _stCopy(stFullType(type), stArgPtr(type, pdest), stArg(type, src), func_flags(STOPS, __VA_ARGS__))

uint32 stHash_gen(stype st, stgeneric stgen, uint32 flags);
_meta_inline uint32 _stHash(stype st, STypeOps *ops, stgeneric stgen, uint32 flags)
{
    // ops is mandatory for custom type
    devAssert(!stHasFlag(st, Custom) || ops);

    if (ops && ops->hash)
        return ops->hash(st, stgen, flags);
    else if (_stDefaultHash[stGetId(st)])
        return _stDefaultHash[stGetId(st)](st, stgen, flags);
    else
        return stHash_gen(st, stgen, flags);
}
#define stHash(type, obj, ...) _stHash(stFullType(type), stArg(type, obj), func_flags(STOPS, __VA_ARGS__))

CX_C_END
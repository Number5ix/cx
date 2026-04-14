#include "cx/cx.h"
#include "cx/utils/murmur.h"

#include "stconvert.h"

#include "cx/buffer/stype_buffer.h"
#include "cx/closure/stype_closure.h"
#include "cx/container/stype_hashtable.h"
#include "cx/container/stype_sarray.h"
#include "cx/obj/stype_obj.h"
#include "cx/string/stype_string.h"
#include "cx/struct/stype_struct.h"
#include "cx/stype/stype_stvar.h"
#include "cx/suid/stype_suid.h"

#include "cx/debug/dbgtypes.h"

// extern inline stype _stype_mktype(uint8 id, uint8 flags, uint16 sz);
// extern inline void stDestroy(stype st, void *ptr, STypeOps *ops);
// extern inline intptr stCmp(stype st, const void *ptr1, const void *ptr2, STypeOps *ops);
// extern inline void stCopy(stype st, void *dest, const void *src, STypeOps *ops);

#define STCMP_GEN(type)                                                                \
    static intptr stCmp_##type(stype st, stgeneric gen1, stgeneric gen2, uint32 flags) \
    {                                                                                  \
        type tmp = gen1.st_##type - gen2.st_##type;                                    \
        return (intptr)tmp;                                                            \
    }
#define STCMP_GEN_OVR(type, ovrtype)                                                   \
    static intptr stCmp_##type(stype st, stgeneric gen1, stgeneric gen2, uint32 flags) \
    {                                                                                  \
        return (intptr)((ovrtype)gen1.st_##type - (ovrtype)gen2.st_##type);            \
    }

STCMP_GEN(int8)
STCMP_GEN(int16)
STCMP_GEN(int32)
STCMP_GEN(uint8)
STCMP_GEN(uint16)
STCMP_GEN(uint32)
#ifdef _64BIT
STCMP_GEN(int64)
STCMP_GEN(uint64)
#else
static intptr stCmp_int64(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    if (gen1.st_int64 == gen2.st_int64)
        return 0;
    return (gen1.st_int64 < gen2.st_int64) ? -1 : 1;
}

static intptr stCmp_uint64(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    if (gen1.st_uint64 == gen2.st_uint64)
        return 0;
    return (gen1.st_uint64 < gen2.st_uint64) ? -1 : 1;
}
#endif

static intptr stCmp_bool(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    if (gen1.st_bool == gen2.st_bool)
        return 0;
    return gen1.st_bool ? 1 : -1;
}

// 'equal enough' values good enough for general use while taking into account small
// amounts of floating-point error.
//
// Notably this does NOT handle the problem of subtracting numbers that are very close
// together, perhaps only 1 representable float apart, and ending up with a value that
// is near zero but also a long way away in floating point space due to having more
// precision closer to zero.
//
// stCmp is mostly used for things like sorting lists or detecting changes, so this is
// good enough to reach a stable result.
//
// Specialized applications will need to decide their own tolerance for comparison

// Implementation note: Checks for equality early to handle NaN and Inf, then uses
// integer representation which in IEEE-754 helpfully means that representable floats
// right next to each other in FP space are also adjacent in integer space because
// the mantissa comes last.

#define STCMP_FLOAT_ULP_EPSILON (2)

#define STCMP_FLOAT(type, itype)                                                       \
    static intptr stCmp_##type(stype st, stgeneric gen1, stgeneric gen2, uint32 flags) \
    {                                                                                  \
        if (gen1.st_##type == gen2.st_##type)                                          \
            return 0;                                                                  \
        itype idiff = gen1.st_##itype - gen2.st_##itype;                               \
        if (idiff <= STCMP_FLOAT_ULP_EPSILON && idiff >= -STCMP_FLOAT_ULP_EPSILON)     \
            return 0;                                                                  \
        return (gen1.st_##type < gen2.st_##type) ? -1 : 1;                             \
    }
STCMP_FLOAT(float32, int32)
STCMP_FLOAT(float64, int64)

// compiler is stupid about void* comparisons
STCMP_GEN_OVR(ptr, char*)

uint32 stHash_gen(stype st, _In_ stgeneric gen, flags_t flags)
{
    if (!stHasFlag(st, PassPtr))
        return hashMurmur3((uint8*)&gen, stGetSize(st));
    else
        return hashMurmur3(gen.st_ptr, stGetSize(st));
}

static intptr stCmp_none(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    // none is never equal to anything, even itself
    return 1;
}

static uint32 stHash_none(stype st, stgeneric gen, uint32 flags)
{
    return 0;
}

static void stCopy_none(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                        flags_t flags)
{
    // Ensure that the debug types are always available.
    // This spot was chosen because it's in a function that is virtually impossible to be omitted
    // during linking.
    relAssertMsg(_unused_debug_types._unused == 0, "Memory corruption");
}

// Canonical STypeInfo structures for built-in types

const STypeInfo _sti_none = {
    .name  = "none",
    .id    = STypeId_none,
    .flags = 0,
    .size  = 0,
    .ops   = { .cmp     = stCmp_none,
              .hash    = stHash_none,
              .copy    = stCopy_none,
              .convert = stConvert_none }
};

#define ST_BASIC_INFO(type, cvttype) \
    const STypeInfo _sti_##type = { \
    .name  = #type, \
    .id    = STypeId_##type, \
    .flags = 0, \
    .size  = sizeof(STStorageType_##type), \
    .ops = { \
        .cmp = stCmp_##type, \
        .convert = stConvert_##cvttype, \
    }, \
}

ST_BASIC_INFO(int8, int);
ST_BASIC_INFO(int16, int);
ST_BASIC_INFO(int32, int);
ST_BASIC_INFO(int64, int);
ST_BASIC_INFO(uint8, int);
ST_BASIC_INFO(uint16, int);
ST_BASIC_INFO(uint32, int);
ST_BASIC_INFO(uint64, int);
ST_BASIC_INFO(float32, float32);
ST_BASIC_INFO(float64, float64);
ST_BASIC_INFO(bool, bool);
ST_BASIC_INFO(ptr, ptr);

const STypeInfo _sti_string = {
    .name  = "string",
    .id    = STypeId_string,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(string)),
    .ops   = { .dtor    = stDtor_string,
              .cmp     = stCmp_string,
              .hash    = stHash_string,
              .copy    = stCopy_string,
              .convert = stConvert_string }
};

const STypeInfo _sti_object = {
    .name  = "object",
    .id    = STypeId_object,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(object)),
    .ops   = { .dtor    = stDtor_obj,
              .cmp     = stCmp_obj,
              .hash    = stHash_obj,
              .copy    = stCopy_obj,
              .convert = stConvert_obj }
};

const STypeInfo _sti_weakref = {
    .name  = "weakref",
    .id    = STypeId_weakref,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(weakref)),
    .ops   = { .dtor = stDtor_weakref, .cmp = stCmp_weakref, .copy = stCopy_weakref }
};

const STypeInfo _sti_suid = {
    .name  = "suid",
    .id    = STypeId_suid,
    .flags = stFlag(PassPtr),
    .size  = sizeof(stStorageType(suid)),
    .ops   = { .cmp = stCmp_suid, .convert = stConvert_suid }
};

const STypeInfo _sti_stvar = {
    .name  = "stvar",
    .id    = STypeId_stvar,
    .flags = stFlag(PassPtr) | stFlag(Object),
    .size  = sizeof(stStorageType(stvar)),
    .ops   = { .dtor    = stDtor_stvar,
              .cmp     = stCmp_stvar,
              .hash    = stHash_stvar,
              .copy    = stCopy_stvar,
              .convert = stConvert_stvar }
};

const STypeInfo _sti_closure = {
    .name  = "closure",
    .id    = STypeId_closure,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(closure)),
    .ops   = { .dtor = stDtor_closure, .cmp = stCmp_closure, .copy = stCopy_closure }
};

const STypeInfo _sti_buffer = {
    .name  = "buffer",
    .id    = STypeId_buffer,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(buffer)),
    .ops   = { .dtor = stDtor_buffer,
              .cmp  = stCmp_buffer,
              .hash = stHash_buffer,
              .copy = stCopy_buffer }
};

// struct is intentionally omitted. It's not legal to create a bare structure directly. cxautogen
// automatically creates canonical types for each defined structure that use the struct type ID

const STypeInfo _sti_structp = {
    .name  = "structp",
    .id    = STypeId_structp,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(structp)),
    .ops   = { .dtor = stDtor_structp,
              .cmp  = stCmp_structp,
              .hash = stHash_structp,
              .copy = stCopy_structp }
};

// non-parameterized version of containers
const STypeInfo _sti_sarray = {
    .name  = "sarray",
    .id    = STypeId_sarray,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(sarray)),
    .ops   = { .cmp = stCmp_sarray, .hash = stHash_sarray, .copy = stCopy_sarray }
};

const STypeInfo _sti_hashtable = {
    .name  = "hashtable",
    .id    = STypeId_hashtable,
    .flags = stFlag(Object),
    .size  = sizeof(stStorageType(hashtable)),
    .ops   = { .cmp = stCmp_hashtable, .hash = stHash_hashtable, .copy = stCopy_hashtable }
};

// As their size is dynamic, opaque and struct must be constructed as a temporary (or statically by
// cxautogen in the struct case). Instead we provide default STypeOps here for them to copy.

const STypeOps _stops_opaque = { 0 };

const STypeOps _stops_struct = { .dtor = stDtor_struct,
                                 .cmp  = stCmp_struct,
                                 .hash = stHash_struct,
                                 .copy = stCopy_struct };

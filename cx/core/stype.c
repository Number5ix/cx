#include "cx/cx.h"
#include "cx/utils/murmur.h"

#include "cx/core/stype_obj.h"
#include "cx/container/stype_sarray.h"
#include "cx/string/stype_string.h"
#include "cx/container/stype_hashtable.h"
#include "cx/core/stype_stvar.h"
#include "cx/core/stype_suid.h"

// extern inline stype _stype_mktype(uint8 id, uint8 flags, uint16 sz);
// extern inline void stDestroy(stype st, void *ptr, STypeOps *ops);
// extern inline intptr stCmp(stype st, const void *ptr1, const void *ptr2, STypeOps *ops);
// extern inline void stCopy(stype st, void *dest, const void *src, STypeOps *ops);

static void stDtor_ptr(stype st, stgeneric *stgen, uint32 flags)
{
    xaSFree(stGenVal(ptr, *stgen));
}

#define STCMP_GEN(type) \
static intptr stCmp_##type(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags) \
{ \
    return (intptr)(stGenVal(type, stgen1) - stGenVal(type, stgen2)); \
}
#define STCMP_GEN_OVR(type, ovrtype) \
static intptr stCmp_##type(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags) \
{ \
    return (intptr)((ovrtype)stGenVal(type, stgen1) - (ovrtype)stGenVal(type, stgen2)); \
}

STCMP_GEN(int8)
STCMP_GEN(int16)
STCMP_GEN(int32)
STCMP_GEN(int64)
STCMP_GEN(uint8)
STCMP_GEN(uint16)
STCMP_GEN(uint32)
STCMP_GEN(uint64)
STCMP_GEN(float32)
STCMP_GEN(float64)
// compiler is stupid about void* comparisons
STCMP_GEN_OVR(ptr, char*)
STCMP_GEN_OVR(sarray, char*)
STCMP_GEN_OVR(hashtable, char*)

uint32 stHash_gen(stype st, stgeneric stgen, uint32 flags)
{
    if (!stHasFlag(st, PassPtr))
        return hashMurmur3((uint8*)&stgen, stGetSize(st));
    else
        return hashMurmur3(stGenVal(ptr, stgen), stGetSize(st));
}

alignMem(64) stDtorFunc _stDefaultDtor[256] = {
    // STCLASS_OPAQUE
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_INT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_UINT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_FLOAT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_PTR
    stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr,
    stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr, stDtor_ptr,
    // 0x50 - 0xdf
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX
    stDtor_string, stDtor_obj, 0, stDtor_stvar, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX_CONTAINER
    stDtor_sarray, stDtor_hashtable, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

alignMem(64) stCmpFunc _stDefaultCmp[256] = {
    // STCLASS_OPAQUE
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_INT
    0, stCmp_int8, stCmp_int16, 0, stCmp_int32, 0, 0, 0, stCmp_int64, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_UINT
    0, stCmp_uint8, stCmp_uint16, 0, stCmp_uint32, 0, 0, 0, stCmp_uint64, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_FLOAT
    0, 0, 0, 0, stCmp_float32, 0, 0, 0, stCmp_float64, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_PTR
    0, 0, 0, 0, sizeof(void*) == sizeof(int32) ? stCmp_ptr : 0, 0, 0, 0,
    sizeof(void*) == sizeof(int64) ? stCmp_ptr : 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x50 - 0xdf
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX
    stCmp_string, stCmp_obj, stCmp_suid, stCmp_stvar, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX_CONTAINER
    stCmp_sarray, stCmp_hashtable, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

alignMem(64) stHashFunc _stDefaultHash[256] = {
    // STCLASS_OPAQUE
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_INT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_UINT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_FLOAT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_PTR
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x50 - 0xdf
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX
    stHash_string, stHash_obj, 0, stHash_stvar, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX_CONTAINER
    stHash_sarray, stHash_hashtable, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

alignMem(64) stCopyFunc _stDefaultCopy[256] = {
    // STCLASS_OPAQUE
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_INT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_UINT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_FLOAT
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_PTR
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x50 - 0xdf
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX
    stCopy_string, stCopy_obj, 0, stCopy_stvar, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // STCLASS_CX_CONTAINER
    stCopy_sarray, stCopy_hashtable, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

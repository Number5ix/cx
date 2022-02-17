#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>

#define sarrayref(typ) sa_##typ
#define sarrayhdl(typ) sa_##typ*

// for creating a named sarray type that can be passed between functions
#define saDeclareType(name, typ) typedef union sa_##name { \
    void *_is_sarray; \
    void *_is_sarray_##name; \
    typ *a; \
} sa_##name
#define saDeclare(name) saDeclareType(name, name)
#define saDeclarePtr(name) saDeclareType(name, name*)
#define saInitNone { .a = 0 }

// sarray type declarations
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

typedef struct SArrayHeader {
    // sarray extended header begins here (only valid if SAINT_Extended is set)
    STypeOps typeops;
    // sarray header begins here
    stype elemtype;
    int32 count;
    int32 capacity;
    uint32 flags;     // high 8 bits = growth
    void *data[1];
} SArrayHeader;

enum SARRAY_CREATE_FLAGS_ENUM
{
    SA_             = 0x0000,   // for the variable function flags macro
    SA_Ref          = 0x0010,   // the array references other data and does not copy/own/destroy it.
                                // only has an effect on pointer or object-type arrays
    SA_Sorted       = 0x0020,   // array is maintined in sorted order
                                // O(log n) search and O(n) insert
    SA_AutoShrink   = 0x0040,   // release memory when number of array elements shrinks

    // internal use only, do not set manually
    SAINT_Extended  = 0x8000,   // includes extended header
};

enum SARRAY_GROW_ENUM {
    // automatically select based on element size
    SA_GROW_Auto,

    // dynamic growth    // 100% - 50% - 25%
    SA_GROW_Normal,      //  16  - 128
    SA_GROW_Aggressive,  //  32  - 256
    SA_GROW_Slow,        //   8  -  64

    // fixed growth
    SA_GROW_100,         // always grow by 100%
    SA_GROW_50,          // 50%
    SA_GROW_25,          // 25%

    // always grow to exact size needed, never allocate extra
    SA_GROW_Minimal,
};

enum SARRAY_FUNC_FLAGS_ENUM {
    SAFUNC_             = 0,

    // Valid for: saPush, saMerge
    // Does not insert if a matching element already exists.
    SAFUNC_Unique       = 0x00010000,

    // Valid for: saExtract, saRemove, saFindRemove
    // When removing/destroying an element, swaps with last element,
    // disrupting the order but removing the element much faster.
    // Not valid for sorted arrays.
    SAFUNC_Fast         = 0x00020000,

    // Valid for: saFind
    // Only valid for sorted arrays, setting this flag will return
    // the index where the element would be inserted if there is no
    // exact match.
    SAFUNC_Inexact      = 0x00100000,

    // ----- INTERNAL USE ONLY -----
    // Destroys original element after inserting.
    SAFUNCINT_Consume   = 0x10000000,
};

#define SA_GROW_MASK (0xff000000)
#define saGrow(rate) (((uint32)SA_GROW_##rate) << 24)
#define SA_Grow(rate) (((uint32)SA_GROW_##rate) << 24)
#define SA_GET_GROW(flags) ((flags) >> 24)

#define SARRAY_HDRSIZE (offsetof(SArrayHeader, data))
#define SARRAY_HDR(ref) ((SArrayHeader*)(((uintptr)((ref).a)) - SARRAY_HDRSIZE))
#define SAREF(r) (((void)&((r)._is_sarray), *(sa_ref*)(&(r))))
#define SAHANDLE(h) ((sahandle)((void)((h) && &((h)->_is_sarray)), (h)))

#define saSize(ref) ((ref)._is_sarray ? SARRAY_HDR(ref)->count : 0)
#define saCapacity(ref) ((ref)._is_sarray ? SARRAY_HDR(ref)->capacity : 0)
#define saElemSize(ref) ((ref)._is_sarray ? stGetSize(SARRAY_HDR(ref)->elemtype) : 0)
#define saElemType(ref) ((ref)._is_sarray ? SARRAY_HDR(ref)->elemtype : 0)
#define saValid(ref) ((ref).a)

void _saInit(sahandle out, stype elemtype, STypeOps *ops, int32 capacity, uint32 flags);
#define saInit(out, type, capacity, ...) _saInit(SAHANDLE(out), stFullType(type), capacity, func_flags(SA, __VA_ARGS__))

void _saDestroy(sahandle handle);
#define saDestroy(handle) _saDestroy(SAHANDLE(handle));

void _saReserve(sahandle handle, int32 capacity);
#define saReserve(handle, capacity) _saReserve(SAHANDLE(handle), capacity)
void _saShrink(sahandle handle, int32 capacity);
#define saShrink(handle, capacity) _saShrink(SAHANDLE(handle), capacity)
void _saSetSize(sahandle handle, int32 size);
#define saSetSize(handle, size) _saSetSize(SAHANDLE(handle), size)

void _saClear(sahandle handle);
#define saClear(handle) _saClear(SAHANDLE(handle))

int32 _saPush(sahandle handle, stype elemtype, stgeneric elem, uint32 flags);
int32 _saPushPtr(sahandle handle, stype elemtype, stgeneric *elem, uint32 flags);
#define saPush(handle, type, elem, ...) _saPush(SAHANDLE(handle), stCheckedArg(type, elem), func_flags(SAFUNC, __VA_ARGS__))
// Consume version of push, requires that elem be a pointer
#define saPushC(handle, type, elem, ...) _saPushPtr(SAHANDLE(handle), stCheckedPtrArg(type, elem), func_flags(SAFUNC, __VA_ARGS__) | SAFUNCINT_Consume)

// Pointer pop transfers ownership to the caller and does not call the destructor
void *_saPopPtr(sahandle handle, int32 idx);
#define saPopPtr(handle) _saPopPtr(SAHANDLE(handle), -1)
#define saPopPtrI(handle, idx) _saPopPtr(SAHANDLE(handle), idx)

int32 _saFind(sa_ref ref, stgeneric elem, uint32 flags);
_meta_inline int32 _saFindChecked(sa_ref ref, stype elemtype, stgeneric elem, uint32 flags)
{
    if (!ref.a)
        return -1;
    devAssert(stEq(saElemType(ref), elemtype));
    return _saFind(ref, elem, flags);
}
#define saFind(ref, type, elem, ...) _saFindChecked(SAREF(ref), stCheckedArg(type, elem), func_flags(SAFUNC, __VA_ARGS__))

bool _saFindRemove(sahandle handle, stgeneric elem, uint32 flags);
_meta_inline bool _saFindRemoveChecked(sahandle handle, stype elemtype, stgeneric elem, uint32 flags)
{
    if (!handle->a)
        return false;
    devAssert(stEq(saElemType(*handle), elemtype));
    return _saFindRemove(handle, elem, flags);
}
#define saFindRemove(handle, type, elem, ...) _saFindRemoveChecked(SAHANDLE(handle), stCheckedArg(type, elem), func_flags(SAFUNC, __VA_ARGS__))

int32 _saInsert(sahandle handle, int32 idx, stgeneric elem);
_meta_inline int32 _saInsertChecked(sahandle handle, int32 idx, stype elemtype, stgeneric elem)
{
    devAssert(handle->_is_sarray);
    devAssert(stEq(saElemType(*handle), elemtype));
    return _saInsert(handle, idx, elem);
}
#define saInsert(handle, idx, type, elem) _saInsertChecked(SAHANDLE(handle), idx, stCheckedArg(type, elem))

bool _saExtract(sahandle handle, int32 idx, stgeneric *elem, uint32 flags);
_meta_inline bool _saExtractChecked(sahandle handle, int32 idx, stype elemtype, stgeneric *elem, uint32 flags)
{
    if (!handle->a)
        return false;
    devAssert(stGetId(elemtype) == stTypeId(none) || stEq(saElemType(*handle), elemtype));
    return _saExtract(handle, idx, elem, flags);
}
#define htExtract(htbl, ktype, key, vtype, val_copy_out) _htExtractChecked(htbl, stCheckedArg(ktype, key), stCheckedPtrArg(vtype, val_copy_out))
#define saExtract(handle, idx, type, elem_copy_out, ...) _saExtractChecked(SAHANDLE(handle), idx, stCheckedPtrArg(type, elem_copy_out), func_flags(SAFUNC, __VA_ARGS__))
#define saRemove(handle, idx, ...) _saExtractChecked(SAHANDLE(handle), idx, stType(none), NULL, func_flags(SAFUNC, __VA_ARGS__))

void _saSort(sahandle handle, bool keep);
#define saSort(handle, keep) _saSort(SAHANDLE(handle), keep)

void _saSlice(sahandle out, sa_ref ref, int32 start, int32 end);
#define saSlice(out, src, start, end) _saSlice(SAHANDLE(out), SAREF(src), start, end)
#define saClone(out, src) _saSlice(SAHANDLE(out), SAREF(src), 0, 0)

void _saMerge(sahandle out, int n, sa_ref *refs, uint32 flags);
#define saMerge(out, ...) _saMerge(SAHANDLE(out), sizeof((sa_ref[]){ __VA_ARGS__ })/sizeof(sa_ref), (sa_ref[]){ __VA_ARGS__ }, 0)
#define saMergeF(out, flags, ...) _saMerge(SAHANDLE(out), sizeof((sa_ref[]){ __VA_ARGS__ })/sizeof(sa_ref), (sa_ref[]){ __VA_ARGS__ }, func_flags(SAFUNC, flags))

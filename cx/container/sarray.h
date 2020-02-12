#pragma once

#include <cx/cx.h>
#include <cx/debug/assert.h>

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

    // Valid for: saFind, saRemove
    // Destroys element if found.
    SAFUNC_Destroy      = 0x00020000,

    // Valid for: saFind
    // Removes element if found (does not destroy, caller becomes owner).
    SAFUNC_RemoveOnly   = 0x00040000,

    // Valid for: saFind, saRemove
    // When removing/destroying an element, swaps with last element,
    // disrupting the order but removing the element much faster.
    // Not valid for sorted arrays.
    SAFUNC_Fast         = 0x00080000,

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
#define SARRAY_HDR(handle) ((SArrayHeader*)(((uintptr)(handle)) - SARRAY_HDRSIZE))

#define saSize(handle) (*(handle) ? SARRAY_HDR(*(handle))->count : 0)
#define saCapacity(handle) (*(handle) ? SARRAY_HDR(*(handle))->capacity : 0)
#define saElemSize(handle) (*(handle) ? stGetSize(SARRAY_HDR(*(handle))->elemtype) : 0)
#define saElemType(handle) (*(handle) ? SARRAY_HDR(*(handle))->elemtype : 0)

#define SAHANDLE(h) ((void**)(h))

void *_saCreate(stype elemtype, STypeOps *ops, int32 capacity, uint32 flags);
#define saCreate(type, capacity, ...) _saCreate(stFullType(type), capacity, func_flags(SA, __VA_ARGS__))

void _saDestroy(void **handle);
#define saDestroy(handle) _saDestroy(SAHANDLE(handle));

void _saReserve(void **handle, int32 capacity);
#define saReserve(handle, capacity) _saReserve(SAHANDLE(handle), capacity)
void _saShrink(void **handle, int32 capacity);
#define saShrink(handle, capacity) _saShrink(SAHANDLE(handle), capacity)
void _saSetSize(void **handle, int32 size);
#define saSetSize(handle, size) _saSetSize(SAHANDLE(handle), size)

void _saClear(void **handle);
#define saClear(handle) _saClear(SAHANDLE(handle))

int32 _saPush(void **handle, stype elemtype, stgeneric elem, uint32 flags);
int32 _saPushPtr(void **handle, stype elemtype, stgeneric *elem, uint32 flags);
#define saPush(handle, type, elem, ...) _saPush(SAHANDLE(handle), stChecked(type, elem), func_flags(SAFUNC, __VA_ARGS__))
// Consume version of push, requires that elem be a pointer
#define saPushC(handle, type, elem, ...) _saPushPtr(SAHANDLE(handle), stCheckedPtr(type, elem), func_flags(SAFUNC, __VA_ARGS__) | SAFUNCINT_Consume)

// Pointer pop transfers ownership to the caller and does not call the destructor
void *_saPopPtr(void **handle, int32 idx);
#define saPopPtr(handle) _saPopPtr(SAHANDLE(handle), -1)
#define saPopPtrI(handle, idx) _saPopPtr(SAHANDLE(handle), idx)

int32 _saFind(void **handle, stgeneric elem, uint32 flags);
_meta_inline int32 _saFindChecked(void **handle, stype elemtype, stgeneric elem, uint32 flags)
{
    if (!*handle)
        return -1;
    devAssert(stEq(saElemType(handle), elemtype));
    return _saFind(handle, elem, flags);
}
#define saFind(handle, type, elem, ...) _saFindChecked(SAHANDLE(handle), stChecked(type, elem), func_flags(SAFUNC, __VA_ARGS__))

int32 _saInsert(void **handle, int32 idx, stgeneric elem);
_meta_inline int32 _saInsertChecked(void **handle, int32 idx, stype elemtype, stgeneric elem)
{
    devAssert(*handle);
    devAssert(stEq(saElemType(handle), elemtype));
    return _saInsert(handle, idx, elem);
}
#define saInsert(handle, idx, type, elem) _saInsertChecked(SAHANDLE(handle), idx, stChecked(type, elem))

bool _saRemove(void **handle, int32 idx, uint32 flags);
#define saRemove(handle, idx, ...) _saRemove(SAHANDLE(handle), idx, func_flags(SAFUNC, __VA_ARGS__))

void _saSort(void **handle, bool keep);
#define saSort(handle, keep) _saSort(SAHANDLE(handle), keep)

void *_saSlice(void **handle, int32 start, int32 end);
#define saSlice(handle, start, end) _saSlice(SAHANDLE(handle), start, end)

void *_saMerge(int n, void **handles, uint32 flags);
#define saMerge(...) _saMerge(sizeof((void*[]){ __VA_ARGS__ })/sizeof(void*), (void*[]){ __VA_ARGS__ }, 0)
#define saMergeF(flags, ...) _saMerge(sizeof((void*[]){ __VA_ARGS__ })/sizeof(void*), (void*[]){ __VA_ARGS__ }, func_flags(SAFUNC, flags))

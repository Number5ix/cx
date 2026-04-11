#include "sarray_private.h"
#include "cx/platform/cpu.h"
#include "cx/string.h"
#include "cx/utils.h"

#if 0
// inline functions get emitted in this translation unit
// Needed as technically C99 doesn't guarantee inlining, but disabled for now
// as all of our supported compilers have a way to enforce a guarantee.
extern inline int32 _saPushChecked(sahandle handle, stype elemtype, void *elem, flags_t flags)
extern inline int32 _saFindChecked(sahandle handle, stype elemtype, void *elem, flags_t flags);
extern inline int32 _saInsertChecked(sahandle handle, int32 idx, stype elemtype, void *elem);
#endif

// qsort routine from Bentley & McIlroy's "Engineering a Sort Function"
static _meta_inline void sa_swap(_Inout_updates_bytes_(sz) void* av,
                                 _Inout_updates_bytes_(sz) void* bv, size_t sz)
{
    uint8* a = av;
    uint8* b = bv;
    uint8 t;

    do {
        t    = *a;
        *a++ = *b;
        *b++ = t;
    } while (--sz > 0);
}

#define vecswap(a, b, n) \
    if ((n) > 0)         \
    sa_swap(a, b, n)

// specializations for various data types for performance
#define SA_SPECIALIZE_FINDONLY(name, typ, comptyp, compfunc)           \
    static int32 sa_find_internal_##name(_In_ SArrayHeader* hdr,       \
                                         _In_ const void* elem,        \
                                         _Inout_ bool* found)          \
    {                                                                  \
        int32 i;                                                       \
                                                                       \
        for (i = 0; i < hdr->count; ++i) {                             \
            if (compfunc(hdr->elemtype, elem, ELEMPTR(hdr, i)) == 0) { \
                *found = true;                                         \
                return i;                                              \
            }                                                          \
        }                                                              \
                                                                       \
        return hdr->count;                                             \
    }

#define SA_SPECIALIZE_BSEARCHONLY(name, typ, comptyp, compfunc)     \
    static int32 sa_bsearch_internal_##name(_In_ SArrayHeader* hdr, \
                                            _In_ const void* elem,  \
                                            _Inout_ bool* found)    \
    {                                                               \
        int32 low, high, i;                                         \
        comptyp comp;                                               \
                                                                    \
        low  = 0;                                                   \
        high = hdr->count;                                          \
        while (high > low) {                                        \
            i    = (high + low) / 2;                                \
            comp = compfunc(hdr->elemtype, elem, ELEMPTR(hdr, i));  \
            if (comp == 0) {                                        \
                low = high = i;                                     \
                *found     = true;                                  \
            } else if (comp < 0)                                    \
                high = i;                                           \
            else if (i == low)                                      \
                low = high;                                         \
            else                                                    \
                low = i;                                            \
        }                                                           \
        return low;                                                 \
    }

#define SA_SPECIALIZE_QSORTONLY(name, typ, comptyp, compfunc)                                   \
    static _meta_inline char*                                                                   \
    med3_##name(_In_ SArrayHeader* hdr, _In_ void* a, _In_ void* b, _In_ void* c)               \
    {                                                                                           \
        return (char*)(compfunc(hdr->elemtype, a, b) < 0 ?                                      \
                           (compfunc(hdr->elemtype, b, c) < 0 ?                                 \
                                b :                                                             \
                                (compfunc(hdr->elemtype, a, c) < 0 ? c : a)) :                  \
                           (compfunc(hdr->elemtype, b, c) > 0 ?                                 \
                                b :                                                             \
                                (compfunc(hdr->elemtype, a, c) < 0 ? a : c)));                  \
    }                                                                                           \
                                                                                                \
    static void sa_qsort_internal_##name(_Inout_ SArrayHeader* hdr, _Inout_ void* a, size_t n)  \
    {                                                                                           \
        char *pa, *pb, *pc, *pd, *pm, *pn;                                                      \
        size_t es, d1, d2;                                                                      \
        comptyp cmp_result;                                                                     \
        int swap_cnt;                                                                           \
                                                                                                \
        es = stGetSize(hdr->elemtype);                                                          \
                                                                                                \
loop:                                                                                           \
        swap_cnt = 0;                                                                           \
                                                                                                \
        if (n < 7) {                                                                            \
            for (pm = (char*)a + es; pm < (char*)a + n * es; pm += es)                          \
                for (char* pl = pm;                                                             \
                     pl > (char*)a && compfunc(hdr->elemtype, (void*)(pl - es), (void*)pl) > 0; \
                     pl -= es)                                                                  \
                    sa_swap(pl, pl - es, es);                                                   \
            return;                                                                             \
        }                                                                                       \
        pm = (char*)a + (n / 2) * es;                                                           \
        if (n > 7) {                                                                            \
            char* pl = a;                                                                       \
            pn       = (char*)a + (n - 1) * es;                                                 \
            if (n > 40) {                                                                       \
                size_t d = (n / 8) * es;                                                        \
                                                                                                \
                pl = med3_##name(hdr, pl, pl + d, pl + 2 * d);                                  \
                pm = med3_##name(hdr, pm - d, pm, pm + d);                                      \
                pn = med3_##name(hdr, pn - 2 * d, pn - d, pn);                                  \
            }                                                                                   \
            pm = med3_##name(hdr, pl, pm, pn);                                                  \
        }                                                                                       \
        sa_swap(a, pm, es);                                                                     \
        pa = pb = (char*)a + es;                                                                \
                                                                                                \
        pc = pd = (char*)a + (n - 1) * es;                                                      \
        for (;;) {                                                                              \
            while (pb <= pc && (cmp_result = compfunc(hdr->elemtype, (void*)pb, a)) <= 0) {     \
                if (cmp_result == 0) {                                                          \
                    swap_cnt = 1;                                                               \
                    sa_swap(pa, pb, es);                                                        \
                    pa += es;                                                                   \
                }                                                                               \
                pb += es;                                                                       \
            }                                                                                   \
            while (pb <= pc && (cmp_result = compfunc(hdr->elemtype, (void*)pc, a)) >= 0) {     \
                if (cmp_result == 0) {                                                          \
                    swap_cnt = 1;                                                               \
                    sa_swap(pc, pd, es);                                                        \
                    pd -= es;                                                                   \
                }                                                                               \
                pc -= es;                                                                       \
            }                                                                                   \
            if (pb > pc)                                                                        \
                break;                                                                          \
            sa_swap(pb, pc, es);                                                                \
            swap_cnt = 1;                                                                       \
            pb += es;                                                                           \
            pc -= es;                                                                           \
        }                                                                                       \
        if (swap_cnt == 0) { /* Switch to insertion sort */                                     \
            for (pm = (char*)a + es; pm < (char*)a + n * es; pm += es)                          \
                for (char* pl = pm;                                                             \
                     pl > (char*)a && compfunc(hdr->elemtype, (void*)(pl - es), (void*)pl) > 0; \
                     pl -= es)                                                                  \
                    sa_swap(pl, pl - es, es);                                                   \
            return;                                                                             \
        }                                                                                       \
                                                                                                \
        pn = (char*)a + n * es;                                                                 \
        d1 = min((pa - (char*)a), pb - pa);                                                     \
        vecswap(a, pb - d1, d1);                                                                \
        d1 = min((size_t)(pd - pc), pn - pd - es);                                              \
        vecswap(pb, pn - d1, d1);                                                               \
                                                                                                \
        d1 = pb - pa;                                                                           \
        d2 = pd - pc;                                                                           \
        if (d1 <= d2) {                                                                         \
            /* Recurse on left partition, then iterate on right partition */                    \
            if (d1 > es) {                                                                      \
                sa_qsort_internal_##name(hdr, a, d1 / es);                                      \
            }                                                                                   \
            if (d2 > es) {                                                                      \
                /* Iterate rather than recurse to save stack space */                           \
                /* qsort(pn - d2, d2 / es, es, cmp); */                                         \
                a = pn - d2;                                                                    \
                n = d2 / es;                                                                    \
                goto loop;                                                                      \
            }                                                                                   \
        } else {                                                                                \
            /* Recurse on right partition, then iterate on left partition */                    \
            if (d2 > es) {                                                                      \
                sa_qsort_internal_##name(hdr, pn - d2, d2 / es);                                \
            }                                                                                   \
            if (d1 > es) {                                                                      \
                /* Iterate rather than recurse to save stack space */                           \
                /* qsort(a, d1 / es, es, cmp); */                                               \
                n = d1 / es;                                                                    \
                goto loop;                                                                      \
            }                                                                                   \
        }                                                                                       \
    }

#define SA_SPECIALIZE(name, typ, comptyp, compfunc)         \
    SA_SPECIALIZE_FINDONLY(name, typ, comptyp, compfunc)    \
    SA_SPECIALIZE_BSEARCHONLY(name, typ, comptyp, compfunc) \
    SA_SPECIALIZE_QSORTONLY(name, typ, comptyp, compfunc)

#define compfunc_stype(st, a, b) _stCmp(st, stStored(st, a), stStored(st, b), 0)
SA_SPECIALIZE_BSEARCHONLY(stype, void*, intptr, compfunc_stype)
SA_SPECIALIZE_QSORTONLY(stype, void*, intptr, compfunc_stype)
#define compfunc_stype_eq(st, a, b) _stCmp(st, stStored(st, a), stStored(st, b), ST_Equality)
SA_SPECIALIZE_FINDONLY(stypeeq, void*, intptr, compfunc_stype_eq)
#define compfunc_str(st, a, b) strCmp(*(string*)(a), *(string*)(b))
SA_SPECIALIZE(string, string, int32, compfunc_str)
#define SA_SPECIALIZE_BUILTIN(name, typ, comptyp)                                       \
    static _meta_inline comptyp compfunc_##name(stype st, const void* a, const void* b) \
    {                                                                                   \
        return *(typ*)a - *(typ*)b;                                                     \
    }                                                                                   \
    SA_SPECIALIZE(name, typ, comptyp, compfunc_##name)
SA_SPECIALIZE_BUILTIN(int8, int8, int)
SA_SPECIALIZE_BUILTIN(uint8, uint8, int)
SA_SPECIALIZE_BUILTIN(int16, int16, int)
SA_SPECIALIZE_BUILTIN(uint16, uint16, int)
SA_SPECIALIZE_BUILTIN(int32, int32, int)
SA_SPECIALIZE_BUILTIN(uint32, uint32, int)
SA_SPECIALIZE_BUILTIN(int64, int64, int64)
SA_SPECIALIZE_BUILTIN(uint64, uint64, int64)
SA_SPECIALIZE_BUILTIN(float32, float32, float32)
SA_SPECIALIZE_BUILTIN(float64, float64, float64)
SA_SPECIALIZE_BUILTIN(ptr, char*, intptr)

typedef int32 (*sa_find_spec)(_In_ SArrayHeader* hdr, _In_ const void* elem, _Out_ bool* found);
typedef void (*sa_qsort_spec)(_Inout_ SArrayHeader* hdr, _Inout_ void* a, size_t n);

// Row = (id >> 8) & 0xF  →  1=INT, 2=UINT, 3=FLOAT, 4=PTR
// Col = ctz(id & 0xFF)   →  0=sz1, 1=sz2,  2=sz4,   3=sz8
#define SA_SPEC_ROWS 5
#define SA_SPEC_COLS 4

#define SA_ROW_INT   1
#define SA_ROW_UINT  2
#define SA_ROW_FLOAT 3
#define SA_ROW_PTR   4
#define SA_COL_SZ1   0
#define SA_COL_SZ2   1
#define SA_COL_SZ4   2
#define SA_COL_SZ8   3

static const sa_find_spec find_spec_basic[SA_SPEC_ROWS][SA_SPEC_COLS] = {
    [SA_ROW_INT][SA_COL_SZ1]   = sa_find_internal_int8,
    [SA_ROW_INT][SA_COL_SZ2]   = sa_find_internal_int16,
    [SA_ROW_INT][SA_COL_SZ4]   = sa_find_internal_int32,
    [SA_ROW_INT][SA_COL_SZ8]   = sa_find_internal_int64,
    [SA_ROW_UINT][SA_COL_SZ1]  = sa_find_internal_uint8,
    [SA_ROW_UINT][SA_COL_SZ2]  = sa_find_internal_uint16,
    [SA_ROW_UINT][SA_COL_SZ4]  = sa_find_internal_uint32,
    [SA_ROW_UINT][SA_COL_SZ8]  = sa_find_internal_uint64,
    [SA_ROW_FLOAT][SA_COL_SZ4] = sa_find_internal_float32,
    [SA_ROW_FLOAT][SA_COL_SZ8] = sa_find_internal_float64,
#if defined(_32BIT)
    [SA_ROW_PTR][SA_COL_SZ4] = sa_find_internal_ptr,   // 32-bit
#elif defined(_64BIT)
    [SA_ROW_PTR][SA_COL_SZ8] = sa_find_internal_ptr,   // 64-bit
#endif
};

static const sa_find_spec bsearch_spec_basic[SA_SPEC_ROWS][SA_SPEC_COLS] = {
    [SA_ROW_INT][SA_COL_SZ1]   = sa_bsearch_internal_int8,
    [SA_ROW_INT][SA_COL_SZ2]   = sa_bsearch_internal_int16,
    [SA_ROW_INT][SA_COL_SZ4]   = sa_bsearch_internal_int32,
    [SA_ROW_INT][SA_COL_SZ8]   = sa_bsearch_internal_int64,
    [SA_ROW_UINT][SA_COL_SZ1]  = sa_bsearch_internal_uint8,
    [SA_ROW_UINT][SA_COL_SZ2]  = sa_bsearch_internal_uint16,
    [SA_ROW_UINT][SA_COL_SZ4]  = sa_bsearch_internal_uint32,
    [SA_ROW_UINT][SA_COL_SZ8]  = sa_bsearch_internal_uint64,
    [SA_ROW_FLOAT][SA_COL_SZ4] = sa_bsearch_internal_float32,
    [SA_ROW_FLOAT][SA_COL_SZ8] = sa_bsearch_internal_float64,
#if defined(_32BIT)
    [SA_ROW_PTR][SA_COL_SZ4] = sa_bsearch_internal_ptr,   // 32-bit
#elif defined(_64BIT)
    [SA_ROW_PTR][SA_COL_SZ8] = sa_bsearch_internal_ptr,   // 64-bit
#endif
};

static const sa_qsort_spec qsort_spec_basic[SA_SPEC_ROWS][SA_SPEC_COLS] = {
    [SA_ROW_INT][SA_COL_SZ1]   = sa_qsort_internal_int8,
    [SA_ROW_INT][SA_COL_SZ2]   = sa_qsort_internal_int16,
    [SA_ROW_INT][SA_COL_SZ4]   = sa_qsort_internal_int32,
    [SA_ROW_INT][SA_COL_SZ8]   = sa_qsort_internal_int64,
    [SA_ROW_UINT][SA_COL_SZ1]  = sa_qsort_internal_uint8,
    [SA_ROW_UINT][SA_COL_SZ2]  = sa_qsort_internal_uint16,
    [SA_ROW_UINT][SA_COL_SZ4]  = sa_qsort_internal_uint32,
    [SA_ROW_UINT][SA_COL_SZ8]  = sa_qsort_internal_uint64,
    [SA_ROW_FLOAT][SA_COL_SZ4] = sa_qsort_internal_float32,
    [SA_ROW_FLOAT][SA_COL_SZ8] = sa_qsort_internal_float64,
#if defined(_32BIT)
    [SA_ROW_PTR][SA_COL_SZ4] = sa_qsort_internal_ptr,
#elif defined(_64BIT)
    [SA_ROW_PTR][SA_COL_SZ8] = sa_qsort_internal_ptr,
#endif
};

static _meta_inline sa_find_spec get_find_spec(stype st)
{
    if (st == stType(string))
        return sa_find_internal_string;
    if (STYPE_CLASS(st->id) != STCLASS_BASIC)
        return NULL;
    uint32 disc = st->id & 0xFF;
    if (!disc || disc > 8)
        return NULL;
    return find_spec_basic[(st->id >> 8) & 0xF][ctz32(disc)];
}

static _meta_inline sa_find_spec get_bsearch_spec(stype st)
{
    if (st == stType(string))
        return sa_bsearch_internal_string;
    if (STYPE_CLASS(st->id) != STCLASS_BASIC)
        return NULL;
    uint32 disc = st->id & 0xFF;
    if (!disc || disc > 8)
        return NULL;
    return bsearch_spec_basic[(st->id >> 8) & 0xF][ctz32(disc)];
}

static _meta_inline sa_qsort_spec get_qsort_spec(stype st)
{
    if (st == stType(string))
        return sa_qsort_internal_string;
    if (STYPE_CLASS(st->id) != STCLASS_BASIC)
        return NULL;
    uint32 disc = st->id & 0xFF;
    if (!disc || disc > 8)
        return NULL;
    return qsort_spec_basic[(st->id >> 8) & 0xF][ctz32(disc)];
}

static int32 sa_find_internal(_In_ SArrayHeader* hdr, _In_ stgeneric stelem, _Out_ bool* found)
{
    void* elem = stGenPtr(hdr->elemtype, stelem);

    *found = false;
    if (hdr->flags & SA_Sorted) {
        sa_find_spec spec = get_bsearch_spec(hdr->elemtype);
        if (spec)
            return spec(hdr, elem, found);
        else
            return sa_bsearch_internal_stype(hdr, elem, found);
    } else {
        sa_find_spec spec = get_find_spec(hdr->elemtype);
        if (spec)
            return spec(hdr, elem, found);
        else
            return sa_find_internal_stypeeq(hdr, elem, found);
    }

    // unreachable
}

static void sa_qsort_internal(_Inout_ SArrayHeader* hdr)
{
    sa_qsort_spec spec = get_qsort_spec(hdr->elemtype);
    if (spec)
        spec(hdr, hdr->data, hdr->count);
    else
        sa_qsort_internal_stype(hdr, hdr->data, hdr->count);
}

_Use_decl_annotations_
bool _saInit(sahandle out, stype elemtype, int32 capacity, bool canfail, flags_t flags)
{
    SArrayHeader* hdr;

    if (capacity == 0)
        capacity = 8;
    else
        capacity = max(capacity, 1);
    relAssert(stGetSize(elemtype) > 0);

    // If we are not using custom ops and this is a POD type, skip all of the copy/destructor
    // logic and just use straight memory copies for speed.
    if (!elemtype->ops.dtor && !elemtype->ops.copy && !stHasFlag(elemtype, Object))
        flags |= SA_Ref;

    hdr = xaAlloc(SARRAY_HDRSIZE + (size_t)capacity * stGetSize(elemtype),
                  canfail ? XA_Optional(High) : 0);
    if (canfail && !hdr)
        return false;

    if (SA_GET_GROW(flags) == SA_GROW_Auto) {
        // auto growth based on element size
        if (stGetSize(elemtype) <= 8)
            flags |= SA_Grow(Aggressive);
        else if (stGetSize(elemtype) <= 256)
            flags |= SA_Grow(Normal);
        else
            flags |= SA_Grow(Slow);
    }

    hdr->arraytype = NULL;   // set lazily if requested
    hdr->elemtype  = stCanonical(elemtype);
    hdr->count     = 0;
    hdr->capacity  = capacity;
    hdr->flags     = flags;
    out->a         = &hdr->data[0];

    return true;
}

_Use_decl_annotations_
bool _saInitFromType(sahandle out, stype arraytype, int32 capacity, bool canfail, flags_t flags)
{
    devAssert(arraytype);
    devAssert(arraytype->id == stTypeId(sarray));

    // arraytype SHOULD already be canonical, but just in case
    arraytype = stCanonical(arraytype);

    stype elemtype = arraytype->param[0];
    devAssert(elemtype);

    bool ret = _saInit(out, elemtype, capacity, canfail, flags);

    if (ret) {
        SArrayHeader* hdr = SARRAY_HDR(*out);
        hdr->arraytype    = arraytype;
    }

    return ret;
}

_Use_decl_annotations_
stype _saType(sa_ref ref)
{
    SArrayHeader* hdr = SARRAY_HDR(ref);
    if (!hdr->arraytype) {
        STypeInfo tmp = stTypeInfo(sarray);   // copy sarray type to use as a base
        tmp.flags |= stFlag(Temporary);
        tmp.param[0]   = hdr->elemtype;
        hdr->arraytype = stCanonical(&tmp);   // this will register the type if needed
    }
    return hdr->arraytype;
}

static bool saRealloc(_Inout_ sahandle handle, _Inout_ptr_ SArrayHeader** hdr, int32 cap,
                      bool canfail)
{
    bool ret = xaResize(hdr,
                        SARRAY_HDRSIZE + (size_t)cap * stGetSize((*hdr)->elemtype),
                        canfail ? XA_Optional(High) : 0);

    if (ret) {
        (*hdr)->capacity = cap;
        handle->a        = &(*hdr)->data[0];
    }
    return ret;
}

static bool _saGrow(_Inout_ sahandle handle, _Inout_ptr_ SArrayHeader** hdr, int32 minsz,
                    bool canfail)
{
    int32 cap = (*hdr)->capacity;

    while (cap < minsz) {
        switch (SA_GET_GROW((*hdr)->flags)) {
        case SA_GROW_Normal:
            if (cap < 16)
                cap *= 2;            // 100%
            else if (cap < 128)
                cap += (cap >> 1);   // 50%
            else
                cap += (cap >> 2);   // 25%
            break;
        case SA_GROW_Aggressive:
            if (cap < 32)
                cap *= 2;            // 100%
            else if (cap < 256)
                cap += (cap >> 1);   // 50%
            else
                cap += (cap >> 2);   // 25%
            break;
        case SA_GROW_Slow:
            if (cap < 8)
                cap *= 2;            // 100%
            else if (cap < 64)
                cap += (cap >> 1);   // 50%
            else
                cap += (cap >> 2);   // 25%
            break;
        case SA_GROW_100:
            cap *= 2;   // 100%
            break;
        case SA_GROW_50:
            cap += (cap >> 1);   // 50%
            break;
        case SA_GROW_25:
            cap += (cap >> 2);   // 25%
            break;
        case SA_GROW_Minimal:
            cap = max(cap, minsz);
            break;
        default:
            devAssert("Invalid sarray grow setting");
            cap += (cap >> 1);
        }
    }

    return saRealloc(handle, hdr, cap, canfail);
}

_Use_decl_annotations_
void _saDestroy(sahandle handle)
{
    if (!handle->a)
        return;

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    _saClear(handle);   // to call dtors
    xaFree(hdr);
    handle->a = NULL;
}

_Use_decl_annotations_
void _saClear(sahandle handle)
{
    if (!handle->a)
        return;

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    if (!(hdr->flags & SA_Ref)) {
        int32 i;

        for (i = 0; i < hdr->count; ++i) {
            _stDestroy(hdr->elemtype, stStoredPtr(hdr->elemtype, ELEMPTR(hdr, i)), 0);
        }
    }
    hdr->count = 0;
}

_Use_decl_annotations_
bool _saReserve(sahandle handle, int32 capacity, bool canfail)
{
    if (!handle->a)
        return false;

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    int32 newcap      = max((capacity == 0) ? 1 : capacity, hdr->count);
    if (newcap > hdr->capacity) {
        return _saGrow(handle, &hdr, newcap, canfail);
    }
    return true;
}

_Use_decl_annotations_
void _saShrink(sahandle handle, int32 capacity)
{
    if (!handle->a)
        return;

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    // Ensure new capacity is at least as large as current count
    int32 newcap      = max((capacity == 0) ? 1 : capacity, hdr->count);
    if (newcap < hdr->capacity) {
        saRealloc(handle, &hdr, newcap, true);
    }
}

_Use_decl_annotations_
void _saSetSize(sahandle handle, int32 size)
{
    if (!handle->a)
        return;

    _saReserve(handle, size, false);
    SArrayHeader* hdr = SARRAY_HDR(*handle);

    if (size > hdr->count) {
        memset(ELEMPTR(hdr, hdr->count),
               0,
               ((uintptr)size - hdr->count) * stGetSize(hdr->elemtype));
    } else if (!(hdr->flags & SA_Ref)) {
        for (int i = size; i < hdr->count; i++) {
            _stDestroy(hdr->elemtype, stStoredPtr(hdr->elemtype, ELEMPTR(hdr, i)), 0);
        }
    }
    hdr->count = size;
}

static _meta_inline void sa_set_elem_internal(_Inout_ SArrayHeader* hdr, int32 idx, _When_(!consume, _In_) _When_(consume, _Pre_notnull_ _Post_invalid_) stgeneric* elem, bool consume)
{
    if (consume) {
        // special case: if we're consuming, just steal the element instead of deep copying it,
        // even if we're the owner
        memcpy(ELEMPTR(hdr, idx), stGenPtr(hdr->elemtype, *elem), stGetSize(hdr->elemtype));

        // destroy source
        if (hdr->flags & SA_Ref)   // weird combo, but respect it
            _stDestroy(hdr->elemtype, elem, 0);
        else if (stGetSize(hdr->elemtype) == sizeof(void*))
            elem->st_ptr = 0;   // if this is a pointer-sized element, clear it out
        return;
    }

    if (!(hdr->flags & SA_Ref))
        _stCopy(hdr->elemtype, stStoredPtr(hdr->elemtype, ELEMPTR(hdr, idx)), *elem, 0);
    else
        memcpy(ELEMPTR(hdr, idx), stGenPtr(hdr->elemtype, *elem), stGetSize(hdr->elemtype));
}

static int32 sa_insert_internal(_Inout_ sahandle handle, _Inout_ SArrayHeader* hdr, int32 idx, _When_(!consume, _In_) _When_(consume, _Pre_notnull_ _Post_invalid_) stgeneric* elem, bool consume)
{
    if (hdr->count == hdr->capacity)
        _saGrow(handle, &hdr, hdr->count + 1, false);

    memmove(ELEMPTR(hdr, (size_t)idx + 1),
            ELEMPTR(hdr, idx),
            ((size_t)hdr->count - idx) * stGetSize(hdr->elemtype));
    hdr->count++;

    sa_set_elem_internal(hdr, idx, elem, consume);

    return idx;
}

_Use_decl_annotations_
int32 _saInsert(sahandle handle, int32 idx, stgeneric elem)
{
    SArrayHeader* hdr = SARRAY_HDR(*handle);

    // negative indicies represent distance from the end of the array
    if (idx < 0)
        idx += hdr->count;

    if (idx < 0 || idx > hdr->count)
        return -1;

    // inserting into a sorted array makes it not be sorted anymore
    if ((hdr->flags & SA_Sorted))
        hdr->flags &= ~SA_Sorted;

    return sa_insert_internal(handle, hdr, idx, &elem, false);
}

_Use_decl_annotations_
int32 _saPushPtr(sahandle handle, stype elemtype, stgeneric* elem, flags_t flags)
{
    if (!handle->a)
        _saInit(handle, elemtype, 0, false, 0);
    devAssert(stEq(saElemType(*handle), elemtype));

    SArrayHeader* hdr = SARRAY_HDR(*handle);

    if (hdr->flags & SA_Sorted) {
        bool found = false;
        int32 idx  = sa_find_internal(hdr, *elem, &found);
        if (found && (flags & SA_Unique)) {
            if (flags & SAINT_Consume)
                _stDestroy(hdr->elemtype, elem, 0);
            return -1;   // don't insert if we already have it
        }

        idx = sa_insert_internal(handle, hdr, idx, elem, flags & SAINT_Consume);
        return idx;
    } else {
        if (flags & SA_Unique) {
            bool found = false;
            sa_find_internal(hdr, *elem, &found);
            if (found) {
                if (flags & SAINT_Consume)
                    _stDestroy(hdr->elemtype, elem, 0);
                return -1;
            }
        }

        if (hdr->count == hdr->capacity)
            _saGrow(handle, &hdr, hdr->count + 1, false);

        sa_set_elem_internal(hdr, hdr->count, elem, flags & SAINT_Consume);

        hdr->count++;
        return hdr->count - 1;
    }
}

_Use_decl_annotations_
int32 _saPush(sahandle handle, stype elemtype, stgeneric elem, flags_t flags)
{
    return _saPushPtr(handle, elemtype, &elem, flags);
}

static void sa_remove_internal(_Inout_ sahandle handle, _Inout_ SArrayHeader* hdr, int32 idx,
                               bool fast)
{
    if (idx != hdr->count - 1) {
        if (fast) {
            // swap last element with the one being removed -- this changes the order!
            if ((hdr->flags & SA_Sorted))
                hdr->flags &= ~SA_Sorted;
            memcpy(ELEMPTR(hdr, idx),
                   ELEMPTR(hdr, (size_t)hdr->count - 1),
                   stGetSize(hdr->elemtype));
        } else {
            memmove(ELEMPTR(hdr, idx),
                    ELEMPTR(hdr, (size_t)idx + 1),
                    ((size_t)hdr->count - idx - 1) * stGetSize(hdr->elemtype));
        }
    }

    hdr->count--;
}

_Use_decl_annotations_
bool _saExtract(sahandle handle, int32 idx, stgeneric* elem, flags_t flags)
{
    if (!handle->a)
        return false;

    SArrayHeader* hdr = SARRAY_HDR(*handle);

    if (idx < 0)
        idx += hdr->count;

    if (idx < 0 || idx >= hdr->count)
        return false;

    // either extract into a variable if one is provided, or just destroy it
    if (elem)
        memcpy(stGenPtr(hdr->elemtype, *elem), ELEMPTR(hdr, idx), stGetSize(hdr->elemtype));
    else if (!(hdr->flags & SA_Ref))
        _stDestroy(hdr->elemtype, stStoredPtr(hdr->elemtype, ELEMPTR(hdr, idx)), 0);

    sa_remove_internal(handle, hdr, idx, flags & SA_Fast);

    if (hdr->flags & SA_AutoShrink) {
        saRealloc(handle, &hdr, hdr->count, true);
    }
    return true;
}

_Use_decl_annotations_
void* _saPopPtr(sahandle handle, int32 idx)
{
    if (!handle->a)
        return NULL;

    // Destructor is intentionally not called, as we are
    // giving our reference to the caller.

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    devAssert(stEq(hdr->elemtype, stType(ptr)) || stEq(hdr->elemtype, stType(object)));

    if (hdr->count == 0)
        return NULL;

    if (idx == -1) {
        // special case for popping from the tail
        hdr->count--;
        return *(void**)ELEMPTR(hdr, hdr->count);
    }

    if (idx < 0)
        idx += hdr->count;

    if (idx < 0 || idx >= hdr->count)
        return NULL;

    void* ret = *(void**)ELEMPTR(hdr, idx);
    sa_remove_internal(handle, hdr, idx, false);
    return ret;
}

_Use_decl_annotations_
int32 _saFind(sa_ref ref, stgeneric elem, flags_t flags)
{
    SArrayHeader* hdr = SARRAY_HDR(ref);

    int32 idx  = -1;
    bool found = false;

    idx = sa_find_internal(hdr, elem, &found);

    if (!found && !((flags & SA_Inexact) && (hdr->flags & SA_Sorted)))
        return -1;

    return idx;
}

_Use_decl_annotations_
bool _saFindRemove(sahandle handle, stgeneric elem, flags_t flags)
{
    SArrayHeader* hdr = SARRAY_HDR(*handle);

    int32 idx  = -1;
    bool found = false;

    idx = sa_find_internal(hdr, elem, &found);

    if (found)
        _saExtract(handle, idx, NULL, flags);

    return found;
}

_Use_decl_annotations_
void _saSort(sahandle handle, bool keep)
{
    if (!handle->a)
        return;

    SArrayHeader* hdr = SARRAY_HDR(*handle);
    sa_qsort_internal(hdr);

    if (keep)
        hdr->flags |= SA_Sorted;
}

_Use_decl_annotations_
void _saSlice(sahandle out, sa_ref ref, int32 start, int32 end)
{
    if (!ref.a) {
        out->a = NULL;
        return;
    }

    SArrayHeader* hdr = SARRAY_HDR(ref);
    stype elemtype    = hdr->elemtype;

    // negative start indicates distance from end of array
    if (start < 0)
        start = clamphigh(clamplow(start + hdr->count, 0), hdr->count);
    else
        start = clamphigh(start, hdr->count);

    // negative end can indicate that as well
    if (end < 0)
        end = clamphigh(clamplow(end + hdr->count, 0), hdr->count);
    else if (end == 0)
        end = hdr->count;   // 0 end means rest of array
    else
        end = clamphigh(end, hdr->count);

    _saInit(out, elemtype, clamplow(end - start, 1), false, hdr->flags);
    SArrayHeader* newhdr = SARRAY_HDR(*out);

    // mask out sorted flag for now to speed up inserts
    newhdr->flags &= ~SA_Sorted;

    int32 i;
    for (i = start; i < end; i++) {
        _saPushPtr(out, elemtype, stStoredPtr(hdr->elemtype, ELEMPTR(hdr, i)), 0);
    }

    // restore flags
    newhdr->flags = hdr->flags;
}

_Use_decl_annotations_
void _saMerge(sahandle out, int n, sa_ref* refs, flags_t flags)
{
    int32 newsize = 0;

    out->a = NULL;

    if (n == 0)
        return;

    // merged array gets flags from first array
    SArrayHeader* fhdr = SARRAY_HDR(refs[0]);
    stype elemtype     = fhdr->elemtype;

    for (int i = 0; i < n; i++) {
        SArrayHeader* shdr = SARRAY_HDR(refs[i]);
        devAssert(stEq(fhdr->elemtype, shdr->elemtype));
        newsize += shdr->count;
    }

    _saInit(out, fhdr->elemtype, newsize, false, fhdr->flags);

    for (int i = 0; i < n; i++) {
        SArrayHeader* shdr = SARRAY_HDR(refs[i]);
        for (int32 j = 0; j < shdr->count; j++) {
            _saPushPtr(out, elemtype, stStoredPtr(shdr->elemtype, ELEMPTR(shdr, j)), flags);
        }
    }
}

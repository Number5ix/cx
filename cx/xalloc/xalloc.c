#include <cx/cx.h>
#include "xalloc_private.h"

#include <cx/debug/assert.h>
#include <cx/utils/macros.h>
#include <string.h>
#include <errno.h>

LazyInitState _xaInitState;

static void xaMimallocError(int err, void *arg)
{
    if (err == EFAULT) {
        relFatalError("Heap corruption detected");
    } else if (err == EAGAIN) {
        devFatalError("Double free detected");
    } else if (err == EINVAL) {
        devFatalError("Tried to free an invalid pointer");
    }
}

static void _xaInit(void *data)
{
    mi_register_error(xaMimallocError, NULL);

#ifndef CX_BUILDING_CXOBJGEN
    _xaInitOutput();
#endif
}

_Check_return_
_When_(flags & XA_Optional_Mask, _Must_inspect_result_ _Ret_maybenull_)
_When_(!(flags & XA_Optional_Mask), _Ret_notnull_)
_When_(!(flags & XA_Optional_Mask) && (flags & XA_Zero), _Ret_valid_)
_Post_writable_byte_size_(size)
void *_xaAlloc(size_t size, unsigned int flags)
{
    void *ret = NULL;

    lazyInit(&_xaInitState, _xaInit, NULL);

    for (int oomphase = 0, oommaxphase = _xaMaxOOMPhase(flags); oomphase <= oommaxphase; oomphase++) {
        if (oomphase > 0)
            _xaFreeUpMemory(oomphase, size);    // previous loop's allocation failed; try to free up some memory and try again

        if (flags & XA_LG_ALIGN_MASK) {
            if (flags & XA_Zero) {
                ret = mi_zalloc_aligned(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            } else {
                ret = mi_malloc_aligned(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            }
        } else {
            if (flags & XA_Zero)
                ret = mi_zalloc(size);
            else
                ret = mi_malloc(size);
        }

        if (ret)
            return ret;
    }

    // if this isn't an optional allocation, assert rather than return NULL
    _xaAllocFailure(size, flags);

    return NULL;
}

// Reallocate ptr to be at least sz byte large, copying it if necessary.
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
_At_(*ptr, _Pre_maybenull_)
_When_(!(flags & XA_Optional_Mask), _At_(*ptr, _Post_valid_))
bool _xaResize(_Inout_ void **ptr, size_t size, unsigned int flags)
{
    void *ret = NULL;

    if (!ptr)
        return false;

    lazyInit(&_xaInitState, _xaInit, NULL);

    for (int oomphase = 0, oommaxphase = _xaMaxOOMPhase(flags); oomphase <= oommaxphase; oomphase++) {
        if (oomphase > 0)
            _xaFreeUpMemory(oomphase, size);    // previous loop's allocation failed; try to free up some memory and try again

        if (flags & XA_LG_ALIGN_MASK) {
            if (flags & XA_Zero) {
                ret = mi_rezalloc_aligned(*ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            } else {
                ret = mi_realloc_aligned(*ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            }
        } else {
            if (flags & XA_Zero) {
                ret = mi_rezalloc(*ptr, size);
            } else {
                ret = mi_realloc(*ptr, size);
            }
        }

        if (ret) {
            *ptr = ret;
            return true;
        }
    }

    // if this isn't an optional allocation, assert rather than return false
    _xaAllocFailure(size, flags);

    return false;
}

// Frees the memory at ptr
void xaFree(_Pre_maybenull_ _Post_invalid_ void *ptr)
{
    lazyInit(&_xaInitState, _xaInit, NULL);
    mi_free(ptr);
}

_At_(*ptr, _Pre_maybenull_ _Post_invalid_)
bool _xaRelease(_Inout_ void **ptr)
{
    if (!ptr) return false;

    void *origptr = *ptr;
    *ptr = NULL;

    xaFree(origptr);            // origptr may be NULL but will be ignored by xaFree
    return origptr != NULL;
}

size_t xaSize(_In_ void *ptr)
{
    lazyInit(&_xaInitState, _xaInit, NULL);
    return mi_usable_size(ptr);
}

size_t xaOptSize(size_t sz)
{
    return mi_good_size(sz);
}

void xaFlush()
{
    lazyInit(&_xaInitState, _xaInit, NULL);
    mi_collect(true);
}

// instantiate these so they can be used as function pointers
extern inline void *xa_malloc(size_t size);
extern inline void *xa_calloc(size_t number, size_t size);
extern inline void *xa_realloc(void *ptr, size_t size);
extern inline void xa_free(void *ptr);
extern inline char *xa_strdup(const char *src);

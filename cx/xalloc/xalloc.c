#include <cx/cx.h>
#include "xalloc.h"

#include <mimalloc.h>
#include <cx/utils/macros.h>
#include <string.h>

void *_xaAlloc(size_t size, unsigned int flags)
{
    void *ret = NULL;

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

    if (!(flags & XA_Opt)) {
        exit(1);
    }

    return NULL;
}

// Reallocate ptr to be at least sz byte large, copying it if necessary.
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
bool _xaResize(void **ptr, size_t size, unsigned int flags)
{
    if (!ptr)
        return false;

    void *ret = NULL;
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

    if (!(flags & XA_Opt)) {
        exit(1);
    }

    return false;
}

// Frees the memory at ptr
void xaFree(void *ptr)
{
    mi_free(ptr);
}

size_t xaSize(void *ptr)
{
    return mi_usable_size(ptr);
}

size_t xaOptSize(size_t sz)
{
    return mi_good_size(sz);
}

void xaFlush()
{
    mi_collect(true);
}

// instantiate these so they can be used as function pointers
extern inline void *xa_malloc(size_t size);
extern inline void *xa_calloc(size_t number, size_t size);
extern inline void *xa_realloc(void *ptr, size_t size);
extern inline void xa_free(void *ptr);
extern inline char *xa_strdup(const char *src);

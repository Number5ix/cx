#pragma once

#include <mimalloc.h>
#include <cx/utils/macros.h>
#include <string.h>

#define XA_LG_ALIGN_MASK ((int)0x3f)

// This looks scary, but the optimizer settings on everything except for DEBUG_LEVEL=2 builds collapse
// almost all of these down into a single mi_* call depending on the flags.

// Allocate memory of at least sz
// Returns: pointer to memory
inline void *_xaAlloc(size_t size, unsigned int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XA_Zero) {
            return mi_zalloc_aligned(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        } else {
            return mi_malloc_aligned(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        }
    }

    if (flags & XA_Zero)
        return mi_zalloc(size);
    else
        return mi_malloc(size);
}
#define xaAlloc(size, ...) _xaAlloc(size, opt_flags(__VA_ARGS__))

// Reallocate ptr to be at least sz byte large, copying it if necessary.
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
inline void *_xaResize(void *ptr, size_t size, unsigned int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XA_Zero) {
            return mi_rezalloc_aligned(ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        } else {
            return mi_realloc_aligned(ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        }
    }

    if (flags & XA_Zero) {
        return mi_rezalloc(ptr, size);
    }
    return mi_realloc(ptr, size);
}
#define xaResize(ptr, size, ...) _xaResize(ptr, size, opt_flags(__VA_ARGS__))

// Tries to expand ptr to at least size, and at most size+extra.
// If it cannot be expanded without copying, returns the current size.
// Returns: size of memory at ptr
inline size_t _xaExpand(void *ptr, size_t size, size_t extra, unsigned int flags)
{
    if (flags & XA_Zero) {
        size_t oldsz = mi_usable_size(ptr);
        if (mi_expand(ptr, size)) {
            size_t newsz = mi_usable_size(ptr);
            memset((char*)ptr + oldsz, 0, newsz - oldsz);
            return newsz;
        } else {
            return oldsz;
        }
    }

    mi_expand(ptr, size);
    return mi_usable_size(ptr);
}
#define xaExpand(ptr, size, extra, ...) _xaExpand(ptr, size, extra, opt_flags(__VA_ARGS__))

// Returns: size of memory at ptr
inline size_t xaSize(void *ptr)
{
    return mi_usable_size(ptr);
}

// Frees the memory at ptr
inline void xaFree(void *ptr)
{
    mi_free(ptr);
}

// Flushes any deferred free() operations and returns as much memory to the OS as possible
inline void xaFlush()
{
    mi_collect(true);
}

#ifdef XALLOC_REMAP_MALLOC
#define malloc(sz) mi_malloc(sz)
#define calloc(num, sz) mi_calloc(num, sz)
#define free(ptr) mi_free(ptr)
#define realloc(ptr, sz) mi_realloc(ptr, sz)
#define strdup(s) cstrDup(s)
#define _strdup(s) cstrDup(s)
#define wcsdup(s) cstrDupw(s)
#define _wcsdup(s) cstrDupw(s)

#ifdef _WIN32
#undef _malloc_dbg
#define _malloc_dbg(sz, t, f, ln) mi_malloc(sz)
#undef _calloc_dbg
#define _calloc_dbg(num, sz, t, f, ln) mi_calloc(num, sz)
#undef _free_dbg
#define _free_dbg(ptr, t) mi_free(ptr)
#undef _realloc_dbg
#define _realloc_dbg(ptr, sz, t, f, ln) mi_realloc(ptr, sz)
#undef _strdup_dbg
#define _strdup_dbg(s, t, f, ln) cstrDup(s)
#undef _wcsdup_dbg
#define _wcsdup_dbg(s, t, f, ln) cstrDupw(s)
#endif
#endif

// for compatibility with 3rd party libraries only
#define xa_malloc mi_malloc
#define xa_calloc mi_calloc
#define xa_realloc mi_realloc
#define xa_free mi_free
#define xa_strdup cstrDup

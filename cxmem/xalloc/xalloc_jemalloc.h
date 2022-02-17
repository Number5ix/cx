#pragma once

#if defined(__FreeBSD__)
#include <malloc_np.h>
#define je_mallocx mallocx
#define je_rallocx rallocx
#define je_xallocx xallocx
#define je_sallocx sallocx
#define je_dallocx dallocx
#else
#include "jemalloc.h"
#endif
#include <cx/utils/macros.h>

// Allocate memory of at least sz
// Returns: pointer to memory
inline void *_xaAlloc(size_t size, int flags)
{
    return je_mallocx(size, flags);
}
#define xaAlloc(size, ...) _xaAlloc(size, func_flags(XAFUNC, __VA_ARGS__))

// Reallocate ptr to be at least sz byte large, copying it if necessary
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
inline void *_xaResize(void *ptr, size_t size, int flags)
{
    return je_rallocx(ptr, size, flags);
}
#define xaResize(ptr, size, ...) _xaResize(ptr, size, func_flags(XAFUNC, __VA_ARGS__))

// Tries to expand ptr to at least size, and at most size+extra.
// If it cannot be expanded without copying, returns the current size.
// Returns: size of memory at ptr
inline size_t _xaExpand(void *ptr, size_t size, size_t extra, int flags)
{
    return je_xallocx(ptr, size, extra, flags);
}
#define xaExpand(ptr, size, extra, ...) _xaExpand(ptr, size, extra, func_flags(XAFUNC, __VA_ARGS__))

// Returns: size of memory at ptr
inline size_t xaSize(void *ptr)
{
    return je_sallocx(ptr, 0);
}

// Frees the memory at ptr
inline void xaFree(void *ptr)
{
    je_dallocx(ptr, 0);
}

// Flushes any deferred free() operations and returns as much memory to the OS as possible
void xaFlush();

#ifdef XALLOC_REMAP_MALLOC
#if !defined(__FreeBSD__)
#define malloc(sz) je_malloc(sz)
#define calloc(num, sz) je_calloc(num, sz)
#define free(ptr) je_free(ptr)
#define realloc(ptr, sz) je_realloc(ptr, sz)
#endif
#define strdup(s) cstrDup(s)
#define _strdup(s) cstrDup(s)
#define wcsdup(s) cstrDupw(s)
#define _wcsdup(s) cstrDupw(s)

#ifdef _WIN32
#undef _malloc_dbg
#define _malloc_dbg(sz, t, f, ln) je_malloc(sz)
#undef _calloc_dbg
#define _calloc_dbg(num, sz, t, f, ln) je_calloc(num, sz)
#undef _free_dbg
#define _free_dbg(ptr, t) je_free(ptr)
#undef _realloc_dbg
#define _realloc_dbg(ptr, sz, t, f, ln) je_realloc(ptr, sz)
#undef _strdup_dbg
#define _strdup_dbg(s, t, f, ln) cstrDup(s)
#undef _wcsdup_dbg
#define _wcsdup_dbg(s, t, f, ln) cstrDupw(s)
#endif
#endif

// for compatibility with 3rd party libraries only
#if defined(__FreeBSD__)
#define xa_malloc malloc
#define xa_calloc calloc
#define xa_realloc realloc
#define xa_free free
#else
#define xa_malloc je_malloc
#define xa_calloc je_calloc
#define xa_realloc je_realloc
#define xa_free je_free
#endif
#define xa_strdup cstrDup

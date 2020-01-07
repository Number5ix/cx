#pragma once

#include <stdlib.h>
#include <string.h>

#define XA_LG_ALIGN_MASK ((int)0x3f)

// This looks scary, but the optimizer settings on everything except for DEBUG_LEVEL=2 builds collapse
// almost all of these down into a single malloc or calloc call depending on the flags.

// Allocate memory of at least sz
// Returns: pointer to memory
inline void *_xaAlloc(size_t size, int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XAFUNC_Zero) {
            void *ret = _aligned_malloc(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            memset(ret, 0, size);
            return ret;
        } else {
            return _aligned_malloc(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        }
    }

    if (flags & XAFUNC_Zero)
        return calloc(1, size);
    else
        return malloc(size);
}
#define xaAlloc(size, ...) _xaAlloc(size, func_flags(XAFUNC, __VA_ARGS__))

// Reallocate ptr to be at least sz byte large, copying it if necessary.
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
inline void *_xaResize(void *ptr, size_t size, int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XAFUNC_Zero) {
            size_t oldsz = _msize(ptr);
            void *ret = _aligned_realloc(ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            memset((char*)ret + oldsz, 0, size - oldsz);
            return ret;
        } else {
            return _aligned_realloc(ptr, size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        }
    }

    if (flags & XAFUNC_Zero) {
        size_t oldsz = _msize(ptr);
        void *ret = realloc(ptr, size);
        memset((char*)ret + oldsz, 0, size - oldsz);
        return ret;
    }
    return realloc(ptr, size);
}
#define xaResize(ptr, size, ...) _xaResize(ptr, size, func_flags(XAFUNC, __VA_ARGS__))

// Tries to expand ptr to at least size, and at most size+extra.
// If it cannot be expanded without copying, returns the current size.
// Returns: size of memory at ptr
inline size_t _xaExpand(void *ptr, size_t size, size_t extra, int flags)
{
    // MSVCRT doesn't have a reliable way to do this, so just return the current size...
    return _msize(ptr);
}
#define xaExpand(ptr, size, extra, ...) _xaExpand(ptr, size, extra, func_flags(XAFUNC, __VA_ARGS__))

// Returns: size of memory at ptr
inline size_t xaSize(void *ptr)
{
    return _msize(ptr);
}

// Frees the memory at ptr
inline void xaFree(void *ptr)
{
    free(ptr);
}

// Flushes any deferred free() operations and returns as much memory to the OS as possible
void xaFlush();

#ifdef XALLOC_REMAP_MALLOC
#define strdup(s) cstrDup(s)
#define _strdup(s) cstrDup(s)
#define wcsdup(s) cstrDupw(s)
#define _wcsdup(s) cstrDupw(s)

#undef _strdup_dbg
#define _strdup_dbg(s, t, f, ln) cstrDup(s)
#undef _wcsdup_dbg
#define _wcsdup_dbg(s, t, f, ln) cstrDupw(s)
#endif

// for compatibility with 3rd party libraries only
#define xa_malloc malloc
#define xa_calloc calloc
#define xa_realloc realloc
#define xa_free free
#define xa_strdup _strdup

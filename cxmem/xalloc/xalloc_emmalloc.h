#pragma once

#include <emscripten/emmalloc.h>
#include <cx/utils/macros.h>
#include <stdalign.h>
#include <string.h>

#define XA_LG_ALIGN_MASK ((int)0x3f)

#define XA_MALLOC_ALIGNMENT alignof(max_align_t)
#define XA_SMALLEST_ALLOCATION_SIZE (2*sizeof(void*))
#define XA_ALIGN_UP(ptr, alignment) ((unsigned char*)((((unsigned long)(ptr)) + ((alignment)-1)) & ~((alignment)-1)))
#define XA_BUCKET_SIZE(size) (size > XA_SMALLEST_ALLOCATION_SIZE ? (size_t)XA_ALIGN_UP(size, sizeof(void*)) : XA_SMALLEST_ALLOCATION_SIZE)

// This looks scary, but the optimizer settings on everything except for DEBUG_LEVEL=2 builds collapse
// almost all of these down into a single mi_* call depending on the flags.

// Allocate memory of at least sz
// Returns: pointer to memory
inline void *_xaAlloc(size_t size, unsigned int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XA_Zero) {
            void *ret = emmalloc_memalign(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
            memset(ret, 0, XA_BUCKET_SIZE(size));
            return ret;
        } else {
            return emmalloc_memalign(size, (size_t)1 << (flags & XA_LG_ALIGN_MASK));
        }
    }

    if (flags & XA_Zero) {
        void *ret = emmalloc_memalign(XA_MALLOC_ALIGNMENT, size);
        memset(ret, 0, XA_BUCKET_SIZE(size));
        return ret;
    } else
        return emmalloc_memalign(XA_MALLOC_ALIGNMENT, size);
}
#define xaAlloc(size, ...) _xaAlloc(size, opt_flags(__VA_ARGS__))

// Reallocate ptr to be at least sz byte large, copying it if necessary.
// NOTE: Unlike realloc, ptr cannot be NULL!
// Returns: pointer to memory
inline void *_xaResize(void *ptr, size_t size, unsigned int flags)
{
    if (flags & XA_LG_ALIGN_MASK) {
        if (flags & XA_Zero) {
            size_t oldsz = emmalloc_usable_size(ptr);
            void *ret = emmalloc_aligned_realloc(ptr, (size_t)1 << (flags & XA_LG_ALIGN_MASK), size);
            memset((char*)ret + oldsz, 0, XA_BUCKET_SIZE(size) - oldsz);
            return ret;
        } else {
            return emmalloc_aligned_realloc(ptr, (size_t)1 << (flags & XA_LG_ALIGN_MASK), size);
        }
    }

    if (flags & XA_Zero) {
        size_t oldsz = emmalloc_usable_size(ptr);
        void *ret = emmalloc_realloc(ptr, size);
        memset((char*)ret + oldsz, 0, XA_BUCKET_SIZE(size) - oldsz);
        return ret;
    }
    return emmalloc_realloc(ptr, size);
}
#define xaResize(ptr, size, ...) _xaResize(ptr, size, opt_flags(__VA_ARGS__))

// Tries to expand ptr to at least size, and at most size+extra.
// If it cannot be expanded without copying, returns the current size.
// Returns: size of memory at ptr
inline size_t _xaExpand(void *ptr, size_t size, size_t extra, unsigned int flags)
{
    if (flags & XA_Zero) {
        size_t oldsz = mi_usable_size(ptr);
        if (emmalloc_realloc_try(ptr, size)) {
            size_t newsz = XA_BUCKET_SIZE(ptr);
            memset((char*)ptr + oldsz, 0, newsz - oldsz);
            return newsz;
        } else {
            return oldsz;
        }
    }

    emmalloc_realloc_try(ptr, size);
    return emmalloc_usable_size(ptr);
}
#define xaExpand(ptr, size, extra, ...) _xaExpand(ptr, size, extra, opt_flags(__VA_ARGS__))

// Returns: size of memory at ptr
inline size_t xaSize(void *ptr)
{
    return emmalloc_usable_size(ptr);
}

// Frees the memory at ptr
inline void xaFree(void *ptr)
{
    emmalloc_free(ptr);
}

// Flushes any deferred free() operations and returns as much memory as possible.
// Note that on WebAssembly this only returns it to the sbrk() heap, not the OS.
inline void xaFlush()
{
    emmalloc_trim(0);
}

#ifdef XALLOC_REMAP_MALLOC
#define malloc(sz) emmalloc_malloc(sz)
#define calloc(num, sz) emmalloc_calloc(num, sz)
#define free(ptr) emmalloc_free(ptr)
#define realloc(ptr, sz) emmalloc_realloc(ptr, sz)
#define strdup(s) cstrDup(s)
#define _strdup(s) cstrDup(s)
#define wcsdup(s) cstrDupw(s)
#define _wcsdup(s) cstrDupw(s)

#ifdef _WIN32
#undef _malloc_dbg
#define _malloc_dbg(sz, t, f, ln) emmalloc_malloc(sz)
#undef _calloc_dbg
#define _calloc_dbg(num, sz, t, f, ln) emmalloc_calloc(num, sz)
#undef _free_dbg
#define _free_dbg(ptr, t) emmalloc_free(ptr)
#undef _realloc_dbg
#define _realloc_dbg(ptr, sz, t, f, ln) emmalloc_realloc(ptr, sz)
#undef _strdup_dbg
#define _strdup_dbg(s, t, f, ln) cstrDup(s)
#undef _wcsdup_dbg
#define _wcsdup_dbg(s, t, f, ln) cstrDupw(s)
#endif
#endif

// for compatibility with 3rd party libraries only
#define xa_malloc emmalloc_malloc
#define xa_calloc emmalloc_calloc
#define xa_realloc emmalloc_realloc
#define xa_free emmalloc_free
#define xa_strdup cstrDup

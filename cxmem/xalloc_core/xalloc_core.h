#pragma once

#include <cx/platform/cpp.h>
#include <cx/utils/macros/optarg.h>
#include <cx/utils/macros/unused.h>
#include <stdbool.h>
#include <stddef.h>

/* XAlloc-Core: The Big Four(TM) allocation functions. Isolated in a standalone library
 * to ease integration of third-party libraries and avoid dependency loops. The rest of
 * XAlloc is located in the main cx library. */

#ifdef XALLOC_REMAP_MALLOC
// these need to be pulled in first so our #defines don't cause chaos when they
// are included
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// never use this; we have heap profilers these days
#undef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#endif

#define XA_LG_ALIGN_MASK ((unsigned int)0x3f)

// Align to a 2^exp byte boundary.
// exp may be 1-63
#define XA_Align(exp) ((unsigned int)(exp & XA_LG_ALIGN_MASK))
// Zero-fills returned memory
#define XA_Zero ((unsigned int)0x40)
// Allows the function to fail and return NULL rather than triggering an out-of-memory assertion.
// Recommended but not required if using XA_Align
#define XA_Opt ((unsigned int)0x80)

CX_C_BEGIN

// void xaAlloc(size_t size, [flags])
// Allocate memory of at least sz
// Flags include XA_Align(pow), XA_Zero, XA_Opt
// Returns: pointer to memory
#define xaAlloc(size, ...) _xaAlloc(size, opt_flags(__VA_ARGS__))
void *_xaAlloc(size_t size, unsigned int flags);

// Reallocate ptr to be at least sz bytes large, copying it if necessary.
// If ptr points to NULL, allocates new memory.
// Returns: True if successfully resized and ptr updated.
// If XA_Opt is set, returns false on failure and ptr is unchanged.
bool _xaResize(void **ptr, size_t size, unsigned int flags);
#ifndef _MSC_VER
// &* is a safe way to ensure the operand is a pointer, even a pointer to void
#define _xa_ptr_ptr_verify(ptr) unused_noeval(&*(*(ptr)))
#else
// &* doesn't work for void* on MSVC, check for void* conversion instead, but this results
// in a false negative for intptr_t* or pointer-sized ints
#define _xa_ptr_ptr_verify(ptr) unused_noeval((void*)(*(ptr)))
#endif
#define xaResize(ptr, size, ...) (_xa_ptr_ptr_verify(ptr), _xaResize((void**)(ptr), size, opt_flags(__VA_ARGS__)))

// Frees the memory at ptr
// Does nothing if ptr is NULL
void xaFree(void *ptr);

CX_C_END

// String utilities because cstrDup is tied in with xalloc
// and there's no better place for them
#include <cxmem/cstrutil/cstrutil.h>

// Safe free
// TODO: Remove this!
#define xaSFree(ptr) if (ptr) { xaFree((void*)ptr); (ptr) = NULL; }

#ifdef XALLOC_REMAP_MALLOC
#define malloc(sz) xa_malloc(sz)
#define calloc(num, sz) xa_calloc(num, sz)
#define free(ptr) xa_free(ptr)
#define realloc(ptr, sz) xa_realloc(ptr, sz)
#define strdup(s) cstrDup(s)
#define _strdup(s) cstrDup(s)
#define wcsdup(s) cstrDupw(s)
#define _wcsdup(s) cstrDupw(s)

#ifdef _WIN32
#undef _malloc_dbg
#define _malloc_dbg(sz, t, f, ln) xa_malloc(sz)
#undef _calloc_dbg
#define _calloc_dbg(num, sz, t, f, ln) xa_calloc(num, sz)
#undef _free_dbg
#define _free_dbg(ptr, t) xa_free(ptr)
#undef _realloc_dbg
#define _realloc_dbg(ptr, sz, t, f, ln) xa_realloc(ptr, sz)
#undef _strdup_dbg
#define _strdup_dbg(s, t, f, ln) cstrDup(s)
#undef _wcsdup_dbg
#define _wcsdup_dbg(s, t, f, ln) cstrDupw(s)
#endif
#endif

// for compatibility with 3rd party libraries only
inline void *xa_malloc(size_t size)
{
    return _xaAlloc(size, XA_Opt);
}

inline void *xa_calloc(size_t number, size_t size)
{
    return _xaAlloc(number * size, XA_Zero | XA_Opt);
}

inline void *xa_realloc(void *ptr, size_t size)
{
    if (_xaResize(&ptr, size, XA_Opt))
        return ptr;
    return (void*)0;
}

inline void xa_free(void *ptr)
{
    xaFree(ptr);
}

inline char *xa_strdup(const char *src)
{
    return cstrDup(src);
}

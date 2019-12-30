#include <cx/cx.h>
#include <cxmem/xalloc/xalloc.h>

#ifdef XALLOC_USE_MSVCRT

#ifdef _DEBUG
#include <crtdbg.h>
#endif

extern const int xalloc_msvcrt_debug;

extern inline void *xaAlloc(size_t size, int flags);
extern inline void *xaResize(void *ptr, size_t size, int flags);
extern inline size_t xaExpand(void *ptr, size_t size, size_t extra, int flags);
extern inline size_t xaSize(void *ptr);
extern inline void xaFree(void *ptr);

void xaFlush()
{
    // not possible with msvcrt? :(
}

#endif

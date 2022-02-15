#include <cx/cx.h>
#include <cxmem/xalloc/xalloc.h>

#ifdef XALLOC_USE_MIMALLOC

extern inline void *xaAlloc(size_t size, unsigned int flags);
extern inline void *xaResize(void *ptr, size_t size, unsigned int flags);
extern inline size_t xaExpand(void *ptr, size_t size, size_t extra, unsigned int flags);
extern inline size_t xaSize(void *ptr);
extern inline void xaFree(void *ptr);
extern inline void xaFlush();

#endif

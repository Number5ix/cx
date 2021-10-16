#include <cx/cx.h>
#include <cxmem/xalloc/xalloc.h>

#ifdef XALLOC_USE_MIMALLOC

extern inline void *_xaAlloc(size_t size, int flags);
extern inline void *_xaResize(void *ptr, size_t size, int flags);
extern inline size_t _xaExpand(void *ptr, size_t size, size_t extra, int flags);
extern inline size_t xaSize(void *ptr);
extern inline void xaFree(void *ptr);
extern inline void xaFlush();

#endif

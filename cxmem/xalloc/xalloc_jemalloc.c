#include <cx/cx.h>
#include <cxmem/xalloc/xalloc.h>

#ifdef XALLOC_USE_JEMALLOC

#if defined(__FreeBSD__)
#define je_mallctl mallctl
#define je_mallctlnametomib mallctlnametomib
#define je_mallctlbymib mallctlbymib
#endif

extern inline void *xaAlloc(size_t size, unsigned int flags);
extern inline void *xaResize(void *ptr, size_t size, unsigned int flags);
extern inline size_t xaExpand(void *ptr, size_t size, size_t extra, unsigned int flags);
extern inline size_t xaSize(void *ptr);
extern inline void xaFree(void *ptr);

void xaFlush()
{
    static size_t purgemib[3];
    if (!purgemib[0]) {
        // technically a race, but a harmless one
        unsigned narenas;
        size_t narenas_sz = sizeof(narenas);
        size_t mib_sz = 3;
        je_mallctl("arenas.narenas", &narenas, &narenas_sz, NULL, 0);

        je_mallctlnametomib("arena.0.purge", purgemib, &mib_sz);
        purgemib[1] = narenas;      // replace the 0 with arenas.narenas
    }
    je_mallctlbymib(purgemib, 3, NULL, NULL, NULL, 0);
}

#endif

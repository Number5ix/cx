#include "cx/platform/os.h"
#include "cx/utils/lazyinit.h"
#include <pthread.h>

#if defined(_PLATFORM_FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_PLATFORM_LINUX)
#include <unistd.h>
#endif

static LazyInitState coreCache;
static int ncores;
static int nlogical;

void osYield()
{
    pthread_yield();
}

static void initCoreCache(void *dummy)
{
#ifdef _PLATFORM_FREEBSD
    int mib[2];
    size_t len = sizeof(nlogical);

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;

    sysctl(mib, 2, &nlogical, &len, NULL, 0);

    ncores = nlogical;      // TODO: figure out a way to get physical core count that
                            // doesn't involve parsing XML...
#elif _PLATFORM_LINUX
    nlogical = sysconf(_SC_NPROCESSORS_ONLN);

    ncores = nlogical;      // TODO: get physical core count
#endif

    // fallback to prevent bad things like dividing by zero
    if (nlogical < 1)
        nlogical = 1;
    if (ncores < 1)
        ncores = 1;
}

int osPhysicalCPUs()
{
    lazyInit(&coreCache, initCoreCache, NULL);
    return ncores;
}

int osLogicalCPUs()
{
    lazyInit(&coreCache, initCoreCache, NULL);
    return nlogical;
}

#include "net_private.h"

LazyInitState netInit_done;

void netInit(void* unused)
{
    netPlatformInit();
}

NetQueue* netqueueCreate(int32 nthreads, flags_t flags)
{
    lazyInit(&netInit_done, netInit, NULL);
    return netPlatformCreateQueue(nthreads, flags);
}
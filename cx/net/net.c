#include "net_private.h"
#include "net.h"
#include <cx/log.h>

LazyInitState netInit_done;

LogChannel* NetLogChannel;

void netInit(void* unused)
{
    NetLogChannel = logChan(_SL("cx/net"));
    netPlatformInit();
}

_Use_decl_annotations_
NetQueue* netqueueCreate(const NetQueueConfig* conf)
{
    lazyInit(&netInit_done, netInit, NULL);
    return netPlatformCreateQueue(conf);
}

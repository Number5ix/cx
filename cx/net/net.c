#include "net_private.h"
#include "net.h"
#include <cx/log.h>

LazyInitState _netInit_done;

LogChannel* NetLogChannel;

void _netInit(void* unused)
{
    NetLogChannel = logChan(_SL("cx/net"));
    netPlatformInit();
}

_Use_decl_annotations_
NetQueue* netqueueCreate(const NetQueueConfig* conf)
{
    lazyInit(&_netInit_done, _netInit, NULL);
    return netPlatformCreateQueue(conf);
}

#include "net_private.h"
#include "net.h"
#include <cx/log.h>

LazyInitState netInit_done;

LogCategory* NetLogCategory;

void netInit(void* unused)
{
    NetLogCategory = logCreateCat(_SL("Network"), false);
    netPlatformInit();
}

_Use_decl_annotations_
NetQueue* netqueueCreate(const NetQueueConfig* conf)
{
    lazyInit(&netInit_done, netInit, NULL);
    return netPlatformCreateQueue(conf);
}

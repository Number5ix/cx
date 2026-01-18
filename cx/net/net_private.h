#pragma once

#include "addr.h"
#include "net/queue.h"
#include "net/socket.h"
#include <cx/utils/lazyinit.h>

extern LazyInitState netInit_done;
void netInit(void* unused);

bool netPlatformInit(void);
NetQueue* netPlatformCreateQueue(int32 nthreads, flags_t flags);
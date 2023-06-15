#pragma once

#include "thread.h"

void _thrDestroy(Thread *thread);

Thread *_thrPlatformCreate();
bool _thrPlatformStart(Thread *thread);
bool _thrPlatformWait(Thread *thread, int64 timeout);

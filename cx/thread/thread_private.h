#pragma once

#include "thread.h"

void _thrDestroy(Thread *thread);

Thread *_thrPlatformAlloc();
bool _thrPlatformStart(Thread *thread);
bool _thrPlatformKill(Thread *thread);
void _thrPlatformDestroy(Thread *thread);
bool _thrPlatformWait(Thread *thread, int64 timeout);

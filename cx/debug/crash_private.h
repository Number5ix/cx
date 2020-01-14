#pragma once

#include "cx/debug/crash.h"
#include "cx/thread/atomic.h"
#include "cx/thread/mutex.h"
#include "cx/container.h"
#include "cx/utils.h"

typedef struct CrashExtraMeta {
    const char *name;
    const char *str;
    int val;
    bool version;
} CrashExtraMeta;

typedef struct CrashMemRange {
    uintptr start;
    uintptr end;
} CrashMemRange;

extern LazyInitState _dbgCrashInitState;
extern atomic_uint32 _dbgCrashMode;
extern CrashExtraMeta *_dbgCrashExtraMeta;
extern CrashMemRange *_dbgCrashDumpMem;
extern Mutex *_dbgCrashMutex;

void _dbgCrashInit(void *data);
bool _dbgCrashPlatformInit();
bool _dbgCrashTriggerCallbacks(bool after);

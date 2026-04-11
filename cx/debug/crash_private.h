#pragma once

#include "cx/container.h"
#include "cx/debug/crash.h"
#include "cx/thread/atomic.h"
#include "cx/thread/mutex.h"
#include "cx/utils.h"

typedef struct CrashExtraMeta {
    const char* name;
    char* str;
    int val;
    bool version;
} CrashExtraMeta;
stDeclare(CrashExtraMeta);
saDeclare(CrashExtraMeta);
#define SType_CrashExtraMeta                         CrashExtraMeta*
#define STStorageType_CrashExtraMeta                 CrashExtraMeta
#define STypeArg_CrashExtraMeta(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_CrashExtraMeta(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_CrashExtraMeta(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_CrashExtraMeta(type, val) stType(type), stArgPtr(type, val)

typedef struct CrashMemRange {
    uintptr start;
    uintptr end;
} CrashMemRange;
stDeclare(CrashMemRange);
saDeclare(CrashMemRange);
#define SType_CrashMemRange                         CrashMemRange*
#define STStorageType_CrashMemRange                 CrashMemRange
#define STypeArg_CrashMemRange(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_CrashMemRange(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_CrashMemRange(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_CrashMemRange(type, val) stType(type), stArgPtr(type, val)

extern LazyInitState _dbgCrashInitState;
extern atomic(uint32) _dbgCrashMode;
extern sa_CrashExtraMeta _dbgCrashExtraMeta;
extern sa_CrashMemRange _dbgCrashDumpMem;
extern Mutex _dbgCrashMutex;

void _dbgCrashInit(void* data);
bool _dbgCrashPlatformInit();
bool _dbgCrashTriggerCallbacks(bool after);

#pragma once

#include <cx/container.h>
#include <cx/thread.h>
#include <cx/utils/lazyinit.h>
#include "log.h"

#define LOG_INITIAL_QUEUE_SIZE 32
#define LOG_MAX_QUEUE_SIZE     262144

extern atomic(bool) _log_running;
extern Mutex _log_run_lock;
extern Thread* _log_thread;

typedef struct LogDest {
    LogChannel* chanfilter;
    LogDestMsg msgfunc;
    LogDestBatchDone batchfunc;
    LogDestClose closefunc;
    void* userdata;
    int maxlevel;
} LogDest;
saDeclarePtr(LogDest);

typedef struct LogEntry LogEntry;
typedef struct LogEntry {
    LogEntry* _next;   // chain for log batches, internal use only
    int64 timestamp;
    LogChannel* chan;
    string msg;
    int level;
} LogEntry;
saDeclarePtr(LogEntry);

// cached for performance, can safely be non-atomic
extern int _log_max_level;

extern PrQueue _log_queue;

// protects _log_dests and _log_channels (structures that are accessed from the log thread)
extern Mutex _log_op_lock;
extern sa_LogDest _log_dests;
extern hashtable _log_channels;

extern LazyInitState _logInitState;

void logCheckInit(void);
void logDestroyEnt(_In_ LogEntry* ent);
void logQueueAdd(_In_ LogEntry* ent);
void logThreadCreate(void);

// does NOT free dhandle, caller is responsible for that!
bool logUnregisterDestLocked(_In_ LogDest* dhandle);

_meta_inline bool applyChanFilter(_In_opt_ LogChannel* filterchan, _In_ LogChannel* testchan)
{
    if (!filterchan) {
        // no filter, we want all channels except for private channels
        return !testchan || !testchan->priv;
    }

    return filterchan == testchan;
}

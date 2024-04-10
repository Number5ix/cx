#pragma once

#include "log.h"
#include <cx/thread.h>
#include <cx/utils/lazyinit.h>

#define LOG_INITIAL_QUEUE_SIZE 32
#define LOG_MAX_QUEUE_SIZE 262144

extern Thread *_log_thread;

typedef struct LogDest {
    LogCategory *catfilter;
    LogDestFunc func;
    void *userdata;
    int maxlevel;
} LogDest;
saDeclarePtr(LogDest);

typedef struct LogEntry LogEntry;
typedef struct LogEntry {
    LogEntry *_next;        // chain for log batches, internal use only
    int64 timestamp;
    LogCategory *cat;
    string msg;
    int level;
} LogEntry;
saDeclarePtr(LogEntry);

// cached for performance, can safely be non-atomic
extern int _log_max_level;

extern PrQueue _log_queue;

extern Mutex _log_dests_lock;
extern sa_LogDest _log_dests;

extern LazyInitState _logInitState;

void logCheckInit(void);
void logDestroyEnt(_In_ LogEntry *ent);
void logQueueAdd(_In_ LogEntry *ent);
void logThreadCreate(void);
bool logUnregisterDestLocked(_In_ LogDest *dhandle);

_meta_inline bool applyCatFilter(_In_opt_ LogCategory *filtercat, _In_ LogCategory *testcat)
{
    if (!filtercat) {
        // no filter, we want all categories except for private categories
        return !testcat || !testcat->priv;
    }

    return filtercat == testcat;
}

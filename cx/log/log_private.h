#pragma once

#include "log.h"
#include <cx/thread.h>
#include <cx/utils/lazyinit.h>

#define LOG_INITIAL_BUFFER_SIZE 32

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

saDeclareType(atomicptr, atomic(ptr));
extern RWLock _log_buffer_lock;             // used for expanding the buffer
extern sa_atomicptr _log_buffer;            // ring buffer
extern atomic(int32) _log_buf_readptr;
extern atomic(int32) _log_buf_writeptr;

extern Mutex _log_dests_lock;
extern sa_LogDest _log_dests;

extern LazyInitState _logInitState;

void logCheckInit(void);
void logDestroyEnt(_In_ LogEntry *ent);
void logBufferAdd(_In_ LogEntry *ent);
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

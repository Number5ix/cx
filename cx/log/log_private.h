#pragma once

#include "log.h"
#include <cx/thread.h>

#define LOG_INITIAL_BUFFER_SIZE 32

extern Thread *_log_thread;
extern Event *_log_event;

typedef struct LogDest {
    LogCategory *catfilter;
    LogDestFunc func;
    void *userdata;
    int maxlevel;
} LogDest;
saDeclarePtr(LogDest);

typedef struct LogEntry {
    int64 timestamp;
    LogCategory *cat;
    string msg;
    int level;
} LogEntry;
saDeclarePtr(LogEntry);

extern _Thread_local sa_LogEntry _log_thread_batch;

// cached for performance, can safely be non-atomic
extern int _log_max_level;

saDeclareType(atomicptr, atomic(ptr));
extern RWLock _log_buffer_lock;            // used for expanding the buffer
extern sa_atomicptr _log_buffer;            // ring buffer
extern atomic(int32) _log_buf_readptr;
extern atomic(int32) _log_buf_writeptr;

extern Mutex _log_dests_lock;
extern sa_LogDest _log_dests;

void logCheckInit(void);
void logDestroyEnt(LogEntry *ent);
void logBufferAdd(LogEntry *ent);
void logBufferAddBatch(sa_LogEntry batch);
void logThreadCreate(void);

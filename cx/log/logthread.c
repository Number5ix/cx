#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/platform/os.h>

Thread *_log_thread;
Event *_log_event;
_Thread_local sa_LogEntry _log_thread_batch;

static int logthread_func(Thread *self)
{
    // wait for systhread registration to complete
    while (!_log_event)
        osYield();

    while (!atomicLoad(bool, &self->requestExit, Acquire)) {
        mutexAcquire(&_log_dests_lock);
        rwlockAcquireRead(&_log_buffer_lock);

        int32 bsize = saSize(_log_buffer);      // size cannot change while read lock is held
        int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Relaxed);
        int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);

        // step through available log entries
        while (rdptr != wrptr) {
            LogEntry *ent = atomicLoad(ptr, &_log_buffer.a[rdptr], Acquire);
            devAssert(ent);
            if (!ent)
                continue;

            foreach(sarray, idx, LogDest*, dest, _log_dests) {
                if (ent->level <= dest->maxlevel && (!dest->catfilter || dest->catfilter == ent->cat)) {
                    // dispatch to log destination
                    dest->func(ent->level, ent->cat, ent->timestamp, ent->msg, dest->userdata);
                }
            } endforeach;

            atomicStore(ptr, &_log_buffer.a[rdptr], NULL, Release);
            logDestroyEnt(ent);
            rdptr = (rdptr + 1) % bsize;
        }
        atomicStore(int32, &_log_buf_readptr, rdptr, Release);

        rwlockReleaseRead(&_log_buffer_lock);
        mutexRelease(&_log_dests_lock);
        eventWait(_log_event);
    }

    return 0;
}

void logThreadCreate(void)
{
    devAssert(!_log_thread);
    _log_thread = thrCreate(logthread_func, stvNone);
    thrRegisterSysThread(_log_thread, &_log_event);
}

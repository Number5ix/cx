// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/platform/os.h>

Thread *_log_thread;
Event *_log_event;
static Event _log_done_event;

static int logthread_func(Thread *self)
{
    sa_LogEntry ents;

    // wait for systhread registration to complete
    while (!_log_event)
        osYield();

    saInit(&ents, ptr, 16);

    while (!atomicLoad(bool, &self->requestExit, Acquire)) {
        rwlockAcquireRead(&_log_buffer_lock);
        int32 bsize = saSize(_log_buffer);      // size cannot change while read lock is held
        int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);
        int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Acquire);

        // grab available log entries
        while (rdptr != wrptr) {
            LogEntry *ent = atomicLoad(ptr, &_log_buffer.a[rdptr], Acquire);
            devAssert(ent);

            if (ent) {
                saPush(&ents, ptr, ent);
                atomicStore(ptr, &_log_buffer.a[rdptr], NULL, Release);
            }
            rdptr = (rdptr + 1) % bsize;
        }
        atomicStore(int32, &_log_buf_readptr, rdptr, Release);
        rwlockReleaseRead(&_log_buffer_lock);

        // now that we have a bunch of log entries and batches, process them
        mutexAcquire(&_log_dests_lock);
        foreach(sarray, ent__idx, LogEntry*, ent, ents) {
            while (ent) {
                LogEntry *next = ent->_next;
                foreach(sarray, dest_idx, LogDest *, dest, _log_dests) {
                    if (ent->level <= dest->maxlevel && (!dest->catfilter || dest->catfilter == ent->cat)) {
                        // dispatch to log destination
                        dest->func(ent->level, ent->cat, ent->timestamp, ent->msg, dest->userdata);
                    }
                } endforeach;
                logDestroyEnt(ent);
                ent = next;         // process the rest of the batch
            }
        } endforeach;
        mutexRelease(&_log_dests_lock);

        saClear(&ents);

        eventSignal(&_log_done_event);
        eventWait(_log_event);
    }

    return 0;
}

void logThreadCreate(void)
{
    devAssert(!_log_thread);
    eventInit(&_log_done_event);
    _log_thread = thrCreate(logthread_func, _S"CX Log Writer", stvNone);
    thrRegisterSysThread(_log_thread, &_log_event);
}

void logFlush(void)
{
    for(;;) {
        // acquire destination lock, then buffer write lock to ensure that rdptr
        // reflects log entries that have been fully written
        mutexAcquire(&_log_dests_lock);
        mutexRelease(&_log_dests_lock);
        rwlockAcquireWrite(&_log_buffer_lock);
        int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Relaxed);
        int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);
        rwlockReleaseWrite(&_log_buffer_lock);

        // nothing to process, all done
        if (rdptr == wrptr)
            break;

        eventReset(&_log_done_event);
        eventSignal(_log_event);
        eventWait(&_log_done_event);
    }
}

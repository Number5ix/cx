// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>

RWLock _log_buffer_lock;
sa_atomicptr _log_buffer;
atomic(int32) _log_buf_readptr;
atomic(int32) _log_buf_writeptr;

// MUST be called with _log_buffer_lock held in read mode
static void logBufferGrow(int32 minsize)
{
    rwlockReleaseRead(&_log_buffer_lock);
    rwlockAcquireWrite(&_log_buffer_lock);

    int32 bsize = saSize(_log_buffer);
    int32 nsize = bsize;
    while (nsize < minsize)
        nsize += nsize >> 1;            // grow by 50%

    sa_atomicptr newbuf;
    saInit(&newbuf, ptr, nsize);
    saSetSize(&newbuf, nsize);

    // these are guaranteed to not change while we hold the write lock
    int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Relaxed);
    int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Relaxed);
    int32 nents = 0;

    // copy buffer contents
    while (rdptr != wrptr) {
        atomicStore(ptr, &newbuf.a[nents], atomicLoad(ptr, &_log_buffer.a[rdptr], Relaxed), Relaxed);
        rdptr = (rdptr + 1) % bsize;
        nents++;
    }

    // swap out buffers
    saDestroy(&_log_buffer);
    _log_buffer = newbuf;

    // read pointer goes to start
    atomicStore(int32, &_log_buf_readptr, 0, Release);
    atomicStore(int32, &_log_buf_writeptr, nents, Release);

    rwlockReleaseWrite(&_log_buffer_lock);
    rwlockAcquireRead(&_log_buffer_lock);
}

// these should almost never block, the only time that happens is if
// the buffer is about to overflow and needs to be expanded
void logBufferAdd(LogEntry *ent)
{
    bool ret;

    rwlockAcquireRead(&_log_buffer_lock);
    for (;;) {
        int32 bsize = saSize(_log_buffer);
        int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Acquire);
        int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);

        // calculate next write pointer position
        int nwrptr = (wrptr + 1) % bsize;
        if (nwrptr == rdptr) {
            // ringbuffer is full, need to expand it
            logBufferGrow(bsize + 1);
            continue;
        }

        // try to fill in the next slot
        void *empty = NULL;
        if (!atomicCompareExchange(ptr, strong, &_log_buffer.a[wrptr], &empty, ent, AcqRel, Relaxed))
            continue;

        // we succeeded, should be impossible to get a conflict on the write pointer but be paranoid in dev mode
        ret = atomicCompareExchange(int32, strong, &_log_buf_writeptr, &wrptr, nwrptr, AcqRel, Relaxed);
        devAssert(ret);
        break;
    }
    eventSignal(_log_event);
    rwlockReleaseRead(&_log_buffer_lock);
}

void logBufferAddBatch(sa_LogEntry batch)
{
    bool ret;

    rwlockAcquireRead(&_log_buffer_lock);
    for (;;) {
        int32 bsize = saSize(_log_buffer);
        int32 rdptr = atomicLoad(int32, &_log_buf_readptr, Acquire);
        int32 wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);

        // calculate how much room is left in the buffer
        int32 navail;
        if (wrptr >= rdptr)
            navail = bsize - wrptr + rdptr;
        else
            navail = rdptr - wrptr;

        // make sure there's enough room for the whole batch,
        // +1 because we have to leave one open slot always to keep
        // rdptr == wrptr from being ambiguous
        if (navail < saSize(batch) + 1) {
            logBufferGrow(bsize + saSize(batch) + 1 - navail);
            continue;
        }

        // try to fill in the slots
        int32 cwrptr = wrptr;
        bool fail = false;
        foreach(sarray, idx, LogEntry*, ent, batch) {
            void *empty = NULL;
            if (!atomicCompareExchange(ptr, strong, &_log_buffer.a[cwrptr], &empty, ent, AcqRel, Relaxed)) {
                // once we get past the first entry, it *should* be impossible for another thread to claim one
                // until we update the write pointer
                devAssert(idx == 0);
                fail = true;
                break;
            }
            cwrptr = (cwrptr + 1) % bsize;
        } endforeach;

        if (fail)
            continue;

        // everything succeeded, update the write pointer
        int nwrptr = (wrptr + saSize(batch)) % bsize;
        ret = atomicCompareExchange(int32, strong, &_log_buf_writeptr, &wrptr, nwrptr, AcqRel, Relaxed);
        devAssert(ret);
        break;
    }
    eventSignal(_log_event);
    rwlockReleaseRead(&_log_buffer_lock);
}

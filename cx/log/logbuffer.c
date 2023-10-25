// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/utils/compare.h>
#include <cx/time/clock.h>
#include <cx/platform/os.h>

#define LOG_BUFFER_MAXENT       262144
#define OVERFLOW_BATCH_MAXENT   65536

// try to keep this low to avoid stalling too much when there's contention
#define MAX_CAS_TRIES           30

RWLock _log_buffer_lock;
sa_atomicptr _log_buffer;
atomic(int32) _log_buf_readptr;
atomic(int32) _log_buf_writeptr;

// per-thread overflow log batch if the main buffer fills up or is under
// high contention
typedef struct LogOverflowTLS {
    LogEntry *head;
    LogEntry *tail;
    int count;
    bool lost;
} LogOverflowTLS;
static _Thread_local LogOverflowTLS _log_overflow;

// MUST be called with _log_buffer_lock held in read mode
static bool logBufferGrow(int32 minsize)
{
    if (!_log_buffer.a)
        return false;

    bool ret = true;
    rwlockReleaseRead(&_log_buffer_lock);
    rwlockAcquireWrite(&_log_buffer_lock);

    int32 bsize = saSize(_log_buffer);
    int32 nsize = bsize;
    while (nsize < minsize)
        nsize += nsize >> 1;            // grow by 50%
    nsize = clamphigh(nsize, LOG_BUFFER_MAXENT);

    // another thread may have grown the buffer when we released the read lock
    if (bsize >= nsize)
        goto out;

    sa_atomicptr newbuf;
    if (!saTryInit(&newbuf, ptr, nsize)) {
        ret = false;
        goto out;           // out of memory, stuff the log entry in overflow for now
    }

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

out:
    rwlockDowngradeWrite(&_log_buffer_lock);
    return ret;
}

static bool logBufferAddInternal(_In_ LogEntry *ent)
{
    int32 bsize, rdptr, wrptr, nwrptr;
    void *empty;
    int nfail = 0;
    bool ret = false;

    if (!ent)
        return true;
    if (!_log_buffer.a)
        return false;

    rwlockAcquireRead(&_log_buffer_lock);

retry:
    if (nfail > MAX_CAS_TRIES)
        goto out;           // give up, it'll have to go to overflow

    bsize = saSize(_log_buffer);
    rdptr = atomicLoad(int32, &_log_buf_readptr, Acquire);
    wrptr = atomicLoad(int32, &_log_buf_writeptr, Acquire);

    // calculate next write pointer position
    nwrptr = (wrptr + 1) % bsize;
    if (nwrptr == rdptr) {
        // ringbuffer is full, need to expand it
        if (bsize >= LOG_BUFFER_MAXENT || !logBufferGrow(bsize + 1))
            goto out;           // can't expand further, they'll have to go to overflow
        goto retry;
    }

    // fill in the next slot, this is where the real race happens
    empty = NULL;
    if (!atomicCompareExchange(ptr, weak, &_log_buffer.a[wrptr], &empty, ent, AcqRel, Acquire)) {
        osYield();
        nfail++;
        goto retry;
    }

    // update the write pointer once we've committed the log entry, otherwise no other thread
    // can write to the buffer due to _log_buffer.a[wrptr] being non-NULL. See comments below for why
    // this needs to be another CAS and not a simple store.
    for(;;) {
        int32 oldwrptr = wrptr;
        if (atomicCompareExchange(int32, weak, &_log_buf_writeptr, &oldwrptr, nwrptr, AcqRel, Release))
            break;
        if (oldwrptr != wrptr) {
            // In rare cases under high contention, it's possible for a long context switch to stall
            // this thead in just the wrong spot to cause us to end up with a stale wrptr that has
            // been passed up by *rdptr*. In that case the buffer will look empty because the reader
            // thread has already cleared it out and the initial CAS will succeed, but when we try to
            // update wrptr we find that another thread has already advanced it.
            //
            // To handle this correctly we need to back out the incorrect buffer entry so that it
            // doesn't block another thread and then retry.
            atomicStore(ptr, &_log_buffer.a[wrptr], NULL, Release);
            nfail++;
            goto retry;
        }
    }
    if (_log_thread)
        eventSignal(&_log_thread->notify);
    ret = true;

out:
    rwlockReleaseRead(&_log_buffer_lock);

    return ret;
}

static void logBufferOverflow(_In_ LogEntry *ent)
{
    if (_log_overflow.count >= OVERFLOW_BATCH_MAXENT) {
        // if the overflow is full, we have no choice but to drop events
        if (!_log_overflow.lost) {
            devAssert(_log_overflow.tail);
            if (!_log_overflow.tail)
                return;
            // insert a message that events were lost
            LogEntry *lostent = xaAlloc(sizeof(LogEntry), XA_Zero);
            strDup(&lostent->msg, _S"One or more log entries were lost due to log buffer overflow.");
            lostent->timestamp = clockWall();
            lostent->level = LOG_Warn;
            _log_overflow.tail->_next = lostent;
            _log_overflow.tail = lostent;
            ++_log_overflow.count;
            _log_overflow.lost = 1;
        }

        while (ent) {
            LogEntry *next = ent->_next;
            logDestroyEnt(ent);
            ent = next;
        }

        return;
    }

    if (_log_overflow.tail) {
        _log_overflow.tail->_next = ent;
        _log_overflow.tail = ent;
    } else {
        _log_overflow.head = ent;
        _log_overflow.tail = ent;
    }

    ++_log_overflow.count;

    // this might have itself been a batch, so chase the tail if necessary
    while (_log_overflow.tail->_next) {
        _log_overflow.tail = _log_overflow.tail->_next;
        ++_log_overflow.count;
    }
}

static bool logBufferRetryOverflow()
{
    if (!_log_overflow.head)
        return true;

    if (logBufferAddInternal(_log_overflow.head)) {
        _log_overflow.head = NULL;
        _log_overflow.tail = NULL;
        _log_overflow.count = 0;
        _log_overflow.lost = false;
        return true;
    }

    return false;
}

// these should almost never block, the only time that happens is if
// the buffer is about to overflow and needs to be expanded
_Use_decl_annotations_
void logBufferAdd(LogEntry *ent)
{
    if (!logBufferRetryOverflow() || !logBufferAddInternal(ent))
        logBufferOverflow(ent);
}

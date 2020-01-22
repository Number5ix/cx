#include "rwlock.h"

// Reader/writer lock implementation

// Heavily inspired by Jeff Preshing's semaphore-based RW locks
// https://github.com/preshing/cpp11-on-multicore/blob/master/common/rwlock.h

// Reworked to support timeouts for both read and write lock acquisition. Also
// does not assert if the maximum number of readers, waiters, or writers is
// reached but rather fails to acquire the lock.

bool rwlockInit(RWLock *l)
{
    memset(l, 0, sizeof(RWLock));
    semaInit(&l->rwait, 0);
    semaInit(&l->wwait, 0);
    return true;
}

bool _rwlockContendedAcquireRead(RWLock *l, int64 timeout)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    uint32 nstate;

    do {
        if (RWLOCK_WRITERS(state) > 0) {
            // if there are any writer we must go into readwait state
            if (RWLOCK_READWAIT(state) == RWLOCK_READWAIT_MAX)
                return false;           // too many already
            nstate = state + RWLOCK_READWAIT_ADD;
        } else {
            // otherwise we're just a normal read lock
            if (RWLOCK_READERS(state) == RWLOCK_READER_MAX)
                return false;           // too many already
            nstate = state + RWLOCK_READ_ADD;
        }
    } while (!atomicCompareExchange(uint32, weak, &l->state, &state, nstate, Acquire, Relaxed));

    // if there are any writers (and we incremented readwait instead of read),
    // wait for them to release the lock and signal rwait
    if (RWLOCK_WRITERS(state) > 0) {
        if (timeout == timeForever)
            return semaDec(&l->rwait);
        else {
            if (!semaTryDecTimeout(&l->rwait, timeout)) {
                // timed out, must undo our increment of readwait
                atomicFetchSub(uint32, &l->state, RWLOCK_READWAIT_ADD, Release);
                return false;
            }
        }
    }

    return true;
}

bool _rwlockContendedAcquireWrite(RWLock *l, int64 timeout)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);

    do {
        // make sure we didn't hit the limit
        if (RWLOCK_WRITERS(state) == RWLOCK_WRITER_MAX)
            return false;
    } while (!atomicCompareExchange(uint32, weak, &l->state, &state, state + RWLOCK_WRITE_ADD, Acquire, Relaxed));

    // if there were any other readers or writers we must wait on them
    if (RWLOCK_READERS(state) > 0 || RWLOCK_WRITERS(state) > 0) {
        if (timeout == timeForever)
            return semaDec(&l->wwait);
        else {
            if (!semaTryDecTimeout(&l->wwait, timeout)) {
                // timed out, must undo our increment of writers
                atomicFetchSub(uint32, &l->state, RWLOCK_WRITE_ADD, Release);
                return false;
            }
        }
    }

    // turned out not to be any contention after all
    return true;
}

bool rwlockReleaseWrite(RWLock *l)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    uint32 nstate, rwait;

    do {
        devAssert(RWLOCK_WRITERS(state) > 0 && RWLOCK_READERS(state) == 0);

        // normally we just subtract a writer
        nstate = state - RWLOCK_WRITE_ADD;

        // but if any threads are waiting to read...
        rwait = RWLOCK_READWAIT(state);
        if (rwait > 0) {
            // build a new state:
            //   writers  is carried over from the old state (minus 1)
            //   readwait is set to 0
            //   readers  is set to what was in readwait
            //     (readwait does not need to be masked because it
            //      is less bits wide than readers)
            nstate = (nstate & RWLOCK_WRITER_MASK) | rwait;
        }
    } while (!atomicCompareExchange(uint32, weak, &l->state, &state, nstate, Release, Relaxed));

    // if any threads are waiting to read, release them
    // waiting readers get priority over writers so that they take turns
    if (rwait > 0)
        semaInc(&l->rwait, rwait);
    else if (RWLOCK_WRITERS(state) > 1)     // otherwise if anyone else is waiting
        semaInc(&l->wwait, 1);              // release one thread

    return true;
}

void rwlockDestroy(RWLock *l)
{
    semaDestroy(&l->rwait);
    semaDestroy(&l->wwait);
}

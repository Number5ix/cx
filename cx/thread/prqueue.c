#include "prqueue.h"
#include <cx/utils/macros/unused.h>

#define NOGROW(prq) (prq->growth == PRQ_Grow_None)
#define ONLYGROW(prq) (prq->growth != PRQ_Grow_None)

// Implementation note: Internally we normalize the slot addresses in the ringbuffer such that
//     head <= tail
// This reduces code complexity and avoids a bunch of branches everywhere.
// This does mean that the values cannot be used directly to address the buffer
// without a modulo against the size first.
#define NORMALIZE \
    tail = tail_un + seg->size * (tail_un < head)

#ifdef PRQ_PERF_STATS
#define INCSTAT(prq, sname) atomicFetchAdd(uint64, &(prq)->stats.##sname, 1, Relaxed)
#else
#define INCSTAT(prq, sname) nop_stmt
#endif

_Use_decl_annotations_
void prqInit(PrQueue *prq, uint32 initsz, uint32 maxsz, PrqGrowth growth)
{
    if(growth == 0)
        growth = PRQ_Grow_100;

    prq->initsz = initsz;
    prq->maxsz = (growth == PRQ_Grow_None) ? initsz : maxsz;
    prq->growth = growth;
    prq->concurrence = clamplow(osLogicalCPUs() * 2, 4);
    prq->retired = NULL;
    atomicStore(int32, &prq->access, 0, Relaxed);

    PrqSegment *seg = xaAlloc(initsz * sizeof(void*) + offsetof(PrqSegment, buffer), XA_Zero);
    seg->size = initsz;
    atomicStore(ptr, &prq->current, seg, Release);

    if(!NOGROW(prq)) {
        mutexInit(&prq->gcmtx);
    }

    memset(&prq->aspin, 0, sizeof(prq->aspin));

#ifdef PRQ_PERF_STATS
    memset(&prq->stats, 0, sizeof(prq->stats));
#endif
}

_Use_decl_annotations_
bool prqPush(PrQueue *prq, void *ptr)
{
    if(!ptr)
        return false;           // invalid to push a NULL pointer

    AdaptiveSpinState astate = { 0 };

    // Safely get the current segment and indicate that we're using it
    if(ONLYGROW(prq))
        atomicFetchAdd(int32, &prq->access, 1, AcqRel);
    PrqSegment *seg = atomicLoad(ptr, &prq->current, Acquire);
    if(ONLYGROW(prq)) {
        atomicFetchAdd(int32, &seg->inuse, 1, AcqRel);
        atomicFetchAdd(int32, &prq->access, -1, Relaxed);
    }

    PrqSegment *nextseg;
    bool full = false;

retry:
    // Chase the segment chain if needed
    if(ONLYGROW(prq)) {
        while((nextseg = atomicLoad(ptr, &seg->nextseg, Acquire))) {
            atomicFetchAdd(int32, &nextseg->inuse, 1, AcqRel);
            atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
            seg = nextseg;
            full = false;
        }
    }

    uint32 head, tail_un, tail;
    for(;;) {
        // check for full segment
        head = atomicLoad(uint32, &seg->head, Acquire);         // TODO: Can one or both of these safely be Relaxed?
        tail_un = atomicLoad(uint32, &seg->tail, Acquire);
        NORMALIZE;

        // check if segment is actually full, can only happen on retry
        if(full) {
            if(ONLYGROW(prq) && seg->size < prq->maxsz) {
                // need to create a new segment to grow the queue
                uint32 newsz;
                switch(prq->growth) {
                case PRQ_Grow_25:
                    newsz = clamp(seg->size + (seg->size >> 2), seg->size + 1, prq->maxsz);
                    break;
                case PRQ_Grow_50:
                    newsz = clamp(seg->size + (seg->size >> 1), seg->size + 1, prq->maxsz);
                    break;
                case PRQ_Grow_100:
                default:
                    newsz = clamp(seg->size << 1, seg->size + 1, prq->maxsz);
                    break;
                case PRQ_Grow_150:
                    newsz = clamp((seg->size << 1) + (seg->size >> 1), seg->size + 1, prq->maxsz);
                    break;
                case PRQ_Grow_200:
                    newsz = clamp(seg->size * 3, seg->size + 1, prq->maxsz);
                    break;
                }

                PrqSegment *newseg = xaAlloc(newsz * sizeof(void *) + offsetof(PrqSegment, buffer), XA_Zero);
                PrqSegment *expected = NULL;
                newseg->size = newsz;
                if(atomicCompareExchange(ptr, strong, &seg->nextseg, &expected, newseg, Release, Relaxed)) {
                    INCSTAT(prq, grow);
                    full = false;
                    goto retry;             // will follow nextseg into the new segment we just allocated
                } else {
                    // failed to swap the pointer, this means another thread grew the buffer first
                    INCSTAT(prq, grow_collision);
                    xaFree(newseg);
                    goto retry;             // will follow nextseg into the new segment the other thread allocated
                }
            } else {
                // cannot grow any more, return failure
                if(ONLYGROW(prq))
                    atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
                return false;
            }
        } else if(tail - head >= seg->size - 1) {
            // Segment APPEARS full, but we can't be certain until an insert is attempted.
            // This is a suboptimal path so we should try to avoid it when possible.
            // Do NOT increment the tail in this case.
            break;
        }

        // try to reserve a slot, repeat the full check if there is contention and we fail
        if(atomicCompareExchange(uint32, strong, &seg->tail, &tail_un, (tail + 1) % seg->size, Release, Relaxed)) {
            tail++;
            break;
        }

        INCSTAT(prq, tail_contention);
        aspinHandleContention(&prq->aspin, &astate);
    }

    // actually do the update
    bool success = false;
    uint32 slot;
    do {
        void *oldptr;
        // scan backwards from tail to head, preferring the slot we allocated, but filling
        // one that another thread has left open if we have to
        for(slot = tail - 1; slot >= head && slot < tail; slot--) {
            oldptr = atomicLoad(ptr, &seg->buffer[slot % seg->size], Relaxed);
            if(oldptr == NULL) {
                if(atomicCompareExchange(ptr, weak, &seg->buffer[slot % seg->size], &oldptr, ptr, Release, Relaxed)) {
                    if(slot == tail - 1)
                        INCSTAT(prq, push_optimal);
                    else
                        INCSTAT(prq, push_other);

                    // for perf reasons, we care that this happened on the suboptimal path of having to scan every slot
                    if(tail - head >= seg->size - 1)
                        INCSTAT(prq, push_appeared_full);

                    success = true;
                    break;
                } else {
                    aspinHandleContention(&prq->aspin, &astate);
                    INCSTAT(prq, push_collision);
                }
            }
        }

        if(!success) {
            uint32 lasttail = tail;
            head = atomicLoad(uint32, &seg->head, Acquire);
            tail_un = atomicLoad(uint32, &seg->tail, Acquire);
            NORMALIZE;

            // if tail didn't move, looping won't do any good and we need a full retry,
            // and possibly growing a full segment
            if(lasttail == tail) {
                if(tail - head >= seg->size - 1) {
                    full = true;
                    INCSTAT(prq, push_actually_full);
                }
                INCSTAT(prq, push_full_retry);
                goto retry;
            }

            INCSTAT(prq, push_retry);
        }
    } while(!success);

    devAssert(success);

    // Make sure that we actually wrote the entry to a slot that's in the correct range.
    head = atomicLoad(uint32, &seg->head, Relaxed);
    tail_un = atomicLoad(uint32, &seg->tail, Relaxed);
    NORMALIZE;

    if(!(slot >= head && slot < tail)) {
        // Uh oh, we committed to a slot that is actually outside the range that is going
        // to be read later. This is a variant of the classic ABA problem and can be caused by
        // a reader thread assisting by advancing the head past the slot we were trying to write
        // to. Try to undo it.
        // This is the downside of fully lockless MPMC, but delaying thread assists until the
        // concurrency factor is met or the buffer is getting full helps to minimize the number
        // of times it happens in practice.
        INCSTAT(prq, push_out_of_range);
        void *oldptr = atomicLoad(ptr, &seg->buffer[slot % seg->size], Relaxed);
        // If oldptr becomes NULL (or something else) but we didn't succeed yet, it means the
        // buffer wrapped around fast enough that it's already been read anyway, or we stalled
        // after writing but before refreshing rdhead, and a reader thread already picked it up.
        // Either way we're good and don't have to do anytihng.
        while(oldptr == ptr) {
            if(atomicCompareExchange(ptr, weak, &seg->buffer[slot % seg->size], &oldptr, NULL, Release, Relaxed)) {
                aspinHandleContention(&prq->aspin, &astate);
                goto retry;         // we successfully backed it out, try to put it somewhere else instead
            }
        }
        INCSTAT(prq, push_unnecessary_backout);
    }

    // all done, release the buffer segment
    if(ONLYGROW(prq))
        atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);

    INCSTAT(prq, push);
    return true;
}

_Use_decl_annotations_
void *prqPop(PrQueue *prq)
{
    AdaptiveSpinState astate = { 0 };

    // Safely get the current segment and indicate that we're using it
    if(ONLYGROW(prq))
        atomicFetchAdd(int32, &prq->access, 1, AcqRel);
    PrqSegment *seg = atomicLoad(ptr, &prq->current, Acquire);
    if(ONLYGROW(prq)) {
        atomicFetchAdd(int32, &seg->inuse, 1, AcqRel);
        atomicFetchAdd(int32, &prq->access, -1, Relaxed);
    }

    // assist if we're over the concurrence factor, or using more than half the queue
    uint32 assist_thresh = min(prq->concurrence, seg->size / 2);
    uint32 head, tail_un, tail;
    bool empty = false;
    bool havenext = false;      // if this segment has been replaced by growing, we always want to try to assist

    head = atomicLoad(uint32, &seg->head, Acquire);         // TODO: Can one or both of these safely be Relaxed?
    tail_un = atomicLoad(uint32, &seg->tail, Acquire);
    NORMALIZE;

    // check if the buffer APPEARS empty, it may still be EFFECTIVELY empty
    // if there are incomplete writes in progress
    empty = (head == tail);

retry:
    if(empty) {
        if(ONLYGROW(prq)) {
            // if this is a growable queue, check the next segment
            PrqSegment *nextseg = atomicLoad(ptr, &seg->nextseg, Acquire);
            if(nextseg) {
                atomicFetchAdd(int32, &nextseg->inuse, 1, AcqRel);
                atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
                seg = nextseg;
                head = atomicLoad(uint32, &seg->head, Acquire);
                tail_un = atomicLoad(uint32, &seg->tail, Acquire);
                NORMALIZE;
                INCSTAT(prq, pop_segtraverse);
                empty = (head == tail);
                goto retry;
            }

            atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
        }
            
        return NULL;                // queue is actually empty!
    } else if(ONLYGROW(prq)) {
        havenext = (atomicLoad(ptr, &seg->nextseg, Relaxed) != NULL);
    }

    // now look for something to read
    uint32 slot;

    void *ptr = NULL;
    bool success = false;
    // scan slots in queue range for something that's been written
    for(slot = head; slot < tail; slot++) {
        ptr = atomicLoad(ptr, &seg->buffer[slot % seg->size], Relaxed);
        if(ptr) {
            if(atomicCompareExchange(ptr, weak, &seg->buffer[slot % seg->size], &ptr, NULL, Release, Relaxed)) {
                if(slot == head)
                    INCSTAT(prq, pop_optimal);
                else
                    INCSTAT(prq, pop_other);
                INCSTAT(prq, pop);
                success = true;
                break;
            } else {
                ptr = NULL;
                aspinHandleContention(&prq->aspin, &astate);
                INCSTAT(prq, pop_collision);
            }
        }
    }

    if(!success) {
        INCSTAT(prq, pop_nonobvious_empty);

        if(tail - head > assist_thresh || havenext) {
            // Queue looks empty, but pointers say it shouldn't be.
            // To maintain best performance, move the head up to the tail.

            if(atomicCompareExchange(uint32, weak, &seg->head, &head, tail % seg->size, Release, Relaxed))
                INCSTAT(prq, pop_assist);
            else
                INCSTAT(prq, pop_assist_fail);
        }

        if(NOGROW(prq))
            return NULL;

        // didn't find anything, try again with another segment if possible
        empty = true;
        goto retry;
    }

    // if we read the first entry, need to update the head pointer
    if(slot == head) {
        while(head < slot + 1) {
            if(atomicCompareExchange(uint32, weak, &seg->head, &head, (slot + 1) % seg->size, Release, Acquire))
                break;

            aspinHandleContention(&prq->aspin, &astate);
            INCSTAT(prq, head_contention);
        }
    } else if((tail - head > assist_thresh && head < slot + 1) || havenext) {
        // we may need to assist another thread
        if(atomicCompareExchange(uint32, weak, &seg->head, &head, (slot + 1) % seg->size, Release, Acquire))
            INCSTAT(prq, pop_assist);
        else
            INCSTAT(prq, pop_assist_fail);
    }

    if (ONLYGROW(prq))
        atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);

    return ptr;
}

_Use_decl_annotations_
bool prqCollect(PrQueue *prq)
{
    if(NOGROW(prq))
        return true;            // GC not needed for queues that can't grow

    if(!mutexTryAcquire(&prq->gcmtx))
        return false;

    INCSTAT(prq, gc_run);

    // try to retire the current segment if it's been replaced
    PrqSegment *seg = atomicLoad(ptr, &prq->current, Acquire);
    PrqSegment *nextseg = atomicLoad(ptr, &seg->nextseg, Acquire);
    PrqSegment *retired;

    if(nextseg) {
        uint32 head = atomicLoad(uint32, &seg->head, Acquire);
        uint32 tail = atomicLoad(uint32, &seg->tail, Acquire);

        // empty?
        if(head == tail) {
            // CAS is not needed because nothing else is allowed to change this pointer except GC, and we have the mutex
            atomicStore(ptr, &prq->current, nextseg, Release);

            // Add to the end of the retired segment chain
            if(prq->retired) {
                retired = prq->retired;
                while(retired->nextretired) {
                    retired = retired->nextretired;
                }
                retired->nextretired = seg;
            } else {
                prq->retired = seg;
            }

            INCSTAT(prq, seg_retired);
        }
    }

    // try to deallocate the oldest retired segment
    retired = prq->retired;
    if(retired) {
        uint32 access = atomicLoad(int32, &prq->access, Acquire);
        uint32 inuse = atomicLoad(int32, &retired->inuse, Acquire);
        // can only safely do this if both values are 0.
        // as they are interlocked, prq->access MUST be read first!
        if(access == 0 && inuse == 0) {
            prq->retired = retired->nextretired;
            xaFree(retired);
            INCSTAT(prq, seg_dealloc);
        } else {
            INCSTAT(prq, seg_dealloc_failinuse);
        }
    }

    mutexRelease(&prq->gcmtx);
    return true;
}

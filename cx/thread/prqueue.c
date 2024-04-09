#include "prqueue.h"
#include <cx/utils/macros/unused.h>

#define NOGROW(prq) (prq->growth == PRQ_Grow_None)
#define ONLYGROW(prq) (prq->growth != PRQ_Grow_None)

#define RESERVED_RETIRING 0x80000000
#define RESERVED_MASK 0x7fffffff

#ifdef PRQ_PERF_STATS
#define INCSTAT(prq, sname) atomicFetchAdd(uint64, &(prq)->stats.sname, 1, Relaxed)
#else
#define INCSTAT(prq, sname) nop_stmt
#endif

_Use_decl_annotations_
void prqInit(PrQueue *prq, uint32 initsz, uint32 maxsz, PrqGrowth growth)
{
    if(growth == 0)
        growth = PRQ_Grow_100;
    if(maxsz == 0)
        maxsz = MAX_UINT32;

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
    bool didreserve = false;

fullretry:
    // Back out the previous write reservation
    if(didreserve) {
        uint32 reserved = atomicLoad(uint32, &seg->reserved, Relaxed);
        while((reserved & RESERVED_MASK) > 0) {
            // need to reduce masked part while preserving retiring bit
            if(atomicCompareExchange(uint32, weak, &seg->reserved, &reserved,
                                     (reserved & RESERVED_RETIRING) | ((reserved & RESERVED_MASK) - 1), Release, Acquire))
                break;
            INCSTAT(prq, reserved_contention);
            aspinHandleContention(&prq->aspin, &astate);
        }
        didreserve = false;
    }

    // Chase the segment chain if needed
    if(ONLYGROW(prq)) {
        while((nextseg = atomicLoad(ptr, &seg->nextseg, Acquire))) {
            atomicFetchAdd(int32, &nextseg->inuse, 1, AcqRel);
            atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
            seg = nextseg;
            full = false;
        }
    }

    bool appearedfull = false;
    uint32 count;

    // check for full segment
    count = atomicLoad(uint32, &seg->count, Acquire);

    // If we are still able to grow, prefer to do that if the segment appears full.
    // The reason is that growing preserves queue ordering better. We should only fall back
    // to scavenging for an empty slot if there's no other choice.
    if((ONLYGROW(prq) && seg->size < prq->maxsz) && count >= seg->size)
        full = true;

    // grow if segment is actually full, usually can only happen on retry
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
            void *expected = NULL;
            newseg->size = newsz;
            if(atomicCompareExchange(ptr, strong, &seg->nextseg, &expected, newseg, Release, Relaxed)) {
                INCSTAT(prq, grow);
                full = false;
                goto fullretry;         // will follow nextseg into the new segment we just allocated
            } else {
                // failed to swap the pointer, this means another thread grew the buffer first
                INCSTAT(prq, grow_collision);
                xaFree(newseg);
                goto fullretry;         // will follow nextseg into the new segment the other thread allocated
            }
        } else {
            // cannot grow any more, return failure
            if(ONLYGROW(prq))
                atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
            return false;
        }
    } else if(count >= seg->size) {
        // Segment APPEARS full, but we can't be certain until an insert is attempted.
        // This is a suboptimal path so we should try to avoid it when possible.
        // Do NOT increment the count in this case.
        appearedfull = true;
    }

    // get a write reservation if we need one
    if(ONLYGROW(prq)) {
        uint32 reserved = atomicLoad(uint32, &seg->reserved, Relaxed);
        for(;;) {
            bool retiring = (reserved & RESERVED_RETIRING);
            reserved = (reserved & RESERVED_MASK);
            // if the segment is being retired, or if we somehow hit 2 billion writers, need to retry instead
            if(retiring || reserved == RESERVED_MASK) {
                INCSTAT(prq, push_noreserve_retiring);
                goto fullretry;
            }

            if(atomicCompareExchange(uint32, weak, &seg->reserved, &reserved, reserved + 1, Release, Acquire))
                break;
            INCSTAT(prq, reserved_contention);
            aspinHandleContention(&prq->aspin, &astate);
        }

        didreserve = true;
    }

    bool success;
    uint32 head = atomicLoad(uint32, &seg->head, Relaxed);
    uint32 slot;

retry:
    // actually do the update
    success = false;
    void *oldptr;
    // scan the segment, starting from the slot where we expect the tail to be
    for(slot = head + count; !success && slot < head + count + seg->size; slot++) {
        oldptr = atomicLoad(ptr, &seg->buffer[slot % seg->size], Relaxed);
        while(oldptr == NULL) {
            if(atomicCompareExchange(ptr, weak, &seg->buffer[slot % seg->size], &oldptr, ptr, Release, Acquire)) {
                if(slot == head + count)
                    INCSTAT(prq, push_optimal);
                else if(slot - (head + count) < (seg->size << 2))
                    INCSTAT(prq, push_fast);
                else
                    INCSTAT(prq, push_slow);

                // for perf reasons, we care that this happened on the suboptimal path of having to scan every slot
                if(appearedfull)
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
        count = atomicLoad(uint32, &seg->count, Acquire);

        // if we're full, need to retry from the top
        if(count >= seg->size) {
            full = true;
            if (appearedfull)
                INCSTAT(prq, push_actually_full);
            INCSTAT(prq, push_full_retry);
            goto fullretry;
        }

        head = atomicLoad(uint32, &seg->head, Relaxed);
        INCSTAT(prq, push_retry);
        goto retry;
    }

    // Need to increment the count
    atomicFetchAdd(uint32, &seg->count, 1, AcqRel);

    // release write reservation if we needed one for this transaction
    if(didreserve) {
        uint32 reserved = atomicLoad(uint32, &seg->reserved, Relaxed);
        while((reserved & RESERVED_MASK) > 0) {
            // need to reduce masked part while preserving retiring bit
            if(atomicCompareExchange(uint32, weak, &seg->reserved, &reserved,
                                     (reserved & RESERVED_RETIRING) | ((reserved & RESERVED_MASK) - 1), Release, Acquire))
                break;
            INCSTAT(prq, reserved_contention);
            aspinHandleContention(&prq->aspin, &astate);
        }
        didreserve = false;
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
    bool empty = false;
    bool havenext = false;      // if this segment has been replaced by growing, we always want to try to assist

    uint32 count = atomicLoad(uint32, &seg->count, Acquire);
    uint32 head = atomicLoad(uint32, &seg->head, Relaxed);          // acquire/release on count maintains head ordering,
                                                                    // which doesn't have to be exact

    // check if the buffer APPEARS empty, it may still be EFFECTIVELY empty
    // if there are incomplete writes in progress
    empty = (count == 0);

retry:
    if(empty) {
        if(ONLYGROW(prq)) {
            // if this is a growable queue, check the next segment
            PrqSegment *nextseg = atomicLoad(ptr, &seg->nextseg, Acquire);
            if(nextseg) {
                atomicFetchAdd(int32, &nextseg->inuse, 1, AcqRel);
                atomicFetchAdd(int32, &seg->inuse, -1, Relaxed);
                seg = nextseg;
                count = atomicLoad(uint32, &seg->count, Acquire);
                head = atomicLoad(uint32, &seg->head, Relaxed);
                INCSTAT(prq, pop_segtraverse);
                empty = (count == 0);
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
    // scan slots in queue for something that's been written
    for(slot = head; slot < head + seg->size; slot++) {
        ptr = atomicLoad(ptr, &seg->buffer[slot % seg->size], Relaxed);
        if(ptr) {
            if(atomicCompareExchange(ptr, weak, &seg->buffer[slot % seg->size], &ptr, NULL, Release, Acquire)) {
                if(slot == head)
                    INCSTAT(prq, pop_optimal);
                else if (slot - head <= (seg->size >> 2))
                    INCSTAT(prq, pop_fast);
                else
                    INCSTAT(prq, pop_slow);
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

        if(NOGROW(prq))
            return NULL;

        // didn't find anything, try again with another segment if possible
        empty = true;
        goto retry;
    }

    // decrement the count ASAP
    atomicFetchAdd(uint32, &seg->count, -1, AcqRel);

    if(slot == head) {
        // if we read the first entry, need to update the head pointer
        uint32 oldhead = head;
        while(oldhead < slot + 1) {
            if(atomicCompareExchange(uint32, weak, &seg->head, &head, (slot + 1) % seg->size, Relaxed, Relaxed)) {
                break;
            } else {
                if(head < oldhead)
                    oldhead = head + seg->size;
                else
                    oldhead = head;
                INCSTAT(prq, head_contention);
                aspinHandleContention(&prq->aspin, &astate);
            }
        }
    } else if (slot - head > assist_thresh || havenext) {
        // we may need to assist another thread
        if(atomicCompareExchange(uint32, weak, &seg->head, &head, (slot + 1) % seg->size, Relaxed, Relaxed))
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
    bool candealloc = true;

    if(nextseg) {
        uint32 count = atomicLoad(uint32, &seg->count, Relaxed);
        uint32 reserved = atomicLoad(uint32, &seg->reserved, Relaxed);

        if(!(reserved & RESERVED_RETIRING)) {
            // Phase 1: Add retired bit to block further writes
            atomicFetchOr(uint32, &seg->reserved, RESERVED_RETIRING, Relaxed);
            candealloc = false;             // no more work this cycle
        } else if(count == 0 && (reserved & RESERVED_MASK) == 0) {
            // Phase 2: Retire segment when it's empty and there are no pending writes
            // CAS is not needed on current because nothing else is allowed to change this pointer except GC, and we have the mutex
            atomicStore(ptr, &prq->current, nextseg, Release);

            candealloc = false;             // no more work this cycle

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

    if(candealloc) {
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
    }

    mutexRelease(&prq->gcmtx);
    return true;
}

// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/platform/os.h>
#include <cx/thread/tlscleanup.h>
#include <cx/time/clock.h>
#include <cx/utils/compare.h>

// String constants
STR_CONST(kLogOverflowMsg, "One or more log entries were lost due to log queue overflow.");

#define OVERFLOW_BATCH_MAXENT 65536

// Per-thread overflow list if a group's queue fills up. There is one per group because the queues
// fill independently: a bulk group backing up behind a rotation must not make the console's queue
// look full.
typedef struct LogOverflowTLS {
    LogQueueNode* head;
    LogQueueNode* tail;
    int count;
    bool lost;
} LogOverflowTLS;

// Allocated on a thread's first overflow rather than living inline in TLS, so a thread that never
// backs the queue up -- nearly all of them, nearly all of the time -- costs one pointer of static
// TLS instead of 24 bytes x LOG_GROUP_MAX. Every thread the process starts pays for its static TLS
// whether it logs or not: glibc and the FreeBSD rtld both memset .tbss per thread, and Windows
// heap-allocates and zeroes the block at thread attach.
static _Thread_local LogOverflowTLS* _log_overflow;

_Use_decl_annotations_
uint32 logQueueCount(LogQueueNode* head)
{
    uint32 n = 0;
    for (LogQueueNode* node = head; node; node = node->next) ++n;
    return n;
}

_Use_decl_annotations_
void logQueueFreeNodes(LogQueueNode* head)
{
    while (head) {
        LogQueueNode* next = head->next;
        LogEntry* ent      = head->ent;

        // An inline node is part of its entry's allocation, so releasing the entry can free the
        // node's own storage: both fields are read out first, and the node is never freed
        // separately.
        if (!logQueueNodeIsInline(head))
            xaFree(head);
        logEntryRelease(ent);

        head = next;
    }
}

static bool logQueueAddInternal(_In_ LogGroup* grp, _In_ LogQueueNode* head, uint32 nents)
{
    bool ret = prqPush(&grp->queue, head);
    if (!ret)
        return false;

    // Depth is tracked here rather than asked of the queue, because the queue counts pushes and
    // a push is a whole batch. The high-water mark is a statistic, so the read-compare-store is
    // deliberately not atomic as a unit; the worst a race costs is one under-reported peak.
    uint32 depth = atomicFetchAdd(uint32, &grp->depth, nents, Relaxed) + nents;
    if (depth > atomicLoad(uint32, &grp->peak, Relaxed))
        atomicStore(uint32, &grp->peak, depth, Relaxed);

    if (grp->thread)
        eventSignal(&grp->thread->notify);
    return true;
}

// Partitions a chain into the records worth keeping and the rest. Both output chains are properly
// terminated, and every node ends up in exactly one of them.
static void logQueueSplitBySeverity(_In_opt_ LogQueueNode* head, int minlevel,
                                    _Out_ LogQueueNode** keep, _Out_ LogQueueNode** drop)
{
    LogQueueNode* keeptail = NULL;
    LogQueueNode* droptail = NULL;
    *keep                  = NULL;
    *drop                  = NULL;

    LogQueueNode* node = head;
    while (node) {
        LogQueueNode* next = node->next;
        node->next         = NULL;

        if (node->ent->level <= minlevel) {
            if (keeptail)
                keeptail->next = node;
            else
                *keep = node;
            keeptail = node;
        } else {
            if (droptail)
                droptail->next = node;
            else
                *drop = node;
            droptail = node;
        }

        node = next;
    }
}

// Last resort for a batch that can be neither queued nor held. Severe records are written from
// this thread instead of being lost: silently dropping a Fatal is the worst failure mode this
// system has. Consumes the batch either way.
static void logQueueDropBatch(_In_ LogGroup* grp, _In_ LogQueueNode* head)
{
    int synclevel = atomicLoad(int32, &_log_synclevel, Relaxed);
    uint32 nents  = logQueueCount(head);
    uint32 nsync  = 0;
    for (LogQueueNode* node = head; node; node = node->next) {
        if (node->ent->level <= synclevel)
            ++nsync;
    }
    if (nsync > 0)
        logWriteSync(grp, head, synclevel);
    atomicFetchAdd(uintptr, &_log_stat_dropped, nents - nsync, Relaxed);
    logQueueFreeNodes(head);
}

// A thread can exit while its queue is still backed up. Whatever it is still holding gets one last
// push, so records are not lost with the thread that made them; anything the queue still will not
// take falls back to the same sync-write-and-drop the overflow-full path uses.
static void logQueueThreadFinish(void* unused)
{
    if (!_log_overflow)
        return;

    // Groups are never removed and a group's index is fixed when it is created, so every list that
    // can be holding anything is below the current count.
    uint32 ngroups = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < ngroups; ++i) {
        LogOverflowTLS* ov = &_log_overflow[i];
        if (!ov->head)
            continue;

        LogGroup* grp = _log_grouptab[i];
        if (!logQueueAddInternal(grp, ov->head, (uint32)ov->count))
            logQueueDropBatch(grp, ov->head);
    }

    xaDestroy(&_log_overflow);
}

static void logQueueOverflow(_In_ LogGroup* grp, _In_ LogQueueNode* head)
{
    // Only records the system would already work to preserve are worth holding. Anything less
    // severe is dropped here, on the same threshold that decides what gets written synchronously.
    int synclevel = atomicLoad(int32, &_log_synclevel, Relaxed);
    LogQueueNode* keep;
    LogQueueNode* drop;
    logQueueSplitBySeverity(head, synclevel, &keep, &drop);

    if (drop) {
        atomicFetchAdd(uintptr, &_log_stat_dropped, logQueueCount(drop), Relaxed);
        logQueueFreeNodes(drop);
    }

    if (!keep)
        return;

    head = keep;

    if (!_log_overflow) {
        // XA_Opt because if we're here, the system is already under stress. Failing to allocate
        // the overflow buffer has to degrade to dropping records, not aborting the process.
        _log_overflow = xaAlloc(sizeof(LogOverflowTLS) * LOG_GROUP_MAX, XA_Zero | XA_Opt);
        if (!_log_overflow) {
            logQueueDropBatch(grp, head);
            return;
        }
        thrRegisterCleanup(logQueueThreadFinish, NULL);
    }

    LogOverflowTLS* ov = &_log_overflow[grp->idx];

    if (ov->count >= OVERFLOW_BATCH_MAXENT) {
        // The queue and the overflow list are both full, so something has to give.
        logQueueDropBatch(grp, head);

        // if the overflow is full, we have no choice but to drop the rest
        if (!ov->lost) {
            devAssert(ov->tail);
            if (!ov->tail)
                return;
            // insert a message that events were lost
            LogEntry* lostent = logEntryCreate(LOG_Warn,
                                               -1,
                                               LogDefault,
                                               NULL,
                                               kLogOverflowMsg,
                                               0,
                                               NULL,
                                               NULL);
            if (!lostent)
                return;
            // The entry is fresh and has never been fanned out, so its own queue node is free for
            // the taking -- worth having on a path that is already out of memory.
            LogQueueNode* lostnode = &lostent->inlnode;
            lostnode->next         = NULL;
            lostnode->ent          = lostent;   // takes the creator's reference
            ov->tail->next         = lostnode;
            ov->tail               = lostnode;
            ++ov->count;
            ov->lost = 1;
        }

        return;
    }

    if (ov->tail) {
        ov->tail->next = head;
        ov->tail       = head;
    } else {
        ov->head = head;
        ov->tail = head;
    }

    ++ov->count;

    // this might have itself been a batch, so chase the tail if necessary
    while (ov->tail->next) {
        ov->tail = ov->tail->next;
        ++ov->count;
    }
}

static bool logQueueRetryOverflow(_In_ LogGroup* grp)
{
    // nothing has ever overflowed on this thread, which is the overwhelmingly common case
    if (!_log_overflow)
        return true;

    LogOverflowTLS* ov = &_log_overflow[grp->idx];

    if (!ov->head)
        return true;

    if (logQueueAddInternal(grp, ov->head, (uint32)ov->count)) {
        ov->head  = NULL;
        ov->tail  = NULL;
        ov->count = 0;
        ov->lost  = false;
        return true;
    }

    return false;
}

// this should never block, except when backpressure escalates to a synchronous write
_Use_decl_annotations_
void logQueueAdd(LogGroup* grp, LogQueueNode* head, uint32 nents)
{
    atomicFetchAdd(uintptr, &_log_stat_enqueued, nents, Relaxed);

    if (!logQueueRetryOverflow(grp) || !logQueueAddInternal(grp, head, nents))
        logQueueOverflow(grp, head);
}

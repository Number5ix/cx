// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/console.h>
#include <cx/container/foreach.h>
#include <cx/string/striter.h>
#include <cx/time.h>

// ok to use private here since it's for emergency panic situations
#include "../console/console_private.h"

// Writing records from the logging thread instead of a drain thread.
//
// Two callers want this, for opposite reasons. Backpressure wants it because a Fatal that a full
// queue would otherwise drop is the worst failure mode the system has, and a slow write beats a
// lost one. logPanicFlush() wants it because the process is dying and the drain threads may
// never run again.
//
// Both take the dispatch lock exclusively, which is what the deferred-destination handoff does
// and for the same reason: a destination callback must not run on two threads at once. Both take
// it with a timeout and proceed anyway if it expires, because a caller that is already in
// trouble should not be made to wait on a drain thread that may be the thing that is broken.

#define LOG_SYNC_WAIT  timeS(1)
#define LOG_PANIC_WAIT timeMS(250)

static void logSyncChain(_In_ LogGroup* grp, _In_opt_ LogRouting* routing,
                         _In_opt_ LogQueueNode* head, _Inout_ sa_LogDest* sent, int minlevel)
{
    saClear(sent);

    uint32 batchid = 0;
    for (LogQueueNode* node = head; node; node = node->next) {
        LogEntry* ent = node->ent;
        if (ent->level > minlevel)
            continue;

        batchid              = ent->batchid;
        LogRenderCache cache = { 0 };
        LogRecord rec;
        logEntryToRecord(&rec, ent, batchid, &cache);
        logDispatchRecord(grp, routing, &rec, sent);
        strDestroy(&cache.str);

        atomicFetchAdd(uint64, &_log_stat_sync, 1, Relaxed);
    }

    foreach (sarray, idx, LogDest*, dest, *sent) {
        if (dest->batchfunc)
            dest->batchfunc(batchid, dest->userdata);
    }
}

// Writes records straight to stderr, touching no lock at all.
//
// Destinations are deliberately not consulted. Getting here means a callback has held the dispatch
// lock for the whole timeout, which in practice means a drain thread is wedged inside one; running
// those same callbacks from this thread is how this thread gets wedged the same way. The console
// lock is no safer, since the console destination's callback may be exactly what is stuck -- hence
// _conWriteLocked() rather than conPuts()/conNL(). Interleaved bytes are an acceptable price for
// output that arrives at all.
//
// Allocation is still fine here: what failed was a lock, not the allocator.
static void logEmergencyChain(_In_opt_ LogQueueNode* head, int minlevel)
{
#ifdef _PLATFORM_WIN
    static const uint8 nl[] = { '\r', '\n' };
#else
    static const uint8 nl[] = { '\n' };
#endif

    ConStream* err = conErr();
    string line    = 0;

    for (LogQueueNode* node = head; node; node = node->next) {
        LogEntry* ent = node->ent;
        if (ent->level > minlevel)
            continue;

        LogRenderCache cache = { 0 };
        LogRecord rec;
        logEntryToRecord(&rec, ent, ent->batchid, &cache);
        logSerialize(&line, NULL, &rec);   // NULL serializer: the record's plain rendered text
        strDestroy(&cache.str);

        // a string may be a rope, so walk its segments rather than assuming one buffer
        striter it;
        striBorrow(&it, line);
        while (it.len > 0) {
            _conWriteLocked(err, it.bytes, it.len);
            striNext(&it);
        }
        _conWriteLocked(err, nl, sizeof(nl));

        atomicFetchAdd(uint64, &_log_stat_sync, 1, Relaxed);
    }

    strDestroy(&line);
}

_Use_decl_annotations_
void logWriteSync(LogGroup* grp, LogQueueNode* head, int minlevel)
{
    // A destination callback that logs would deadlock here; the timeout turns that into a record
    // written somewhere safe rather than a hung process. Nothing in cx does it -- it is the same
    // hazard the CX_LOCK_DEBUG undef at the top of every log .c file exists to prevent -- but this
    // is the path that runs when things are already going wrong.
    if (!mutexTryAcquireTimeout(&grp->dispatchlock, LOG_SYNC_WAIT)) {
        logEmergencyChain(head, minlevel);
        return;
    }

    sa_LogDest sent;
    saInit(&sent, ptr, 8, SA_Sorted);

    logSyncChain(grp, logRoutingCurrentUnsafe(), head, &sent, minlevel);

    mutexRelease(&grp->dispatchlock);
    saDestroy(&sent);
}

void logPanicFlush(void)
{
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    sa_LogDest sent;
    saInit(&sent, ptr, 8, SA_Sorted);

    // No routing-version reference is taken. A drain thread could in principle retire the
    // version underneath this, but reclamation only happens under _log_op_lock during
    // reconfiguration or a flush, and the alternative -- registering as a drain thread from a
    // signal handler -- is worse. This is the one place in the log system that trades a
    // theoretical race for the ability to run at all in a dying process.
    LogRouting* routing = logRoutingCurrentUnsafe();

    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++) {
        LogGroup* grp = _log_grouptab[i];

        // Each group gets the full wait rather than a share of one budget: a group whose drain is
        // wedged must not spend the time a later, healthy group needs to flush. The flush still
        // proceeds if the lock never comes free, but that applies only to the group actually stuck,
        // while every other group is dispatched properly excluded.
        bool locked = mutexTryAcquireTimeout(&grp->dispatchlock, LOG_PANIC_WAIT);

        LogQueueNode* node;
        while ((node = (LogQueueNode*)prqPop(&grp->queue))) {
            atomicFetchSub(uint32, &grp->depth, logQueueCount(node), Relaxed);
            logSyncChain(grp, routing, node, &sent, LOG_Count);
            logQueueFreeNodes(node);
        }

        if (locked)
            mutexRelease(&grp->dispatchlock);
    }

    saDestroy(&sent);
}

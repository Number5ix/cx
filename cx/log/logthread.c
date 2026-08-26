// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/platform/cpu.h>
#include <cx/platform/os.h>
#include <cx/time.h>

#define LOG_BATCH_SIZE 256

// Pops the lowest set bit of a mask word. ctz64() is only available on 64-bit MSVC targets, so
// the halves are scanned separately rather than assuming it.
_meta_inline uint32 logMaskNext(_Inout_ uint64* bits)
{
    uint32 lo  = (uint32)*bits;
    uint32 idx = lo ? (uint32)ctz32(lo) : 32 + (uint32)ctz32((uint32)(*bits >> 32));
    *bits &= *bits - 1;
    return idx;
}

_Use_decl_annotations_
void logDispatchRecord(LogGroup* grp, LogRouting* routing, const LogRecord* rec, sa_LogDest* sent)
{
    // A channel interned after this version was published has no row in it; the record is
    // dropped, which is the accepted cost of a reconfiguration race.
    if (!routing || rec->chan->idx >= routing->nchan)
        return;

    uint64* row = &routing->destmask[(size_t)rec->chan->idx * routing->nwords];

    for (uint32 w = 0; w < routing->nwords; w++) {
        uint64 bits = row[w];
        while (bits) {
            uint32 i      = w * 64 + logMaskNext(&bits);
            LogDest* dest = routing->dests[i];
            // A record released from a debug ring is filtered at the severity that released it
            // rather than its own, so the trace leading up to an error reaches the destinations
            // that would have seen the error and no others.
            int filterlevel = (rec->trigger >= 0) ? rec->trigger : rec->level;

            // The mask is the interested set for the channel, across every group; this thread
            // delivers only its own group's share of it. Without that check an entry that fanned
            // out to two groups would be delivered twice to every destination.
            if (!dest || dest->group != grp || filterlevel > dest->maxlevel)
                continue;

            // Anything at or below where this destination's backfill reached has already been
            // delivered to it from the boot ring, including entries that were still in this queue
            // when it registered. False for every destination that was not backfilled.
            if (dest->backfilled && !logSeqBefore(dest->backfillseq, rec->seq))
                continue;

            dest->msgfunc(rec, dest->userdata);

            // remember that we've sent something to this destination for this batch
            saPush(sent, ptr, dest, SA_Unique);
        }
    }
}

static void logNotifyBatch(_In_ sa_LogDest* sent, uint32 batchid)
{
    foreach (sarray, dest_idx, LogDest*, dest, *sent) {
        if (dest->batchfunc)
            dest->batchfunc(batchid, dest->userdata);
    }
}

_Use_decl_annotations_
int logGroupThread(Thread* self)
{
    LogGroup* grp = stvlNextPtr(&self->args);
    sa_LogQueueNode chains;
    sa_LogDest sent;
    saInit(&chains, ptr, 16);
    saInit(&sent, ptr, 16, SA_Sorted);

    while (thrLoop(self)) {
        bool empty = false;
        // grab some available log entries
        for (int i = 0; i < LOG_BATCH_SIZE; i++) {
            LogQueueNode* node = (LogQueueNode*)prqPop(&grp->queue);
            if (node) {
                saPush(&chains, ptr, node);
            } else {
                empty = true;
                break;   // queue empty
            }
        }

        // This group's dispatch lock, which excludes only the paths that deliver to one of its
        // destinations from a thread that is not this one: the synchronous backpressure write and
        // the panic flush, both in logpanic.c, plus logDestSetGroup() moving a destination in or
        // out.
        //
        // The routing version is taken inside the lock so that a mover holding it knows every
        // batch dispatched after it releases uses the routing it published.
        withMutex (&grp->dispatchlock) {
            // Take a reference to the current routing version for the whole batch. One atomic
            // load amortized over up to LOG_BATCH_SIZE entries buys a consistent view of the
            // destination table with no configuration lock held, so nothing here contends with
            // logChan() or logRegisterDest().
            LogRouting* routing = logDrainEnter(grp->drain);

            // Windows that closed while this thread was asleep, or during the last batch, get
            // their summaries here: it is the one point in the loop that holds a routing
            // version and is not in the middle of somebody's batch.
            saClear(&sent);
            logDedupFlush(grp, routing, &sent, false);
            logNotifyBatch(&sent, 0);

            foreach (sarray, chain__idx, LogQueueNode*, chain, chains) {
                LogQueueNode* node = chain;
                uint32 batchid     = 0;
                saClear(&sent);

                while (node) {
                    LogEntry* ent = node->ent;
                    batchid       = ent->batchid;

                    // one rendering shared by every text destination this record reaches;
                    // structured destinations never trigger it at all
                    LogRenderCache cache = { 0 };
                    LogRecord rec;
                    logEntryToRecord(&rec, ent, batchid, &cache);

                    if (logDedupPasses(grp, &rec))
                        logDispatchRecord(grp, routing, &rec, &sent);

                    strDestroy(&cache.str);
                    node = node->next;
                }

                // notify relevant destinations that the batch is done
                logNotifyBatch(&sent, batchid);
            }
        }

        // no reference to the routing version is held past this point
        logDrainIdle(grp->drain);

        foreach (sarray, chain_idx, LogQueueNode*, chain, chains) {
            atomicFetchSub(uint32, &grp->depth, logQueueCount(chain), Relaxed);
            logQueueFreeNodes(chain);
        }
        saClear(&chains);

        if (empty) {
            prqCollect(&grp->queue);   // run GC before event signal so it's not running during
                                       // shutdown
            eventSignal(&grp->doneevent);
            logStatsTick(grp);
            logRingTick(grp);

            // Sleep only until the earliest open deduplication window needs closing. Without
            // that a burst that stops would never get its summary, because nothing else is going
            // to wake this thread.
            int64 wait = logDedupWait(grp);
            if (wait == timeForever)
                eventWait(&self->notify);
            else
                eventWaitTimeout(&self->notify, wait);
        }
    }

    // one last pass so a window that was open at shutdown still says what it swallowed
    withMutex (&grp->dispatchlock) {
        LogRouting* routing = logDrainEnter(grp->drain);
        saClear(&sent);
        logDedupFlush(grp, routing, &sent, true);
        logNotifyBatch(&sent, 0);
    }
    logDrainIdle(grp->drain);
    logDedupDestroy(grp);

    saDestroy(&sent);
    saDestroy(&chains);

    return 0;
}

static void logGroupFlush(_Inout_ LogGroup* grp)
{
    if (!grp->thread)
        return;

    eventReset(&grp->doneevent);
    eventSignal(&grp->thread->notify);
    eventWait(&grp->doneevent);

    // signal the thread twice because the event above may be from a partially-complete run
    // that was already processing when this function was called

    eventReset(&grp->doneevent);
    eventSignal(&grp->thread->notify);
    eventWait(&grp->doneevent);
}

void logFlush(void)
{
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    // Every group in turn. A record that fanned out to several groups is only accounted for when
    // the last of them has drained, which is what makes logFlush() mean "everything logged
    // before this call has reached every destination" and not merely "one thread caught up".
    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++) logGroupFlush(_log_grouptab[i]);

    // Every drain thread is now idle and therefore quiescent, so this is the cheapest possible
    // moment to reclaim retired routing versions and destinations. It also makes an
    // unregistered destination's closefunc run by the time a caller that flushes expects it to.
    withMutex (&_log_op_lock) {
        logRoutingSweep();
    }
}

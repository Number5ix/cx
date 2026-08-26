// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/time.h>

// Sampling, statistics and the periodic metrics record.
//
// Everything here is counters and configuration. The two mechanisms that need real machinery
// live elsewhere: drain-side deduplication in logdedup.c and the synchronous write path in
// logpanic.c, which the backpressure policy shares with logPanicFlush().
atomic(uint64) _log_stat_enqueued;
atomic(uint64) _log_stat_dropped;
atomic(uint64) _log_stat_sampled;
atomic(uint64) _log_stat_suppressed;
atomic(uint64) _log_stat_sync;

// Records this severe or worse are written from the logging thread rather than dropped when a
// queue is full. Dropping a Fatal silently is the worst failure mode the system has, so this
// defaults to on.
atomic(int32) _log_synclevel = atomicInit(LOG_Error);

static atomic(int64) _log_statsinterval;   // 0 disables
static atomic(int64) _log_statslast;       // clockTimer() of the last emission

_Use_decl_annotations_
void logChanSetSampling(LogChannel* chan, uint32 n)
{
    logCheckInit();
    if (!chan)
        chan = LogDefault;

    // Restart the count as well, so the first record after a rate is configured is kept rather
    // than landing wherever a previous rate left the counter.
    atomicStore(uint32, &chan->samplecnt, 0, Relaxed);
    atomicStore(uint32, &chan->sample, (n > 1) ? n : 0, Release);
}

_Use_decl_annotations_
bool logSamplePasses(LogChannel* chan, int level, uint32* rate)
{
    *rate = 1;

    uint32 n = atomicLoad(uint32, &chan->sample, Relaxed);
    if (n < 2)
        return true;

    // A sampling rate is set for the traffic that needs thinning, which is never the traffic
    // that matters most. An Error discarded because someone sampled a chatty channel is a bug
    // report nobody can answer, so severity wins over the rate unconditionally.
    if (level <= LOG_Error)
        return true;

    // Not serialized, for the same reason the call-site gates are not: an exact 1-in-N needs a
    // lock on the hot path, and the point of sampling is that it is nearly free.
    if ((atomicFetchAdd(uint32, &chan->samplecnt, 1, Relaxed) % n) != 0) {
        atomicFetchAdd(uint64, &_log_stat_sampled, 1, Relaxed);
        return false;
    }

    *rate = n;
    return true;
}

void logSetSyncLevel(int level)
{
    atomicStore(int32, &_log_synclevel, level, Release);
}

_Use_decl_annotations_
void logGetStats(LogStats* out)
{
    memset(out, 0, sizeof(LogStats));

    out->enqueued    = atomicLoad(uint64, &_log_stat_enqueued, Relaxed);
    out->dropped     = atomicLoad(uint64, &_log_stat_dropped, Relaxed);
    out->sampled     = atomicLoad(uint64, &_log_stat_sampled, Relaxed);
    out->suppressed  = atomicLoad(uint64, &_log_stat_suppressed, Relaxed);
    out->synchronous = atomicLoad(uint64, &_log_stat_sync, Relaxed);

    uint32 n    = atomicLoad(uint32, &_log_ngroups, Acquire);
    out->groups = n;
    for (uint32 i = 0; i < n; i++) {
        out->queued += atomicLoad(uint32, &_log_grouptab[i]->depth, Relaxed);
        out->queuedmax += atomicLoad(uint32, &_log_grouptab[i]->peak, Relaxed);
    }
}

void logResetStats(void)
{
    atomicStore(uint64, &_log_stat_enqueued, 0, Relaxed);
    atomicStore(uint64, &_log_stat_dropped, 0, Relaxed);
    atomicStore(uint64, &_log_stat_sampled, 0, Relaxed);
    atomicStore(uint64, &_log_stat_suppressed, 0, Relaxed);
    atomicStore(uint64, &_log_stat_sync, 0, Relaxed);

    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++)
        atomicStore(uint32, &_log_grouptab[i]->peak, 0, Relaxed);
}

void logSetStatsInterval(int64 interval)
{
    logCheckInit();
    atomicStore(int64, &_log_statsinterval, interval, Release);
    atomicStore(int64, &_log_statslast, clockTimer(), Release);
}

STR_CONST(kLogStatsChan, "cx/log/stats");
// Every argument is keyed, both so a structured destination gets named fields and because a
// keyed argument is invisible to an unkeyed placeholder -- the text form has to name them too.
STR_CONST(kLogStatsMsg,
          "queued ${uint:queued} (peak ${uint:peak}) across ${uint:groups} groups; "
          "logged ${uint:enqueued}, dropped ${uint:dropped}, sampled ${uint:sampled}, "
          "suppressed ${uint:suppressed}, written inline ${uint:synchronous}");

_Use_decl_annotations_
void logStatsTick(LogGroup* grp)
{
    // Only one group emits, so the record does not multiply by group count. The default group is
    // the one guaranteed to exist and the one most likely to be awake.
    if (grp->idx != 0)
        return;

    int64 interval = atomicLoad(int64, &_log_statsinterval, Relaxed);
    if (interval <= 0)
        return;

    int64 now  = clockTimer();
    int64 last = atomicLoad(int64, &_log_statslast, Relaxed);
    if (now - last < interval)
        return;
    atomicStore(int64, &_log_statslast, now, Relaxed);

    // Restricted, so it reaches only a destination that names it: metrics do not belong in a
    // general-purpose log by accident.
    static LogChannel* statschan;
    if (!statschan)
        statschan = logDeclareChan(kLogStatsChan, 0);
    if (!statschan)
        return;

    LogStats st;
    logGetStats(&st);

    // Logged from the drain thread, which is safe because this runs after the dispatch lock has
    // been released: enqueueing takes no lock, and the record simply comes back around on the
    // next iteration.
    logFmtC(Notice,
            statschan,
            kLogStatsMsg,
            stvark(queued, uint32, st.queued),
            stvark(peak, uint32, st.queuedmax),
            stvark(groups, uint32, st.groups),
            stvark(enqueued, uint64, st.enqueued),
            stvark(dropped, uint64, st.dropped),
            stvark(sampled, uint64, st.sampled),
            stvark(suppressed, uint64, st.suppressed),
            stvark(synchronous, uint64, st.synchronous));
}

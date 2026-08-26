// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/string.h>
#include <cx/time.h>

// Drain-thread deduplication.
//
// This is the counterpart to the call-site gating in log.h, not a replacement for it. A call site
// throttling itself with logStrEveryN() is nearly free but can only see its own arrivals; this
// sees every site, and because it is downstream of the queue it can say what it suppressed --
// "...and 4,812 more like this in the last 10s", which the call site has no way to know.
//
// The key is the address of the call site's LogSite: stable for the life of the process, unique
// per call site, disclosing nothing, and a strictly better key than hashing the message. Two
// sites that happen to log the same sentence keep separate budgets, and one site whose message
// varies per record still shares one.
//
// The table is per group and touched only by that group's drain thread, so none of it is
// synchronized and none of it needs to be.

STR_CONST(kLogDedupMsg,
          "${string} ... and ${uint:suppressed} more like this in the last ${uint:seconds}s");

static atomic(int64) _log_dedupwindow;   // 0 disables
static atomic(uint32) _log_dedupthreshold;

typedef struct LogDedupState {
    const LogSite* site;   // the table key, kept here so a sweep can remove without a lookup
    int64 winstart;
    uint32 count;        // records from this site in the current window
    uint32 suppressed;   // ...of which this many were not delivered
    string sample;       // rendering of the first suppressed record, for the summary
    LogChannel* chan;
    int level;
} LogDedupState;

void logSetDedup(int64 window, uint32 threshold)
{
    logCheckInit();
    atomicStore(uint32, &_log_dedupthreshold, threshold ? threshold : 1, Release);
    atomicStore(int64, &_log_dedupwindow, window, Release);
}

static void logDedupFreeState(_Pre_valid_ _Post_invalid_ LogDedupState* st)
{
    strDestroy(&st->sample);
    xaFree(st);
}

// Emits one summary in place of everything a window swallowed. Built on the stack: a record is a
// view, so nothing here needs an entry, a queue, or an allocation beyond the rendering itself.
static void logDedupSummary(_In_ LogGroup* grp, _In_opt_ LogRouting* routing,
                            _In_ LogDedupState* st, int64 window, _Inout_ sa_LogDest* sent)
{
    uint32 secs = (uint32)(timeToMsec(window) / 1000);
    stvar args[] = { stvar(string, st->sample),
                     stvark(suppressed, uint32, st->suppressed),
                     stvark(seconds, uint32, secs ? secs : 1) };

    LogRenderCache cache = { 0 };
    LogRecord rec        = { 0 };
    rec.level            = st->level;
    rec.chan             = st->chan;
    rec.timestamp        = clockWall();
    rec.seq              = logNextSeq();
    rec.msgtmpl          = kLogDedupMsg;
    rec.args             = args;
    rec.nargs            = 3;
    rec.istmpl           = true;
    rec.sample           = 1;
    rec.trigger          = -1;
    rec._cache           = &cache;

    logDispatchRecord(grp, routing, &rec, sent);
    strDestroy(&cache.str);
}

_Use_decl_annotations_
bool logDedupPasses(LogGroup* grp, const LogRecord* rec)
{
    int64 window = atomicLoad(int64, &_log_dedupwindow, Relaxed);
    if (window <= 0 || !rec->site)
        return true;

    if (!grp->dedup)
        htInit(&grp->dedup, ptr, ptr, 16);

    LogDedupState* st = NULL;
    if (!htFind(grp->dedup, ptr, (void*)rec->site, ptr, &st)) {
        st           = xaAllocStruct(LogDedupState, XA_Zero);
        st->site     = rec->site;
        st->winstart = clockTimer();
        htInsert(&grp->dedup, ptr, (void*)rec->site, ptr, st);
    }

    // Windows are opened and counted here but only ever closed by logDedupFlush(), which runs
    // once per drain iteration with a routing version in hand. A window that expires in the
    // middle of a batch therefore keeps suppressing until the top of the next iteration, which
    // costs a few extra suppressed records and saves the whole question of emitting a summary
    // from the middle of someone else's batch.
    st->chan  = rec->chan;
    st->level = rec->level;

    uint32 threshold = atomicLoad(uint32, &_log_dedupthreshold, Relaxed);
    if (++st->count <= threshold)
        return true;

    ++st->suppressed;
    atomicFetchAdd(uint64, &_log_stat_suppressed, 1, Relaxed);

    // keep the first one that was actually dropped, so the summary says what was suppressed
    if (strEmpty(st->sample))
        logRecordRender(&st->sample, rec);

    return false;
}

_Use_decl_annotations_
void logDedupFlush(LogGroup* grp, LogRouting* routing, sa_LogDest* sent, bool all)
{
    if (!grp->dedup)
        return;

    int64 window = atomicLoad(int64, &_log_dedupwindow, Relaxed);
    int64 now    = clockTimer();

    // Collect first, act second: emitting a summary runs destination callbacks, and a callback
    // is not something to run in the middle of a hashtable iteration.
    sa_ptr expired;
    saInit(&expired, ptr, 8);

    foreach (hashtable, hti, grp->dedup) {
        LogDedupState* st = (LogDedupState*)htiVal(ptr, hti);
        if (all || (window > 0 && now - st->winstart >= window))
            saPush(&expired, ptr, st);
    }

    foreach (sarray, idx, LogDedupState*, st, expired) {
        if (st->suppressed > 0)
            logDedupSummary(grp, routing, st, window, sent);

        if (st->count == 0) {
            // nothing arrived in a whole window, so the site has gone quiet; forget it rather
            // than keep a row per call site the process has ever reached
            htRemove(&grp->dedup, ptr, (void*)st->site);
            logDedupFreeState(st);
            continue;
        }

        st->winstart   = now;
        st->count      = 0;
        st->suppressed = 0;
        strDestroy(&st->sample);
    }

    saDestroy(&expired);
}

_Use_decl_annotations_
int64 logDedupWait(LogGroup* grp)
{
    int64 window = atomicLoad(int64, &_log_dedupwindow, Relaxed);
    if (window <= 0 || !grp->dedup || htSize(grp->dedup) == 0)
        return timeForever;

    // The thread is about to sleep, so nothing else will close these windows. Wake for the
    // earliest of them instead, which is what makes a burst that stops still produce its summary.
    int64 now  = clockTimer();
    int64 wait = window;
    foreach (hashtable, hti, grp->dedup) {
        LogDedupState* st = (LogDedupState*)htiVal(ptr, hti);
        int64 left        = st->winstart + window - now;
        if (left < wait)
            wait = left;
    }

    return (wait > 0) ? wait : 0;
}

_Use_decl_annotations_
void logDedupDestroy(LogGroup* grp)
{
    if (!grp->dedup)
        return;

    foreach (hashtable, hti, grp->dedup) {
        logDedupFreeState((LogDedupState*)htiVal(ptr, hti));
    }
    htDestroy(&grp->dedup);
}

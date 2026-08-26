// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/string.h>
#include <cx/time.h>

// Bounded retention rings and the boot window.
//
// A ring holds references to entries nobody has asked for yet. Its default outcome is discard --
// what makes it affordable is that nothing is copied on the way in and nothing is rendered until
// something releases it.
//
// The entries are held by reference rather than serialized on the way in. Eager serialization
// would be the right answer for a ring feeding a *remote* subscriber: nothing application-owned
// stays pinned, and there is no framing to guess. But there is no neutral structured form to
// serialize into until forwarding lands, and a locally replayed record has to arrive at a
// destination as a LogRecord regardless. Holding references costs a retention footprint that is
// bounded in count by the ring caps but unbounded in depth -- an object argument keeps whatever it
// transitively owns alive for as long as it sits in a ring. Revisit when there is a wire format to
// serialize into.
//
// Nothing here takes _log_op_lock on a path a log call can reach. A destination's closefunc runs
// under that lock, and a closefunc that logs would deadlock against itself.

typedef struct LogRing {
    Mutex lock;
    LogEntry** ents;    // circular buffer of references, oldest at head
    uint32 cap;
    uint32 count;
    uint32 head;
    uint64 bytes;       // approximate footprint of what is held
    uint64 maxbytes;    // 0 for no byte cap
    int maxlevel;
    int trigger;        // level that releases this ring, -1 if only an explicit call does
    bool keepoldest;    // stop accepting when full, rather than evicting the oldest
} LogRing;

// Approximate, and only ever measured against a cap the caller chose as a rough bound: the header
// and argument array are exact, the template is counted even though it is usually a COW reference
// to a literal, and what an object argument transitively owns is not counted at all.
static uint64 logRingEntryBytes(_In_ const LogEntry* ent)
{
    return sizeof(LogEntry) + sizeof(stvar) * (uint64)(ent->nargs > 0 ? ent->nargs : 0) +
           strLen(ent->msgtmpl);
}

_Use_decl_annotations_
LogRing* logRingCreate(int maxlevel, uint32 cap, uint64 maxbytes, int trigger, bool keepoldest)
{
    LogRing* ring = xaAllocStruct(LogRing, XA_Zero);
    mutexInit(&ring->lock);
    ring->ents       = xaAlloc(sizeof(LogEntry*) * cap, XA_Zero);
    ring->cap        = cap;
    ring->maxbytes   = maxbytes;
    ring->maxlevel   = maxlevel;
    ring->trigger    = trigger;
    ring->keepoldest = keepoldest;
    return ring;
}

_Use_decl_annotations_
int logRingMaxLevel(LogRing* ring)
{
    return ring->maxlevel;   // fixed at creation and at reconfiguration, both under _log_op_lock
}

static void logRingClearLocked(_Inout_ LogRing* ring)
{
    for (uint32 i = 0; i < ring->count; i++)
        logEntryRelease(ring->ents[(ring->head + i) % ring->cap]);

    ring->count = 0;
    ring->head  = 0;
    ring->bytes = 0;
}

_Use_decl_annotations_
void logRingClear(LogRing* ring)
{
    withMutex (&ring->lock) {
        logRingClearLocked(ring);
    }
}

_Use_decl_annotations_
void logRingReconfigure(LogRing* ring, int maxlevel, uint32 cap, uint64 maxbytes, int trigger)
{
    withMutex (&ring->lock) {
        logRingClearLocked(ring);

        if (cap != ring->cap) {
            xaFree(ring->ents);
            ring->ents = xaAlloc(sizeof(LogEntry*) * cap, XA_Zero);
            ring->cap  = cap;
        }
        ring->maxlevel = maxlevel;
        ring->maxbytes = maxbytes;
        ring->trigger  = trigger;
    }
}

_Use_decl_annotations_
void logRingDestroy(LogRing** pring)
{
    LogRing* ring = *pring;
    if (!ring)
        return;
    *pring = NULL;

    logRingClear(ring);
    mutexDestroy(&ring->lock);
    xaFree(ring->ents);
    xaFree(ring);
}

_Use_decl_annotations_
uint32 logRingCount(LogRing* ring)
{
    uint32 n = 0;
    withMutex (&ring->lock) {
        n = ring->count;
    }
    return n;
}

_Use_decl_annotations_
bool logRingPush(LogRing* ring, LogEntry* ent)
{
    if (ent->level > ring->maxlevel)
        return false;

    uint64 sz  = logRingEntryBytes(ent);
    bool taken = false;

    withMutex (&ring->lock) {
        bool full = (ring->count == ring->cap) ||
                    (ring->maxbytes && ring->bytes + sz > ring->maxbytes);

        if (full && ring->keepoldest)
            break;   // this ring came for the beginning; the rest is not more interesting

        // evict until there is room -- a byte cap can need several
        while (ring->count > 0 && (ring->count == ring->cap ||
                                   (ring->maxbytes && ring->bytes + sz > ring->maxbytes))) {
            LogEntry* old = ring->ents[ring->head];
            ring->bytes -= logRingEntryBytes(old);
            ring->head = (ring->head + 1) % ring->cap;
            --ring->count;
            logEntryRelease(old);
        }

        // an entry larger than the whole byte cap has emptied the ring above and still does not
        // fit; keep it anyway rather than retain nothing at all
        ring->ents[(ring->head + ring->count) % ring->cap] = logEntryAcquire(ent);
        ++ring->count;
        ring->bytes += sz;
        taken = true;
    }

    return taken;
}

_Use_decl_annotations_
uint32 logRingTake(LogRing* ring, LogEntry*** out, bool drain)
{
    LogEntry** ents = NULL;
    uint32 n        = 0;

    withMutex (&ring->lock) {
        if (ring->count == 0)
            break;

        // Flattened oldest-first into an array the caller owns, so that releasing a ring -- which
        // runs destination callbacks, and can log -- never happens with the ring lock held.
        n    = ring->count;
        ents = xaAlloc(sizeof(LogEntry*) * n);
        for (uint32 i = 0; i < n; i++) {
            LogEntry* ent = ring->ents[(ring->head + i) % ring->cap];
            ents[i]       = drain ? ent : logEntryAcquire(ent);
        }

        if (drain) {
            // the references move to the caller rather than being dropped
            ring->count = 0;
            ring->head  = 0;
            ring->bytes = 0;
        }
    }

    *out = ents;
    return n;
}

_Use_decl_annotations_
void logRingFreeTaken(LogEntry** ents, uint32 n)
{
    for (uint32 i = 0; i < n; i++)
        logEntryRelease(ents[i]);
    xaFree(ents);
}

// ---------------------------------------------------------------------------------------
// the boot window
// ---------------------------------------------------------------------------------------

// Read by the routing table to raise every channel's ceiling while the window is open, so that
// entries no destination wants still get built. -1 is "no window", which is also the state a
// window that has hit its deadline is left in until a drain thread tidies up after it.
atomic(int32) _log_bootlevel = atomicInit(-1);

// Created on the first logBootWindowBegin() of a run and destroyed only at shutdown, so that a
// capturing thread can read the pointer with no lock and never race a free. Closing a window
// empties the ring; it does not take it away.
static atomic(ptr) _log_bootring;
static atomic(int64) _log_bootdeadline;   // clockTimer() value, 0 for no deadline

static _Ret_opt_valid_ LogRing* logBootRing(void)
{
    return (LogRing*)atomicLoad(ptr, &_log_bootring, Acquire);
}

bool logBootWindowActive(void)
{
    return atomicLoad(int32, &_log_bootlevel, Relaxed) >= 0;
}

_Use_decl_annotations_
void logBootWindowBegin(int maxlevel, uint32 maxentries, uint64 maxbytes, int64 duration)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || maxlevel < 0)
        return;

    if (maxentries == 0)
        maxentries = LOG_BOOT_DEFAULT_ENTRIES;
    if (duration == 0)
        duration = LOG_BOOT_DEFAULT_DURATION;

    withMutex (&_log_op_lock) {
        if (logBootWindowActive())
            break;   // reopening an open window would silently discard what it has

        LogRing* ring = logBootRing();
        if (!ring) {
            // keepoldest: the window exists for what happened at startup, and the interesting part
            // of a startup is its beginning. A ring that evicted would end up holding whatever the
            // process was doing when it filled, which is the least useful window into it.
            ring = logRingCreate(maxlevel, maxentries, maxbytes, -1, true);
            atomicStore(ptr, &_log_bootring, ring, Release);
        } else {
            logRingReconfigure(ring, maxlevel, maxentries, maxbytes, -1);
        }

        atomicStore(int64,
                    &_log_bootdeadline,
                    (duration > 0) ? (clockTimer() + duration) : 0,
                    Relaxed);

        // the ceiling has to rise before anything can be retained, and every channel's ceiling is
        // owned by the routing table
        atomicStore(int32, &_log_bootlevel, maxlevel, Release);
        logRoutingPublish();
    }
}

void logBootWindowEnd(void)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    LogRing* ring = logBootRing();
    if (!ring)
        return;

    withMutex (&_log_op_lock) {
        // Unconditional rather than gated on the window still being open: a window closed by its
        // deadline has already dropped the level and left the tidying to whoever came next.
        atomicStore(int32, &_log_bootlevel, -1, Release);
        logRoutingPublish();
    }

    // outside the lock, because releasing an entry can free an object whose destructor is
    // somebody else's code, and that code may log
    logRingClear(ring);
}

uint32 logBootWindowCount(void)
{
    LogRing* ring = logBootRing();
    return ring ? logRingCount(ring) : 0;
}

static void logBootCapture(_In_ LogEntry* ent)
{
    // The fast path out is the level, which is -1 whenever no window is open.
    int32 boot = atomicLoad(int32, &_log_bootlevel, Relaxed);
    if (boot < 0 || ent->level > boot)
        return;

    // The deadline is checked on the way past rather than on a timer, because the log system has
    // no clock of its own. Closing it here only drops the level, which is lock-free; the routing
    // recompute and the discard are left to a drain thread, because this path is reachable from
    // inside _log_op_lock by way of a destination callback that logs.
    int64 deadline = atomicLoad(int64, &_log_bootdeadline, Relaxed);
    if (deadline != 0 && clockTimer() >= deadline) {
        atomicStore(int32, &_log_bootlevel, -1, Release);
        return;
    }

    LogRing* ring = logBootRing();
    if (ring)
        logRingPush(ring, ent);
}

// ---------------------------------------------------------------------------------------
// the retroactive debug ring
// ---------------------------------------------------------------------------------------

// Every debug ring ever created, so that a ring detached from a channel is not freed while a
// thread that read the channel's pointer a moment ago is still holding it. Configuration churn is
// rare and a detached ring is emptied, so the cost of keeping the object until shutdown is one
// header; the cost of getting the lifetime wrong is a use-after-free on the logging path.
static sa_ptr _log_debugrings;

static _Ret_opt_valid_ LogRing* logChanRing(_In_ LogChannel* chan)
{
    return (LogRing*)atomicLoad(ptr, &chan->ring, Acquire);
}

// Propagates ring ownership down the channel tree. Channels are appended in creation order and a
// parent is always created before its children, so one forward pass resolves the whole tree --
// the same property the visibility gate pass relies on. Called under _log_op_lock.
static void logChanUpdateRingsAll(void)
{
    foreach (sarray, idx, LogChannel*, chan, _log_chans) {
        if (chan->ownring || !chan->parent)
            continue;
        atomicStore(ptr, &chan->ring, atomicLoad(ptr, &chan->parent->ring, Relaxed), Release);
    }
}

_Use_decl_annotations_
bool logChanSetDebugRing(LogChannel* chan, int maxlevel, uint32 maxentries, int triglevel)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || maxlevel < 0)
        return false;

    if (!chan)
        chan = LogDefault;
    if (maxentries == 0)
        maxentries = LOG_DEBUGRING_DEFAULT_ENTRIES;

    withMutex (&_log_op_lock) {
        LogRing* ring = chan->ownring ? logChanRing(chan) : NULL;
        if (ring) {
            logRingReconfigure(ring, maxlevel, maxentries, 0, triglevel);
        } else {
            // keepoldest false: the context of a failure is whatever immediately preceded it, so
            // a full debug ring evicts. That is the one place it differs from the boot window.
            ring = logRingCreate(maxlevel, maxentries, 0, triglevel, false);
            if (!_log_debugrings.a)
                saInit(&_log_debugrings, ptr, 4);
            saPush(&_log_debugrings, ptr, ring);

            chan->ownring = true;
            atomicStore(ptr, &chan->ring, ring, Release);
            logChanUpdateRingsAll();
        }

        // the ceiling has to rise before anything can be retained
        logRoutingPublish();
    }

    return true;
}

_Use_decl_annotations_
void logChanClearDebugRing(LogChannel* chan)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    if (!chan)
        chan = LogDefault;

    LogRing* ring = NULL;
    withMutex (&_log_op_lock) {
        if (!chan->ownring)
            break;
        ring = logChanRing(chan);

        chan->ownring = false;
        atomicStore(ptr,
                    &chan->ring,
                    chan->parent ? atomicLoad(ptr, &chan->parent->ring, Relaxed) : NULL,
                    Release);
        logChanUpdateRingsAll();
        logRoutingPublish();
    }

    // The ring object outlives its detachment; only its contents go, and outside the lock,
    // because releasing an entry can run somebody else's destructor.
    if (ring)
        logRingClear(ring);
}

_Use_decl_annotations_
uint32 logChanDebugRingCount(LogChannel* chan)
{
    if (!atomicLoad(bool, &_log_running, Acquire))
        return 0;
    if (!chan)
        chan = LogDefault;

    LogRing* ring = logChanRing(chan);
    return ring ? logRingCount(ring) : 0;
}

_Use_decl_annotations_
int logChanRingLevel(LogChannel* chan)
{
    LogRing* ring = logChanRing(chan);
    return ring ? logRingMaxLevel(ring) : -1;
}

// Releases everything a ring holds into the destinations that route each entry's channel, ahead
// of the record that triggered it. The entries were never fanned out -- only records no
// destination wanted are retained -- so their batch chain is free to use.
//
// Their *inline* queue nodes are not, which is why this fans out as a replay. Retention is
// decided against destlevel a moment before fan-out reads it, so a destination appearing in
// between is enough for one of these entries to have been queued after all, and it would then be
// holding the node this would otherwise hand to a second group.
static void logRingRelease(_Inout_ LogRing* ring, int triglevel)
{
    LogEntry** ents = NULL;
    uint32 n        = logRingTake(ring, &ents, true);
    if (n == 0)
        return;

    LogEntry* head = NULL;
    LogEntry* tail = NULL;
    for (uint32 i = 0; i < n; i++) {
        ents[i]->trigger = triglevel;
        ents[i]->_next   = NULL;
        if (tail)
            tail->_next = ents[i];
        else
            head = ents[i];
        tail = ents[i];
    }
    xaFree(ents);

    // consumes the references the ring handed over
    logFanout(head, false);
}

static void logDebugCapture(_In_ LogEntry* ent)
{
    LogRing* ring = logChanRing(ent->chan);
    if (!ring)
        return;

    // Retain only what no destination wanted. A record that is being delivered anyway would
    // otherwise arrive a second time when the ring is released, which is the one way this feature
    // can corrupt a log rather than merely add to it.
    if (ent->level > atomicLoad(int32, &ent->chan->destlevel, Relaxed))
        logRingPush(ring, ent);

    if (ring->trigger >= 0 && ent->level <= ring->trigger)
        logRingRelease(ring, ent->level);
}

_Use_decl_annotations_
void logRingCapture(LogEntry* ent)
{
    logBootCapture(ent);
    logDebugCapture(ent);
}

_Use_decl_annotations_
void logRingTick(LogGroup* grp)
{
    // One group does the tidying, for the same reason one group emits the metrics record.
    if (grp->idx != 0 || logBootWindowActive())
        return;

    LogRing* ring = logBootRing();
    if (ring && logRingCount(ring) > 0)
        logBootWindowEnd();
}

_Use_decl_annotations_
void logRingReplay(LogDest* dest)
{
    if (!logBootWindowActive())
        return;

    LogRing* ring = logBootRing();
    if (!ring)
        return;

    LogEntry** ents = NULL;
    uint32 n        = logRingTake(ring, &ents, false);
    if (n == 0)
        return;

    // The destination is not published yet, so nothing else can be delivering to it and no lock
    // is needed to keep the two apart -- the backfill runs entirely on the registering thread,
    // ahead of anything live, and excludes no one.
    uint32 batchid = 0;
    bool any       = false;

    // How far this backfill reaches, over the whole snapshot rather than only the entries that
    // survived the filter: an entry the filter rejected here is rejected on the live path too, so
    // covering it costs nothing and leaves no gap between the two rules.
    for (uint32 i = 0; i < n; i++) {
        if (!dest->backfilled || logSeqBefore(dest->backfillseq, ents[i]->seq)) {
            dest->backfillseq = ents[i]->seq;
            dest->backfilled  = true;
        }
    }

    for (uint32 i = 0; i < n; i++) {
        LogEntry* ent = ents[i];
        if (ent->level > dest->maxlevel || !logChanRuleMatch(dest, ent->chan))
            continue;

        LogRenderCache cache = { 0 };
        LogRecord rec;
        logEntryToRecord(&rec, ent, ent->batchid, &cache);
        batchid = ent->batchid;
        any     = true;

        dest->msgfunc(&rec, dest->userdata);
        strDestroy(&cache.str);
    }

    if (any && dest->batchfunc)
        dest->batchfunc(batchid, dest->userdata);

    logRingFreeTaken(ents, n);
}

void logRingShutdown(void)
{
    // Called with every drain thread stopped and nothing capturing. Channels outlive a
    // shutdown/restart cycle, so their ring pointers have to be cleared here as well: a stale one
    // would keep building entries for a ring that is no longer there.
    atomicStore(int32, &_log_bootlevel, -1, Release);

    LogRing* ring = logBootRing();
    atomicStore(ptr, &_log_bootring, NULL, Release);
    logRingDestroy(&ring);

    foreach (sarray, cidx, LogChannel*, chan, _log_chans) {
        chan->ownring = false;
        atomicStore(ptr, &chan->ring, NULL, Release);
    }

    foreach (sarray, ridx, void*, dring, _log_debugrings) {
        LogRing* r = (LogRing*)dring;
        logRingDestroy(&r);
    }
    saDestroy(&_log_debugrings);
}

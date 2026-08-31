// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>

// Versioned routing table and its reclamation scheme.
//
// Writers build a new LogRouting under _log_op_lock and swap it in with one atomic store.
// Drain threads take a reference to the current version once per batch and walk it without
// taking any lock, which is what keeps channel creation and destination registration from
// blocking behind a destination doing filesystem work.
//
// A retired version cannot simply be freed: a drain thread may have loaded the pointer and not
// yet finished with it. Reclamation is therefore quiescent-state based. The reader set is
// exactly the drain threads, which are enumerable, hold a reference for at most one batch, and
// have a natural quiescent point when they go to sleep. Each publishes the generation it is
// working at; anything retired at generation G is freeable once every drain thread is either
// idle or working at a generation at least as new as G.
//
// Ordering matters in both directions: the version pointer is swapped *before* the generation
// is bumped, so a drain thread that has published generation G can only be holding a version at
// least as new as G. In the other direction a drain thread publishes its epoch before loading
// the version pointer.

static atomic(ptr) _log_routing;

// Last published generation. Generations start at 1, so LOG_QUIESCENT (0) can never be mistaken
// for a generation a drain thread is working at.
static atomic(uint32) _log_routing_gen;

typedef struct LogRetired LogRetired;
struct LogRetired {
    LogRetired* next;
    uint32 gen;            // generation at which this object became unreachable
    LogRouting* routing;   // exactly one of routing/dest is set
    LogDest* dest;
};
static LogRetired* _log_retired;

static sa_LogDrain _log_drains;

void logRoutingInit(void)
{
    // called from logInit, which runs single-threaded under lazy init or the run lock
    if (!_log_drains.a)
        saInit(&_log_drains, ptr, 4);
    atomicStore(uint32, &_log_routing_gen, 0, Relaxed);
    atomicStore(ptr, &_log_routing, NULL, Release);
}

static void logRetire(_In_opt_ LogRouting* routing, _In_opt_ LogDest* dest, uint32 gen)
{
    if (!routing && !dest)
        return;

    LogRetired* r = xaAllocStruct(LogRetired, XA_Zero);
    r->gen        = gen;
    r->routing    = routing;
    r->dest       = dest;
    r->next       = _log_retired;
    _log_retired  = r;
}

// Minimum mask width: a floor of 128 destinations rather than 64, because a large server
// filtering dozens of subsystems into separate files reaches 64 more easily than it looks, and a
// limit that is usually sufficient is the worst kind.
#define LOG_MIN_MASK_WORDS 2

#define LOG_ROUTING_HDR ((sizeof(LogRouting) + 7) & ~(size_t)7)

static _Ret_valid_ LogRouting* logRoutingAlloc(uint32 nchan)
{
    uint32 ndest  = saSize(_log_dests);
    uint32 nwords = (ndest + 63) / 64;
    if (nwords < LOG_MIN_MASK_WORDS)
        nwords = LOG_MIN_MASK_WORDS;

    size_t masksz  = (size_t)nchan * nwords * sizeof(uint64);
    LogRouting* nr = xaAlloc(LOG_ROUTING_HDR + masksz + (size_t)ndest * sizeof(LogDest*), XA_Zero);

    nr->ndest    = ndest;
    nr->nchan    = nchan;
    nr->nwords   = nwords;
    nr->destmask = (uint64*)((uint8*)nr + LOG_ROUTING_HDR);
    nr->dests    = (LogDest**)((uint8*)nr->destmask + masksz);

    for (uint32 i = 0; i < ndest; i++)
        nr->dests[i] = _log_dests.a[i];

    return nr;
}

// how much room to leave for channels that have not been interned yet
static uint32 logRoutingChanCap(uint32 nchan)
{
    return nchan + nchan / 2 + 8;
}

// Evaluates every destination's filter against one channel, filling in its row of the
// destination mask and returning the level ceiling and the set of drain groups that falls out of
// it. All matching happens here, at bind time, which is what lets a filter be arbitrarily
// expressive without costing a cycle per message.
//
// The group set is the destination mask reduced to one word: the enqueue path needs to know
// *which queues* to push to, not which destinations, and it runs on arbitrary caller threads
// where taking a routing-version reference would be a real cost.
static int32 logRoutingChanRow(_In_ LogRouting* routing, _In_ LogChannel* chan,
                               _Out_ uint32* groupmask, _Out_ int32* destlevel)
{
    uint64* row    = &routing->destmask[(size_t)chan->idx * routing->nwords];
    int32 maxlevel = -1;
    uint32 gmask   = 0;

    // one split for the whole destination table: every rule of every destination matches against
    // these same components, and the rules arrived pre-split, so nothing below this line splits
    sa_string comps;
    logChanSplitPath(&comps, chan->path);

    // Loop prevention, first layer: a destination that leaves the machine never binds to cx's own
    // transport channel.
    bool transport = logChanIsTransport(chan);

    for (uint32 i = 0; i < routing->ndest; i++) {
        // A destination that wants nothing is left out of the row rather than given a level
        // nothing passes. Dispatch applies the same test, so the mask bit alone would reach no
        // one -- but its group bit would still cost a queue node, a refcount and a drain thread
        // wakeup on every record some other destination wanted on this channel. That's what an
        // unsubscribed forwarder costs if it stays in the mask.
        LogDest* dest = routing->dests[i];
        if (!dest || dest->maxlevel < 0 || (transport && dest->remote) ||
            !logChanRuleMatchComps(dest, chan, &comps))
            continue;

        row[i / 64] |= (uint64)1 << (i % 64);
        gmask |= (uint32)1 << dest->group->idx;
        if (dest->maxlevel > maxlevel)
            maxlevel = dest->maxlevel;
    }

    saDestroy(&comps);

    // What a destination wants is reported separately from the ceiling, because the difference
    // between the two is exactly the set of records that exist only to be retained -- which is
    // what a debug ring keeps and what it must not keep twice.
    *destlevel = maxlevel;

    // A retention ring wants entries no destination asked for, and the call site decides whether
    // an entry exists at all by this ceiling alone. Folding the ring levels in here rather than
    // adding a second field to check keeps the enqueue path exactly as cheap as it was; the
    // per-destination filter at dispatch is what stops the extra records from reaching anyone.
    int32 ring = atomicLoad(int32, &_log_bootlevel, Relaxed);
    int32 dbg  = logChanRingLevel(chan);
    if (dbg > ring)
        ring = dbg;
    if (ring > maxlevel)
        maxlevel = ring;

    *groupmask = gmask;
    return maxlevel;
}

static uint32 logRoutingPublishVersion(_In_ LogRouting* nr)
{
    LogRouting* old = (LogRouting*)atomicExchange(ptr, &_log_routing, nr, AcqRel);

    // the generation must be bumped only after the new version is visible, so that a drain
    // thread publishing this generation cannot still be holding the old one
    uint32 gen = atomicFetchAdd(uint32, &_log_routing_gen, 1, Release) + 1;

    logRetire(old, NULL, gen);
    return gen;
}

uint32 logRoutingPublish(void)
{
    uint32 nchan   = saSize(_log_chans);
    LogRouting* nr = logRoutingAlloc(logRoutingChanCap(nchan));

    int32* levels  = xaAlloc(sizeof(int32) * (nchan ? nchan : 1), XA_Zero);
    int32* dlevels = xaAlloc(sizeof(int32) * (nchan ? nchan : 1), XA_Zero);
    uint32* gmasks = xaAlloc(sizeof(uint32) * (nchan ? nchan : 1), XA_Zero);
    for (uint32 i = 0; i < nchan; i++)
        levels[i] = logRoutingChanRow(nr, _log_chans.a[i], &gmasks[i], &dlevels[i]);

    // The enqueue path reads only the level ceiling, the destination ceiling and the group set,
    // so ordering those updates around the publish is what keeps reconfiguration from losing a
    // message: all three widen before the destination that wanted them becomes visible, and
    // narrow only once it is gone. A stale read costs at most an entry that is enqueued and then
    // discarded during dispatch -- or, for destlevel, one that fan-out skips a moment before a
    // new destination that would have taken it becomes reachable at all.
    for (uint32 i = 0; i < nchan; i++) {
        LogChannel* chan = _log_chans.a[i];
        if (levels[i] > atomicLoad(int32, &chan->maxlevel, Relaxed))
            atomicStore(int32, &chan->maxlevel, levels[i], Release);
        if (dlevels[i] > atomicLoad(int32, &chan->destlevel, Relaxed))
            atomicStore(int32, &chan->destlevel, dlevels[i], Release);
        atomicFetchOr(uint32, &chan->groupmask, gmasks[i], Release);
    }

    uint32 gen = logRoutingPublishVersion(nr);

    for (uint32 i = 0; i < nchan; i++) {
        atomicStore(int32, &_log_chans.a[i]->maxlevel, levels[i], Release);
        atomicStore(int32, &_log_chans.a[i]->destlevel, dlevels[i], Release);
        atomicStore(uint32, &_log_chans.a[i]->groupmask, gmasks[i], Release);
    }

    xaFree(gmasks);
    xaFree(dlevels);
    xaFree(levels);
    return gen;
}

_Use_decl_annotations_
void logRoutingAddChan(LogChannel* chan)
{
    LogRouting* cur = (LogRouting*)atomicLoad(ptr, &_log_routing, Acquire);
    uint32 gmask    = 0;
    int32 dlevel    = -1;

    if (cur && chan->idx < cur->nchan) {
        // The row is unreachable until the channel pointer is published, so it can be written
        // into the live version in place: no new version, and no grace period at all. Adding a
        // channel never changes any other channel's row.
        int32 level = logRoutingChanRow(cur, chan, &gmask, &dlevel);
        atomicStore(uint32, &chan->groupmask, gmask, Release);
        atomicStore(int32, &chan->destlevel, dlevel, Release);
        atomicStore(int32, &chan->maxlevel, level, Release);
        return;
    }

    // out of room, so grow geometrically; this happens O(log n) times over a process lifetime
    uint32 nchan   = saSize(_log_chans);
    LogRouting* nr = logRoutingAlloc(logRoutingChanCap(nchan + 1) * 2);

    uint32 unused;
    int32 unusedlevel;
    for (uint32 i = 0; i < nchan; i++)
        logRoutingChanRow(nr, _log_chans.a[i], &unused, &unusedlevel);

    int32 level = logRoutingChanRow(nr, chan, &gmask, &dlevel);
    atomicStore(uint32, &chan->groupmask, gmask, Release);
    atomicStore(int32, &chan->destlevel, dlevel, Release);
    atomicStore(int32, &chan->maxlevel, level, Release);

    logRoutingPublishVersion(nr);
}

_Use_decl_annotations_
void logRoutingRetireDest(LogDest* dest, uint32 gen)
{
    logRetire(NULL, dest, gen);
}

bool logRoutingSlotPending(uint32 idx)
{
    // a slot may not be handed out again until its previous occupant has been reclaimed,
    // otherwise a drain thread walking a stale version would route to a different destination
    // than the one the version was built for
    for (LogRetired* r = _log_retired; r; r = r->next) {
        if (r->dest && r->dest->idx == idx)
            return true;
    }
    return false;
}

static bool logDrainsPast(uint32 gen)
{
    foreach (sarray, idx, LogDrain*, drain, _log_drains) {
        uint32 epoch = atomicLoad(uint32, &drain->epoch, Acquire);
        if (epoch == LOG_QUIESCENT)
            continue;   // idle threads hold no references at all
        if ((int32)(epoch - gen) < 0)
            return false;
    }
    return true;
}

static void logReclaim(_Pre_valid_ _Post_invalid_ LogRetired* r)
{
    if (r->dest) {
        if (r->dest->closefunc)
            r->dest->closefunc(r->dest->userdata);
        logDestFreeRules(r->dest);
        xaFree(r->dest);
    }
    xaFree(r->routing);
    xaFree(r);
}

void logRoutingSweep(void)
{
    LogRetired** prev = &_log_retired;
    LogRetired* r     = _log_retired;

    while (r) {
        LogRetired* next = r->next;
        if (logDrainsPast(r->gen)) {
            *prev = next;
            logReclaim(r);
        } else {
            prev = &r->next;
        }
        r = next;
    }
}

void logRoutingShutdown(void)
{
    // Channels outlive the log system, so their level ceilings have to come down with the
    // destinations that raised them; otherwise a call site would keep allocating entries for a
    // system that is no longer running.
    foreach (sarray, idx, LogChannel*, chan, _log_chans) {
        atomicStore(int32, &chan->maxlevel, -1, Release);
        atomicStore(uint32, &chan->groupmask, 0, Release);
    }

    // every drain thread has exited by this point, so nothing can be holding a reference
    LogRetired* r = _log_retired;
    _log_retired  = NULL;
    while (r) {
        LogRetired* next = r->next;
        logReclaim(r);
        r = next;
    }

    LogRouting* cur = (LogRouting*)atomicExchange(ptr, &_log_routing, NULL, AcqRel);
    xaFree(cur);
}

_Use_decl_annotations_
LogDrain* logDrainRegister(void)
{
    LogDrain* drain = xaAllocStruct(LogDrain, XA_Zero);
    atomicStore(uint32, &drain->epoch, LOG_QUIESCENT, Release);

    withMutex (&_log_op_lock) {
        saPush(&_log_drains, ptr, drain);
    }
    return drain;
}

_Use_decl_annotations_
void logDrainUnregister(LogDrain* drain)
{
    withMutex (&_log_op_lock) {
        for (int32 i = saSize(_log_drains) - 1; i >= 0; --i) {
            if (_log_drains.a[i] == drain)
                saRemove(&_log_drains, i);
        }
    }
    xaFree(drain);
}

_Use_decl_annotations_
LogRouting* logRoutingCurrentUnsafe(void)
{
    return (LogRouting*)atomicLoad(ptr, &_log_routing, Acquire);
}

_Use_decl_annotations_
LogRouting* logDrainEnter(LogDrain* drain)
{
    // publish the generation being worked at before loading the version pointer
    atomicStore(uint32,
                &drain->epoch,
                atomicLoad(uint32, &_log_routing_gen, Acquire),
                Release);
    return (LogRouting*)atomicLoad(ptr, &_log_routing, Acquire);
}

_Use_decl_annotations_
void logDrainIdle(LogDrain* drain)
{
    atomicStore(uint32, &drain->epoch, LOG_QUIESCENT, Release);
}

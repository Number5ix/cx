#pragma once

#include <cx/container.h>
#include <cx/thread.h>
#include <cx/utils/lazyinit.h>
#include "log.h"
#include "logctx.h"
#include "loggroup.h"
#include "logring.h"
#include "logserializer.h"
#include "logvolume.h"

#define LOG_INITIAL_QUEUE_SIZE 32
#define LOG_MAX_QUEUE_SIZE     262144

extern atomic(bool) _log_running;
extern Mutex _log_run_lock;

// One include or exclude rule of a destination's channel filter. litdepth is the number of path
// components before the rule's first wildcard, which is both its specificity and what decides
// whether it can pass a restricted channel's gate.
//
// comps is the pattern already split on '/', for the same reason litdepth is precomputed: a
// routing recompute evaluates every rule of every destination against every channel, and none of
// that work depends on the channel.
typedef struct LogFilterRule {
    string pattern;
    sa_string comps;
    uint32 litdepth;
    bool exclude;
} LogFilterRule;

typedef struct LogDest {
    LogFilterRule* rules;
    uint32 nrules;
    LogDestMsg msgfunc;
    LogDestBatchDone batchfunc;
    LogDestClose closefunc;
    void* userdata;
    int maxlevel;
    uint32 idx;        // stable slot index in the destination table
    LogGroup* group;   // drain group whose thread does this destination's work; never NULL

    // Where this destination's history begins, for one backfilled from the boot ring.
    //
    // The ring captures an entry when it is logged, not when it is delivered, so an entry can be
    // in the ring *and* still sitting in a queue. Backfilling would then deliver it twice: once
    // from the ring, once when the drain thread gets to the queue. Recording how far the backfill
    // reached and dropping anything at or below it closes that, and needs no lock -- both fields
    // are written while the destination is still private to the registering thread and published
    // with it.
    //
    // The rule this states is that the backfill *defines* where the destination's log starts.
    // Records older than the end of it either came from it or predate this destination entirely;
    // either way they are not delivered a second time.
    uint64 backfillseq;
    bool backfilled;   // ...and whether there was a backfill at all, since 0 is a valid sequence
} LogDest;
saDeclarePtr(LogDest);
saDeclarePtr(LogChannel);

// A queued record, before any destination has seen it.
//
// The entry keeps the message template and a copy of the arguments rather than a rendered
// string, so formatting happens once on the drain thread instead of on whichever thread logged
// the message, and structured destinations get fields rather than a sentence to re-parse. An
// entry from logStr() carries its message in the same field but is marked not-a-template, because
// a literal message that happens to contain ${...} must survive rendering unchanged.
//
// An entry is refcounted because it is delivered to every drain group interested in it and
// destroyed by whichever of them finishes last.
typedef struct LogEntry LogEntry;

// One entry's presence in one group's queue. The queue element is a chain of these: a chain is
// one push, which is what keeps a batch from being interleaved with another thread's entries.
//
// Most entries reach exactly one group, so an entry carries one of these inline and only the
// second and later groups allocate; see LogEntry.inlnode and logQueueNodeIsInline().
typedef struct LogQueueNode LogQueueNode;
typedef struct LogQueueNode {
    LogQueueNode* next;
    LogEntry* ent;   // holds a reference
} LogQueueNode;
saDeclarePtr(LogQueueNode);

typedef struct LogEntry {
    // Chain used only while a batch is being accumulated on the logging thread, before the
    // entries reach any queue. Once enqueued a batch is held together by LogQueueNode instead,
    // because each group sees a different subset of the batch and one pointer cannot express two
    // chains.
    //
    // It is deliberately not shared with inlnode.next. logRingRelease() rebuilds _next on entries
    // it takes from a debug ring, and one of those can still be sitting in a group's queue, so
    // aliasing the two would let the logging thread corrupt a chain a drain thread is walking.
    LogEntry* _next;

    // Queue node for the first group this entry reaches, so that the overwhelmingly common
    // single-group case allocates nothing beyond the entry itself and transfers the creator's
    // reference rather than taking a second one.
    //
    // Only the fan-out that follows the entry's creation claims it, on the creating thread, which
    // is what makes the claim race-free with no flag to guard it. An entry being fanned out a
    // second time -- logRingRelease() replaying a debug ring -- allocates its nodes, because it
    // cannot know whether the first fan-out queued this one.
    LogQueueNode inlnode;

    // One per queue node. The creator's reference is transferred into the inline node when it is
    // claimed, or released outright when no group wanted the entry.
    atomic(uint32) refs;
    int64 timestamp;
    uint64 seq;            // monotonic global sequence; see logNextSeq()
    LogChannel* chan;
    const LogSite* site;   // call site identity, NULL for a dynamically generated entry
    string msgtmpl;
    stvar* args;           // points into the tail of this allocation, NULL when nargs is 0
    LogCtx* ctx;           // snapshot of the logging thread's context; owned
    int nargs;
    int level;
    uint32 batchid;   // assigned at enqueue, so every group sees the same one
    uint32 sample;    // sampling rate this entry survived; 0 or 1 for none
    int trigger;      // severity that released this entry from a ring; -1 if it never was
    bool istmpl;      // msgtmpl is a format template, not a literal message
} LogEntry;
saDeclarePtr(LogEntry);

// Is this node the one embedded in the entry it refers to?
//
// Such a node is part of the entry's allocation, so it is never freed on its own -- and because
// releasing the entry may free the node's own storage, nothing may touch the node after that
// release.
_meta_inline bool logQueueNodeIsInline(_In_ const LogQueueNode* node)
{
    return node == &node->ent->inlnode;
}

// Immutable snapshot of the destination table and the per-channel routing masks. A new version is
// built under _log_op_lock and published with a single atomic pointer store; drain threads take a
// reference once per batch and walk it with no lock held. Slots are stable: an entry is NULL if
// the slot is free, and a slot is not handed out again until its previous occupant has been
// reclaimed.
//
// The masks live here rather than inline on the channel because at the design's floor of 128
// destinations they are multi-word, and a multi-word mask cannot be read atomically -- the
// version pointer is what makes them consistent to read at any width. The channel dimension is
// sized with slack so that interning a channel, which is a lazy per-call-site event, does not
// produce a version at all.
typedef struct LogRouting {
    uint32 ndest;       // number of destination slots
    uint32 nchan;       // channel rows this version has room for
    uint32 nwords;      // (ndest + 63) / 64, minimum 2
    LogDest** dests;    // ndest entries, NULL for a free slot
    uint64* destmask;   // nchan * nwords, row for channel i starts at i * nwords
    // storage for both arrays follows the header
} LogRouting;

// Idle state of one drain thread. The thread publishes the generation it is working at before it
// touches a routing version, and LOG_QUIESCENT while it is asleep holding no references at all.
#define LOG_QUIESCENT 0
typedef struct LogDrain {
    atomic(uint32) epoch;
} LogDrain;
saDeclarePtr(LogDrain);

// A named drain group. The struct is permanent -- the registry survives
// logShutdown()/logRestart() the same way the channel registry does -- but the queue and thread
// exist only while the log system is running.
typedef struct LogGroup {
    string name;
    uint32 idx;
    PrQueue queue;
    Thread* thread;
    LogDrain* drain;
    Event doneevent;

    // Excludes destination callbacks for the destinations that belong to this group. The drain
    // thread holds it across its dispatch loop; logWriteSync() and logPanicFlush() take it to
    // deliver from a thread that is not the drain.
    Mutex dispatchlock;

    atomic(uint32) depth;   // entries waiting in this group's queue
    atomic(uint32) peak;    // high-water mark of the above
    hashtable dedup;        // LogSite* -> LogDedupState*, drain-thread private
    int64 dedupdue;         // when the earliest open dedup window closes, 0 if none
} LogGroup;

// Groups are looked up on the enqueue path with no lock, so the table is a fixed array published
// by count rather than a growable one: a group is written into its slot before the count that
// makes it visible is bumped, and groups are never removed.
extern LogGroup* _log_grouptab[LOG_GROUP_MAX];
extern atomic(uint32) _log_ngroups;

// Protects the configuration side of the log system: _log_dests, _log_channels, the group table,
// the routing version pointer and the retire list. Drain threads never take this lock, so
// creating a channel or registering a destination cannot block behind a destination doing I/O.
extern Mutex _log_op_lock;
extern sa_LogDest _log_dests;
extern hashtable _log_channels;    // channel path -> LogChannel*
extern sa_LogChannel _log_chans;   // indexed by LogChannel.idx

extern LazyInitState _logInitState;

// Monotonic global sequence number, assigned once per entry when the entry is created.
//
// Wall-clock timestamps are not an ordering: they can jump, and two entries logged on different
// threads in the same microsecond are indistinguishable by time. A single atomic increment gives
// exact cross-thread ordering, which is what recovers the true order of entries that reached the
// queue out of order -- the per-thread overflow chain in logqueue.c is exactly that case, since
// it holds entries back and re-pushes them behind later ones.
uint64 logNextSeq(void);

// Wrap-aware ordering test, the same discipline TCP sequence numbers use: correct as long as the
// two are less than half the counter's range apart, which holds for anything being collated
// within a batch window.
//
// Not really needed anymore with 64-bit sequence numbers, but keep anyway just in case somebody
// keeps a process running for like 1000 years or something.
_meta_inline bool logSeqBefore(uint64 a, uint64 b)
{
    return (int64)(a - b) < 0;
}

void logCheckInit(void);

// Builds an entry, deep-copying the arguments so the caller's temporaries can go out of scope.
// Assigns a fresh sequence number, once, for the life of the entry: replaying an entry never
// renumbers it, which is what lets a backfill be compared against one. Returns NULL if the
// allocation could not be satisfied.
_Ret_opt_valid_ LogEntry*
logEntryCreate(int level, int64 timestamp, _In_ LogChannel* chan, _In_opt_ const LogSite* site,
               _In_opt_ strref tmpl, int nargs, _In_opt_ stvar* args, _In_opt_ LogCtx* ctx);
// Fills in the destination-facing view of an entry. cache may be NULL, in which case every
// destination that renders the record pays for its own rendering.
void logEntryToRecord(_Out_ LogRecord* rec, _In_ const LogEntry* ent, uint32 batchid,
                      _In_opt_ LogRenderCache* cache);

// Delivers a chain of entries to every drain group interested in it, consuming the caller's
// reference on each. The chain is threaded through LogEntry._next.
//
// `fresh` says these entries have never been fanned out before, so their inline queue nodes are
// free to claim. It is true for the enqueue path, where the entries were just created on this
// thread, and false for a replay (logRingRelease), where an entry may already be queued.
void logFanout(_In_opt_ LogEntry* head, bool fresh);

// Entry references. A fan-out takes one per queue node; the last release destroys the entry.
_Ret_valid_ LogEntry* logEntryAcquire(_In_ LogEntry* ent);
void logEntryRelease(_In_ LogEntry* ent);

// Pushes a chain of queue nodes onto one group's queue as a single unit, so nothing interleaves
// with a batch. Never blocks; on overflow the chain is held on a per-thread, per-group list, and
// if that is full too the chain is dropped -- except for entries at or above the synchronous
// level, which are written from this thread instead.
void logQueueAdd(_In_ LogGroup* group, _In_ LogQueueNode* head, uint32 nents);
// Releases the entries a chain of queue nodes refers to and frees the nodes.
void logQueueFreeNodes(_In_opt_ LogQueueNode* head);
// Number of entries in a chain of queue nodes.
uint32 logQueueCount(_In_opt_ LogQueueNode* head);

// Drain groups (loggroup.c). logGroupInit() runs under the run lock or lazy init and creates the
// queues; the threads are started separately, because starting one takes _log_op_lock.
void logGroupInit(void);
void logGroupStartAll(void);
void logGroupStopAll(void);
void logGroupShutdown(void);
int logGroupThread(_Inout_ Thread* self);

// Delivers one record to every destination of this group that its channel routes to. Shared by
// the drain loop, the deduplicator's summary records and logPanicFlush(). `sent` accumulates the
// destinations that were reached, for the batch-done callbacks.
void logDispatchRecord(_In_ LogGroup* grp, _In_opt_ LogRouting* routing, _In_ const LogRecord* rec,
                       _Inout_ sa_LogDest* sent);
// The live routing version, with no grace-period reference taken. Only for a caller that is not
// a registered drain thread and accepts the risk: logPanicFlush() in a dying process.
_Ret_opt_valid_ LogRouting* logRoutingCurrentUnsafe(void);

// Volume control (logvolume.c, logdedup.c).
extern atomic(uint64) _log_stat_enqueued;
extern atomic(uint64) _log_stat_dropped;
extern atomic(uint64) _log_stat_sampled;
extern atomic(uint64) _log_stat_suppressed;
extern atomic(uint64) _log_stat_sync;
extern atomic(int32) _log_synclevel;

// Decides whether a record on this channel survives sampling, counting it if it does not. Runs
// at the call site, before an entry exists.
bool logSamplePasses(_In_ LogChannel* chan, int level, _Out_ uint32* rate);

// Drain-side deduplication. Returns false if the record should not be delivered. Both are called
// only from a drain thread, which is what makes the per-group table lock-free.
bool logDedupPasses(_In_ LogGroup* grp, _In_ const LogRecord* rec);
// Emits summaries for every window that has closed. Called with a routing version in hand.
void logDedupFlush(_In_ LogGroup* grp, _In_opt_ LogRouting* routing, _Inout_ sa_LogDest* sent,
                   bool all);
void logDedupDestroy(_Inout_ LogGroup* grp);
// How long the drain thread may sleep before a dedup window needs closing; timeForever if none.
int64 logDedupWait(_In_ LogGroup* grp);

// Periodic self-logging of the statistics (logvolume.c). Called by a drain thread when it goes
// idle; does nothing until the configured interval has elapsed.
void logStatsTick(_In_ LogGroup* grp);

// Retention rings (logring.c).
//
// A ring holds references to entries no destination asked for, until something releases them.
// keepoldest stops accepting once the ring is full instead of evicting, which is what the boot
// window wants and the retroactive debug ring does not.
typedef struct LogRing LogRing;
_Ret_valid_ LogRing* logRingCreate(int maxlevel, uint32 cap, uint64 maxbytes, int trigger,
                                   bool keepoldest);
int logRingMaxLevel(_In_ LogRing* ring);
void logRingDestroy(_Inout_ LogRing** pring);
void logRingClear(_Inout_ LogRing* ring);
void logRingReconfigure(_Inout_ LogRing* ring, int maxlevel, uint32 cap, uint64 maxbytes,
                        int trigger);
uint32 logRingCount(_Inout_ LogRing* ring);
// Retains an entry, taking a reference. False if the ring did not want it.
bool logRingPush(_Inout_ LogRing* ring, _In_ LogEntry* ent);
// Flattens the ring oldest-first into an array the caller owns and must pass to
// logRingFreeTaken(). With drain set the ring is emptied and its references move to the caller.
uint32 logRingTake(_Inout_ LogRing* ring, _Outptr_ LogEntry*** out, bool drain);
void logRingFreeTaken(_Pre_valid_ _Post_invalid_ LogEntry** ents, uint32 n);

// Most verbose level any open ring retains, or -1. Folded into every channel's ceiling by the
// routing table, because an entry no destination wants is otherwise dropped before it exists.
extern atomic(int32) _log_bootlevel;

// Offers a freshly created entry to whatever rings want it. Called on the logging thread.
void logRingCapture(_In_ LogEntry* ent);
// Most verbose level the ring covering this channel retains, or -1 if it has none.
int logChanRingLevel(_In_ LogChannel* chan);
// Backfills a destination that is not published yet from the boot ring.
void logRingReplay(_In_ LogDest* dest);
// Finishes closing a boot window that hit its deadline. Called by a drain thread when idle.
void logRingTick(_In_ LogGroup* grp);
void logRingShutdown(void);

// Writes a chain of queue nodes from the calling thread, skipping anything less severe than
// minlevel. The backpressure path for entries a full queue would otherwise drop (logpanic.c).
void logWriteSync(_In_ LogGroup* grp, _In_opt_ LogQueueNode* head, int minlevel);

// Channel registry (logchan.c). The registry is permanent and is built exactly once.
void logChanInit(void);
uint32 logChanLitDepth(_In_ strref pattern);
bool logChanMatch(_In_ strref pattern, _In_opt_ strref path);
// Splits a channel path or filter pattern into the component form the matcher works in. The
// output is initialized by this call and belongs to the caller.
void logChanSplitPath(_Inout_ sa_string* _Nonnull out, _In_opt_ strref path);
// Does this destination's filter reach this channel? Evaluated at bind time only.
bool logChanRuleMatch(_In_ LogDest* dest, _In_ LogChannel* chan);
// Same, for a caller that is testing many destinations against one channel and has already split
// its path with logChanSplitPath().
bool logChanRuleMatchComps(_In_ LogDest* dest, _In_ LogChannel* chan, _In_ sa_string* _Nonnull comps);

// Routing table versioning and reclamation (logrouting.c). Everything but the drain-side epoch
// calls must be called with _log_op_lock held.
void logRoutingInit(void);
// Rebuilds every channel's routing mask and level ceiling from _log_dests, publishes the new
// version and retires the previous one. Called whenever a destination or a channel declaration
// changes what matches what. Returns the generation the new version was published at.
uint32 logRoutingPublish(void);
// Computes the row for a channel that is about to become reachable, growing the table only if
// there is no room left. Adding a channel never changes any other channel's row.
void logRoutingAddChan(_Inout_ LogChannel* chan);
void logRoutingRetireDest(_In_ LogDest* dest, uint32 gen);
bool logRoutingSlotPending(uint32 idx);
// Frees everything that has become unreachable and whose grace period has expired.
void logRoutingSweep(void);
// Unconditional teardown; only valid once every drain thread has exited.
void logRoutingShutdown(void);

_Ret_opt_valid_ LogDrain* logDrainRegister(void);
void logDrainUnregister(_Pre_valid_ _Post_invalid_ LogDrain* drain);
// Publishes the generation the drain thread is about to work at and returns the routing version
// to use for this batch. Must be paired with logDrainIdle() before the thread sleeps.
_Ret_opt_valid_ LogRouting* logDrainEnter(_Inout_ LogDrain* drain);
void logDrainIdle(_Inout_ LogDrain* drain);

// does NOT free dhandle, the caller is responsible for retiring it
bool logUnregisterDestLocked(_In_ LogDest* dhandle);
// inserts into a free slot of the destination table; does not publish
void logDestInsertLocked(_In_ LogDest* dest);
void logDestAddRuleLocked(_Inout_ LogDest* dest, _In_opt_ strref pattern, bool exclude);
void logDestFreeRules(_Inout_ LogDest* dest);

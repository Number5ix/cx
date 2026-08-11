// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>
#include <cx/time.h>
#include "log/logsnapshot.h"

atomic(bool) _log_running;
Mutex _log_op_lock;
Mutex _log_run_lock;

// see logNextSeq() in log_private.h for why this is a uintptr
static atomic(uintptr) _log_seq;

uintptr logNextSeq(void)
{
    return atomicFetchAdd(uintptr, &_log_seq, 1, Relaxed) + 1;
}

// Batch ids are assigned here, on the enqueueing thread, rather than by a drain thread: with more
// than one group the same batch is drained in several places and would otherwise be given a
// different id in each.
static atomic(uint32) _log_batchid;

static uint32 logNextBatchId(void)
{
    return atomicFetchAdd(uint32, &_log_batchid, 1, Relaxed) + 1;
}

typedef struct LogBatchTLS {
    LogEntry* head;
    LogEntry* tail;
    int level;
} LogBatchTLS;
static _Thread_local LogBatchTLS _log_batch;

STR_CONSTR(kLevelFatal, "Fatal");
STR_CONSTR(kLevelError, "Error");
STR_CONSTR(kLevelWarn, "Warn");
STR_CONSTR(kLevelNotice, "Notice");
STR_CONSTR(kLevelInfo, "Info");
STR_CONSTR(kLevelVerbose, "Verbose");
STR_CONSTR(kLevelDiag, "Diag");
STR_CONSTR(kLevelDebug, "Debug");
STR_CONSTR(kLevelTrace, "Trace");

STR_CONSTR(kAbbrevF, "F");
STR_CONSTR(kAbbrevE, "E");
STR_CONSTR(kAbbrevW, "W");
STR_CONSTR(kAbbrevN, "N");
STR_CONSTR(kAbbrevI, "I");
STR_CONSTR(kAbbrevV, "V");
STR_CONSTR(kAbbrevD, "D");
STR_CONSTR(kAbbrevT, "T");

strref LogLevelNames[LOG_Count] = { _SR(kLevelFatal),  _SR(kLevelError), _SR(kLevelWarn),
                                    _SR(kLevelNotice), _SR(kLevelInfo),  _SR(kLevelVerbose),
                                    _SR(kLevelDiag),   _SR(kLevelDebug), _SR(kLevelTrace) };

// note that Diag and Debug intentionally share the same abbreviation
strref LogLevelAbbrev[LOG_Count] = { _SR(kAbbrevF), _SR(kAbbrevE), _SR(kAbbrevW),
                                     _SR(kAbbrevN), _SR(kAbbrevI), _SR(kAbbrevV),
                                     _SR(kAbbrevD), _SR(kAbbrevD), _SR(kAbbrevT) };

LazyInitState _logInitState;
static void logInit(void* isrestart)
{
    devAssert(atomicLoad(bool, &_log_running, Acquire) == false);

    // if this is being run from lazy init, initialize the locks. logRestart calls this with a
    // nonzero pointer to indicate that it's a restart.
    if (!isrestart) {
        // these locks are NOT torn down by logShutdown and are only initialized once per process.
        // This especially applies to  _log_run_lock because it's used to serialize shutdown and
        // restart itself.
        mutexInit(&_log_run_lock);
        mutexInit(&_log_op_lock);
    }

    saInit(&_log_dests, ptr, 8);

    logRoutingInit();

    // Channels are permanent for the lifetime of the process and the registry deliberately
    // survives a shutdown/restart cycle, so that a cached channel pointer never dangles. Only
    // built the first time through, and after the routing table because interning the built-in
    // `cx` channel adds a row to it.
    logChanInit();

    logGroupInit();

    atomicStore(bool, &_log_running, true, Release);

    // after _log_running, because a drain thread's first act is to look at a system that has to
    // be up by then
    logGroupStartAll();
}

void logCheckInit(void)
{
    lazyInit(&_logInitState, logInit, NULL);
}

STR_CONST(kLogObjFmt, "${object}");

// Late rendering is inherent to deferral and stvarCopy cannot fix it: an object refcounted into
// an entry would render its state when the drain thread reached it, not when it was logged. So
// object arguments are rendered at the call site and everything else is deferred, decided per
// argument by type during the copy walk that is already happening.
//
// The rendering is wrapped in a LogSnapshot rather than stored as a plain string, because the
// template still says ${object} and a placeholder resolves by type; substituting a string would
// leave the placeholder with nothing to match. An object already carrying a snapshot -- a record
// replayed out of the defer buffer -- is left alone, and anything the formatter cannot render is
// left as the original object so it fails exactly the way it always did.
//
// The cost is that format options on an ${object} placeholder no longer reach the object's own
// format method: the value is rendered before the template is parsed, which is precisely what
// makes it cheap.
static void logCopyArg(_Out_ stvar* dst, _In_ stvar* src)
{
    if (stEq(stvarType(src), stType(object)) && !objDynCast(LogSnapshot, src->data.st_object)) {
        string rendered = 0;
        if (_strFormat(&rendered, kLogObjFmt, 1, src)) {
            LogSnapshot* snap = logsnapshotCreate(rendered);
            _stvarInitK(dst, stType(object), stArg(object, snap), stvarKey(src));
            objRelease(&snap);
            strDestroy(&rendered);
            return;
        }
        strDestroy(&rendered);
    }

    stvarCopy(dst, *src);
}

_Use_decl_annotations_
LogEntry* logEntryCreate(int level, int64 timestamp, LogChannel* chan, const LogSite* site,
                         strref tmpl, int nargs, stvar* args, LogCtx* ctx)
{
    if (nargs < 0)
        nargs = 0;

    // one allocation for the header and the argument array together
    LogEntry* ent = xaAlloc(sizeof(LogEntry) + sizeof(stvar) * (size_t)nargs,
                            XA_Zero | XA_Optional(High));
    if (!ent)
        return NULL;

    atomicStore(uint32, &ent->refs, 1, Relaxed);   // the creator's, released after fan-out
    ent->timestamp = (timestamp != -1) ? timestamp : clockWall();
    ent->seq       = logNextSeq();
    ent->level     = level;
    ent->chan      = chan;
    ent->site      = site;
    ent->ctx       = logCtxAcquire(ctx);
    ent->nargs     = nargs;
    ent->trigger   = -1;   // not released from a ring; see logChanSetDebugRing()
    strDup(&ent->msgtmpl, tmpl);

    if (nargs > 0) {
        ent->args = (stvar*)(ent + 1);
        for (int i = 0; i < nargs; i++) logCopyArg(&ent->args[i], &args[i]);
    }

    return ent;
}

_Use_decl_annotations_
void logEntryToRecord(LogRecord* rec, const LogEntry* ent, uint32 batchid, LogRenderCache* cache)
{
    rec->level     = ent->level;
    rec->chan      = ent->chan;
    rec->timestamp = ent->timestamp;
    rec->seq       = ent->seq;
    rec->site      = ent->site;
    rec->msgtmpl   = ent->msgtmpl;
    rec->args      = ent->args;
    rec->nargs     = ent->nargs;
    rec->ctx       = ent->ctx;
    rec->istmpl    = ent->istmpl;
    rec->trigger   = ent->trigger;
    rec->batchid   = batchid;
    rec->sample    = ent->sample ? ent->sample : 1;
    rec->_cache    = cache;
}

static void logDestroyEnt(_In_ LogEntry* ent)
{
    for (int i = 0; i < ent->nargs; i++) stvarDestroy(&ent->args[i]);
    logCtxRelease(&ent->ctx);
    strDestroy(&ent->msgtmpl);
    xaFree(ent);
}

_Use_decl_annotations_
LogEntry* logEntryAcquire(LogEntry* ent)
{
    atomicFetchAdd(uint32, &ent->refs, 1, Relaxed);
    return ent;
}

_Use_decl_annotations_
void logEntryRelease(LogEntry* ent)
{
    // AcqRel so that everything the releasing thread did with the entry happens-before the
    // destruction the last releaser performs
    if (atomicFetchSub(uint32, &ent->refs, 1, AcqRel) == 1)
        logDestroyEnt(ent);
}

// Delivers a chain of entries to every drain group interested in it.
//
// The chain cannot simply be handed to each group: two groups see different subsets of the same
// batch, and the entry has one _next pointer. So each group gets its own chain of queue nodes
// over shared, refcounted entries, with no copying of the entry, its template, its arguments or
// its context. Most entries reach exactly one group and use the node embedded in the entry, so
// the common case allocates nothing here at all.
//
// The walk is entry-major rather than group-major for two reasons: the chain is traversed once
// and each channel's groupmask read once instead of once per group, and claiming an inline node
// group-major would overwrite the links the next group still has to walk.
void logFanout(LogEntry* head, bool fresh)
{
    if (!head)
        return;

    // one id for the whole push, the same in every group
    uint32 batchid = logNextBatchId();
    for (LogEntry* ent = head; ent; ent = ent->_next) ent->batchid = batchid;

    uint32 ngroups = atomicLoad(uint32, &_log_ngroups, Acquire);
    LogQueueNode* chead[LOG_GROUP_MAX];
    LogQueueNode* ctail[LOG_GROUP_MAX];
    uint32 nents[LOG_GROUP_MAX];

    // only the groups that exist are touched, so the usual single-group process pays three stores
    for (uint32 g = 0; g < ngroups; g++) {
        chead[g] = NULL;
        ctail[g] = NULL;
        nents[g] = 0;
    }

    LogEntry* ent = head;
    while (ent) {
        // we'll need to continue walking through the batch even if the entry is released
        LogEntry* next = ent->_next;

        // whether the creator's reference has moved into the entry's own queue node
        bool inlused = false;

        // destlevel is the ceiling across every destination on the channel and dispatch applies
        // exactly this test (logthread.c), so skipping here reaches no one who would otherwise have
        // been reached. It differs from maxlevel only while a retention ring is open -- which is
        // precisely when entries exist that no destination asked for, and is what would otherwise
        // put a queue node, a wakeup and a full dispatch walk behind every record a debug ring
        // retains.
        int filterlevel = (ent->trigger >= 0) ? ent->trigger : ent->level;
        if (filterlevel <= atomicLoad(int32, &ent->chan->destlevel, Relaxed)) {
            uint32 gmask = atomicLoad(uint32, &ent->chan->groupmask, Relaxed);

            for (uint32 g = 0; g < ngroups && gmask; g++) {
                // loop through groups only so long as some are interested
                if (!(gmask & ((uint32)1 << g)))
                    continue;
                gmask &= ~((uint32)1 << g);

                LogQueueNode* node;
                if (fresh && !inlused) {
                    // the first group takes the entry's own node, and with it the creator's
                    // reference; see LogEntry.inlnode for why only a fresh entry may do this
                    node       = &ent->inlnode;
                    node->next = NULL;
                    node->ent  = ent;
                    inlused    = true;
                } else {
                    node = xaAllocStruct(LogQueueNode, XA_Zero | XA_Optional(High));
                    if (!node)
                        continue;
                    node->ent = logEntryAcquire(ent);
                }

                if (chead[g])
                    ctail[g]->next = node;
                else
                    chead[g] = node;
                ctail[g] = node;
                ++nents[g];
            }
        }

        // the creator's reference was either transferred into the inline node above or is dropped
        // here, once every group that wanted the entry holds one of its own
        if (!inlused)
            logEntryRelease(ent);

        ent = next;
    }

    for (uint32 g = 0; g < ngroups; g++) {
        if (chead[g])
            logQueueAdd(_log_grouptab[g], chead[g], nents[g]);
    }
}

static void logEnqueue(int level, int64 timestamp, _In_ LogChannel* chan,
                       _In_opt_ const LogSite* site, _In_opt_ strref tmpl, bool istmpl, int nargs,
                       _In_opt_ stvar* args, uint32 sample)
{
    // the context is snapshotted here, on the logging thread, because that is the only place it
    // is known; taking a reference is all it costs
    LogEntry*
        ent = logEntryCreate(level, timestamp, chan, site, tmpl, nargs, args, logCtxCurrent());
    if (!ent)
        return;
    ent->sample = sample;
    ent->istmpl = istmpl;

    // Before the batch, and before fan-out: a ring retains entries that no destination wants and
    // that therefore may never reach a queue at all.
    logRingCapture(ent);

    if (!_log_batch.level) {
        // straight out to the interested groups
        logFanout(ent, true);
    } else {
        // this thread is preparing a batch
        if (_log_batch.tail) {
            _log_batch.tail->_next = ent;
            _log_batch.tail        = ent;
        } else {
            _log_batch.head = ent;
            _log_batch.tail = ent;
        }
    }
}

_Use_decl_annotations_
void _logStr(int level, int64 timestamp, LogChannel* chan, const LogSite* site, strref str)
{
    lazyInit(&_logInitState, logInit, NULL);

    if (!chan)
        chan = LogDefault;

    // Early out if no destination is listening to this channel at this level. This is per
    // channel rather than one global maximum, so a Trace destination on one subsystem costs
    // nothing anywhere else in the process.
    if (level > atomicLoad(int32, &chan->maxlevel, Relaxed))
        return;

    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    // Sampling runs before the entry exists, which is the whole point of doing it at the call
    // site rather than on the drain thread.
    uint32 sample;
    if (!logSamplePasses(chan, level, &sample))
        return;

    logEnqueue(level, timestamp, chan, site, str, false, 0, NULL, sample);
}

_Use_decl_annotations_
void _logFmt(int level, int64 timestamp, LogChannel* chan, const LogSite* site, strref fmtstr,
             int n, stvar* args)
{
    lazyInit(&_logInitState, logInit, NULL);

    if (!chan)
        chan = LogDefault;

    // per-channel early out; see _logStr()
    if (level > atomicLoad(int32, &chan->maxlevel, Relaxed))
        return;

    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    // per-channel sampling; see _logStr()
    uint32 sample;
    if (!logSamplePasses(chan, level, &sample))
        return;

    // The format string becomes the message template and is not expanded here; that happens on
    // the drain thread, or not at all if every destination is structured.
    logEnqueue(level, timestamp, chan, site, fmtstr, true, n, args, sample);
}

void logBatchBegin(void)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    _log_batch.level++;
}

void logBatchEnd(void)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    devAssert(_log_batch.level > 0);
    if (--_log_batch.level == 0) {
        // send the whole batch to be fanned out to interested groups
        logFanout(_log_batch.head, true);
        _log_batch.head = NULL;
        _log_batch.tail = NULL;
    }
}

void logShutdown(void)
{
    // Implementation note: Normally the log system is initialized by lazy init. Once shut down,
    // however, the lazy init won't run again and logging will not function. The system can be
    // manually restarted by calling logRestart().

    logCheckInit();

    withMutex (&_log_run_lock) {
        if (!atomicLoad(bool, &_log_running, Acquire))
            break;

        logFlush();

        // Stop every drain thread before tearing anything down. Nothing can be dispatching to a
        // destination once they have exited, so destinations are closed directly here rather
        // than through the grace period in logRoutingShutdown(), which no longer has any reader
        // to wait for.
        logGroupStopAll();

        withMutex (&_log_op_lock) {
            // remove all log destinations
            foreach (sarray, idx, LogDest*, dest, _log_dests) {
                if (!dest)
                    continue;   // free slot
                if (dest->closefunc)
                    dest->closefunc(dest->userdata);
                logDestFreeRules(dest);
                xaFree(dest);
            }
            saDestroy(&_log_dests);

            // Channels are deliberately not freed: they are permanent for the lifetime of the
            // process and outlive the queue and the destination table.

            logRoutingShutdown();
        }

        // Only now that every channel ceiling is back to -1 can the queues go: a caller that
        // beat the teardown to the level check is already past it, and pushing onto a destroyed
        // queue is worse than the entry it would have lost anyway.
        logGroupShutdown();
        logRingShutdown();

        atomicStore(bool, &_log_running, false, Release);
    }
}

void logRestart(void)
{
    logCheckInit();

    withMutex (&_log_run_lock) {
        if (atomicLoad(bool, &_log_running, Acquire))
            break;

        // Log system was initially started by lazy init, then shut down later.
        // To restart it, we call the init function again but with the run lock held,
        // preventing a race with another shutdown.
        logInit((void*)1);
    }
}

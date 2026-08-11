// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>

// Drain groups.
//
// A group owns one queue and one drain thread. Destinations name a group, channels carry the set
// of groups their destinations live in, and the enqueueing thread reads that one word and pushes
// the entry onto each interested group's queue.
//
// Like channels, the group structs are permanent: the registry survives
// logShutdown()/logRestart() so a cached LogGroup* never dangles. Only the queue and the thread
// have the log system's lifetime. The table is a fixed array rather than a growable one because
// it is read on the enqueue path with no lock held.

STR_CONST(kLogGroupThreadDefault, "CX Log Writer");
STR_CONST(kLogGroupThreadFmt, "CX Log Writer [${string}]");

LogGroup* _log_grouptab[LOG_GROUP_MAX];
atomic(uint32) _log_ngroups;

// Must be called with _log_op_lock held. Does not start the group's thread; starting one takes
// the same lock.
static _Ret_opt_valid_ LogGroup* logGroupInternLocked(_In_opt_ strref name, _Out_ bool* created)
{
    *created = false;

    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++) {
        if (strEq(_log_grouptab[i]->name, name))
            return _log_grouptab[i];
    }

    if (n >= LOG_GROUP_MAX)
        return NULL;

    LogGroup* grp = xaAllocStruct(LogGroup, XA_Zero);
    grp->idx      = n;
    strDup(&grp->name, name);
    eventInit(&grp->doneevent);
    mutexInit(&grp->dispatchlock);
    prqInitDynamic(&grp->queue,
                   LOG_INITIAL_QUEUE_SIZE,
                   LOG_INITIAL_QUEUE_SIZE * 2,
                   LOG_MAX_QUEUE_SIZE,
                   PRQ_Grow_100,
                   PRQ_Grow_100);

    // the slot is filled before the count that makes it visible is bumped
    _log_grouptab[n] = grp;
    atomicStore(uint32, &_log_ngroups, n + 1, Release);

    *created = true;
    return grp;
}

static void logGroupStart(_Inout_ LogGroup* grp)
{
    if (grp->thread)
        return;

    string thrname = NULL;
    if (strEmpty(grp->name))
        strDup(&thrname, kLogGroupThreadDefault);
    else
        strFormat(&thrname, kLogGroupThreadFmt, stvar(string, grp->name));

    grp->drain  = logDrainRegister();
    grp->thread = thrCreate(logGroupThread, thrname, stvar(ptr, grp));
    strDestroy(&thrname);
    if (!grp->thread)
        relFatalError("Failed to create log thread");
    thrRegisterSysThread(grp->thread);
}

static void logGroupStop(_Inout_ LogGroup* grp)
{
    if (!grp->thread)
        return;

    thrRequestExit(grp->thread);
    thrWait(grp->thread, timeS(10));
    thrRelease(&grp->thread);

    logDrainUnregister(grp->drain);
    grp->drain = NULL;
}

void logGroupInit(void)
{
    // Called from logInit, which runs single-threaded under lazy init or the run lock. On a
    // restart the groups already exist; only their queues need rebuilding.
    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++) {
        prqInitDynamic(&_log_grouptab[i]->queue,
                       LOG_INITIAL_QUEUE_SIZE,
                       LOG_INITIAL_QUEUE_SIZE * 2,
                       LOG_MAX_QUEUE_SIZE,
                       PRQ_Grow_100,
                       PRQ_Grow_100);
    }

    if (n == 0) {
        bool created;
        withMutex (&_log_op_lock) {
            logGroupInternLocked(NULL, &created);
        }
    }
}

void logGroupStartAll(void)
{
    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++)
        logGroupStart(_log_grouptab[i]);
}

void logGroupStopAll(void)
{
    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++)
        logGroupStop(_log_grouptab[i]);
}

void logGroupShutdown(void)
{
    // The threads are already gone, so anything still queued will never be dispatched. The group
    // structs and their names stay: a cached LogGroup* has to survive a restart.
    uint32 n = atomicLoad(uint32, &_log_ngroups, Acquire);
    for (uint32 i = 0; i < n; i++) {
        LogGroup* grp = _log_grouptab[i];
        LogQueueNode* node;
        while ((node = (LogQueueNode*)prqPop(&grp->queue)))
            logQueueFreeNodes(node);
        prqDestroy(&grp->queue);
        atomicStore(uint32, &grp->depth, 0, Relaxed);
    }
}

_Use_decl_annotations_
LogGroup* logGroup(strref name)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return NULL;

    LogGroup* grp;
    bool created = false;

    withMutex (&_log_op_lock) {
        grp = logGroupInternLocked(name, &created);
    }

    // outside the lock: registering the drain takes it
    if (grp && created)
        logGroupStart(grp);

    return grp;
}

_Use_decl_annotations_
LogGroup* logDefaultGroup(void)
{
    // Lazy-inits like every other public accessor. Without it this is the one entry point whose
    // answer depends on whether something else has logged yet, and the first call in a process
    // would return NULL for a group that is about to exist.
    logCheckInit();
    return atomicLoad(uint32, &_log_ngroups, Acquire) > 0 ? _log_grouptab[0] : NULL;
}

_Use_decl_annotations_
strref logGroupName(LogGroup* group)
{
    return group->name;
}

_Use_decl_annotations_
bool logDestSetGroup(LogDest* dhandle, strref name)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    LogGroup* grp = logGroup(name);
    if (!grp)
        return false;

    // Flush first so that nothing already queued for the old group is stranded: once the move is
    // published, the old group's drain thread skips this destination, because dispatch only
    // reaches destinations that belong to the group doing the dispatching.
    logFlush();

    bool ret = false;
    withMutex (&_log_op_lock) {
        for (int32 i = 0; i < saSize(_log_dests); i++) {
            if (_log_dests.a[i] == dhandle) {
                ret = true;
                break;
            }
        }

        LogGroup* old = ret ? dhandle->group : NULL;

        if (ret && old != grp) {
            // Both dispatch locks are held across the move, so no drain thread can observe the
            // destination as belonging to neither group or to both. Without this, a record
            // enqueued between the flush above and the publish below could be dispatched twice or
            // not at all.
            //
            // Taken in index order because a group's index is fixed when it is created, which
            // makes that a total order and keeps two concurrent moves from deadlocking against
            // each other. Nesting them inside _log_op_lock is safe in the other direction too:
            // nothing that holds a dispatch lock ever acquires _log_op_lock, which is the same
            // property that lets a drain thread run without blocking reconfiguration.
            LogGroup* lo = (old && old->idx < grp->idx) ? old : grp;
            LogGroup* hi = (lo == grp) ? old : grp;

            mutexAcquire(&lo->dispatchlock);
            if (hi)
                mutexAcquire(&hi->dispatchlock);

            dhandle->group = grp;
            // the group set is derived from the destination table, so republish to recompute it
            logRoutingPublish();

            if (hi)
                mutexRelease(&hi->dispatchlock);
            mutexRelease(&lo->dispatchlock);
        }
    }

    return ret;
}

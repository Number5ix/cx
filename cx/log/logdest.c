// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"

sa_LogDest _log_dests;

_Use_decl_annotations_
void logDestInsertLocked(LogDest* dest)
{
    // reuse a free slot if there is one, but never one whose previous occupant is still
    // waiting to be reclaimed
    uint32 ndest = saSize(_log_dests);
    for (uint32 i = 0; i < ndest; i++) {
        if (!_log_dests.a[i] && !logRoutingSlotPending(i)) {
            dest->idx        = i;
            _log_dests.a[i] = dest;
            return;
        }
    }

    dest->idx = ndest;
    saPush(&_log_dests, ptr, dest);
}

void logDestAddRuleLocked(_Inout_ LogDest* dest, _In_opt_ strref pattern, bool exclude)
{
    if (strEmpty(pattern))
        return;

    xaResize(&dest->rules, sizeof(LogFilterRule) * (dest->nrules + 1));
    LogFilterRule* rule = &dest->rules[dest->nrules++];

    rule->pattern  = 0;
    strDup(&rule->pattern, pattern);
    logChanSplitPath(&rule->comps, pattern);
    rule->litdepth = logChanLitDepth(pattern);
    rule->exclude  = exclude;
}

void logDestFreeRules(_Inout_ LogDest* dest)
{
    for (uint32 i = 0; i < dest->nrules; i++) {
        strDestroy(&dest->rules[i].pattern);
        saDestroy(&dest->rules[i].comps);
    }
    xaDestroy(&dest->rules);
    dest->nrules = 0;
}

_Use_decl_annotations_
LogDest* logRegisterDest(int maxlevel, strref chanfilter, LogDestMsg msgfunc,
                         LogDestBatchDone batchfunc, LogDestClose closefunc, void* userdata)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return NULL;

    LogDest* ndest = xaAlloc(sizeof(LogDest), XA_Zero);

    ndest->maxlevel   = maxlevel;
    ndest->msgfunc    = msgfunc;
    ndest->batchfunc  = batchfunc;
    ndest->closefunc  = closefunc;
    ndest->userdata   = userdata;
    // everything starts on the default group; logDestSetGroup() moves it
    ndest->group      = logDefaultGroup();

    // The rules go on before the backfill, because the backfill is filtered by them. Both happen
    // while the destination is still private to this thread: nothing can be delivering to it
    // concurrently, so the boot ring replays with no lock held and cannot interleave with a live
    // record. What the backfill may overlap with is the queue -- an entry can be in the ring and
    // still waiting to be dispatched -- which is what dest->backfillseq settles.
    logDestAddRuleLocked(ndest, chanfilter, false);
    logRingReplay(ndest);

    withMutex (&_log_op_lock) {
        logRoutingSweep();   // reconfiguration is the sweep cadence

        logDestInsertLocked(ndest);
        logRoutingPublish();
    }
    return ndest;
}

_Use_decl_annotations_
bool logDestAddFilter(LogDest* dhandle, strref pattern, bool exclude)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        // only touch a destination that is actually registered; a retired handle is on its way
        // to being freed and its rules are about to become irrelevant
        for (int32 i = 0; i < saSize(_log_dests); i++) {
            if (_log_dests.a[i] == dhandle) {
                ret = true;
                break;
            }
        }

        if (ret) {
            logDestAddRuleLocked(dhandle, pattern, exclude);
            logRoutingPublish();
        }
    }

    return ret;
}

_Use_decl_annotations_
bool logUnregisterDestLocked(LogDest* dhandle)
{
    bool ret = false;

    for (int32 i = saSize(_log_dests) - 1; i >= 0; --i) {
        if (_log_dests.a[i] == dhandle) {
            _log_dests.a[i] = NULL;
            ret             = true;
        }
    }

    if (!ret)
        return false;

    // publish the removal first -- which also lowers every channel ceiling this destination was
    // holding up -- then retire the destination at the generation that no longer references it;
    // the handle and its closefunc survive until the grace period expires
    uint32 gen = logRoutingPublish();
    logRoutingRetireDest(dhandle, gen);

    return ret;
}

_Use_decl_annotations_
bool logUnregisterDest(LogDest* dhandle)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        logRoutingSweep();
        ret = logUnregisterDestLocked(dhandle);
    }

    return ret;
}

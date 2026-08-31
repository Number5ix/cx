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

static void logDestFreeRuleList(_Inout_ LogFilterRule** rules, _Inout_ uint32* n)
{
    for (uint32 i = 0; i < *n; i++) {
        strDestroy(&(*rules)[i].pattern);
        saDestroy(&(*rules)[i].comps);
    }
    xaDestroy(rules);
    *n = 0;
}

void logDestFreeRules(_Inout_ LogDest* dest)
{
    logDestFreeRuleList(&dest->rules, &dest->nrules);
    logDestFreeRuleList(&dest->subrules, &dest->nsubrules);
}

_Use_decl_annotations_
void logDestSetSubRulesLocked(LogDest* dest, const sa_string* patterns)
{
    logDestFreeRuleList(&dest->subrules, &dest->nsubrules);

    uint32 n = patterns ? saSize(*patterns) : 0;
    for (uint32 i = 0; i < n; i++) {
        if (strEmpty(patterns->a[i]))
            continue;

        xaResize(&dest->subrules, sizeof(LogFilterRule) * (dest->nsubrules + 1));
        LogFilterRule* rule = &dest->subrules[dest->nsubrules++];

        rule->pattern = 0;
        strDup(&rule->pattern, patterns->a[i]);
        logChanSplitPath(&rule->comps, patterns->a[i]);
        rule->litdepth = logChanLitDepth(patterns->a[i]);
        rule->exclude  = false;
    }
}

// Is this handle a destination that is actually registered? A retired one is on its way to being
// freed and its configuration is about to become irrelevant.
static bool logDestLiveLocked(_In_ const LogDest* dhandle)
{
    for (int32 i = 0; i < saSize(_log_dests); i++) {
        if (_log_dests.a[i] == dhandle)
            return true;
    }
    return false;
}

static _Ret_opt_valid_ LogDest*
logRegisterDestInternal(int maxlevel, _In_opt_ strref chanfilter, _In_ LogDestMsg msgfunc,
                        _In_opt_ LogDestBatchDone batchfunc, _In_opt_ LogDestClose closefunc,
                        _In_opt_ void* userdata, bool remote)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return NULL;

    LogDest* ndest = xaAlloc(sizeof(LogDest), XA_Zero);

    // Loop prevention's first layer reads this during the bind below, so it has to be set before
    // the destination is inserted rather than turned on afterwards: a forwarder that was briefly
    // bound to cx/net would forward the transport's own chatter for exactly as long as that
    // window lasted.
    ndest->remote     = remote;
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
LogDest* logRegisterDest(int maxlevel, strref chanfilter, LogDestMsg msgfunc,
                         LogDestBatchDone batchfunc, LogDestClose closefunc, void* userdata)
{
    return logRegisterDestInternal(maxlevel, chanfilter, msgfunc, batchfunc, closefunc, userdata,
                                   false);
}

_Use_decl_annotations_
LogDest* logRegisterRemoteDest(int maxlevel, strref chanfilter, LogDestMsg msgfunc,
                               LogDestBatchDone batchfunc, LogDestClose closefunc, void* userdata)
{
    return logRegisterDestInternal(maxlevel, chanfilter, msgfunc, batchfunc, closefunc, userdata,
                                   true);
}

_Use_decl_annotations_
bool logDestAddFilter(LogDest* dhandle, strref pattern, bool exclude)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        ret = logDestLiveLocked(dhandle);

        if (ret) {
            logDestAddRuleLocked(dhandle, pattern, exclude);
            logRoutingPublish();
        }
    }

    return ret;
}

_Use_decl_annotations_
bool logDestSetFilter(LogDest* dhandle, strref pattern)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        ret = logDestLiveLocked(dhandle);
        if (!ret)
            break;

        // Only the local rule set; a subscription's rules are separate and are not disturbed by
        // reconfiguring what this process is willing to offer.
        for (uint32 i = 0; i < dhandle->nrules; i++) {
            strDestroy(&dhandle->rules[i].pattern);
            saDestroy(&dhandle->rules[i].comps);
        }
        xaDestroy(&dhandle->rules);
        dhandle->nrules = 0;

        logDestAddRuleLocked(dhandle, pattern, false);
        logRoutingPublish();
    }

    return ret;
}

_Use_decl_annotations_
bool logDestSetSubFilter(LogDest* dhandle, const sa_string* patterns)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        ret = logDestLiveLocked(dhandle);
        if (!ret)
            break;

        logDestSetSubRulesLocked(dhandle, patterns);
        logRoutingPublish();
    }

    return ret;
}

_Use_decl_annotations_
bool logDestSubscribe(LogDest* dhandle, const sa_string* patterns, int maxlevel)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    mutexAcquire(&_log_op_lock);
    if (!logDestLiveLocked(dhandle)) {
        mutexRelease(&_log_op_lock);
        return false;
    }

    // Acquire dispatch lock before replay: only this group's drain thread delivers here.
    // Lock order: _log_op_lock > dispatchlock (as established in logDestSetGroup).
    LogGroup* grp = dhandle->group;
    mutexAcquire(&grp->dispatchlock);

    // Publish rules before replay: keeps records queued instead of dropped during backfill.
    logDestSetSubRulesLocked(dhandle, patterns);
    dhandle->maxlevel = maxlevel;
    logRoutingPublish();

    // Release _log_op_lock before replay (callbacks may log, requiring this lock).
    // Dispatch lock held across replay.
    mutexRelease(&_log_op_lock);

    logRingReplay(dhandle);

    mutexRelease(&grp->dispatchlock);
    return true;
}

_Use_decl_annotations_
bool logDestSetLevel(LogDest* dhandle, int maxlevel)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !dhandle)
        return false;

    bool ret = false;
    withMutex (&_log_op_lock) {
        ret = logDestLiveLocked(dhandle);
        if (!ret)
            break;

        dhandle->maxlevel = maxlevel;
        logRoutingPublish();
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

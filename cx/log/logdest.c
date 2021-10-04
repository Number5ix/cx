// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>

Mutex _log_dests_lock;
sa_LogDest _log_dests;

LogDest *logRegisterDest(int maxlevel, LogCategory *catfilter, LogDestFunc dest, void *userdata)
{
    logCheckInit();

    LogDest *ndest = xaAlloc(sizeof(LogDest), Zero);
    if (!ndest)
        return NULL;

    ndest->maxlevel = maxlevel;
    ndest->catfilter = catfilter;
    ndest->func = dest;
    ndest->userdata = userdata;

    mutexAcquire(&_log_dests_lock);
    saPush(&_log_dests, ptr, ndest);
    if (maxlevel > _log_max_level)
        _log_max_level = maxlevel;
    mutexRelease(&_log_dests_lock);
    return ndest;
}

static bool logUnregisterDestLocked(LogDest *dhandle)
{
    bool ret = false;

    // need to recalculate max level from what's left
    _log_max_level = -1;

    for (int i = saSize(_log_dests) - 1; i >= 0; --i) {
        if (_log_dests.a[i] == dhandle) {
            saRemove(&_log_dests, i);
            ret = true;
        } else if (_log_dests.a[i]->maxlevel > _log_max_level) {
            _log_max_level = _log_dests.a[i]->maxlevel;
        }
    }

    return ret;
}

bool logUnregisterDest(LogDest *dhandle)
{
    logCheckInit();

    bool ret = false;
    mutexAcquire(&_log_dests_lock);
    ret = logUnregisterDestLocked(dhandle);
    mutexRelease(&_log_dests_lock);

    // notify dest that it's no longer needed
    dhandle->func(-1, NULL, 0, NULL, dhandle->userdata);
    xaFree(dhandle);
    return ret;
}

void logShutdown(void)
{
    logCheckInit();

    logFlush();

    // remove all log destinations
    mutexAcquire(&_log_dests_lock);
    foreach(sarray, idx, LogDest*, dest, _log_dests) {
        dest->func(-1, NULL, 0, NULL, dest->userdata);
    } endforeach;
    saClear(&_log_dests);
    _log_max_level = -1;
    mutexRelease(&_log_dests_lock);
}

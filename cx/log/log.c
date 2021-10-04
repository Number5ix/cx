// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>
#include <cx/time.h>
#include <cx/utils/lazyinit.h>

int _log_max_level = -1;
LogCategory* LogDefault;

strref LogLevelNames[LOG_Count] = {
    (strref)"\xE1\xC1\x05""Fatal",
    (strref)"\xE1\xC1\x05""Error",
    (strref)"\xE1\xC1\x04""Warn",
    (strref)"\xE1\xC1\x06""Notice",
    (strref)"\xE1\xC1\x04""Info",
    (strref)"\xE1\xC1\x07""Verbose",
    (strref)"\xE1\xC1\x05""Debug",
    (strref)"\xE1\xC1\x05""Trace"
};

strref LogLevelAbbrev[LOG_Count] = {
    (strref)"\xE1\xC1\x01""F",
    (strref)"\xE1\xC1\x01""E",
    (strref)"\xE1\xC1\x01""W",
    (strref)"\xE1\xC1\x01""N",
    (strref)"\xE1\xC1\x01""I",
    (strref)"\xE1\xC1\x01""V",
    (strref)"\xE1\xC1\x01""D",
    (strref)"\xE1\xC1\x01""T"
};

static LazyInitState logInitState;
static void logInit(void *dummy)
{
    LogDefault = xaAlloc(sizeof(LogCategory), Zero);
    saInit(&_log_dests, ptr, 8);
    mutexInit(&_log_dests_lock);
    rwlockInit(&_log_buffer_lock);
    saInit(&_log_buffer, ptr, LOG_INITIAL_BUFFER_SIZE);
    saSetSize(&_log_buffer, LOG_INITIAL_BUFFER_SIZE);
    logThreadCreate();
}

void logCheckInit(void)
{
    lazyInit(&logInitState, logInit, NULL);
}

void logDestroyEnt(LogEntry *ent)
{
    strDestroy(&ent->msg);
    xaFree(ent);
}

LogCategory *logCreateCat(strref name)
{
    LogCategory *ret = xaAlloc(sizeof(LogCategory), Zero);
    strDup(&ret->name, name);
    return ret;
}

static void _logStrInternal(int level, LogCategory *cat, strref str)
{
    LogEntry *ent = xaAlloc(sizeof(LogEntry), Zero);
    if (!ent)
        return;

    ent->timestamp = clockWall();
    ent->level = level;
    ent->cat = cat;
    strDup(&ent->msg, str);

    if (!_log_thread_batch.a) {
        // to the global log buffer
        logBufferAdd(ent);
    } else {
        // this thread is preparing a batch
        saPush(&_log_thread_batch, ptr, ent);
    }
}

void _logStr(int level, LogCategory *cat, strref str)
{
    lazyInit(&logInitState, logInit, NULL);

    // early out if no destinations are listening for this log level
    if (level > _log_max_level)
        return;

    _logStrInternal(level, cat, str);
}

void _logFmt(int level, LogCategory *cat, strref fmtstr, int n, stvar *args)
{
    lazyInit(&logInitState, logInit, NULL);

    // early out if no destinations are listening for this log level
    if (level > _log_max_level)
        return;

    string logmsg = 0;
    _strFormat(&logmsg, fmtstr, n, args);
    _logStrInternal(level, cat, logmsg);
    strDestroy(&logmsg);
}

void logBatchBegin(void)
{
    devAssert(!_log_thread_batch.a);
    saInit(&_log_thread_batch, ptr, LOG_INITIAL_BUFFER_SIZE / 2);
}

void logBatchEnd(void)
{
    devAssert(_log_thread_batch.a);
    logBufferAddBatch(_log_thread_batch);
    saRelease(&_log_thread_batch);
}

// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>
#include <cx/time.h>

int _log_max_level = -1;
static LogChannel _logDefault;
LogChannel* LogDefault = &_logDefault;

atomic(bool) _log_running;
Mutex _log_op_lock;
Mutex _log_run_lock;
hashtable _log_channels;

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

strref LogLevelNames[LOG_Count] = { _SR(kLevelFatal),   _SR(kLevelError), _SR(kLevelWarn),
                                    _SR(kLevelNotice),  _SR(kLevelInfo),  _SR(kLevelVerbose),
                                    _SR(kLevelDiag),    _SR(kLevelDebug), _SR(kLevelTrace) };

// note that Diag and Debug intentionally share the same abbreviation
strref LogLevelAbbrev[LOG_Count] = { _SR(kAbbrevF), _SR(kAbbrevE), _SR(kAbbrevW),
                                     _SR(kAbbrevN), _SR(kAbbrevI), _SR(kAbbrevV),
                                     _SR(kAbbrevD), _SR(kAbbrevD), _SR(kAbbrevT) };

LazyInitState _logInitState;
static void logInit(void* dummy)
{
    devAssert(atomicLoad(bool, &_log_running, Acquire) == false);

    saInit(&_log_dests, ptr, 8);
    htInit(&_log_channels, ptr, none, 8);
    mutexInit(&_log_op_lock);
    prqInitDynamic(&_log_queue,
                   LOG_INITIAL_QUEUE_SIZE,
                   LOG_INITIAL_QUEUE_SIZE * 2,
                   LOG_MAX_QUEUE_SIZE,
                   PRQ_Grow_100,
                   PRQ_Grow_100);
    logThreadCreate();

    atomicStore(bool, &_log_running, true, Release);
}

void logCheckInit(void)
{
    lazyInit(&_logInitState, logInit, NULL);
}

_Use_decl_annotations_
void logDestroyEnt(LogEntry* ent)
{
    strDestroy(&ent->msg);
    xaFree(ent);
}

_Use_decl_annotations_
LogChannel* logCreateChan(strref name, bool priv)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire))
        return NULL;

    LogChannel* ret = xaAlloc(sizeof(LogChannel), XA_Zero);
    strDup(&ret->name, name);
    ret->priv = priv;

    withMutex (&_log_op_lock) {
        htInsert(&_log_channels, ptr, ret, none, NULL);
    }
    return ret;
}

static void _logStrInternal(int level, int64 timestamp, _In_ LogChannel* chan, _In_ strref str)
{
    LogEntry* ent = xaAlloc(sizeof(LogEntry), XA_Zero | XA_Optional(High));
    if (!ent)
        return;

    ent->timestamp = (timestamp != -1) ? timestamp : clockWall();
    ent->level     = level;
    ent->chan      = chan;
    strDup(&ent->msg, str);

    if (!_log_batch.level) {
        // to the global log buffer
        logQueueAdd(ent);
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
void _logStr(int level, int64 timestamp, LogChannel* chan, strref str)
{
    lazyInit(&_logInitState, logInit, NULL);

    // early out if no destinations are listening for this log level
    if (level > _log_max_level)
        return;

    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    _logStrInternal(level, timestamp, chan, str);
}

_Use_decl_annotations_
void _logFmt(int level, int64 timestamp, LogChannel* chan, strref fmtstr, int n, stvar* args)
{
    lazyInit(&_logInitState, logInit, NULL);

    // early out if no destinations are listening for this log level
    if (level > _log_max_level)
        return;

    if (!atomicLoad(bool, &_log_running, Acquire))
        return;

    string logmsg = 0;
    _strFormat(&logmsg, fmtstr, n, args);
    _logStrInternal(level, timestamp, chan, logmsg);
    strDestroy(&logmsg);
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
        logQueueAdd(_log_batch.head);
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

        withMutex (&_log_op_lock) {
            // remove all log destinations
            foreach (sarray, idx, LogDest*, dest, _log_dests) {
                if (dest->closefunc)
                    dest->closefunc(dest->userdata);
                xaFree(dest);
            }
            saDestroy(&_log_dests);
            _log_max_level = -1;

            // remove all saved channels
            foreach (hashtable, hti, _log_channels) {
                xaFree(htiKey(ptr, hti));
            }
            htDestroy(&_log_channels);
        }

        logFlush();

        // shut down log thread
        thrRequestExit(_log_thread);
        thrWait(_log_thread, timeS(10));
        thrRelease(&_log_thread);

        prqDestroy(&_log_queue);

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
        logInit(NULL);
    }
}

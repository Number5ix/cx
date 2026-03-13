#include "dbglog.h"
#include <cx/log/logmembuf.h>
#include "crash.h"

char* dbgLog;
static LogMembufData* logmemdata;
static LogDest* logdest;

void dbgLogEnable(int level)
{
    if (dbgLog)
        dbgLogDisable();

    logmemdata = logmembufCreate(DBGLOG_SIZE);
    dbgLog     = logmemdata->buf;
    dbgCrashIncludeMemory(dbgLog, DBGLOG_SIZE);
    logdest = logmembufRegister(level, NULL, logmemdata);
}

void dbgLogDisable()
{
    if (!dbgLog)
        return;

    dbgCrashExcludeMemory(dbgLog, DBGLOG_SIZE);
    logUnregisterDest(logdest);
    logdest    = NULL;
    logmemdata = NULL;
    dbgLog     = NULL;
}

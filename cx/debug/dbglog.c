#include "dbglog.h"
#include <cx/log/logmembuf.h>
#include "crash.h"

char* dbgLog;
static LogDest* logdest;

void dbgLogEnable(int level)
{
    if (dbgLog)
        dbgLogDisable();

    logdest = logmembufRegister(level, NULL, DBGLOG_SIZE, NULL);
    if (!logdest)
        return;

    dbgLog = logmembufData(logdest)->buf;
    dbgCrashIncludeMemory(dbgLog, DBGLOG_SIZE);
}

void dbgLogDisable()
{
    if (!dbgLog)
        return;

    dbgCrashExcludeMemory(dbgLog, DBGLOG_SIZE);
    logUnregisterDest(logdest);
    logdest = NULL;
    dbgLog  = NULL;
}

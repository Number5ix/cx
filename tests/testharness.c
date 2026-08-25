#include "testharness.h"

#include <cx/log.h>
#include <cx/string.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
// getenv() is fine here: single-threaded-at-startup environment reads
#pragma warning(disable : 4996)
#endif

LogChannel* cxTestLogChan;

// See the comment on cxTestLogChanGet()'s declaration in testharness.h.
LogChannel* cxTestLogChanGet(void)
{
    return cxTestLogChan;
}

// The level resolved by the most recent cxTestHarnessBefore(), or -1 if no verbose output was
// requested -- kept around so cxTestHarnessReattach() can re-register without re-parsing.
static int cxTestLogLevel = -1;

static int levelByName(const char* name)
{
    for (int i = 0; i < LOG_Count; i++) {
        if (strEqi((strref)name, LogLevelNames[i]))
            return i;
    }
    return -1;
}

// An unrecognized level name is a usage error, not something to silently ignore -- run without
// the requested verbosity and the failure this was meant to diagnose goes uninvestigated.
static int requireLevelByName(const char* name, const char* source)
{
    int level = levelByName(name);
    if (level < 0) {
        fprintf(stderr, "test harness: unknown log level '%s' (from %s)\n", name, source);
        exit(1);
    }
    return level;
}

static int resolveLogLevel(int ac, char* av[])
{
    for (int i = 1; i < ac; i++) {
        if (strBeginsWith((strref)av[i], _SL("-log=")))
            return requireLevelByName(av[i] + 5, "-log=");
    }

    const char* env = getenv("CX_TEST_LOGLEVEL");
    if (env && env[0])
        return requireLevelByName(env, "CX_TEST_LOGLEVEL");

    return -1;
}

static void registerConsoleDest(void)
{
    LogConsoleConfig cfg = { .stderrLevel = LOG_Count };
    // Scoped to exactly the shared "tests" channel plus cx's own internal channel -- not "**".
    // Test files (logtest.c especially) route plenty of traffic across their own private
    // channels (LogDefault, "hier", "grp/**", "vol/**", ...) as test data for the log system
    // itself; a harness destination that reached "**" would both drown a verbose run in that
    // noise and falsify the "nothing is listening on this channel" premise several of those
    // tests assert on. TEST_FAIL/TEST_WARN/TEST_INFO all log to cxTestLogChan ("tests"), so
    // scoping here to "tests/**" is all a troubleshooting run needs.
    LogDest* dest = logconsoleRegister(cxTestLogLevel, _SL("tests/**"), NULL, NULL, &cfg, NULL);
    logDestAddFilter(dest, _SL("cx/**"), false);
}

void cxTestHarnessBefore(int ac, char* av[])
{
    cxTestLogLevel = resolveLogLevel(ac, av);

    logRestart();
    // Deliberately not under "cx/" -- that root is LOG_Restricted, and a NULL/"**" filter (what
    // nearly every existing test destination uses) cannot reach into a restricted subtree. This
    // channel needs to stay reachable by those unqualified filters.
    cxTestLogChan = logChan(_SL("tests"));

    if (cxTestLogLevel >= 0)
        registerConsoleDest();
}

void cxTestHarnessAfter(void)
{
    logFlush();
    logShutdown();
}

void cxTestHarnessReattach(void)
{
    if (cxTestLogLevel >= 0)
        registerConsoleDest();
}

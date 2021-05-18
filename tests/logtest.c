#include <stdio.h>
#include <cx/log.h>
#include <cx/thread.h>

#include <cx/time.h>
#define TEST_FILE logtest
#define TEST_FUNCS logtest_funcs
#include "common.h"

static Event logtestevent;

typedef struct LogTestData {
    int test;
    int count;
    bool fail;
} LogTestData;

static void testdest(int level, LogCategory *cat, int64 timestamp, strref msg, void *userdata)
{
    LogTestData *td = (LogTestData*)userdata;

    if (td->test == 1) {
        td->fail = !strEq(msg, _S"Info test");
    } else if (td->test == 2) {
        td->fail = !strEq(msg, _S"Notice test");
    } else if (td->test == 3) {
        td->fail = !strEq(msg, _S"Info test") && !strEq(msg, _S"Notice test");
    } else if (td->test == 4) {
        // should NOT receive this test
        td->fail = true;
    } else if (td->test == 5) {
        td->fail = !strEq(msg, _S"Error test");
    } else if (td->test == 1000 && level == -1) {
        td->fail = false;
    } else {
        td->fail = true;
    }

    td->count++;
    if (!((td->test == 3 || td->test == 5) && td->count < 2))
        eventSignal(&logtestevent);
}

static int test_log_levels()
{
    int ret = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogDest *tdest1 = logRegisterDest(LOG_Info, NULL, testdest, &td);
    LogDest *tdest2 = logRegisterDest(LOG_Error, NULL, testdest, &td);

    td.test = 1;
    td.count = 0;
    td.fail = true;
    logStr(Info, _S"Info test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 1)
        ret = 1;

    td.test = 2;
    td.count = 0;
    td.fail = true;
    logStr(Notice, _S"Notice test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 1)
        ret = 1;

    td.test = 3;
    td.count = 0;
    td.fail = true;
    logStr(Info, _S"Info test");
    logStr(Notice, _S"Notice test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 2)
        ret = 1;

    // should NOT be received by the destination
    td.test = 4;
    td.count = 0;
    td.fail = false;
    logStr(Verbose, _S"Verbose test");
    if (td.fail || td.count != 0)
        ret = 1;

    // should be received by both destinations
    td.test = 5;
    td.count = 0;
    logStr(Verbose, _S"Verbose test 2");
    logStr(Verbose, _S"Verbose test 3");
    logStr(Verbose, _S"Verbose test 4");
    logStr(Error, _S"Error test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 2)
        ret = 1;

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_shutdown()
{
    int ret = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogDest *tdest1 = logRegisterDest(LOG_Info, NULL, testdest, &td);

    td.test = 1000;
    td.fail = true;
    logShutdown();
    if (td.fail)
        ret = 1;

    eventDestroy(&logtestevent);
    return ret;
}

testfunc logtest_funcs[] = {
    { "levels", test_log_levels },
    { "shutdown", test_log_shutdown },
    { 0, 0 }
};

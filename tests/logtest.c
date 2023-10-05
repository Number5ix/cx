#include <stdio.h>
#include <cx/log.h>
#include <cx/string.h>
#include <cx/thread.h>
#include <cx/platform/os.h>

#include <cx/time.h>
#define TEST_FILE logtest
#define TEST_FUNCS logtest_funcs
#include "common.h"

static Event logtestevent;

typedef struct LogTestData {
    int test;
    int count;
    bool fail;
    LogCategory *lastcat;
} LogTestData;

static void testdest(int level, LogCategory *cat, int64 timestamp, strref msg, void *userdata)
{
    LogTestData *td = (LogTestData*)userdata;
    bool signal = true;

    td->count++;
    td->lastcat = cat;

    if (td->test == 1) {
        td->fail = !strEq(msg, _S"Info test");
    } else if (td->test == 2) {
        td->fail = !strEq(msg, _S"Notice test");
    } else if (td->test == 3) {
        signal = (td->count == 2);
        td->fail = !strEq(msg, _S"Info test") && !strEq(msg, _S"Notice test");
    } else if (td->test == 4) {
        // should NOT receive this test
        td->fail = true;
    } else if (td->test == 5) {
        signal = (td->count == 2);
        td->fail = !strEq(msg, _S"Error test");
    } else if (td->test == 1000 && level == -1) {
        td->fail = false;
    } else if (td->test == 20) {
        signal = (td->count == 16);
        td->fail = false;
    } else if (td->test == 21) {
        signal = (td->count == 1600);
        td->fail = false;
    } else {
        td->fail = true;
    }

    if (signal)
        eventSignal(&logtestevent);
}

static int test_log_levels()
{
    int ret = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogMembufData *lmd = logmembufCreate(4096);
    logRegisterDest(LOG_Verbose, NULL, logmembufDest, lmd);
    logRegisterDest(LOG_Info, NULL, testdest, &td);
    logRegisterDest(LOG_Error, NULL, testdest, &td);

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
    osSleep(timeMS(100));
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

    if (lmd->cur != 284)
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

    logRegisterDest(LOG_Info, NULL, testdest, &td);

    td.test = 1000;
    td.fail = true;
    logShutdown();
    if (td.fail)
        ret = 1;

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_batch()
{
    int ret = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogMembufData *lmd = logmembufCreate(128 * 1024);
    logRegisterDest(LOG_Verbose, NULL, logmembufDest, lmd);
    logRegisterDest(LOG_Info, NULL, testdest, &td);
    logRegisterDest(LOG_Error, NULL, testdest, &td);

    td.test = 20;
    td.count = 0;
    td.fail = true;
    logBatchBegin();
    for (int i = 0; i < 16; i++) {
        logStr(Info, _S"Info test");
    }
    if (td.count != 0)
        ret = 1;
    logBatchEnd();
    eventWait(&logtestevent);
    if (td.fail || td.count != 16)
        ret = 1;

    td.test = 21;
    td.count = 0;
    td.fail = true;
    logBatchBegin();
    for (int i = 0; i < 1600; i++) {
        logStr(Info, _S"Info test");
    }
    osSleep(timeMS(100));
    if (td.count != 0)
        ret = 1;
    logBatchEnd();
    eventWait(&logtestevent);
    if (td.fail || td.count != 1600)
        ret = 1;

    if (lmd->cur != 46864)
        ret = 1;

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_categories()
{
    int ret = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogMembufData *lmd = logmembufCreate(4096);
    LogCategory *cat1 = logCreateCat(_S"cat1", false);
    LogCategory *cat2 = logCreateCat(_S"cat2", true);
    LogCategory *cat3 = logCreateCat(_S"cat3", true);
    logRegisterDest(LOG_Verbose, NULL, logmembufDest, lmd);
    logRegisterDest(LOG_Info, NULL, testdest, &td);
    logRegisterDest(LOG_Info, cat1, testdest, &td);
    logRegisterDest(LOG_Info, cat2, testdest, &td);

    // should only be received by the NULL filter
    td.test = 1;
    td.count = 0;
    td.fail = true;
    logStr(Info, _S"Info test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 1)
        ret = 1;

    // should be received by cat1 and NULL filter
    td.lastcat = NULL;
    td.test = 3;
    td.count = 0;
    td.fail = true;
    logStrC(Info, cat1, _S"Info test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 2 || td.lastcat != cat1)
        ret = 1;

    // should ONLY be received by cat2 filter
    td.lastcat = NULL;
    td.test = 1;
    td.count = 0;
    td.fail = false;
    logStrC(Info, cat2, _S"Info test");
    eventWait(&logtestevent);
    if (td.fail || td.count != 1 || td.lastcat != cat2)
        ret = 1;

    // should not be received by ANY destination
    td.lastcat = NULL;
    td.test = 4;
    td.count = 0;
    td.fail = false;
    logStrC(Info, cat3, _S"Info test");
    osSleep(timeMS(100));
    if (td.fail || td.count != 0 || td.lastcat != NULL)
        ret = 1;

    if (lmd->cur != 65)
        ret = 1;

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

testfunc logtest_funcs[] = {
    { "levels", test_log_levels },
    { "shutdown", test_log_shutdown },
    { "batch", test_log_batch },
    { "categories", test_log_categories },
    { 0, 0 }
};

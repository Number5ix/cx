#include <cx/buffer.h>
#include <cx/console.h>
#include <cx/format.h>
#include <cx/log.h>
#include <cx/obj.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/taskqueue.h>
#include <cx/thread.h>
#include <cx/xalloc/cstrutil.h>
#include <ctype.h>
#include <stdio.h>

#include <cx/time.h>
#define TEST_FILE  logtest
#define TEST_FUNCS logtest_funcs
#include "common.h"
#include "fmttestobj.h"

// This file exercises the logging system's own channel behavior directly, so it must not
// inherit common.h's rebinding of LOG_CHANNEL to the shared "tests" channel -- otherwise the
// bare logStr()/logFmt() calls below, which are themselves the subject under test, would
// silently target a different channel than what the assertions check against. A TEST_FAIL-style
// call added to this file should name cxTestLogChan explicitly (logFmtC) rather than relying on
// LOG_CHANNEL.
#undef LOG_CHANNEL
#define LOG_CHANNEL LogDefault

// Similar to how cpptest.cpp must use a custom variant of TEST_FAIL to work around C++ issues,
// here we define a custom TEST_FAILV that logs to cxTestLogChan explicitly rather than the
// default channel, which is needed for the log system tests.
#define TEST_FAILV_LOG(var, code, fmt, ...)                \
    do {                                                   \
        logFmtC(Error, cxTestLogChan, fmt, ##__VA_ARGS__); \
        (var) = (code);                                    \
    } while (0)

static Event logtestevent;

// Serialized records are newline-terminated, so counting lines is a format-independent way of
// asking how many messages reached the buffer.
static int membufLines(LogMembufData* lmd)
{
    int n = 0;
    for (uint32 i = 0; i < lmd->cur; i++) {
        if (lmd->buf[i] == '\n')
            n++;
    }
    return n;
}

// The write position is read once and used for both the allocation and the copy. A destination
// can still be delivering while this runs -- the drain thread emits the periodic stats record on
// its own schedule, not the caller's -- and reading lmd->cur twice lets it grow in between, which
// overflows the buffer strBuffer() just sized.
static void membufSnapshot(string* out, LogMembufData* lmd)
{
    uint32 cur = lmd->cur;
    memcpy(strBuffer(out, cur), lmd->buf, cur);
    strSetLen(out, cur);
}

// How many times a line appears in a snapshot -- the difference between "arrived" and "arrived
// exactly once", which is what a delivery handoff has to get right.
static int membufCount(strref snap, strref needle)
{
    int n = 0;
    for (int32 pos = 0; (pos = strFind(snap, pos, needle)) >= 0; pos += strLen(needle)) n++;
    return n;
}

typedef struct LogTestData {
    int test;
    int count;
    int batches;
    bool fail;
    LogChannel* lastchan;
} LogTestData;

static void testdest(const LogRecord* rec, void* userdata)
{
    LogTestData* td = (LogTestData*)userdata;
    bool signal     = true;
    string msg      = 0;

    logRecordRender(&msg, rec);
    td->count++;
    td->lastchan = rec->chan;

    if (td->test == 1) {
        td->fail = !strEq(msg, _S"Info test");
    } else if (td->test == 2) {
        td->fail = !strEq(msg, _S"Notice test");
    } else if (td->test == 3) {
        signal   = (td->count == 2);
        td->fail = !strEq(msg, _S"Info test") && !strEq(msg, _S"Notice test");
    } else if (td->test == 4) {
        // should NOT receive this test
        td->fail = true;
    } else if (td->test == 5) {
        signal   = (td->count == 2);
        td->fail = !strEq(msg, _S"Error test");
    } else if (td->test == 20) {
        signal   = false;
        td->fail = false;
    } else if (td->test == 21) {
        signal   = false;
        td->fail = false;
    } else if (td->test == 31) {
        signal   = (td->count == 5);
        td->fail = !(td->count == 5);
    } else if (td->test == 32) {
        signal   = (td->count == 3);
        td->fail = !(td->count == 3);
    } else {
        td->fail = true;
    }

    strDestroy(&msg);

    if (signal)
        eventSignal(&logtestevent);
}

static void testdestbatch(uint32 batchid, void* userdata)
{
    LogTestData* td = (LogTestData*)userdata;

    td->batches++;
    eventSignal(&logtestevent);
}

static void testdestclose(void* userdata)
{
    LogTestData* td = (LogTestData*)userdata;

    if (td->test == 1000)
        td->fail = false;
}

static int test_log_levels()
{
    int ret        = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogMembufData* lmd = logmembufData(logmembufRegister(LOG_Verbose, NULL, 4096, NULL));
    logRegisterDest(LOG_Info, NULL, testdest, NULL, NULL, &td);
    logRegisterDest(LOG_Error, NULL, testdest, NULL, NULL, &td);

    td.test  = 1;
    td.count = 0;
    td.fail  = true;
    logStr(Info, _S"Info test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 1"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    td.test  = 2;
    td.count = 0;
    td.fail  = true;
    logStr(Notice, _S"Notice test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 1"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    td.test  = 3;
    td.count = 0;
    td.fail  = true;
    logStr(Info, _S"Info test");
    logStr(Notice, _S"Notice test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 2"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // should NOT be received by the destination
    td.test  = 4;
    td.count = 0;
    td.fail  = false;
    logStr(Verbose, _S"Verbose test");
    osSleep(timeMS(100));
    if (td.fail || td.count != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 0"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // should be received by both destinations
    td.test  = 5;
    td.count = 0;
    logStr(Verbose, _S"Verbose test 2");
    logStr(Verbose, _S"Verbose test 3");
    logStr(Verbose, _S"Verbose test 4");
    logStr(Error, _S"Error test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 2"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // every message the buffer's level filter accepted landed in it, one line each
    logFlush();
    if (membufLines(lmd) != 9)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 9"), stvar(int32, membufLines(lmd)));

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_shutdown()
{
    int ret        = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    logRegisterDest(LOG_Info, NULL, testdest, NULL, testdestclose, &td);

    td.test = 1000;
    td.fail = true;
    logShutdown();
    if (td.fail)
        TEST_FAILV_LOG(ret, 1, _SL("td.fail=${int}"), stvar(int32, (int32)(td.fail)));

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_batch()
{
    int ret        = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogMembufData* lmd = logmembufData(logmembufRegister(LOG_Verbose, NULL, 128 * 1024, NULL));
    logRegisterDest(LOG_Info, NULL, testdest, testdestbatch, NULL, &td);
    logRegisterDest(LOG_Error, NULL, testdest, testdestbatch, NULL, &td);

    td.test  = 20;
    td.count = 0;
    td.fail  = true;
    logBatchBegin();
    for (int i = 0; i < 16; i++) {
        logFmt(Info, _S"${string} test", stvar(string, _S"Info"));
    }
    if (td.count != 0 || td.batches != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.count=${int} != 0 || td.batches=${int} != 0"),
                       stvar(int32, td.count),
                       stvar(int32, td.batches));
    logBatchEnd();
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 16 || td.batches != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 16 || td.batches=${int} != 1"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count),
                       stvar(int32, td.batches));

    td.test  = 21;
    td.count = 0;
    td.fail  = true;
    logBatchBegin();
    for (int i = 0; i < 1600; i++) {
        logFmt(Info, _S"${string} ${string}", stvar(strref, _S"Info"), stvar(strref, _S"test"));
    }
    osSleep(timeMS(100));
    if (td.count != 0 || td.batches != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.count=${int} != 0 || td.batches=${int} != 1"),
                       stvar(int32, td.count),
                       stvar(int32, td.batches));
    logBatchEnd();
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 1600 || td.batches != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 1600 || td.batches=${int} != 2"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count),
                       stvar(int32, td.batches));

    logFlush();
    if (membufLines(lmd) != 16 + 1600)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd) != 16 + 1600"), stvNone);

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_channels()
{
    int ret        = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    LogChannel* chan1 = logChan(_S"chan1");
    LogChannel* chan2 = logDeclareChan(_S"chan2", 0);
    LogChannel* chan3 = logDeclareChan(_S"chan3", 0);

    LogMembufData* lmd = logmembufData(logmembufRegister(LOG_Verbose, NULL, 4096, NULL));
    logRegisterDest(LOG_Info, NULL, testdest, NULL, NULL, &td);
    logRegisterDest(LOG_Info, _S"chan1", testdest, NULL, NULL, &td);
    logRegisterDest(LOG_Info, _S"chan2", testdest, NULL, NULL, &td);

    // should only be received by the NULL filter
    td.test  = 1;
    td.count = 0;
    td.fail  = true;
    logStr(Info, _S"Info test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 1"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // should be received by chan1 and NULL filter
    td.lastchan = NULL;
    td.test     = 3;
    td.count    = 0;
    td.fail     = true;
    logStrC(Info, chan1, _S"Info test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 2 || td.lastchan != chan1)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("td.fail=${int} || td.count=${int} != 2 || td.lastchan=${ptr} != chan1=${ptr}"),
            stvar(int32, (int32)(td.fail)),
            stvar(int32, td.count),
            stvar(ptr, td.lastchan),
            stvar(ptr, chan1));

    // should ONLY be received by chan2 filter
    td.lastchan = NULL;
    td.test     = 1;
    td.count    = 0;
    td.fail     = false;
    logStrC(Info, chan2, _S"Info test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 1 || td.lastchan != chan2)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("td.fail=${int} || td.count=${int} != 1 || td.lastchan=${ptr} != chan2=${ptr}"),
            stvar(int32, (int32)(td.fail)),
            stvar(int32, td.count),
            stvar(ptr, td.lastchan),
            stvar(ptr, chan2));

    // should not be received by ANY destination
    td.lastchan = NULL;
    td.test     = 4;
    td.count    = 0;
    td.fail     = false;
    logStrC(Info, chan3, _S"Info test");
    osSleep(timeMS(100));
    if (td.fail || td.count != 0 || td.lastchan != NULL)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 0 || td.lastchan=${ptr} != NULL"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count),
                       stvar(ptr, td.lastchan));

    // chan2 and chan3 are declared, so they are restricted: the unfiltered buffer sees neither
    logFlush();
    if (membufLines(lmd) != 2)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 2"), stvar(int32, membufLines(lmd)));

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

static int test_log_backfill()
{
    int ret        = 0;
    LogTestData td = { 0 };
    eventInit(&logtestevent);

    // The boot window captures early records and hands them to whatever destination
    // registers later; one window serves every destination.
    logBootWindowBegin(LOG_Verbose, 0, 0, -1);

    td.test  = 31;
    td.count = 0;
    td.fail  = true;

    logStr(Info, _S"Info test");
    logStr(Notice, _S"Notice test");
    logStr(Warn, _S"Warn test");
    logStr(Verbose, _S"Verbose test");
    logStr(Error, _S"Error test");
    logStr(Info, _S"Info test 2");

    // nothing is delivered yet -- there is nowhere for it to go, only the ring holding it
    osSleep(timeMS(100));
    if (td.count != 0 || logBootWindowCount() != 6)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.count=${int} != 0 || logBootWindowCount()=${int} != 6"),
                       stvar(int32, td.count),
                       stvar(int32, logBootWindowCount()));

    LogMembufData* lmd = logmembufData(logmembufRegister(LOG_Verbose, NULL, 4096, NULL));
    logRegisterDest(LOG_Info, NULL, testdest, NULL, NULL, &td);

    // Specifically check for 5 events. The Verbose entry was retained, because the window was
    // opened at that level, but is filtered out by this destination's own level on the way in.
    // The backfill runs on the registering thread, so it has already happened by now.
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 5)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 5"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // closing the window changes nothing for a destination already registered
    logBootWindowEnd();

    td.test  = 32;
    td.count = 0;
    td.fail  = true;
    logStr(Info, _S"Info test");
    logStr(Notice, _S"Notice test");
    logStr(Warn, _S"Warn test");
    if (!eventWaitTimeout(&logtestevent, timeS(1)))
        TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&logtestevent, timeS(1))"), stvNone);
    if (td.fail || td.count != 3)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("td.fail=${int} || td.count=${int} != 3"),
                       stvar(int32, (int32)(td.fail)),
                       stvar(int32, td.count));

    // the membuf *should* include the Verbose entry
    logFlush();
    if (membufLines(lmd) != 9)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 9"), stvar(int32, membufLines(lmd)));

    logShutdown();

    eventDestroy(&logtestevent);
    return ret;
}

// ---------------------------------------------------------------------------------------
// hierarchy: interning, path matching, restriction gates and per-channel levels
// ---------------------------------------------------------------------------------------

typedef struct HierTestData {
    int count;
    string last;
} HierTestData;

static void hiertestmsg(const LogRecord* rec, void* userdata)
{
    HierTestData* hd = (HierTestData*)userdata;
    hd->count++;
    strDup(&hd->last, rec->chan ? rec->chan->path : NULL);
}

static int test_log_hierarchy()
{
    int ret          = 0;
    HierTestData all = { 0 }, sub = { 0 }, exact = { 0 };

    // interning gives back the same channel every time, and every ancestor along the path exists
    LogChannel* http = logChan(_S"hier/http");
    LogChannel* req  = logChan(_S"hier/http/request");
    if (http != logChan(_S"hier/http"))
        TEST_FAILV_LOG(ret, 1, _SL("http != logChan(_S\"hier/http\")"), stvNone);
    if (req->parent != http || http->parent != logChan(_S"hier"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("req->parent != http || http->parent != logChan(_S\"hier\")"),
                       stvNone);
    if (logChan(_S"hier")->parent != LogDefault)
        TEST_FAILV_LOG(ret, 1, _SL("logChan(_S\"hier\")->parent != LogDefault"), stvNone);

    LogDest* dall   = logRegisterDest(LOG_Info, NULL, hiertestmsg, NULL, NULL, &all);
    LogDest* dsub   = logRegisterDest(LOG_Info, _S"hier/**", hiertestmsg, NULL, NULL, &sub);
    LogDest* dexact = logRegisterDest(LOG_Info, _S"hier/http", hiertestmsg, NULL, NULL, &exact);

    // an exact rule names one channel and does not reach a child of it
    logStrC(Info, req, _S"one");
    logFlush();
    if (all.count != 1 || sub.count != 1 || exact.count != 0)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("all.count=${int} != 1 || sub.count=${int} != 1 || exact.count=${int} != 0"),
            stvar(int32, all.count),
            stvar(int32, sub.count),
            stvar(int32, exact.count));
    if (!strEq(sub.last, _S"hier/http/request"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: sub.last=${string} vs 'hier/http/request'"),
                       stvar(strref, sub.last));

    logStrC(Info, http, _S"two");
    logFlush();
    if (all.count != 2 || sub.count != 2 || exact.count != 1)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("all.count=${int} != 2 || sub.count=${int} != 2 || exact.count=${int} != 1"),
            stvar(int32, all.count),
            stvar(int32, sub.count),
            stvar(int32, exact.count));

    // restricting a node gates its whole subtree at that node: a rule reaches it only by naming
    // the gate in its literal prefix, so hier/** still sees everything and ** sees none of it
    logDeclareChan(_S"hier", 0);
    logStrC(Info, req, _S"three");
    logFlush();
    if (all.count != 2 || sub.count != 3 || exact.count != 1)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("all.count=${int} != 2 || sub.count=${int} != 3 || exact.count=${int} != 1"),
            stvar(int32, all.count),
            stvar(int32, sub.count),
            stvar(int32, exact.count));

    // ...and re-opening a subtree beneath a restricted parent puts it back in view of **
    logDeclareChan(_S"hier/http", LOG_Broadcast);
    logStrC(Info, req, _S"four");
    logFlush();
    if (all.count != 3 || sub.count != 4)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("all.count=${int} != 3 || sub.count=${int} != 4"),
                       stvar(int32, all.count),
                       stvar(int32, sub.count));

    // an exclude rule wins over a less specific include
    logDestAddFilter(dsub, _S"hier/http/**", true);
    logStrC(Info, req, _S"five");
    logFlush();
    if (sub.count != 4 || all.count != 4)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("sub.count=${int} != 4 || all.count=${int} != 4"),
                       stvar(int32, sub.count),
                       stvar(int32, all.count));

    // the level ceiling is per channel, so a Trace destination on one subtree costs nothing
    // anywhere else
    LogChannel* other = logChan(_S"hier2");
    logRegisterDest(LOG_Trace, _S"hier/**", hiertestmsg, NULL, NULL, &sub);
    if (!logWouldLog(LOG_Trace, req) || logWouldLog(LOG_Trace, other))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("!logWouldLog(LOG_Trace, req) || logWouldLog(LOG_Trace, other)"),
                       stvNone);
    if (!logWouldLog(LOG_Info, other))
        TEST_FAILV_LOG(ret, 1, _SL("!logWouldLog(LOG_Info, other)"), stvNone);

    // a channel nothing is listening to has no ceiling at all
    logUnregisterDest(dall);
    logUnregisterDest(dexact);
    logFlush();
    if (logWouldLog(LOG_Fatal, other))
        TEST_FAILV_LOG(ret, 1, _SL("logWouldLog(LOG_Fatal, other)"), stvNone);

    logShutdown();

    // channels are permanent, so the pointers survive a shutdown/restart cycle
    logRestart();
    cxTestHarnessReattach();   // logRestart() above dropped the harness's own destination
    if (logChan(_S"hier/http") != http)
        TEST_FAILV_LOG(ret, 1, _SL("logChan(_S\"hier/http\") != http"), stvNone);
    logShutdown();

    strDestroy(&all.last);
    strDestroy(&sub.last);
    strDestroy(&exact.last);
    return ret;
}

// The framework's own channels are gated by a built-in declaration made when the registry is
// built, so cx's internal logging never reaches an application destination that did not ask for
// it by name.
static int test_log_cxchan()
{
    int ret          = 0;
    HierTestData all = { 0 }, cx = { 0 };

    LogChannel* cxroot = logChan(_S"cx");
    if (!(cxroot->flags & LOG_Restricted) || cxroot->gatedepth != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("!(cxroot->flags & LOG_Restricted) || cxroot->gatedepth != 1"),
                       stvNone);

    // a channel interned beneath it inherits the gate without declaring anything itself
    LogChannel* sub = logChan(_S"cx/logtest/sub");
    if (sub->gatedepth != 1)
        TEST_FAILV_LOG(ret, 1, _SL("sub->gatedepth != 1"), stvNone);

    LogDest* dall = logRegisterDest(LOG_Info, NULL, hiertestmsg, NULL, NULL, &all);
    LogDest* dcx  = logRegisterDest(LOG_Info, _S"cx/**", hiertestmsg, NULL, NULL, &cx);

    // ** does not pass the gate; cx/** names it literally and sees the whole subtree
    logStrC(Info, sub, _S"one");
    logStrC(Info, cxroot, _S"two");
    logFlush();
    if (all.count != 0 || cx.count != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("all.count=${int} != 0 || cx.count=${int} != 2"),
                       stvar(int32, all.count),
                       stvar(int32, cx.count));

    // ...while an application channel of its own is unaffected by any of it
    logStrC(Info, logChan(_S"cxtest"), _S"three");
    logFlush();
    if (all.count != 1 || cx.count != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("all.count=${int} != 1 || cx.count=${int} != 2"),
                       stvar(int32, all.count),
                       stvar(int32, cx.count));

    logUnregisterDest(dcx);
    logUnregisterDest(dall);
    logShutdown();

    strDestroy(&all.last);
    strDestroy(&cx.last);
    return ret;
}

// ---------------------------------------------------------------------------------------
// dest: destination registration, retirement and slot reuse
// ---------------------------------------------------------------------------------------

typedef struct DestTestData {
    int count;
    int closed;
} DestTestData;

static void desttestmsg(const LogRecord* rec, void* userdata)
{
    DestTestData* dd = (DestTestData*)userdata;
    dd->count++;
}

static void desttestclose(void* userdata)
{
    DestTestData* dd = (DestTestData*)userdata;
    dd->closed++;
}

static int test_log_dest()
{
    int ret         = 0;
    DestTestData d1 = { 0 }, d2 = { 0 }, d3 = { 0 };

    LogDest* dest1 = logRegisterDest(LOG_Info, NULL, desttestmsg, NULL, desttestclose, &d1);
    LogDest* dest2 = logRegisterDest(LOG_Info, NULL, desttestmsg, NULL, desttestclose, &d2);

    logStr(Info, _S"one");
    logFlush();
    if (d1.count != 1 || d2.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("d1.count=${int} != 1 || d2.count=${int} != 1"),
                       stvar(int32, d1.count),
                       stvar(int32, d2.count));

    // the close callback runs once the retired handle has passed its grace period, which is
    // guaranteed by the time a flush completes
    if (!logUnregisterDest(dest2))
        TEST_FAILV_LOG(ret, 1, _SL("!logUnregisterDest(dest2)"), stvNone);
    logFlush();
    if (d2.closed != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d2.closed=${int} != 1"), stvar(int32, d2.closed));

    // unregistering a handle that is no longer registered must be a no-op, not a double free
    if (logUnregisterDest(dest2))
        TEST_FAILV_LOG(ret, 1, _SL("logUnregisterDest(dest2)"), stvNone);
    if (d2.closed != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d2.closed=${int} != 1"), stvar(int32, d2.closed));

    // the freed slot is reusable, and the remaining destination still receives
    LogDest* dest3 = logRegisterDest(LOG_Info, NULL, desttestmsg, NULL, desttestclose, &d3);
    logStr(Info, _S"two");
    logFlush();
    if (d1.count != 2 || d2.count != 1 || d3.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("d1.count=${int} != 2 || d2.count=${int} != 1 || d3.count=${int} != 1"),
                       stvar(int32, d1.count),
                       stvar(int32, d2.count),
                       stvar(int32, d3.count));

    // the level ceiling falls once the last destination that wanted a level is gone
    if (!logUnregisterDest(dest1) || !logUnregisterDest(dest3))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("!logUnregisterDest(dest1) || !logUnregisterDest(dest3)"),
                       stvNone);
    logStr(Info, _S"three");
    logFlush();
    if (d1.count != 2 || d3.count != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("d1.count=${int} != 2 || d3.count=${int} != 1"),
                       stvar(int32, d1.count),
                       stvar(int32, d3.count));
    if (d1.closed != 1 || d3.closed != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("d1.closed=${int} != 1 || d3.closed=${int} != 1"),
                       stvar(int32, d1.closed),
                       stvar(int32, d3.closed));

    logShutdown();

    // everything is closed, and closed exactly once
    if (d1.closed != 1 || d2.closed != 1 || d3.closed != 1)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("d1.closed=${int} != 1 || d2.closed=${int} != 1 || d3.closed=${int} != 1"),
            stvar(int32, d1.closed),
            stvar(int32, d2.closed),
            stvar(int32, d3.closed));

    return ret;
}

// ---------------------------------------------------------------------------------------
// site: per-call-site statics and the rate-limiting gates built on them
// ---------------------------------------------------------------------------------------

// Reached repeatedly while nothing is listening at Verbose, then again once something is. It has
// to be a function rather than a loop body so that every arrival is the same call site.
static void siteVerboseOnce(void)
{
    logStrOnce(Verbose, _S"verbose once");
}

// same reason: the interval gate can only be observed reopening if every arrival is one site
static void siteEveryT(void)
{
    logStrEveryT(Info, timeMS(300), _S"every 300ms");
}

static int test_log_site()
{
    int ret         = 0;
    DestTestData d1 = { 0 };

    LogDest* dest1 = logRegisterDest(LOG_Info, NULL, desttestmsg, NULL, desttestclose, &d1);

    // logStrOnce emits the first time its call site is reached and never again
    d1.count = 0;
    for (int i = 0; i < 10; i++) logStrOnce(Info, _S"once");
    logFlush();
    if (d1.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 1"), stvar(int32, d1.count));

    // an identical message elsewhere is a different call site with its own budget, which is the
    // property that makes a site static a better key than a hash of the rendered message
    d1.count = 0;
    for (int i = 0; i < 10; i++) logStrOnce(Info, _S"once");
    logFlush();
    if (d1.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 1"), stvar(int32, d1.count));

    // every Nth arrival, starting with the first: 12 arrivals at N=4 emit at 0, 4 and 8
    d1.count = 0;
    for (int i = 0; i < 12; i++) logStrEveryN(Info, 4, _S"every 4");
    logFlush();
    if (d1.count != 3)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 3"), stvar(int32, d1.count));

    // N below 1 is a rate limit that limits nothing rather than a division by zero
    d1.count = 0;
    for (int i = 0; i < 5; i++) logStrEveryN(Info, 0, _S"every 0");
    logFlush();
    if (d1.count != 5)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 5"), stvar(int32, d1.count));

    // the formatted forms gate identically and still render their arguments
    d1.count = 0;
    for (int i = 0; i < 6; i++) logFmtEveryN(Info, 3, _S"i=${int}", stvar(int32, i));
    logFlush();
    if (d1.count != 2)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 2"), stvar(int32, d1.count));

    // at most one emission per interval, and the first arrival always emits
    d1.count = 0;
    for (int i = 0; i < 5; i++) siteEveryT();
    logFlush();
    if (d1.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 1"), stvar(int32, d1.count));

    // ...and the interval reopens once it has elapsed
    osSleep(timeMS(350));
    for (int i = 0; i < 5; i++) siteEveryT();
    logFlush();
    if (d1.count != 2)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 2"), stvar(int32, d1.count));

    // A gate is only consumed when something is actually listening, so a logStrOnce() that runs
    // before its destination exists is not silently spent. Nothing is at Verbose yet.
    d1.count = 0;
    for (int i = 0; i < 5; i++) siteVerboseOnce();
    logFlush();
    if (d1.count != 0)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 0"), stvar(int32, d1.count));

    LogDest* dest2 = logRegisterDest(LOG_Verbose, NULL, desttestmsg, NULL, desttestclose, &d1);
    siteVerboseOnce();
    logFlush();
    if (d1.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 1"), stvar(int32, d1.count));
    siteVerboseOnce();
    logFlush();
    if (d1.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("d1.count=${int} != 1"), stvar(int32, d1.count));

    logUnregisterDest(dest1);
    logUnregisterDest(dest2);
    logShutdown();

    return ret;
}

// ---------------------------------------------------------------------------------------
// ctx: per-thread log context, and its propagation across a thread boundary
// ---------------------------------------------------------------------------------------

typedef struct CtxTestData {
    int count;
    string reqid;
    string tenant;
    string msg;
    bool havectx;
} CtxTestData;

static void ctxtestmsg(const LogRecord* rec, void* userdata)
{
    CtxTestData* cd = (CtxTestData*)userdata;
    cd->count++;
    cd->havectx = (rec->ctx != NULL);
    strClear(&cd->reqid);
    strClear(&cd->tenant);
    logRecordRender(&cd->msg, rec);

    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c)) {
        const stvar* vars = logCtxVars(c);
        for (uint32 i = 0; i < logCtxNumVars(c); i++) {
            const char* key = stvarKey(&vars[i]);
            if (!key || logCtxShadowed(rec->ctx, c, i, key))
                continue;
            if (cstrEq(key, "reqid"))
                logVarText(&cd->reqid, &vars[i]);
            else if (cstrEq(key, "tenant"))
                logVarText(&cd->tenant, &vars[i]);
        }
    }
}

static Event ctxtaskev;

static bool ctxTaskFunc(TaskQueue* tq, void* data)
{
    // runs on a worker thread, but should still see the submitter's context
    logStr(Info, _S"from a task");
    eventSignal(&ctxtaskev);
    return true;
}

static int test_log_ctx()
{
    int ret        = 0;
    CtxTestData cd = { 0 };

    LogDest* dest = logRegisterDest(LOG_Info, NULL, ctxtestmsg, NULL, NULL, &cd);

    // outside any context there is nothing to inherit
    logStr(Info, _S"bare");
    logFlush();
    if (cd.havectx)
        TEST_FAILV_LOG(ret, 1, _SL("cd.havectx=${int}"), stvar(int32, (int32)(cd.havectx)));

    withLogCtx(stvark(reqid, string, _S"abc123"), stvark(tenant, string, _S"acme"))
    {
        logStr(Info, _S"inside");
        logFlush();
        if (!strEq(cd.reqid, _S"abc123") || !strEq(cd.tenant, _S"acme")) {
            logFmtC(
                Error,
                cxTestLogChan,
                _SL("strEq mismatch: cd.reqid=${string} vs 'abc123' || strEq mismatch: cd.tenant=${string} vs 'acme'"),
                stvar(strref, cd.reqid),
                stvar(strref, cd.tenant));
            ret = 1;
        }

        // nesting shadows: the inner reqid wins and the outer one is not emitted twice
        withLogCtx(stvark(reqid, string, _S"def456"))
        {
            logStr(Info, _S"nested");
            logFlush();
            if (!strEq(cd.reqid, _S"def456") || !strEq(cd.tenant, _S"acme")) {
                logFmtC(
                    Error,
                    cxTestLogChan,
                    _SL("strEq mismatch: cd.reqid=${string} vs 'def456' || strEq mismatch: cd.tenant=${string} vs 'acme'"),
                    stvar(strref, cd.reqid),
                    stvar(strref, cd.tenant));
                ret = 1;
            }
        }

        // ...and unwinds back to the outer value
        logStr(Info, _S"unwound");
        logFlush();
        if (!strEq(cd.reqid, _S"abc123"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.reqid=${string} vs 'abc123'"),
                           stvar(strref, cd.reqid));
    }

    // the context is gone once the block exits
    logStr(Info, _S"after");
    logFlush();
    if (cd.havectx)
        TEST_FAILV_LOG(ret, 1, _SL("cd.havectx=${int}"), stvar(int32, (int32)(cd.havectx)));

    // A record holds its own reference, so it renders the fields that were in scope when it was
    // logged even though that scope is long gone by the time the drain thread reaches it.
    logStrC(Info, LogDefault, _S"unflushed");
    withLogCtx(stvark(reqid, string, _S"deferred"))
    {
        logStr(Info, _S"logged inside, rendered outside");
    }
    logFlush();
    if (!strEq(cd.reqid, _S"deferred"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: cd.reqid=${string} vs 'deferred'"),
                       stvar(strref, cd.reqid));

    // A message template can name context fields directly, so a correlation id can appear in the
    // sentence rather than only in the annotation a destination appends.
    withLogCtx(stvark(reqid, string, _S"abc123"), stvark(tenant, string, _S"acme"))
    {
        logFmt(Info, _S"handling ${string:reqid} for ${string:tenant}", stvNone);
        logFlush();
        if (!strEq(cd.msg, _S"handling abc123 for acme"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.msg=${string} vs 'handling abc123 for acme'"),
                           stvar(strref, cd.msg));

        // ...without disturbing the positional arguments: a keyed variant is invisible to an
        // unkeyed placeholder, so the call site's own arguments keep their numbering
        logFmt(Info, _S"n=${int} req=${string:reqid}", stvar(int32, 7));
        logFlush();
        if (!strEq(cd.msg, _S"n=7 req=abc123"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.msg=${string} vs 'n=7 req=abc123'"),
                           stvar(strref, cd.msg));

        // an argument sharing a key is the more specific of the two and wins
        logFmt(Info, _S"req=${string:reqid}", stvark(reqid, string, _S"override"));
        logFlush();
        if (!strEq(cd.msg, _S"req=override"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.msg=${string} vs 'req=override'"),
                           stvar(strref, cd.msg));

        // and nesting shadows here the same way it does everywhere else
        withLogCtx(stvark(reqid, string, _S"def456"))
        {
            logFmt(Info, _S"req=${string:reqid} tenant=${string:tenant}", stvNone);
            logFlush();
            if (!strEq(cd.msg, _S"req=def456 tenant=acme"))
                TEST_FAILV_LOG(ret,
                               1,
                               _SL("strEq mismatch: cd.msg=${string} vs 'req=def456 tenant=acme'"),
                               stvar(strref, cd.msg));
        }
    }

    // Outside any context the key resolves to nothing, and an unmatched placeholder fails the
    // whole format -- the ordinary strFormat contract, which context fields do not get an
    // exemption from. A template that expects to render either way says so with a default.
    logFmt(Info, _S"req=${string:reqid}", stvNone);
    logFlush();
    if (!strEmpty(cd.msg))
        TEST_FAILV_LOG(ret, 1, _SL("cd.msg=${string} (expected empty)"), stvar(strref, cd.msg));

    logFmt(Info, _S"req=${string:reqid;none}", stvNone);
    logFlush();
    if (!strEq(cd.msg, _S"req=none"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: cd.msg=${string} vs 'req=none'"),
                       stvar(strref, cd.msg));

    // a literal message is never a template, so ${...} in one survives even in a context
    withLogCtx(stvark(reqid, string, _S"abc123"))
    {
        logStr(Info, _S"literally ${string:reqid}");
        logFlush();
        if (!strEq(cd.msg, _S"literally ${string:reqid}"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.msg=${string} vs 'literally ${string:reqid}'"),
                           stvar(strref, cd.msg));
    }

    // work that hops threads keeps the context of whoever submitted it
    eventInit(&ctxtaskev);
    TaskQueueConfig conf;
    tqPresetBalanced(&conf);
    conf.flags |= TQ_NoComplex;
    TaskQueue* tq = tqCreate(_S"LogCtx Test", &conf);
    if (!tq || !tqStart(tq)) {
        TEST_FAILV_LOG(ret, 1, _SL("!tq || !tqStart(tq)"), stvNone);
    } else {
        withLogCtx(stvark(reqid, string, _S"task42"))
        {
            tqCall(tq, ctxTaskFunc, NULL);
        }
        if (!eventWaitTimeout(&ctxtaskev, timeS(5)))
            TEST_FAILV_LOG(ret, 1, _SL("!eventWaitTimeout(&ctxtaskev, timeS(5))"), stvNone);
        logFlush();
        if (!strEq(cd.reqid, _S"task42"))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("strEq mismatch: cd.reqid=${string} vs 'task42'"),
                           stvar(strref, cd.reqid));

        // ...and the worker does not keep it afterwards
        tqShutdown(tq, timeS(10));
        tqRelease(&tq);
    }
    eventDestroy(&ctxtaskev);

    logUnregisterDest(dest);
    logShutdown();

    strDestroy(&cd.reqid);
    strDestroy(&cd.tenant);
    strDestroy(&cd.msg);
    return ret;
}

// ---------------------------------------------------------------------------------------
// serializer: the serializer/transport split, and the NDJSON serializer it exists for
// ---------------------------------------------------------------------------------------

static int test_log_serializer()
{
    int ret = 0;

    // the same transport, given a different serializer, stores something else entirely
    LogDest* jdest     = logmembufRegister(LOG_Info, NULL, 8192, logNdjsonSerializer(NULL));
    LogMembufData* jmd = logmembufData(jdest);

    LogChannel* chan = logChan(_S"ser/test");
    logFmtC(Info,
            chan,
            _S"connected to ${string}",
            stvar(string, _S"web01"),
            stvark(port, int32, 8080),
            stvark(secure, bool, true));
    logFlush();

    // the ring is raw bytes, not a string, so take a copy of what is in it so far
    string line = 0;
    membufSnapshot(&line, jmd);

    // the record's own fields, then one field per keyed argument, with types preserved
    if (strFind(line, 0, _S"\"level\":\"Info\"") < 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strFind(line, 0, _S\"\\\"level\\\":\\\"Info\\\"\") < 0"),
                       stvNone);
    if (strFind(line, 0, _S"\"chan\":\"ser/test\"") < 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strFind(line, 0, _S\"\\\"chan\\\":\\\"ser/test\\\"\") < 0"),
                       stvNone);
    if (strFind(line, 0, _S"\"msg\":\"connected to web01\"") < 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strFind(line, 0, _S\"\\\"msg\\\":\\\"connected to web01\\\"\") < 0"),
                       stvNone);
    if (strFind(line, 0, _S"\"port\":8080") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S\"\\\"port\\\":8080\") < 0"), stvNone);
    if (strFind(line, 0, _S"\"secure\":true") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S\"\\\"secure\\\":true\") < 0"), stvNone);
    // the unkeyed argument belongs to the template and is not repeated as a field
    if (strFind(line, 0, _S"\"web01\":") >= 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S\"\\\"web01\\\":\") >= 0"), stvNone);
    // one object per record, newline delimited
    if (membufLines(jmd) != 1)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(jmd)=${int} != 1"), stvar(int32, membufLines(jmd)));

    // anything that would break the JSON has to come back out escaped
    logStrC(Info, chan, _S"quote \" backslash \\ newline \n");
    logFlush();
    membufSnapshot(&line, jmd);
    if (strFind(line, 0, _S"quote \\\" backslash \\\\ newline \\n") < 0)
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("strFind(line, 0, _S\"quote \\\\\\\" backslash \\\\\\\\ newline \\\\n\") < 0"),
            stvNone);
    if (membufLines(jmd) != 2)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(jmd)=${int} != 2"), stvar(int32, membufLines(jmd)));

    logUnregisterDest(jdest);
    logShutdown();

    strDestroy(&line);
    return ret;
}

// ---------------------------------------------------------------------------------------
// textomit: the text serializer's prefix pieces, and what the line looks like without them
// ---------------------------------------------------------------------------------------

static int test_log_textomit()
{
    int ret = 0;

    // LOG_OmitDate drops the timestamp; the level prefix loses the leading space it only ever
    // carried as a separator from the date
    LogTextConfig nodate = {
        .flags = LOG_OmitDate | LOG_ShortLevel,
    };
    LogDest* ndest     = logmembufRegister(LOG_Info, NULL, 4096, logTextSerializer(&nodate));
    LogMembufData* nmd = logmembufData(ndest);

    // with a channel between the (absent) date and the level, the channel is what loses it
    LogTextConfig nodatechan = {
        .flags = LOG_OmitDate | LOG_ShortLevel | LOG_IncludeChannel | LOG_ChannelFirst,
    };
    LogDest* cdest     = logmembufRegister(LOG_Info, NULL, 4096, logTextSerializer(&nodatechan));
    LogMembufData* cmd = logmembufData(cdest);

    // nothing in the prefix at all leaves the message alone, with no orphaned spacing run
    LogTextConfig bare = {
        .flags = LOG_OmitDate | LOG_OmitLevel | LOG_AddColon,
    };
    LogDest* bdest     = logmembufRegister(LOG_Info, NULL, 4096, logTextSerializer(&bare));
    LogMembufData* bmd = logmembufData(bdest);

    // LOG_DateTimeOnly keeps a timestamp but drops the calendar date, which is the other half
    // of what a console usually wants
    LogTextConfig timeonly = {
        .dateFormat = LOG_DateTimeOnly,
        .flags      = LOG_ShortLevel,
    };
    LogDest* tdest     = logmembufRegister(LOG_Info, NULL, 4096, logTextSerializer(&timeonly));
    LogMembufData* tmd = logmembufData(tdest);

    LogChannel* chan = logChan(_S"omit/test");
    logStrC(Info, chan, _S"hello");
    logFlush();

    string line = 0;
    membufSnapshot(&line, nmd);
    if (!strEq(line, _S"I  hello\n"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: line=${string} vs _S\"I  hello\\n\"=${string}"),
                       stvar(strref, line),
                       stvar(strref, _S"I  hello\n"));
    membufSnapshot(&line, cmd);
    if (!strEq(line, _S"omit/test I  hello\n"))
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("strEq mismatch: line=${string} vs _S\"omit/test I  hello\\n\"=${string}"),
            stvar(strref, line),
            stvar(strref, _S"omit/test I  hello\n"));
    membufSnapshot(&line, bmd);
    if (!strEq(line, _S"hello\n"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: line=${string} vs _S\"hello\\n\"=${string}"),
                       stvar(strref, line),
                       stvar(strref, _S"hello\n"));

    // the clock reading itself is whatever time it is, so check the shape: HH:MM:SS and then
    // the rest of the line exactly as any other date format would leave it
    membufSnapshot(&line, tmd);
    if (strLen(line) != 18 || strGetChar(line, 2) != ':' || strGetChar(line, 5) != ':') {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strLen(line)=${int} != 18 || strGetChar(line, 2) != ':' || strGetChar(line, 5) != ':'"),
            stvar(int32, strLen(line)));
        ret = 1;
    }
    for (int i = 0; i < 8; i++) {
        if (i != 2 && i != 5 && !isdigit(strGetChar(line, i)))
            TEST_FAILV_LOG(ret,
                           1,
                           _SL("i != 2 && i != 5 && !isdigit(strGetChar(line, i))"),
                           stvNone);
    }
    string tail = 0;
    strSubStr(&tail, line, 8, strEnd);
    if (!strEq(tail, _S" I  hello\n"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: tail=${string} vs _S\" I  hello\\n\"=${string}"),
                       stvar(strref, tail),
                       stvar(strref, _S" I  hello\n"));
    strDestroy(&tail);

    logUnregisterDest(ndest);
    logUnregisterDest(cdest);
    logUnregisterDest(bdest);
    logUnregisterDest(tdest);
    logShutdown();

    strDestroy(&line);
    return ret;
}

// ---------------------------------------------------------------------------------------
// groups: one queue and one drain thread per named group
// ---------------------------------------------------------------------------------------

static int test_log_groups()
{
    int ret = 0;

    // the default group is what everything starts on, and is named by the empty string
    LogGroup* def = logDefaultGroup();
    if (!def || !strEmpty(logGroupName(def)))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("!def || logGroupName(def)=${string} (expected empty)"),
                       stvar(strref, logGroupName(def)));
    if (logGroup(NULL) != def)
        TEST_FAILV_LOG(ret, 1, _SL("logGroup(NULL) != def"), stvNone);

    // groups intern like channels do: same name, same pointer
    LogGroup* bulk = logGroup(_S "bulk");
    if (!bulk || bulk == def)
        TEST_FAILV_LOG(ret, 1, _SL("!bulk || bulk == def"), stvNone);
    if (logGroup(_S "bulk") != bulk)
        TEST_FAILV_LOG(ret, 1, _SL("logGroup(_S \"bulk\") != bulk"), stvNone);
    if (!strEq(logGroupName(bulk), _S "bulk"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: logGroupName(bulk)=${string} vs 'bulk'"),
                       stvar(strref, logGroupName(bulk)));

    LogDest* adest     = logmembufRegister(LOG_Info, _S "grp/**", 8192, NULL);
    LogMembufData* amd = logmembufData(adest);
    LogDest* bdest     = logmembufRegister(LOG_Info, _S "grp/**", 8192, NULL);
    LogMembufData* bmd = logmembufData(bdest);

    if (!logDestSetGroup(bdest, _S "bulk"))
        TEST_FAILV_LOG(ret, 1, _SL("!logDestSetGroup(bdest, _S \"bulk\")"), stvNone);

    // Both destinations match the channel, but they are on different groups, so the entry is
    // pushed onto two queues. Each destination must still see it exactly once -- a drain thread
    // walks the whole interested set and has to deliver only its own group's share of it.
    LogChannel* chan = logChan(_S "grp/one");
    logStrC(Info, chan, _S "first");
    logStrC(Info, chan, _S "second");
    logStrC(Info, chan, _S "third");
    logFlush();

    if (membufLines(amd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(amd)=${int} != 3"), stvar(int32, membufLines(amd)));
    if (membufLines(bmd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(bmd)=${int} != 3"), stvar(int32, membufLines(bmd)));

    // a batch fans out the same way, and stays whole in each group
    logBatchBegin();
    logStrC(Info, chan, _S "batched one");
    logStrC(Info, chan, _S "batched two");
    logBatchEnd();
    logFlush();

    if (membufLines(amd) != 5)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(amd)=${int} != 5"), stvar(int32, membufLines(amd)));
    if (membufLines(bmd) != 5)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(bmd)=${int} != 5"), stvar(int32, membufLines(bmd)));

    // a channel only one group is interested in reaches only that group
    LogDest* cdest     = logmembufRegister(LOG_Info, _S "solo/**", 8192, NULL);
    LogMembufData* cmd = logmembufData(cdest);
    if (!logDestSetGroup(cdest, _S "bulk"))
        TEST_FAILV_LOG(ret, 1, _SL("!logDestSetGroup(cdest, _S \"bulk\")"), stvNone);

    logStrC(Info, logChan(_S "solo/only"), _S "solo message");
    logFlush();

    if (membufLines(cmd) != 1)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(cmd)=${int} != 1"), stvar(int32, membufLines(cmd)));
    if (membufLines(amd) != 5)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(amd)=${int} != 5"), stvar(int32, membufLines(amd)));
    if (membufLines(bmd) != 5)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(bmd)=${int} != 5"), stvar(int32, membufLines(bmd)));

    // moving a destination back to the default group works the same way
    if (!logDestSetGroup(cdest, NULL))
        TEST_FAILV_LOG(ret, 1, _SL("!logDestSetGroup(cdest, NULL)"), stvNone);
    logStrC(Info, logChan(_S "solo/only"), _S "back home");
    logFlush();
    if (membufLines(cmd) != 2)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(cmd)=${int} != 2"), stvar(int32, membufLines(cmd)));

    logUnregisterDest(cdest);
    logUnregisterDest(bdest);
    logUnregisterDest(adest);
    logShutdown();

    // groups are permanent: the registry survives a shutdown/restart cycle the way channels do
    logRestart();
    cxTestHarnessReattach();   // logRestart() above dropped the harness's own destination
    if (logGroup(_S "bulk") != bulk)
        TEST_FAILV_LOG(ret, 1, _SL("logGroup(_S \"bulk\") != bulk"), stvNone);
    logShutdown();

    return ret;
}

// ---------------------------------------------------------------------------------------
// volume: sampling, deduplication, backpressure and metrics
// ---------------------------------------------------------------------------------------

static int test_log_volume()
{
    int ret = 0;

    logResetStats();

    LogDest* dest      = logmembufRegister(LOG_Debug, _S "vol/**", 64 * 1024, NULL);
    LogMembufData* lmd = logmembufData(dest);

    // --- sampling -----------------------------------------------------------------------
    LogChannel* schan = logChan(_S "vol/sampled");
    logChanSetSampling(schan, 4);

    for (int i = 0; i < 40; i++) logStrC(Info, schan, _S "sampled message");
    logFlush();

    if (membufLines(lmd) != 10)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("membufLines(lmd)=${int} != 10"),
                       stvar(int32, membufLines(lmd)));

    LogStats st;
    logGetStats(&st);
    if (st.sampled != 30)
        TEST_FAILV_LOG(ret, 1, _SL("st.sampled != 30"), stvNone);

    // an Error is never sampled away, whatever the rate
    logStrC(Error, schan, _S "error message");
    logStrC(Error, schan, _S "error message");
    logFlush();
    if (membufLines(lmd) != 12)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("membufLines(lmd)=${int} != 12"),
                       stvar(int32, membufLines(lmd)));

    logChanSetSampling(schan, 0);
    logStrC(Info, schan, _S "unsampled again");
    logFlush();
    if (membufLines(lmd) != 13)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("membufLines(lmd)=${int} != 13"),
                       stvar(int32, membufLines(lmd)));

    // the surviving record carries the rate it survived at
    LogDest* jdest     = logmembufRegister(LOG_Info, _S "vol/**", 8192, logNdjsonSerializer(NULL));
    LogMembufData* jmd = logmembufData(jdest);
    LogChannel* rchan  = logChan(_S "vol/rate");
    logChanSetSampling(rchan, 3);
    logStrC(Info, rchan, _S "rate carried");
    logFlush();
    logChanSetSampling(rchan, 0);

    string line = 0;
    membufSnapshot(&line, jmd);
    if (strFind(line, 0, _S "\"sample\":3") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S \"\\\"sample\\\":3\") < 0"), stvNone);
    logUnregisterDest(jdest);

    // --- deduplication ------------------------------------------------------------------
    logResetStats();
    LogDest* ddest     = logmembufRegister(LOG_Debug, _S "vol/dedup", 64 * 1024, NULL);
    LogMembufData* dmd = logmembufData(ddest);

    logSetDedup(timeMS(200), 3);

    // one call site, hit hard: three get through and the rest are counted
    for (int i = 0; i < 50; i++) logStrC(Info, logChan(_S "vol/dedup"), _S "flooding");
    logFlush();

    if (membufLines(dmd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(dmd)=${int} != 3"), stvar(int32, membufLines(dmd)));
    logGetStats(&st);
    if (st.suppressed != 47)
        TEST_FAILV_LOG(ret, 1, _SL("st.suppressed != 47"), stvNone);

    // when the window closes, one summary goes out saying what was swallowed -- and it arrives
    // without anything else being logged, because the drain thread wakes for it
    osSleep(timeMS(400));
    logFlush();

    if (membufLines(dmd) != 4)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(dmd)=${int} != 4"), stvar(int32, membufLines(dmd)));
    membufSnapshot(&line, dmd);
    if (strFind(line, 0, _S "flooding ... and 47 more like this") < 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strFind(line, 0, _S \"flooding ... and 47 more like this\") < 0"),
                       stvNone);

    logSetDedup(0, 0);
    logUnregisterDest(ddest);

    // --- metrics ------------------------------------------------------------------------
    logResetStats();
    for (int i = 0; i < 10; i++) logStrC(Info, logChan(_S "vol/metrics"), _S "counted");
    logFlush();

    logGetStats(&st);
    if (st.enqueued != 10)
        TEST_FAILV_LOG(ret, 1, _SL("st.enqueued=${int} != 10"), stvar(uint64, st.enqueued));
    if (st.dropped != 0)
        TEST_FAILV_LOG(ret, 1, _SL("st.dropped != 0"), stvNone);
    if (st.queued != 0)   // everything has drained by now
        TEST_FAILV_LOG(ret, 1, _SL("st.queued=${int} != 0"), stvar(uint32, st.queued));
    if (st.queuedmax < 1)
        TEST_FAILV_LOG(ret, 1, _SL("st.queuedmax < 1"), stvNone);
    if (st.groups < 1)
        TEST_FAILV_LOG(ret, 1, _SL("st.groups < 1"), stvNone);

    // the periodic record goes to a restricted channel, so it takes a destination that names it
    LogDest* sdest     = logmembufRegister(LOG_Notice, _S "cx/log/stats", 8192, NULL);
    LogMembufData* smd = logmembufData(sdest);
    logSetStatsInterval(timeMS(1));
    osSleep(timeMS(50));
    logStrC(Info, logChan(_S "vol/metrics"), _S "wake the drain thread");
    logFlush();
    logFlush();
    logSetStatsInterval(0);

    // With the interval off no new stats record can be generated, so this last flush is the one
    // that makes the buffer stop changing: everything the drain thread had already queued for it
    // is delivered before the snapshot reads it.
    logFlush();

    membufSnapshot(&line, smd);
    if (strFind(line, 0, _S "queued ") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S \"queued \") < 0"), stvNone);
    if (strFind(line, 0, _S "groups") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(line, 0, _S \"groups\") < 0"), stvNone);
    logUnregisterDest(sdest);

    // --- panic flush --------------------------------------------------------------------
    // nothing is queued at this point, so this exercises the path rather than its output
    logPanicFlush();

    logUnregisterDest(dest);
    logShutdown();

    strDestroy(&line);
    return ret;
}

// ---------------------------------------------------------------------------------------
// record: message templates, deferred formatting and structured access
// ---------------------------------------------------------------------------------------

typedef struct RecTestData {
    int count;
    int nargs;
    string tmpl;   // template exactly as it was logged, unrendered
    string rendered;
    string host;   // resolved from the keyed argument, never from the template
    int32 port;
    bool haveport;
} RecTestData;

// a structured destination: it never renders, it reads the arguments
static void rectestmsg(const LogRecord* rec, void* userdata)
{
    RecTestData* rd = (RecTestData*)userdata;
    rd->count++;
    rd->nargs = rec->nargs;
    strDup(&rd->tmpl, rec->msgtmpl);
    logRecordRender(&rd->rendered, rec);

    stvlist args;
    stvlInit(&args, rec->nargs, (stvar*)rec->args);
    string host = 0;
    if (stvlFind(args, host, string, &host))
        strDup(&rd->host, host);
    rd->haveport = stvlFind(args, port, int32, &rd->port);
}

static int test_log_record()
{
    int ret        = 0;
    RecTestData rd = { 0 };

    LogDest* dest = logRegisterDest(LOG_Info, NULL, rectestmsg, NULL, NULL, &rd);

    // the destination receives the template and the arguments, not a finished line
    logFmt(Info,
           _S"connected to ${string} on port ${int}",
           stvar(string, _S"web01"),
           stvar(int32, 8080));
    logFlush();
    if (rd.count != 1 || rd.nargs != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("rd.count=${int} != 1 || rd.nargs != 2"),
                       stvar(int32, rd.count));
    if (!strEq(rd.tmpl, _S"connected to ${string} on port ${int}"))
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("strEq mismatch: rd.tmpl=${string} vs 'connected to ${string} on port ${int}'"),
            stvar(strref, rd.tmpl));
    if (!strEq(rd.rendered, _S"connected to web01 on port 8080"))
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("strEq mismatch: rd.rendered=${string} vs 'connected to web01 on port 8080'"),
            stvar(strref, rd.rendered));

    // keyed arguments survive the copy, so a structured destination can name its fields while a
    // text one still renders a sentence from the same record
    logFmt(Info, _S"connected to ${:host}", stvark(host, string, _S"web02"), stvark(port, int32, 443));
    logFlush();
    if (!strEq(rd.host, _S"web02") || !rd.haveport || rd.port != 443) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strEq mismatch: rd.host=${string} vs 'web02' || !rd.haveport || rd.port != 443"),
            stvar(strref, rd.host));
        ret = 1;
    }
    if (!strEq(rd.rendered, _S"connected to web02"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: rd.rendered=${string} vs 'connected to web02'"),
                       stvar(strref, rd.rendered));

    // a string argument is owned by the entry, so it renders correctly even though the caller's
    // string is gone by the time the drain thread gets to it
    string tmp = 0;
    strDup(&tmp, _S"transient");
    logFmt(Info, _S"value ${string}", stvar(string, tmp));
    strDestroy(&tmp);
    logFlush();
    if (!strEq(rd.rendered, _S"value transient"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: rd.rendered=${string} vs 'value transient'"),
                       stvar(strref, rd.rendered));

    // An object argument is rendered at the call site, not on the drain thread, because its
    // state can change in between. The batch is what makes the window deterministic: the
    // entry exists after logFmt, but is not queued until logBatchEnd.
    FmtTestClass* obj = fmttestclassCreate(1, _S"before");
    logBatchBegin();
    logFmt(Info, _S"obj ${object}", stvar(object, obj));
    obj->iv = 2;
    strDup(&obj->sv, _S"after");
    logBatchEnd();
    logFlush();
    if (!strEq(rd.rendered, _S"obj Object(before:One)"))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("strEq mismatch: rd.rendered=${string} vs 'obj Object(before:One)'"),
                       stvar(strref, rd.rendered));
    objRelease(&obj);

    logUnregisterDest(dest);
    logShutdown();

    strDestroy(&rd.tmpl);
    strDestroy(&rd.rendered);
    strDestroy(&rd.host);
    return ret;
}

// ---------------------------------------------------------------------------------------
// console: logconsole routes by stderrLevel and styles by level, over memory-backed streams
// ---------------------------------------------------------------------------------------

static int test_log_console()
{
    int ret = 0;

    ConCaps caps   = { .istty = true, .vt = true, .color = CON_Color16 };
    ConStream* out = conCreateMem(&caps);
    ConStream* err = conCreateMem(&caps);

    LogConsoleConfig cfg = {
        .stderrLevel = LOG_Warn,
        .colorMode   = LOGCON_ColorOn,
    };
    LogTextConfig tcfg = {
        .dateFormat = LOG_DateISOCompact,
        .flags      = LOG_ShortLevel,
    };
    LogDest* dest = logconsoleRegister(LOG_Info, NULL, out, err, &cfg, logTextSerializer(&tcfg));

    logStr(Info, _S"info msg");
    logStr(Error, _S"error msg");
    logFlush();

    string outbuf = 0, errbuf = 0;
    conMemGet(out, &outbuf);
    conMemGet(err, &errbuf);

    // Info (less severe than stderrLevel) goes to stdout only
    if (strFind(outbuf, 0, _S"info msg") < 0 || strFind(outbuf, 0, _S"error msg") >= 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(outbuf, 0, _S\"info msg\") < 0 || strFind(outbuf, 0, _S\"error msg\") >= 0"),
            stvNone);
        ret = 1;
    }
    // Error (at/above stderrLevel) goes to stderr only, wrapped in the built-in bright-red style
    if (strFind(errbuf, 0, _S"error msg") < 0 || strFind(errbuf, 0, _S"info msg") >= 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(errbuf, 0, _S\"error msg\") < 0 || strFind(errbuf, 0, _S\"info msg\") >= 0"),
            stvNone);
        ret = 1;
    }
    if (strFind(errbuf, 0, _S"\x1b[0;91m") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(errbuf, 0, _S\"\\x1b[0;91m\") < 0"), stvNone);

    strDestroy(&outbuf);
    strDestroy(&errbuf);
    logUnregisterDest(dest);
    logShutdown();
    conDestroy(&out);
    conDestroy(&err);
    return ret;
}

static int test_log_console_style()
{
    int ret = 0;

    ConCaps caps      = { .istty = true, .vt = true, .color = CON_Color16 };
    ConStream* off    = conCreateMem(&caps);
    ConStream* custom = conCreateMem(&caps);

    LogConsoleConfig cfgOff = {
        .stderrLevel = LOG_Count,
        .colorMode   = LOGCON_ColorOff,
    };
    LogDest* destOff = logconsoleRegister(LOG_Info, NULL, off, off, &cfgOff, NULL);

    LogConsoleConfig cfgCustom = {
        .stderrLevel = LOG_Count,
        .colorMode   = LOGCON_ColorOn,
    };
    cfgCustom.levelStyle[LOG_Info] = CONSTYLE(CON_Green, 0);
    LogDest* destCustom = logconsoleRegister(LOG_Info, NULL, custom, custom, &cfgCustom, NULL);

    logStr(Info, _S"plain");
    logFlush();

    string offbuf = 0, custombuf = 0;
    conMemGet(off, &offbuf);
    conMemGet(custom, &custombuf);

    // LOGCON_ColorOff never emits an escape sequence
    if (strFind(offbuf, 0, _S"\x1b") >= 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(offbuf, 0, _S\"\\x1b\") >= 0"), stvNone);
    // a non-zero levelStyle override wins over the built-in default for that level
    if (strFind(custombuf, 0, _S"\x1b[0;32m") < 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(custombuf, 0, _S\"\\x1b[0;32m\") < 0"), stvNone);

    strDestroy(&offbuf);
    strDestroy(&custombuf);
    logUnregisterDest(destOff);
    logUnregisterDest(destCustom);
    logShutdown();
    conDestroy(&off);
    conDestroy(&custom);
    return ret;
}

// ---------------------------------------------------------------------------------------
// bootwindow: retention from process start, replayed into destinations that arrive later
// ---------------------------------------------------------------------------------------

static int test_log_bootwindow()
{
    int ret = 0;

    // nothing is retained until a window is opened -- the ring is opt-in
    if (logBootWindowActive() || logBootWindowCount() != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowActive() || logBootWindowCount()=${int} != 0"),
                       stvar(int32, logBootWindowCount()));
    logStr(Info, _S "before the window");
    if (logBootWindowCount() != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowCount()=${int} != 0"),
                       stvar(int32, logBootWindowCount()));

    // no deadline, so the window lasts exactly as long as the test says it does
    logBootWindowBegin(LOG_Verbose, 64, 0, -1);
    if (!logBootWindowActive())
        TEST_FAILV_LOG(ret, 1, _SL("!logBootWindowActive()"), stvNone);

    // With no destination at all, these would normally be discarded at the call site: the
    // window is what raises the channel ceiling far enough for an entry to exist.
    LogChannel* one = logChan(_S "boot/one");
    LogChannel* two = logChan(_S "boot/two");
    logStrC(Info, one, _S "one alpha");
    logStrC(Info, one, _S "one beta");
    logStrC(Verbose, two, _S "two gamma");
    logStrC(Diag, two, _S "below the window");   // more verbose than the window retains
    if (logBootWindowCount() != 3)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowCount()=${int} != 3"),
                       stvar(int32, logBootWindowCount()));

    // a destination registering during the window is backfilled before it sees anything live
    LogDest* adest     = logmembufRegister(LOG_Verbose, NULL, 8192, NULL);
    LogMembufData* amd = logmembufData(adest);
    if (membufLines(amd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(amd)=${int} != 3"), stvar(int32, membufLines(amd)));

    // the backfill is filtered by the destination's own spec, not by the ring's
    LogDest* bdest     = logmembufRegister(LOG_Info, _S "boot/one/**", 8192, NULL);
    LogMembufData* bmd = logmembufData(bdest);
    if (membufLines(bmd) != 2)   // the two Info records on boot/one, not the Verbose on boot/two
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(bmd)=${int} != 2"), stvar(int32, membufLines(bmd)));

    // ...and it does not consume the ring, so a later destination still gets everything
    LogDest* cdest     = logmembufRegister(LOG_Verbose, NULL, 8192, NULL);
    LogMembufData* cmd = logmembufData(cdest);
    if (membufLines(cmd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(cmd)=${int} != 3"), stvar(int32, membufLines(cmd)));

    // live records carry on to every destination exactly once; nothing is delivered twice
    logStrC(Info, one, _S "one delta");
    logFlush();
    if (membufLines(amd) != 4 || membufLines(bmd) != 3 || membufLines(cmd) != 4) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("membufLines(amd)=${int} != 4 || membufLines(bmd)=${int} != 3 || membufLines(cmd)=${int} != 4"),
            stvar(int32, membufLines(amd)),
            stvar(int32, membufLines(bmd)),
            stvar(int32, membufLines(cmd)));
        ret = 1;
    }

    // closing the window discards the ring and lowers the ceilings again
    logBootWindowEnd();
    if (logBootWindowActive() || logBootWindowCount() != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowActive() || logBootWindowCount()=${int} != 0"),
                       stvar(int32, logBootWindowCount()));

    logStrC(Info, one, _S "after the window");
    logFlush();
    if (logBootWindowCount() != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowCount()=${int} != 0"),
                       stvar(int32, logBootWindowCount()));

    // a destination registering now has nothing to be backfilled from
    LogDest* ddest     = logmembufRegister(LOG_Verbose, NULL, 8192, NULL);
    LogMembufData* dmd = logmembufData(ddest);
    if (membufLines(dmd) != 0)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(dmd)=${int} != 0"), stvar(int32, membufLines(dmd)));

    logUnregisterDest(adest);
    logUnregisterDest(bdest);
    logUnregisterDest(cdest);
    logUnregisterDest(ddest);

    // A full window keeps the oldest, not the newest: startup diagnostics are what it is for,
    // and the interesting part of a startup is its beginning.
    logBootWindowBegin(LOG_Info, 3, 0, -1);
    for (int i = 0; i < 8; i++) logFmtC(Info, one, _S "capped ${int}", stvar(int32, i));
    if (logBootWindowCount() != 3)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logBootWindowCount()=${int} != 3"),
                       stvar(int32, logBootWindowCount()));

    LogDest* edest     = logmembufRegister(LOG_Info, NULL, 8192, NULL);
    LogMembufData* emd = logmembufData(edest);
    string snap        = 0;
    membufSnapshot(&snap, emd);
    if (membufLines(emd) != 3)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(emd)=${int} != 3"), stvar(int32, membufLines(emd)));
    if (strFind(snap, 0, _S "capped 0") < 0 || strFind(snap, 0, _S "capped 2") < 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(snap, 0, _S \"capped 0\") < 0 || strFind(snap, 0, _S \"capped 2\") < 0"),
            stvNone);
        ret = 1;
    }
    if (strFind(snap, 0, _S "capped 3") >= 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(snap, 0, _S \"capped 3\") >= 0"), stvNone);

    logBootWindowEnd();
    logUnregisterDest(edest);
    logShutdown();

    strDestroy(&snap);
    return ret;
}

// ---------------------------------------------------------------------------------------
// bootreplay: the backfill handoff, with traffic already in flight
// ---------------------------------------------------------------------------------------

// test_log_bootwindow covers the backfill with no destination registered during capture, so
// nothing is ever queued and the replay is the only path an entry can take. This is the harder
// case, and the one a real startup actually has: a destination is already listening while the
// window captures, so every entry is *both* retained in the ring and queued for delivery. A
// backfill that is not ordered against the drain thread will deliver some of them to the new
// destination twice -- once from the ring, once from the queue it was already sitting in.
static int test_log_bootreplay()
{
    int ret = 0;

    logBootWindowBegin(LOG_Verbose, 8192, 0, -1);

    // the destination that is already listening while the window captures
    LogDest* adest     = logmembufRegister(LOG_Verbose, NULL, 1 << 20, NULL);
    LogMembufData* amd = logmembufData(adest);

    // Enough to leave a backlog in the queue at the moment the second destination registers.
    // The drain thread wakes on every enqueue, so a handful of records would usually be gone
    // before the registration got started and the race would go unobserved.
    const int nburst = 4000;
    for (int i = 0; i < nburst; i++) logFmt(Info, _S "burst ${int}", stvar(int32, i));

    // Deliberately no logFlush() here: records are still in flight, which is the whole point.
    LogDest* bdest     = logmembufRegister(LOG_Verbose, NULL, 1 << 20, NULL);
    LogMembufData* bmd = logmembufData(bdest);

    logFlush();

    // Each destination sees each record exactly once. Too many means an entry arrived from both
    // the ring and the queue; too few means it fell between them.
    if (membufLines(amd) != nburst)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("membufLines(amd)=${int} != nburst=${int}"),
                       stvar(int32, membufLines(amd)),
                       stvar(int32, nburst));
    if (membufLines(bmd) != nburst)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("membufLines(bmd)=${int} != nburst=${int}"),
                       stvar(int32, membufLines(bmd)),
                       stvar(int32, nburst));

    // A count alone would let one duplicate hide one loss, so check the two ends individually.
    string snap = 0;
    membufSnapshot(&snap, bmd);
    if (membufCount(snap, _S "burst 0\n") != 1 || membufCount(snap, _S "burst 3999\n") != 1) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("membufCount(snap, _S \"burst 0\\n\")=${int} != 1 || membufCount(snap, _S \"burst 3999\\n\")=${int} != 1"),
            stvar(int32, membufCount(snap, _S "burst 0\n")),
            stvar(int32, membufCount(snap, _S "burst 3999\n")));
        ret = 1;
    }
    strDestroy(&snap);

    logBootWindowEnd();
    logUnregisterDest(adest);
    logUnregisterDest(bdest);
    logShutdown();

    return ret;
}

// ---------------------------------------------------------------------------------------
// debugring: recent verbose traffic kept per channel and released when an error fires
// ---------------------------------------------------------------------------------------

static int test_log_debugring()
{
    int ret = 0;

    // Diag rather than Debug throughout: Debug and Trace are compiled out of release builds, so a
    // test written against them would silently stop exercising anything there.
    LogChannel* net   = logChan(_S "ring/net");
    LogChannel* req   = logChan(_S "ring/net/request");
    LogChannel* other = logChan(_S "ring/other");

    LogDest* dest      = logmembufRegister(LOG_Info, _S "ring/**", 16384, NULL);
    LogMembufData* lmd = logmembufData(dest);

    // Off by default: nothing has a ring, so a Verbose record on a channel whose destination
    // wants Info is discarded at the call site exactly as it always was.
    if (logChanDebugRingCount(net) != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logChanDebugRingCount(net)=${int} != 0"),
                       stvar(int32, logChanDebugRingCount(net)));
    logStrC(Verbose, net, _S "not retained");
    logStrC(Error, net, _S "error with no ring");
    logFlush();
    if (membufLines(lmd) != 1)   // the error only
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 1"), stvar(int32, membufLines(lmd)));
    if (logChanDebugRingCount(net) != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logChanDebugRingCount(net)=${int} != 0"),
                       stvar(int32, logChanDebugRingCount(net)));

    // A ring on ring/net covers ring/net/request too, and nothing outside the subtree.
    if (!logChanSetDebugRing(net, LOG_Diag, 16, LOG_Error))
        TEST_FAILV_LOG(ret, 1, _SL("!logChanSetDebugRing(net, LOG_Diag, 16, LOG_Error)"), stvNone);

    logStrC(Diag, net, _S "ctx one");
    logStrC(Diag, req, _S "ctx two");
    logStrC(Verbose, req, _S "ctx three");
    logStrC(Diag, other, _S "outside the subtree");
    // the child does not get a ring of its own: it resolves to the parent's, which is why both
    // report the same count and why the two records logged on it landed in the same place
    if (logChanDebugRingCount(net) != 3 || logChanDebugRingCount(req) != 3) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("logChanDebugRingCount(net)=${int} != 3 || logChanDebugRingCount(req)=${int} != 3"),
            stvar(int32, logChanDebugRingCount(net)),
            stvar(int32, logChanDebugRingCount(req)));
        ret = 1;
    }
    if (logChanDebugRingCount(other) != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logChanDebugRingCount(other)=${int} != 0"),
                       stvar(int32, logChanDebugRingCount(other)));

    // nothing has been delivered: the ring holds what no destination wanted
    logFlush();
    if (membufLines(lmd) != 1)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 1"), stvar(int32, membufLines(lmd)));

    // an error releases the ring ahead of itself, and empties it
    logStrC(Error, req, _S "the failure");
    logFlush();
    if (logChanDebugRingCount(net) != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logChanDebugRingCount(net)=${int} != 0"),
                       stvar(int32, logChanDebugRingCount(net)));
    if (membufLines(lmd) != 5)   // 1 + three retained + the error
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(lmd)=${int} != 5"), stvar(int32, membufLines(lmd)));

    string snap = 0;
    membufSnapshot(&snap, lmd);
    if (strFind(snap, 0, _S "ctx one") < 0 || strFind(snap, 0, _S "ctx three") < 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(snap, 0, _S \"ctx one\") < 0 || strFind(snap, 0, _S \"ctx three\") < 0"),
            stvNone);
        ret = 1;
    }
    if (strFind(snap, 0, _S "outside the subtree") >= 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(snap, 0, _S \"outside the subtree\") >= 0"), stvNone);
    // the retained context precedes the record that released it
    if (strFind(snap, 0, _S "ctx one") > strFind(snap, 0, _S "the failure"))
        TEST_FAILV_LOG(
            ret,
            1,
            _SL("strFind(snap, 0, _S \"ctx one\") > strFind(snap, 0, _S \"the failure\")"),
            stvNone);

    // A record a destination already wanted is not retained, so releasing the ring cannot
    // deliver it twice.
    logStrC(Info, net, _S "delivered once");
    logStrC(Diag, net, _S "retained");
    logStrC(Error, net, _S "release again");
    logFlush();
    membufSnapshot(&snap, lmd);
    if (strFind(snap, 0, _S "delivered once") != strFindR(snap, strEnd, _S "delivered once")) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(snap, 0, _S \"delivered once\")=${int} != strFindR(snap, strEnd, _S \"delivered once\")=${int}"),
            stvar(int32, strFind(snap, 0, _S "delivered once")),
            stvar(int32, strFindR(snap, strEnd, _S "delivered once")));
        ret = 1;
    }

    // A full ring evicts the oldest: the context of a failure is what immediately preceded it.
    logChanSetDebugRing(net, LOG_Diag, 3, LOG_Error);
    for (int i = 0; i < 8; i++) logFmtC(Diag, net, _S "evicted ${int}", stvar(int32, i));
    if (logChanDebugRingCount(net) != 3)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logChanDebugRingCount(net)=${int} != 3"),
                       stvar(int32, logChanDebugRingCount(net)));

    // A destination too coarse to have seen the error does not get the trace either: a released
    // record is filtered at the severity that released it.
    LogDest* wdest     = logmembufRegister(LOG_Fatal, _S "ring/**", 8192, NULL);
    LogMembufData* wmd = logmembufData(wdest);

    logStrC(Error, net, _S "second failure");
    logFlush();
    membufSnapshot(&snap, lmd);
    if (strFind(snap, 0, _S "evicted 7") < 0 || strFind(snap, 0, _S "evicted 5") < 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("strFind(snap, 0, _S \"evicted 7\") < 0 || strFind(snap, 0, _S \"evicted 5\") < 0"),
            stvNone);
        ret = 1;
    }
    if (strFind(snap, 0, _S "evicted 4") >= 0)
        TEST_FAILV_LOG(ret, 1, _SL("strFind(snap, 0, _S \"evicted 4\") >= 0"), stvNone);
    if (membufLines(wmd) != 0)   // Fatal-only destination saw neither the error nor its context
        TEST_FAILV_LOG(ret, 1, _SL("membufLines(wmd)=${int} != 0"), stvar(int32, membufLines(wmd)));

    // taking the ring away puts the subtree back to retaining nothing
    logChanClearDebugRing(net);
    logStrC(Diag, net, _S "gone again");
    logFlush();
    if (logChanDebugRingCount(net) != 0 || logChanDebugRingCount(req) != 0) {
        logFmtC(
            Error,
            cxTestLogChan,
            _SL("logChanDebugRingCount(net)=${int} != 0 || logChanDebugRingCount(req)=${int} != 0"),
            stvar(int32, logChanDebugRingCount(net)),
            stvar(int32, logChanDebugRingCount(req)));
        ret = 1;
    }

    logUnregisterDest(wdest);
    logUnregisterDest(dest);
    logShutdown();

    strDestroy(&snap);
    return ret;
}

// ---------------------------------------------------------------------------------------
// Wire codec
// ---------------------------------------------------------------------------------------

// Frame kinds counted straight out of the byte stream, so a test asserting "one segment" is
// reading the framing rather than trusting the encoder to say so.
static bool wireNextFrame(const uint8* buf, size_t len, size_t* pos, uint64* kind, size_t* bodyoff,
                          uint64* bodylen)
{
    uint64 v[2];
    size_t p = *pos;

    for (int f = 0; f < 2; f++) {
        uint64 acc = 0;
        int shift  = 0;
        for (;;) {
            if (p >= len || shift > 63)
                return false;
            uint8 b = buf[p++];
            acc |= (uint64)(b & 0x7f) << shift;
            shift += 7;
            if (!(b & 0x80))
                break;
        }
        v[f] = acc;
    }

    if (len - p < v[1])
        return false;

    *kind    = v[0];
    *bodyoff = p;
    *bodylen = v[1];
    *pos     = p + (size_t)v[1];
    return true;
}

static int wireCountKind(Buffer frames, uint64 want)
{
    int n      = 0;
    size_t pos = 0;
    uint64 kind, blen;
    size_t boff;
    while (wireNextFrame(frames->data, frames->len, &pos, &kind, &boff, &blen)) {
        if (kind == want)
            n++;
    }

    return n;
}

typedef struct WireTestData {
    int nentry;
    int ngap;
    int nother;
    LogWireGap gap;

    // a snapshot of the last record to arrive
    int level;
    int64 timestamp;
    uint64 seq;
    uint32 sample;
    int trigger;
    uint8 hops;
    bool istmpl;
    int nargs;
    int nctx;
    string chanpath;
    string origin;
    string tmpl;
    string rendered;
} WireTestData;

static void wireTestClear(WireTestData* wd)
{
    strDestroy(&wd->chanpath);
    strDestroy(&wd->origin);
    strDestroy(&wd->tmpl);
    strDestroy(&wd->rendered);
    memset(wd, 0, sizeof(*wd));
}

// Renders a decoded record the way logRecordRender() renders a live one, so the assertion is that
// the record still says what it said rather than that some particular field survived.
static void wireRender(string* out, const LogWireRecord* rec)
{
    strClear(out);
    if (!rec->istmpl) {
        strDup(out, rec->msgtmpl);
        return;
    }

    int total   = rec->nargs + rec->nctx;
    stvar* args = (total > 0) ? xaAlloc(sizeof(stvar) * (size_t)total) : NULL;
    int n       = 0;
    for (int i = 0; i < rec->nargs; i++) args[n++] = rec->args[i];
    for (int i = 0; i < rec->nctx; i++) args[n++] = rec->ctx[i];

    _strFormat(out, rec->msgtmpl, n, args);
    xaFree(args);
}

static bool wireTestCB(const LogWireFrame* frame, void* ctx)
{
    WireTestData* wd = (WireTestData*)ctx;

    if (frame->kind == LOG_WireGap) {
        wd->ngap++;
        wd->gap = *frame->gap;
        return true;
    }

    if (frame->kind != LOG_WireEntry) {
        wd->nother++;
        return true;
    }

    const LogWireRecord* r = frame->rec;
    wd->nentry++;
    wd->level     = r->level;
    wd->timestamp = r->timestamp;
    wd->seq       = r->seq;
    wd->sample    = r->sample;
    wd->trigger   = r->trigger;
    wd->hops      = r->hops;
    wd->istmpl    = r->istmpl;
    wd->nargs     = r->nargs;
    wd->nctx      = r->nctx;
    strDup(&wd->chanpath, r->chanpath);
    strDup(&wd->origin, r->origin);
    strDup(&wd->tmpl, r->msgtmpl);
    wireRender(&wd->rendered, r);
    return true;
}

// Feeds a whole buffer of frames to a decoder in one go.
static bool wireFeed(LogWireDecoder* dec, Buffer frames, LogWireFrameCB cb, void* ctx)
{
    return logWireDecode(dec, bufLen(frames) ? frames->data : NULL, bufLen(frames), cb, ctx);
}

// Feeds only what has arrived since the last call. A stream cannot be restarted partway through
// -- everything after a segment frame leans on it -- so a test watching a forwarder as it is
// reconfigured has to keep one decoder and keep feeding it, rather than re-reading the sink.
static bool wireFeedTail(LogWireDecoder* dec, Buffer frames, size_t* fed, LogWireFrameCB cb,
                         void* ctx)
{
    if (bufLen(frames) <= *fed)
        return true;

    size_t from = *fed;
    *fed        = frames->len;
    return logWireDecode(dec, frames->data + from, *fed - from, cb, ctx);
}

static void wireFillRecord(LogRecord* rec, LogChannel* chan, strref tmpl, bool istmpl, int nargs,
                           stvar* args)
{
    memset(rec, 0, sizeof(*rec));
    rec->level     = LOG_Info;
    rec->chan      = chan;
    rec->timestamp = 1700000000000000LL;
    rec->seq       = 4242;
    rec->trigger   = -1;
    rec->sample    = 1;
    rec->msgtmpl   = tmpl;
    rec->istmpl    = istmpl;
    rec->args      = args;
    rec->nargs     = nargs;
}

// A destination that encodes everything it receives, which is the whole of what a forwarder does
// with a record before it reaches a transport.
typedef struct WireEncDest {
    LogWireEncoder* enc;
    Buffer scratch;   // one record's frames, which the next record overwrites
    Buffer frames;    // ...accumulated into the whole stream
} WireEncDest;

static void wireEncDestMsg(const LogRecord* rec, void* userdata)
{
    WireEncDest* wed = (WireEncDest*)userdata;
    if (logWireEncode(wed->enc, &wed->scratch, rec))
        bufAppend(&wed->frames, wed->scratch);
}

static int test_log_wire(void)
{
    int ret = 0;
    WireTestData wd = { 0 };
    Buffer frames   = 0;

    LogChannel* chan = logChan(_SL("wire/codec"));

    LogWireEncoder* enc = logWireEncoderCreate(_SL("web-01"), 0);
    LogWireDecoder* dec = logWireDecoderCreate();

    // Every scalar a log call can carry, keyed and unkeyed together. The keyed ones are what a
    // structured destination reads; the unkeyed ones are what the template consumes.
    stvar args[] = {
        stvar(int32, -7),
        stvar(uint64, 900000000000ULL),
        stvar(float64, 1.5),
        stvar(bool, true),
        stvar(strref, _SL("inline")),
        stvark(host, strref, _SL("web02")),
        stvark(port, int32, 443),
        stvark(ratio, float32, 0.5f),
    };

    LogRecord rec;
    wireFillRecord(&rec,
                   chan,
                   _SL("v=${int} u=${uint} f=${float} b=${uint} s=${string} "
                       "h=${string:host} p=${int:port} r=${float:ratio}"),
                   true,
                   8,
                   args);

    if (!logWireEncode(enc, &frames, &rec))
        TEST_FAILV_LOG(ret, 1, _SL("logWireEncode failed"), stvNone);
    if (!wireFeed(dec, frames, wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("logWireDecode failed"), stvNone);

    if (wd.nentry != 1)
        TEST_FAILV_LOG(ret, 1, _SL("wd.nentry=${int} != 1"), stvar(int32, wd.nentry));
    if (!strEq(wd.chanpath, _SL("wire/codec")))
        TEST_FAILV_LOG(ret, 1, _SL("chanpath=${string} != 'wire/codec'"), stvar(strref, wd.chanpath));
    if (!strEq(wd.origin, _SL("web-01")))
        TEST_FAILV_LOG(ret, 1, _SL("origin=${string} != 'web-01'"), stvar(strref, wd.origin));
    if (wd.timestamp != 1700000000000000LL)
        TEST_FAILV_LOG(ret, 1, _SL("timestamp=${int} != 1700000000000000"), stvar(int64, wd.timestamp));
    if (wd.seq != 4242)
        TEST_FAILV_LOG(ret, 1, _SL("seq=${uint} != 4242"), stvar(uint64, wd.seq));
    if (wd.hops != 1)
        TEST_FAILV_LOG(ret, 1, _SL("hops=${uint} != 1"), stvar(uint32, (uint32)wd.hops));
    if (wd.nargs != 8)
        TEST_FAILV_LOG(ret, 1, _SL("nargs=${int} != 8"), stvar(int32, wd.nargs));
    // The strongest thing a codec test can assert: the record renders to exactly what it rendered
    // before it was encoded, so nothing had to be said here about how any one type formats.
    string expected = 0;
    logRecordRender(&expected, &rec);
    if (!strEq(wd.rendered, expected))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("rendered=${string} != ${string}"),
                       stvar(strref, wd.rendered),
                       stvar(strref, expected));
    strDestroy(&expected);

    // A literal message is not a template, so one containing ${...} has to come back untouched.
    wireTestClear(&wd);
    wireFillRecord(&rec, chan, _SL("literal ${int} stays"), false, 0, NULL);
    logWireEncode(enc, &frames, &rec);
    wireFeed(dec, frames, wireTestCB, &wd);
    if (wd.istmpl || !strEq(wd.rendered, _SL("literal ${int} stays")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("literal message came back as ${string}"),
                       stvar(strref, wd.rendered));

    // Context fields cross as fields and stay nameable from the template.
    wireTestClear(&wd);
    withLogCtx (stvark(reqid, strref, _SL("abc123"))) {
        LogRecord crec;
        wireFillRecord(&crec, chan, _SL("req ${string:reqid}"), true, 0, NULL);
        crec.ctx = logCtxCurrent();
        logWireEncode(enc, &frames, &crec);
    }
    wireFeed(dec, frames, wireTestCB, &wd);
    if (wd.nctx != 1 || !strEq(wd.rendered, _SL("req abc123")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("nctx=${int}, rendered=${string} != 'req abc123'"),
                       stvar(int32, wd.nctx),
                       stvar(strref, wd.rendered));

    // A record carrying an object argument, logged for real so that the entry holds whatever the
    // log system put there rather than what this test guessed. The rendering taken at the call
    // site has to come back as something an ${object} placeholder still matches.
    wireTestClear(&wd);
    WireEncDest wed = { .enc = enc };
    LogDest* odest  = logRegisterDest(LOG_Info, _SL("wire/**"), wireEncDestMsg, NULL, NULL, &wed);
    FmtTestClass* obj = fmttestclassCreate(1, _S "before");
    logFmtC(Info, chan, _S "obj ${object}", stvar(object, obj));
    logFlush();
    logUnregisterDest(odest);
    wireFeed(dec, wed.frames, wireTestCB, &wd);
    if (!strEq(wd.rendered, _SL("obj Object(before:One)")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("object arg rendered=${string} != 'obj Object(before:One)'"),
                       stvar(strref, wd.rendered));
    objRelease(&obj);
    bufDestroy(&wed.scratch);
    bufDestroy(&wed.frames);

    logWireDecoderDestroy(&dec);
    logWireEncoderDestroy(&enc);
    wireTestClear(&wd);
    bufDestroy(&frames);
    return ret;
}

static int test_log_wiresegment(void)
{
    int ret         = 0;
    WireTestData wd = { 0 };
    Buffer first = 0, second = 0, one = 0;

    LogChannel* chan    = logChan(_SL("wire/seg"));
    LogWireEncoder* enc = logWireEncoderCreate(_SL("web-01"), 0);

    LogRecord rec;
    stvar args[] = { stvar(int32, 1) };
    wireFillRecord(&rec, chan, _SL("n=${int}"), true, 1, args);

    // Each encode replaces what is in its output buffer, so building a stream out of several is
    // the caller's job.
    for (int i = 0; i < 3; i++) {
        logWireEncode(enc, &one, &rec);
        bufAppend(&first, one);
    }

    // The boundary is what makes what follows independently decodable; the dictionary and every
    // declaration start over.
    logWireEndSegment(enc);

    for (int i = 0; i < 3; i++) {
        logWireEncode(enc, &one, &rec);
        bufAppend(&second, one);
    }

    if (wireCountKind(first, LOG_WireSegment) != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("first half has ${int} segment frames, want 1"),
                       stvar(int32, wireCountKind(first, LOG_WireSegment)));
    if (wireCountKind(second, LOG_WireSegment) != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("second half has ${int} segment frames, want 1"),
                       stvar(int32, wireCountKind(second, LOG_WireSegment)));

    // The declarations were re-sent, which is what says the dictionary really did reset.
    if (wireCountKind(second, LOG_WireChanDecl) != 1)
        TEST_FAILV_LOG(ret, 1, _SL("second half did not redeclare its channel"), stvNone);

    // Everything decodes when fed as one stream...
    Buffer both = 0;
    bufAppend(&both, first);
    bufAppend(&both, second);
    LogWireDecoder* dec = logWireDecoderCreate();
    if (!wireFeed(dec, both, wireTestCB, &wd) || wd.nentry != 6)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("whole stream decoded ${int} entries, want 6"),
                       stvar(int32, wd.nentry));
    logWireDecoderDestroy(&dec);

    // ...and the second segment still decodes on its own, which is the property that lets a spool
    // drop what came before it.
    wireTestClear(&wd);
    dec = logWireDecoderCreate();
    if (!wireFeed(dec, second, wireTestCB, &wd) || wd.nentry != 3)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("second segment alone decoded ${int} entries, want 3"),
                       stvar(int32, wd.nentry));
    logWireDecoderDestroy(&dec);

    // Bytes arriving one at a time have to produce the same frames as one big feed.
    wireTestClear(&wd);
    dec            = logWireDecoderCreate();
    const uint8* p = both->data;
    bool ok        = true;
    for (size_t i = 0; ok && i < both->len; i++)
        ok = logWireDecode(dec, p + i, 1, wireTestCB, &wd);
    if (!ok || wd.nentry != 6)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("byte-at-a-time decoded ${int} entries, want 6"),
                       stvar(int32, wd.nentry));
    logWireDecoderDestroy(&dec);

    logWireEncoderDestroy(&enc);
    wireTestClear(&wd);
    bufDestroy(&both);
    bufDestroy(&one);
    bufDestroy(&second);
    bufDestroy(&first);
    return ret;
}

static int test_log_wiredict(void)
{
    int ret = 0;

    LogChannel* chan = logChan(_SL("wire/dict"));
    LogRecord rec;

    // Unique string *values* are what a real log produces most of -- request ids, hostnames, error
    // text. They must not accumulate anywhere, so the stream stays one segment however many go
    // through it. This is the regression test for the concern that shaped the format.
    LogWireEncoder* enc = logWireEncoderCreate(_SL("web-01"), 0);
    Buffer frames       = 0;
    Buffer one          = 0;
    for (int i = 0; i < 1000; i++) {
        string val = 0;
        strFromInt64(&val, i, 10);
        strPrepend(_SL("request-"), &val);
        stvar args[] = { stvark(reqid, string, val) };
        wireFillRecord(&rec, chan, _SL("req ${string:reqid}"), true, 1, args);
        logWireEncode(enc, &one, &rec);
        bufAppend(&frames, one);
        strDestroy(&val);
    }
    if (wireCountKind(frames, LOG_WireSegment) != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("1000 unique values produced ${int} segments, want 1"),
                       stvar(int32, wireCountKind(frames, LOG_WireSegment)));
    logWireEncoderDestroy(&enc);
    bufDestroy(&frames);

    // Unique field *names* do accumulate, because a map key is always interned. An application
    // generating them without bound would grow the dictionary forever, so the cap cuts the
    // segment instead, which starts a fresh one.
    enc = logWireEncoderCreate(_SL("web-01"), 0);
    for (int i = 0; i < 6000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "field%d", i);
        // The name only has to outlive the encode call: the encoder copies it into its dictionary
        // and the record does not survive the loop.
        stvar args[] = { stvarkn(key, int32, i) };
        wireFillRecord(&rec, chan, _SL("n"), false, 1, args);
        logWireEncode(enc, &one, &rec);
        bufAppend(&frames, one);
    }
    int nseg = wireCountKind(frames, LOG_WireSegment);
    if (nseg < 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("6000 unique keys produced ${int} segments, want at least 2"),
                       stvar(int32, nseg));
    logWireEncoderDestroy(&enc);
    bufDestroy(&frames);
    bufDestroy(&one);

    return ret;
}

typedef struct InjectTestData {
    int count;
    int64 timestamp;
    uint64 seq;
    uint8 hops;
    string chanpath;
    string origin;
    string rendered;
} InjectTestData;

static void injectTestMsg(const LogRecord* rec, void* userdata)
{
    InjectTestData* id = (InjectTestData*)userdata;
    id->count++;
    id->timestamp = rec->timestamp;
    id->seq       = rec->seq;
    id->hops      = rec->hops;
    strDup(&id->chanpath, rec->chan->path);
    strDup(&id->origin, rec->origin);
    logRecordRender(&id->rendered, rec);
}

// Injects every entry frame it is handed, which is the whole of a receiver's job.
static bool injectFrameCB(const LogWireFrame* frame, void* ctx)
{
    if (frame->kind == LOG_WireEntry)
        logInject(frame->rec->chanpath, frame->rec);
    return true;
}

static int test_log_inject(void)
{
    int ret            = 0;
    InjectTestData id  = { 0 };
    Buffer frames      = 0;

    LogChannel* chan = logChan(_SL("inject/db"));
    LogDest* dest    = logRegisterDest(LOG_Info, _SL("inject/**"), injectTestMsg, NULL, NULL, &id);

    LogWireEncoder* enc = logWireEncoderCreate(_SL("db-07"), 0);
    LogWireDecoder* dec = logWireDecoderCreate();

    stvar args[] = { stvar(int32, 91), stvark(table, strref, _SL("users")) };
    LogRecord rec;
    wireFillRecord(&rec, chan, _SL("rows=${int} on ${string:table}"), true, 2, args);

    logWireEncode(enc, &frames, &rec);
    if (!wireFeed(dec, frames, injectFrameCB, NULL))
        TEST_FAILV_LOG(ret, 1, _SL("decode of the injected record failed"), stvNone);
    logFlush();

    if (id.count != 1)
        TEST_FAILV_LOG(ret, 1, _SL("id.count=${int} != 1"), stvar(int32, id.count));

    // The sender's timestamp and sequence number, not fresh ones: they are what identify the
    // record on the instance that produced it.
    if (id.timestamp != 1700000000000000LL)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("injected timestamp=${int} != 1700000000000000"),
                       stvar(int64, id.timestamp));
    if (id.seq != 4242)
        TEST_FAILV_LOG(ret, 1, _SL("injected seq=${uint} != 4242"), stvar(uint64, id.seq));

    // The channel path stays canonical; the machine it came from is a field.
    if (!strEq(id.chanpath, _SL("inject/db")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("injected chan=${string} != 'inject/db'"),
                       stvar(strref, id.chanpath));
    if (!strEq(id.origin, _SL("db-07")))
        TEST_FAILV_LOG(ret, 1, _SL("injected origin=${string} != 'db-07'"), stvar(strref, id.origin));
    if (id.hops != 1)
        TEST_FAILV_LOG(ret, 1, _SL("injected hops=${uint} != 1"), stvar(uint32, (uint32)id.hops));
    if (!strEq(id.rendered, _SL("rows=91 on users")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("injected rendered=${string} != 'rows=91 on users'"),
                       stvar(strref, id.rendered));

    // The receiver's own level check applies: nothing below what this destination asked for gets
    // in just because a sender chose to send it.
    id.count = 0;
    wireFillRecord(&rec, chan, _SL("chatty"), false, 0, NULL);
    rec.level = LOG_Debug;
    logWireEncode(enc, &frames, &rec);
    wireFeed(dec, frames, injectFrameCB, NULL);
    logFlush();
    if (id.count != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a Debug record reached an Info destination ${int} times"),
                       stvar(int32, id.count));

    logUnregisterDest(dest);
    logWireDecoderDestroy(&dec);
    logWireEncoderDestroy(&enc);

    strDestroy(&id.chanpath);
    strDestroy(&id.origin);
    strDestroy(&id.rendered);
    bufDestroy(&frames);
    return ret;
}

static int test_log_wirebad(void)
{
    int ret         = 0;
    WireTestData wd = { 0 };

    // A stream that stops in the middle of a frame is not an error -- the rest may still be on its
    // way -- so the decoder holds what it has and reports nothing.
    LogChannel* chan    = logChan(_SL("wire/bad"));
    LogWireEncoder* enc = logWireEncoderCreate(_SL("web-01"), 0);
    Buffer frames       = 0;
    LogRecord rec;
    stvar args[] = { stvar(int32, 5) };
    wireFillRecord(&rec, chan, _SL("n=${int}"), true, 1, args);
    logWireEncode(enc, &frames, &rec);

    const uint8* p = frames->data;
    size_t len     = frames->len;

    LogWireDecoder* dec = logWireDecoderCreate();
    if (!logWireDecode(dec, p, len - 3, wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("a truncated stream was reported as malformed"), stvNone);
    if (wd.nentry != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a truncated stream produced ${int} entries"),
                       stvar(int32, wd.nentry));
    // ...and completing it delivers the frame that was waiting
    if (!logWireDecode(dec, p + len - 3, 3, wireTestCB, &wd) || wd.nentry != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("completing the stream produced ${int} entries, want 1"),
                       stvar(int32, wd.nentry));
    logWireDecoderDestroy(&dec);

    // Garbage is malformed, and says so rather than guessing.
    wireTestClear(&wd);
    dec                 = logWireDecoderCreate();
    static const uint8 junk[] = { 0x04, 0x08, 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04 };
    if (logWireDecode(dec, junk, sizeof(junk), wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("garbage was accepted"), stvNone);
    logWireDecoderDestroy(&dec);

    // A declared length past the cap is rejected outright rather than believed and allocated for.
    wireTestClear(&wd);
    dec = logWireDecoderCreate();
    static const uint8 huge[] = { 0x04, 0xff, 0xff, 0xff, 0xff, 0x7f };
    if (logWireDecode(dec, huge, sizeof(huge), wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("an over-long frame length was accepted"), stvNone);
    logWireDecoderDestroy(&dec);

    // A frame whose body ends inside a value it declared: the reader must fail rather than wait
    // forever for bytes the framing already said are not coming.
    wireTestClear(&wd);
    dec = logWireDecoderCreate();
    Buffer trunc = 0;
    size_t pos   = 0;
    uint64 kind, blen;
    size_t boff;
    wireNextFrame(p, len, &pos, &kind, &boff, &blen);   // the segment frame, kept intact

    // An entry frame that declares three payload bytes and then opens a map of fifteen, so the
    // reader runs out of body inside a value the framing already said is complete.
    static const uint8 shortbody[] = { LOG_WireEntry, 0x03, 0x0b, 0x0f, 0x00 };
    bufAppendBytes(&trunc, p, pos);
    bufAppendBytes(&trunc, shortbody, sizeof(shortbody));
    if (wireFeed(dec, trunc, wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("a frame truncated inside a value was accepted"), stvNone);
    logWireDecoderDestroy(&dec);
    bufDestroy(&trunc);

    logWireEncoderDestroy(&enc);
    wireTestClear(&wd);
    bufDestroy(&frames);
    return ret;
}

// ---------------------------------------------------------------------------------------
// Forwarding
// ---------------------------------------------------------------------------------------

typedef struct FwdTestData {
    Mutex lock;
    Buffer sink;     // everything the transport accepted, in order
    int nsend;       // calls to send
    bool refuse;     // the transport is not taking anything
    Event gate;      // held closed to make send block, for the group test
    bool usegate;
    LogChannel* logchan;   // send logs here when set, which is the synchronous loop case
    bool logasync;         // ...or off-thread, after returning, which is the case a scope misses
} FwdTestData;

static Thread* fwdAsyncThread;

static int fwdAsyncProc(Thread* self)
{
    // The case a thread-local scope structurally cannot see: a non-blocking transport reports on
    // a send after the send has returned, on a thread of its own.
    LogChannel* chan = stvlNextPtr(&self->args);
    for (int i = 0; i < 8; i++)
        logStrC(Error, chan, _S "transport send failed");
    return 0;
}

static bool fwdTestSend(void* ctx, const uint8* buf, size_t len)
{
    FwdTestData* fd = (FwdTestData*)ctx;

    if (fd->usegate)
        eventWaitTimeout(&fd->gate, timeS(5));

    if (fd->logchan) {
        if (fd->logasync) {
            if (!fwdAsyncThread)
                fwdAsyncThread = thrCreate(fwdAsyncProc, _S "fwdasync", stvar(ptr, fd->logchan));
        } else {
            logStrC(Error, fd->logchan, _S "transport send failed");
        }
    }

    withMutex (&fd->lock) {
        fd->nsend++;
        if (!fd->refuse)
            bufAppendBytes(&fd->sink, buf, len);
    }

    return !fd->refuse;
}

static const LogForwardHandlers kFwdTestHandlers = { .send = fwdTestSend };

static void fwdTestInit(FwdTestData* fd)
{
    memset(fd, 0, sizeof(*fd));
    mutexInit(&fd->lock);
    eventInit(&fd->gate);
}

static void fwdTestDestroy(FwdTestData* fd)
{
    bufDestroy(&fd->sink);
    eventDestroy(&fd->gate);
    mutexDestroy(&fd->lock);
}

// A forwarder ships nothing until a receiver asks, so every forwarding test has to ask first.
static void fwdSubscribe(LogForwarder* fwd, int maxlevel, strref pattern)
{
    LogSubSpec spec = { .maxlevel = maxlevel };
    saInit(&spec.patterns, string, 2);
    if (!strEmpty(pattern))
        saPush(&spec.patterns, string, (string)pattern);
    logForwardApplySub(fwd, &spec);
    saDestroy(&spec.patterns);
}

static int test_log_forward(void)
{
    int ret         = 0;
    WireTestData wd = { 0 };
    FwdTestData fd;
    fwdTestInit(&fd);

    LogChannel* chan = logChan(_SL("fwd/app"));
    LogForwardConfig cfg = { .origin = _SL("leaf-1") };
    LogForwarder* fwd =
        logforwardRegister(LOG_Info, _SL("fwd/**"), &kFwdTestHandlers, &fd, &cfg);
    if (!fwd) {
        TEST_FAILV_LOG(ret, 1, _SL("logforwardRegister returned NULL"), stvNone);
        fwdTestDestroy(&fd);
        return ret;
    }

    fwdSubscribe(fwd, LOG_Info, NULL);

    logFmtC(Info, chan, _S "rows=${int} table=${string:table}",
            stvar(int32, 17), stvark(table, strref, _SL("orders")));
    logStrC(Info, chan, _S "second");
    logFlush();

    // Whatever reached the transport has to decode end to end, which is the only assertion that
    // says the seam actually carries a record rather than merely bytes.
    LogWireDecoder* dec = logWireDecoderCreate();
    if (!wireFeed(dec, fd.sink, wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("forwarded bytes did not decode"), stvNone);
    if (wd.nentry != 2)
        TEST_FAILV_LOG(ret, 1, _SL("decoded ${int} entries, want 2"), stvar(int32, wd.nentry));
    if (!strEq(wd.chanpath, _SL("fwd/app")))
        TEST_FAILV_LOG(ret, 1, _SL("chanpath=${string} != 'fwd/app'"), stvar(strref, wd.chanpath));
    if (!strEq(wd.origin, _SL("leaf-1")))
        TEST_FAILV_LOG(ret, 1, _SL("origin=${string} != 'leaf-1'"), stvar(strref, wd.origin));
    logWireDecoderDestroy(&dec);

    LogForwardStats st;
    logForwardStats(fwd, &st);
    if (st.sent != 2 || st.spooled != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("sent=${uint} spooled=${uint}, want 2 and 0"),
                       stvar(uint64, st.sent),
                       stvar(uint64, st.spooled));

    logforwardUnregister(fwd);
    logFlush();
    wireTestClear(&wd);
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_forwardspool(void)
{
    int ret         = 0;
    WireTestData wd = { 0 };
    FwdTestData fd;
    fwdTestInit(&fd);

    LogChannel* chan = logChan(_SL("fwdspool/app"));

    // Small enough that a few hundred records overflow it several times over, so eviction is what
    // the test is actually watching rather than a corner it never reaches.
    LogForwardConfig cfg = {
        .origin     = _SL("leaf-2"),
        .spoolbytes = 8192,
        .segbytes   = 1024,
    };
    LogForwarder* fwd =
        logforwardRegister(LOG_Info, _SL("fwdspool/**"), &kFwdTestHandlers, &fd, &cfg);

    fwdSubscribe(fwd, LOG_Info, NULL);

    fd.refuse = true;
    for (int i = 0; i < 400; i++)
        logFmtC(Info, chan, _S "n=${int} pad=${string}", stvar(int32, i),
                stvar(strref, _SL("0123456789012345678901234567890123456789")));
    logFlush();

    LogForwardStats st;
    logForwardStats(fwd, &st);
    if (st.dropped == 0)
        TEST_FAILV_LOG(ret, 1, _SL("nothing was dropped from a bounded spool"), stvNone);
    if (st.pending > cfg.spoolbytes + cfg.segbytes)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("spool holds ${uint} bytes, over its ${uint} byte bound"),
                       stvar(uint64, st.pending),
                       stvar(uint64, cfg.spoolbytes));

    // The transport comes back, and everything still spooled goes out.
    fd.refuse = false;
    logForwardResume(fwd);

    LogWireDecoder* dec = logWireDecoderCreate();
    if (!wireFeed(dec, fd.sink, wireTestCB, &wd))
        TEST_FAILV_LOG(ret, 1, _SL("what survived the spool did not decode"), stvNone);

    // A gap record says what is missing, rather than the receiver silently seeing a shorter log
    // than the sender produced.
    if (wd.ngap == 0)
        TEST_FAILV_LOG(ret, 1, _SL("no gap record was emitted"), stvNone);
    if (wd.gap.count == 0 || wd.gap.firstseq == 0 || wd.gap.lastseq < wd.gap.firstseq)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("gap count=${uint} first=${uint} last=${uint} is not a range"),
                       stvar(uint64, wd.gap.count),
                       stvar(uint64, wd.gap.firstseq),
                       stvar(uint64, wd.gap.lastseq));

    // ...and what did survive is the newest traffic, which is the half closest to whatever went
    // wrong.
    if (wd.nentry == 0)
        TEST_FAILV_LOG(ret, 1, _SL("nothing at all survived the spool"), stvNone);
    logForwardStats(fwd, &st);
    if ((uint64)wd.nentry + st.dropped < 400)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("delivered ${int} + dropped ${uint} does not account for 400 records"),
                       stvar(int32, wd.nentry),
                       stvar(uint64, st.dropped));
    logWireDecoderDestroy(&dec);

    logforwardUnregister(fwd);
    logFlush();
    wireTestClear(&wd);
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_forwardgroup(void)
{
    int ret = 0;
    FwdTestData fd;
    fwdTestInit(&fd);
    fd.usegate = true;   // send blocks until the gate opens

    LogChannel* rchan = logChan(_SL("fwdgrp/remote"));
    LogChannel* lchan = logChan(_SL("fwdgrp/local"));

    LogDest* ldest     = logmembufRegister(LOG_Info, _S "fwdgrp/local", 8192, NULL);
    LogMembufData* lmd = logmembufData(ldest);
    LogForwarder* fwd =
        logforwardRegister(LOG_Info, _SL("fwdgrp/remote"), &kFwdTestHandlers, &fd, NULL);

    fwdSubscribe(fwd, LOG_Info, NULL);

    // The forwarder's drain thread parks inside send...
    logStrC(Info, rchan, _S "stalls the remote group");

    // ...and the default group has to keep going regardless, which is the whole reason a
    // forwarder lands in a group of its own.
    logStrC(Info, lchan, _S "must not wait for it");

    int64 deadline = clockTimer() + timeS(3);
    while (membufLines(lmd) < 1 && clockTimer() < deadline)
        osSleep(timeMS(5));

    if (membufLines(lmd) != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("the local destination saw ${int} lines while a forwarder was "
                           "stalled, want 1"),
                       stvar(int32, membufLines(lmd)));

    eventSignal(&fd.gate);
    logFlush();

    logforwardUnregister(fwd);
    logUnregisterDest(ldest);
    logFlush();
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_forwardloop(void)
{
    int ret = 0;
    FwdTestData fd;
    fwdTestInit(&fd);

    LogChannel* netchan = logChan(_SL("cx/net"));
    LogChannel* subchan = logChan(_SL("cx/net/socket"));
    LogChannel* appchan = logChan(_SL("loop/app"));

    // The filter names cx's transport explicitly, which is exactly the configuration that would
    // otherwise close the loop. The exclusion is not a default that a filter can override.
    LogForwardConfig cfg = { .origin = _SL("leaf-3"), .maxhops = 2 };
    LogForwarder* fwd = logforwardRegister(LOG_Trace, _SL("cx/**"), &kFwdTestHandlers, &fd, &cfg);
    logDestAddFilter(fwd ? logForwardDest(fwd) : NULL, _SL("loop/**"), false);
    fwdSubscribe(fwd, LOG_Trace, NULL);

    logStrC(Error, netchan, _S "socket error");
    logStrC(Error, subchan, _S "socket error beneath it");
    logFlush();

    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("cx/net reached a forwarder ${int} times with a cx/** filter"),
                       stvar(int32, fd.nsend));

    // A record produced inside the transport's own send does not come back to it either.
    fd.logchan = appchan;
    logStrC(Info, appchan, _S "triggers a send that logs");
    logFlush();
    logFlush();   // the record send logged has to have been through a whole drain cycle
    fd.logchan = NULL;

    int after = fd.nsend;
    if (after != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a record logged inside send produced ${int} sends, want 1"),
                       stvar(int32, after));

    // withLogLocal() extends the same protection to an application's own transport code: the
    // record still reaches local destinations, so the transport stays diagnosable.
    LogDest* ldest     = logmembufRegister(LOG_Info, _S "loop/**", 8192, NULL);
    LogMembufData* lmd = logmembufData(ldest);
    withLogLocal() {
        logStrC(Info, appchan, _S "local only");
    }
    logFlush();
    if (membufLines(lmd) != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("withLogLocal record reached ${int} local lines, want 1"),
                       stvar(int32, membufLines(lmd)));
    if (fd.nsend != after)
        TEST_FAILV_LOG(ret, 1, _SL("a withLogLocal record was forwarded"), stvNone);

    // Cross-process loop prevention: a record that has been round enough times, and one this
    // instance originally produced, both stop here.
    LogWireRecord wrec = {
        .level     = LOG_Info,
        .chanpath  = _SL("loop/app"),
        .timestamp = 1700000000000000LL,
        .seq       = 5000,
        .sample    = 1,
        .trigger   = -1,
        .hops      = 2,
        .origin    = _SL("somewhere-else"),
        .msgtmpl   = _SL("travelled too far"),
    };
    logInject(_SL("loop/app"), &wrec);

    wrec.hops   = 0;
    wrec.seq    = 5001;
    wrec.origin = _SL("leaf-3");   // this forwarder's own identity, come back around
    logInject(_SL("loop/app"), &wrec);
    logFlush();

    LogForwardStats st;
    logForwardStats(fwd, &st);
    if (st.looped != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("looped=${uint}, want 2"),
                       stvar(uint64, st.looped));
    if (fd.nsend != after)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a looping record was forwarded: nsend=${int} want ${int}"),
                       stvar(int32, fd.nsend),
                       stvar(int32, after));

    logforwardUnregister(fwd);
    logUnregisterDest(ldest);
    logFlush();
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_forwardloopasync(void)
{
    int ret = 0;
    FwdTestData fd;
    fwdTestInit(&fd);

    LogChannel* appchan = logChan(_SL("loopa/app"));
    LogChannel* netchan = logChan(_SL("cx/net"));

    // The case that decided the shape of loop prevention. A non-blocking transport logs about a
    // send from one of its own threads, after the send returned, where no thread-local scope can
    // still be in effect. Only the unconditional cx/net exclusion stops this, so a build that had
    // the scope and nothing else would fail here.
    fd.logchan  = netchan;
    fd.logasync = true;

    LogForwarder* fwd = logforwardRegister(LOG_Trace, _SL("cx/**"), &kFwdTestHandlers, &fd, NULL);
    logDestAddFilter(fwd ? logForwardDest(fwd) : NULL, _SL("loopa/**"), false);
    fwdSubscribe(fwd, LOG_Trace, NULL);

    logStrC(Info, appchan, _S "one record, one send");
    logFlush();

    if (fwdAsyncThread) {
        thrWait(fwdAsyncThread, timeS(5));
        thrRelease(&fwdAsyncThread);
    }
    logFlush();
    logFlush();

    fd.logchan  = NULL;
    fd.logasync = false;

    // One record in, one send out. Anything more means the transport's own reports came back
    // around, which is the loop this is here to prove does not exist.
    if (fd.nsend != 1)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("nsend=${int} after one record, want 1 -- the transport's own "
                           "off-thread logging came back around"),
                       stvar(int32, fd.nsend));

    logforwardUnregister(fwd);
    logFlush();
    fwdTestDestroy(&fd);
    return ret;
}

// ---------------------------------------------------------------------------------------
// Subscription and catalog
// ---------------------------------------------------------------------------------------

typedef struct CatTestData {
    int ncat;
    int nchans;
    bool sawrestricted;
    bool sawplain;
} CatTestData;

static bool catTestCB(const LogWireFrame* frame, void* ctx)
{
    CatTestData* cd = (CatTestData*)ctx;
    if (frame->kind != LOG_WireCatalog)
        return true;

    cd->ncat++;
    cd->nchans = frame->cat->nchans;
    for (int i = 0; i < frame->cat->nchans; i++) {
        if (strEq(frame->cat->chans[i].path, _SL("cattest/secret")) &&
            (frame->cat->chans[i].flags & LOG_Restricted))
            cd->sawrestricted = true;
        if (strEq(frame->cat->chans[i].path, _SL("cattest/open")))
            cd->sawplain = true;
    }
    return true;
}

static int test_log_subscribe(void)
{
    int ret = 0;
    FwdTestData fd;
    fwdTestInit(&fd);

    LogChannel* db   = logChan(_SL("sub/db"));
    LogChannel* http = logChan(_SL("sub/http"));

    LogForwarder* fwd = logforwardRegister(LOG_Info, _SL("sub/**"), &kFwdTestHandlers, &fd, NULL);

    // Nothing has asked for anything, so nothing is sent. This is §10.7's whole position and the
    // part most likely to be assumed theoretical.
    logStrC(Info, db, _S "before anybody subscribed");
    logStrC(Info, http, _S "also before");
    logFlush();
    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("an unsubscribed forwarder sent ${int} times"),
                       stvar(int32, fd.nsend));

    // A subscription arriving as encoded control frames -- the way a real collector sends one --
    // and the same subscription applied directly have to produce identical routing.
    LogWireEncoder* cenc = logWireEncoderCreate(NULL, 0);
    Buffer ctl           = 0;
    LogSubSpec spec      = { .maxlevel = LOG_Info };
    saInit(&spec.patterns, string, 1);
    saPush(&spec.patterns, string, _S "sub/db");
    logWireEncodeSub(cenc, &ctl, &spec);
    saDestroy(&spec.patterns);

    if (!logForwardRecv(fwd, ctl->data, ctl->len))
        TEST_FAILV_LOG(ret, 1, _SL("logForwardRecv rejected a subscription frame"), stvNone);

    logStrC(Info, db, _S "wanted");
    logStrC(Info, http, _S "not wanted");
    logFlush();

    WireTestData wd     = { 0 };
    size_t fed          = 0;
    LogWireDecoder* dec = logWireDecoderCreate();
    wireFeedTail(dec, fd.sink, &fed, wireTestCB, &wd);
    if (wd.nentry != 1 || !strEq(wd.chanpath, _SL("sub/db")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a subscription to sub/db delivered ${int} entries, last on ${string}"),
                       stvar(int32, wd.nentry),
                       stvar(strref, wd.chanpath));

    // The same thing said directly rather than over the wire has to produce identical routing.
    fwdSubscribe(fwd, LOG_Info, _SL("sub/db"));
    logStrC(Info, db, _S "wanted again");
    logStrC(Info, http, _S "still not wanted");
    logFlush();

    wireFeedTail(dec, fd.sink, &fed, wireTestCB, &wd);
    if (wd.nentry != 2 || !strEq(wd.chanpath, _SL("sub/db")))
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("logForwardApplySub delivered ${int} entries in total, last on "
                           "${string}"),
                       stvar(int32, wd.nentry),
                       stvar(strref, wd.chanpath));
    logWireDecoderDestroy(&dec);

    // A subscription narrows what registration allowed; it can never widen it. Asking for a
    // channel outside the registration filter, at a level above the registration level, gets
    // neither.
    fd.nsend = 0;
    fwdSubscribe(fwd, LOG_Trace, _SL("**"));
    LogChannel* other = logChan(_SL("elsewhere/thing"));
    logStrC(Info, other, _S "outside what registration allowed");
    logStrC(Debug, db, _S "more verbose than registration allowed");
    logFlush();
    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a subscription widened past registration: ${int} sends"),
                       stvar(int32, fd.nsend));

    // A subscription that has run out stops the traffic. The expiry is noticed on the drain
    // thread, which may only mark it -- reconfiguring a destination takes the log system's
    // configuration lock, which a drain thread must never hold -- so the records stop at once and
    // the routing catches up on the next call from an application thread.
    fd.nsend        = 0;
    LogSubSpec lapsing = { .maxlevel = LOG_Info, .expiry = clockWall() - timeS(1) };
    saInit(&lapsing.patterns, string, 1);
    saPush(&lapsing.patterns, string, _S "sub/db");
    logForwardApplySub(fwd, &lapsing);
    saDestroy(&lapsing.patterns);

    logStrC(Info, db, _S "after the subscription lapsed");
    logFlush();
    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a lapsed subscription still sent ${int} times"),
                       stvar(int32, fd.nsend));

    logForwardResume(fwd);   // any application-thread call finishes the teardown
    LogForwardStats lst;
    logForwardStats(fwd, &lst);
    if (lst.subscribed)
        TEST_FAILV_LOG(ret, 1, _SL("a lapsed subscription still reports as subscribed"), stvNone);

    logStrC(Info, db, _S "and after the teardown caught up");
    logFlush();
    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("a lapsed subscription sent ${int} times after teardown"),
                       stvar(int32, fd.nsend));

    // Unsubscribing returns it to silence.
    fd.nsend = 0;
    logForwardApplySub(fwd, NULL);
    logStrC(Info, db, _S "after unsubscribing");
    logFlush();
    if (fd.nsend != 0)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("an unsubscribed forwarder sent ${int} times"),
                       stvar(int32, fd.nsend));

    LogForwardStats st;
    logForwardStats(fwd, &st);
    if (st.subscribed)
        TEST_FAILV_LOG(ret, 1, _SL("stats still report a subscription after unsubscribing"), stvNone);

    logforwardUnregister(fwd);
    logFlush();
    logWireEncoderDestroy(&cenc);
    wireTestClear(&wd);
    bufDestroy(&ctl);
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_catalog(void)
{
    int ret = 0;
    FwdTestData fd;
    fwdTestInit(&fd);
    CatTestData cd = { 0 };

    logDeclareChan(_SL("cattest/secret"), 0);   // restricted, which is what declaring one means
    logChan(_SL("cattest/open"));

    LogForwarder* fwd = logforwardRegister(LOG_Info, _SL("cattest/**"), &kFwdTestHandlers, &fd,
                                           NULL);

    Buffer frames = 0;
    if (!logForwardCatalog(fwd, &frames))
        TEST_FAILV_LOG(ret, 1, _SL("logForwardCatalog failed"), stvNone);

    LogWireDecoder* dec = logWireDecoderCreate();
    if (!wireFeed(dec, frames, catTestCB, &cd))
        TEST_FAILV_LOG(ret, 1, _SL("the catalog did not decode"), stvNone);

    if (cd.ncat != 1)
        TEST_FAILV_LOG(ret, 1, _SL("decoded ${int} catalog frames, want 1"), stvar(int32, cd.ncat));
    if (cd.nchans < 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("the catalog listed ${int} channels, want at least 2"),
                       stvar(int32, cd.nchans));

    // The flags travel with the inventory, so a receiver cannot mistake a restricted channel for
    // an ordinary one.
    if (!cd.sawrestricted)
        TEST_FAILV_LOG(ret, 1, _SL("cattest/secret was not listed as restricted"), stvNone);
    if (!cd.sawplain)
        TEST_FAILV_LOG(ret, 1, _SL("cattest/open was not in the catalog"), stvNone);

    logWireDecoderDestroy(&dec);
    logforwardUnregister(fwd);
    logFlush();
    bufDestroy(&frames);
    fwdTestDestroy(&fd);
    return ret;
}

static int test_log_destfilter(void)
{
    int ret = 0;

    LogChannel* a = logChan(_SL("dfilt/a"));
    LogChannel* b = logChan(_SL("dfilt/b"));

    LogDest* dest     = logmembufRegister(LOG_Info, _S "dfilt/a", 8192, NULL);
    LogMembufData* md = logmembufData(dest);

    logStrC(Info, a, _S "one");
    logStrC(Info, b, _S "not matched yet");
    logFlush();
    if (membufLines(md) != 1)
        TEST_FAILV_LOG(ret, 1, _SL("membufLines=${int} != 1"), stvar(int32, membufLines(md)));

    // Replacing the filter is not the same as adding to it: the old rule is gone, so what used to
    // match no longer does.
    if (!logDestSetFilter(dest, _SL("dfilt/b")))
        TEST_FAILV_LOG(ret, 1, _SL("logDestSetFilter failed"), stvNone);

    logStrC(Info, a, _S "no longer matched");
    logStrC(Info, b, _S "matched now");
    logFlush();
    if (membufLines(md) != 2)
        TEST_FAILV_LOG(ret,
                       1,
                       _SL("after logDestSetFilter membufLines=${int} != 2"),
                       stvar(int32, membufLines(md)));

    string snap = 0;
    membufSnapshot(&snap, md);
    if (membufCount(snap, _S "no longer matched") != 0)
        TEST_FAILV_LOG(ret, 1, _SL("the replaced filter still matched its old channel"), stvNone);
    if (membufCount(snap, _S "matched now") != 1)
        TEST_FAILV_LOG(ret, 1, _SL("the new filter did not take effect"), stvNone);

    // A runtime level change takes effect on the next record, and raises the channel's ceiling so
    // the call site starts producing at all. Verbose rather than Debug because Debug is compiled
    // out of a release build entirely, and this test has to mean the same thing in every build.
    if (!logDestSetLevel(dest, LOG_Verbose))
        TEST_FAILV_LOG(ret, 1, _SL("logDestSetLevel failed"), stvNone);
    logStrC(Verbose, b, _S "now verbose");
    logFlush();

    strClear(&snap);
    membufSnapshot(&snap, md);
    if (membufCount(snap, _S "now verbose") != 1)
        TEST_FAILV_LOG(ret, 1, _SL("logDestSetLevel did not take effect"), stvNone);

    // ...and down again
    if (!logDestSetLevel(dest, LOG_Warn))
        TEST_FAILV_LOG(ret, 1, _SL("logDestSetLevel failed on the way back down"), stvNone);
    logStrC(Info, b, _S "too quiet now");
    logFlush();

    strClear(&snap);
    membufSnapshot(&snap, md);
    if (membufCount(snap, _S "too quiet now") != 0)
        TEST_FAILV_LOG(ret, 1, _SL("lowering the level did not take effect"), stvNone);

    logUnregisterDest(dest);
    logFlush();
    strDestroy(&snap);
    return ret;
}

testfunc logtest_funcs[] = {
    { "levels",        test_log_levels        },
    { "shutdown",      test_log_shutdown      },
    { "batch",         test_log_batch         },
    { "channels",      test_log_channels      },
    { "backfill",      test_log_backfill      },
    { "dest",          test_log_dest          },
    { "site",          test_log_site          },
    { "record",        test_log_record        },
    { "serializer",    test_log_serializer    },
    { "textomit",      test_log_textomit      },
    { "groups",        test_log_groups        },
    { "volume",        test_log_volume        },
    { "bootwindow",    test_log_bootwindow    },
    { "bootreplay",    test_log_bootreplay    },
    { "debugring",     test_log_debugring     },
    { "ctx",           test_log_ctx           },
    { "hierarchy",     test_log_hierarchy     },
    { "cxchan",        test_log_cxchan        },
    { "console",       test_log_console       },
    { "console_style", test_log_console_style },
    { "wire",          test_log_wire          },
    { "wiresegment",   test_log_wiresegment   },
    { "wiredict",      test_log_wiredict      },
    { "inject",        test_log_inject        },
    { "wirebad",       test_log_wirebad       },
    { "forward",       test_log_forward       },
    { "forwardspool",  test_log_forwardspool  },
    { "forwardgroup",  test_log_forwardgroup  },
    { "forwardloop",   test_log_forwardloop   },
    { "forwardloopasync", test_log_forwardloopasync },
    { "subscribe",     test_log_subscribe     },
    { "catalog",       test_log_catalog       },
    { "destfilter",    test_log_destfilter    },
    { 0,               0                      }
};

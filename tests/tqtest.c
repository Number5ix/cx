#include <stdio.h>
#include <cx/string.h>
#include <cx/taskqueue.h>
#include <cx/log/logmembuf.h>
#include "tqtestobj.h"

#define TEST_FILE tqtest
#define TEST_FUNCS tqtest_funcs
#include "common.h"

static atomic(intptr) accum1;
static atomic(intptr) accum2;
static Event notifyev;

#define NUM_TASK_TEST 50000

static int test_tqtest_task(void)
{
    int ret = 0;
    int total = 0, total2 = 0;
    sa_TQTest1 ttasks;

    TaskQueueConfig conf;
    tqPresetBalanced(&conf);
    TaskQueue *q = tqCreate(_S"Test", &conf);
    if(!q || !tqStart(q))
        return 1;

    saInit(&ttasks, object, NUM_TASK_TEST);

    eventInit(&notifyev);

    // create tasks
    for(int i = 0; i < NUM_TASK_TEST; i++) {
        int n1 = rand(), n2 = rand();
        total += n1 + n2;
        TQTest1 *t = tqtest1Create(n1, n2, &notifyev);
        if(t) {
            saPushC(&ttasks, object, &t);
        } else {
            ret = 1;
        }
    }

    // run tasks
    int ntasks = saSize(ttasks);
    for(int i = 0; i < ntasks; i++) {
        tqAdd(q, ttasks.a[i]);
    }

    // wait for tasks
    int64 timeStart = clockTimer();
    for(;;) {
        bool done = true;
        for(int i = 0; i < ntasks; i++) {
            switch(btaskState(ttasks.a[i])) {
            case TASK_Succeeded:
                break;
            case TASK_Failed:
                ret = 1;
                break;
            default:
                done = false;
            }
        }

        if(done)
            break;

        eventWaitTimeout(&notifyev, timeS(3));
        if(clockTimer() - timeStart > timeS(60)) {
            ret = 1;
            break;
        }
    }

    eventDestroy(&notifyev);
    tqShutdown(q, timeS(60));
    tqRelease(&q);

    for(int i = 0; i < ntasks; i++) {
        if(btaskState(ttasks.a[i]) != TASK_Succeeded)
            ret = 1;
        total2 += ttasks.a[i]->total;
    }

    if(total != total2)
        ret = 1;

    saDestroy(&ttasks);

    return ret;
}

static int test_tqtest_failure(void)
{
    int ret = 0;
    int nsuc = 0, nfail = 0;
    sa_TQTestFail ttasks;

    TaskQueueConfig conf;
    tqPresetBalanced(&conf);
    TaskQueue *q = tqCreate(_S"Test", &conf);
    if(!q || !tqStart(q))
        return 1;

    saInit(&ttasks, object, NUM_TASK_TEST);

    eventInit(&notifyev);

    // create tasks
    for(int i = 0; i < NUM_TASK_TEST; i++) {
        TQTestFail *t = tqtestfailCreate(i, &notifyev);
        if(t) {
            saPushC(&ttasks, object, &t);
        } else {
            ret = 1;
        }
    }

    // run tasks
    int ntasks = saSize(ttasks);
    for(int i = 0; i < ntasks; i++) {
        tqAdd(q, ttasks.a[i]);
    }

    // wait for tasks
    int64 timeStart = clockTimer();
    for(;;) {
        bool done = true;
        for(int i = 0; i < ntasks; i++) {
            switch(btaskState(ttasks.a[i])) {
            case TASK_Succeeded:
            case TASK_Failed:
                break;
            default:
                done = false;
            }
        }

        if(done)
            break;

        eventWaitTimeout(&notifyev, timeS(3));
        if(clockTimer() - timeStart > timeS(60)) {
            ret = 1;
            break;
        }
    }

    tqShutdown(q, timeS(60));
    tqRelease(&q);
    eventDestroy(&notifyev);

    for(int i = 0; i < ntasks; i++) {
        if(btaskState(ttasks.a[i]) == TASK_Succeeded)
            nsuc++;
        else if(btaskState(ttasks.a[i]) == TASK_Failed)
            nfail++;
    }

    if(nsuc != ntasks / 2 || nfail != ntasks / 2)
        ret = 1;

    saDestroy(&ttasks);

    return ret;
}

static int test_tqtest_concurrency(void)
{
    int ret = 0;
    int total = 0;
    int accum = 0;
    int count = 0;

    TaskQueueConfig conf = { 0 };
    conf.wInitial = 8;
    conf.wIdle = 8;
    conf.wBusy = 16;
    conf.wMax = 64;
    conf.tIdle = timeS(1);
    conf.tRampUp = timeMS(2);
    conf.tRampDown = timeMS(50);
    TaskQueue *spmc = tqCreate(_S"SPMC Test", &conf);
    if(!spmc || !tqStart(spmc))
        return 1;

    conf.wInitial = 1;
    conf.wIdle = 1;
    conf.wBusy = 1;
    conf.wMax = 1;
    conf.tIdle = 0;
    conf.tRampUp = 0;
    conf.tRampDown = 0;
    TaskQueue *mpsc = tqCreate(_S"MPSC Test", &conf);
    if(!mpsc || !tqStart(mpsc))
        return 1;

    eventInit(&notifyev);

    // fire off some tasks into the input queue
    for(int i = 0; i < NUM_TASK_TEST; i++) {
        int n1 = rand(), n2 = rand();
        total += n1 + n2;
        TQTestCC1 *t = tqtestcc1Create(n1, n2, mpsc, &accum, &count, &notifyev);
        tqRun(spmc, &t);
    }

    // wait for tasks
    int64 timeStart = clockTimer();
    while(count < NUM_TASK_TEST) {
        eventWaitTimeout(&notifyev, timeS(3));
        if(clockTimer() - timeStart > timeS(60)) {
            ret = 1;
            break;
        }
    }

    eventDestroy(&notifyev);
    tqShutdown(spmc, timeS(60));
    tqRelease(&spmc);
    tqShutdown(mpsc, timeS(60));
    tqRelease(&mpsc);

    if(total != accum)
        ret = 1;

    return ret;
}

static bool tqtest_callcb(TaskQueue *tq, void *data)
{
    atomicFetchAdd(intptr, &accum1, (intptr)data, Relaxed);
    atomicFetchAdd(intptr, &accum2, 1, AcqRel);
    eventSignal(&notifyev);
    return true;
}

#define NUM_CALL_TEST 50000

static int test_tqtest_call(void)
{
    TaskQueueConfig conf;
    int ret = 0;
    intptr total = 0;

    eventInit(&notifyev);
    atomicStore(intptr, &accum1, 0, Relaxed);
    atomicStore(intptr, &accum2, 0, Relaxed);

    tqPresetBalanced(&conf);
    TaskQueue *q = tqCreate(_S"Test", &conf);
    if(!q || !tqStart(q))
        return 1;

    for(int i = 0; i < NUM_CALL_TEST; i++) {
        intptr rnum = rand() % 255;
        total += rnum;
        tqCall(q, tqtest_callcb, (void *)rnum);
    }

    int64 timeStart = clockTimer();
    while(atomicLoad(intptr, &accum2, Acquire) < NUM_CALL_TEST) {
        eventWaitTimeout(&notifyev, timeS(3));
        if(clockTimer() - timeStart > timeS(60)) {
            ret = 1;
            break;
        }
    }

    tqShutdown(q, timeS(60));

    if(atomicLoad(intptr, &accum1, Acquire) != total)
        ret = 1;

    tqRelease(&q);

    eventDestroy(&notifyev);

    return ret;
}

#define NUM_DEFER_STEPS 10

static bool is_monitor_test = false;
static LogCategory *moncat;
atomic(int32) tqtestd_order;

static bool defer_oncomplete(stvlist *cvars, stvlist *args)
{
    atomic(int32) *nsucceed;
    Task *task;

    nsucceed = stvlNextPtr(cvars);
    task = stvlNextObj(args, Task);
    if(!(nsucceed && task))
        return false;

    if(btaskState(task) == TASK_Succeeded) {
        atomicFetchAdd(int32, nsucceed, 1, AcqRel);
    }

    return true;
}

static int test_tqtest_defer(void)
{
    int ret = 0;
    sa_TQTestDefer dtasks;
    atomic(int32) nsucceed = atomicInit(0);

    TaskQueueConfig conf;
    tqPresetBalanced(&conf);
    if(is_monitor_test) {
        conf.mInterval = timeMS(5);
        conf.mTaskStalled = timeMS(110);
        conf.mTaskRunning = timeMS(20);
        conf.mTaskWaiting = timeMS(20);
        conf.mSuppress = timeMS(500);
        conf.mLogCat = moncat;
    }
    conf.wInitial = 4;
    conf.wIdle = 4;
    conf.wBusy = 4;
    conf.wMax = 4;
    TaskQueue *q = tqCreate(_S"Test", &conf);

    if(!q || !tqStart(q))
        return 1;

    saInit(&dtasks, object, NUM_DEFER_STEPS);

    atomicStore(int32, &tqtestd_order, 0, Relaxed);
    eventInit(&notifyev);

    // create tasks
    int64 dtime = timeMS(20);
    for(int i = 0; i < NUM_DEFER_STEPS; i++) {
        TQTestD1 *d1 = tqtestd1Create(i, dtime, &notifyev);
        TQTestD2 *d2 = tqtestd2Create(d1, &notifyev);

        // completion callbacks
        cchainAttach(&d1->oncomplete, defer_oncomplete, stvar(ptr, &nsucceed));
        cchainAttach(&d2->oncomplete, defer_oncomplete, stvar(ptr, &nsucceed));

        saPushC(&dtasks, object, &d2);           // run them in reverse order to test inversion
        saPushC(&dtasks, object, &d1);
        dtime += timeMS(20);
    }

    TQDelayTest *dlt = tqdelaytestCreate(timeMS(150));
    tqRun(q, &dlt);

    // run tasks
    int ntasks = saSize(dtasks);
    for(int i = 0; i < ntasks; i++) {
        tqAdd(q, dtasks.a[i]);
    }

    // wait for tasks
    int64 timeStart = clockTimer();
    for(;;) {
        bool done = true;
        for(int i = 0; i < ntasks; i++) {
            switch(btaskState(dtasks.a[i])) {
            case TASK_Succeeded:
                break;
            case TASK_Failed:
                ret = 1;
                break;
            default:
                done = false;
            }
        }

        if(done)
            break;

        eventWaitTimeout(&notifyev, timeS(3));
        if(clockTimer() - timeStart > timeS(60)) {
            ret = 1;
            break;
        }
    }

    eventDestroy(&notifyev);
    tqShutdown(q, timeS(60));
    tqRelease(&q);

    for(int i = 0; i < ntasks; i++) {
        if(btaskState(dtasks.a[i]) != TASK_Succeeded)
            ret = 1;
    }

    if(atomicLoad(int32, &nsucceed, Acquire) != NUM_DEFER_STEPS * 2)
        ret = 1;

    saDestroy(&dtasks);

    return ret;
}

static int test_tqtest_monitor(void)
{
    LogMembufData *mbuf = logmembufCreate(65536);
    moncat = logCreateCat(_S"MonitorTest", true);
    logRegisterDest(LOG_Diag, moncat, logmembufDest, mbuf);

    // reuse the defer test, but with the monitor enabled
    is_monitor_test = true;
    int ret = test_tqtest_defer();
    is_monitor_test = false;

    logFlush();

    if(mbuf->buf[0] == 0)
        ret = 1;

    logShutdown();

    return ret;
}

#define NUM_MTASK_TASKS 500

static int test_tqtest_mtask(void)
{
    Event notifyev;
    Event dummy;
    int ret = 0;

    atomicStore(int32, &tqtestd_order, 0, Relaxed);
    eventInit(&notifyev);
    eventInit(&dummy);

    TaskQueueConfig conf;
    tqPresetBalanced(&conf);
    conf.wInitial = 2;
    conf.wIdle = 2;
    conf.wBusy = 3;
    conf.wMax = 16;
    TaskQueue *q = tqCreate(_S"Test", &conf);
    if(!q || !tqStart(q))
        return 1;

    TQMTest *tqmtest = tqmtestCreate(&notifyev, q, 10);

    // create tasks
    int64 dtime = timeMS(20);
    for(int i = 0; i < NUM_DEFER_STEPS; i++) {
        TQTestD1 *d1 = tqtestd1Create(i, dtime, &dummy);
        TQTestD2 *d2 = tqtestd2Create(d1, &dummy);

        mtaskAdd(tqmtest, d2);
        mtaskAdd(tqmtest, d1);

        dtime += timeMS(20);
    }

    for(int i = 0; i < NUM_MTASK_TASKS; i++) {
        TQTest1 *t = tqtest1Create(1, 1, &dummy);
        mtaskAdd(tqmtest, t);
        objRelease(&t);
    }

    tqAdd(q, tqmtest);

    if(!eventWaitTimeout(&notifyev, timeS(60)))
        ret = 1;

    if(saSize(tqmtest->finished) != NUM_DEFER_STEPS * 2 + NUM_MTASK_TASKS)
        ret = 1;

    if(!taskSucceeded(tqmtest))
        ret = 1;

    objRelease(&tqmtest);

    eventDestroy(&dummy);
    eventDestroy(&notifyev);
    tqShutdown(q, timeS(60));
    tqRelease(&q);

    return ret;
}

testfunc tqtest_funcs[] = {
    { "task", test_tqtest_task },
    { "failure", test_tqtest_failure },
    { "concurrency", test_tqtest_concurrency },
    { "call", test_tqtest_call },
    { "defer", test_tqtest_defer },
    { "monitor", test_tqtest_monitor },
    { "mtask", test_tqtest_mtask },
    { 0, 0 }
};
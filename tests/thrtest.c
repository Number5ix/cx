#include <cx/thread.h>
#include <cx/string/strtest.h>
#include <cx/container.h>
#include <cx/thread/sema.h>
#include <cx/thread/event.h>
#include <cx/thread/mutex.h>
#include <cx/thread/rwlock.h>
#include <cx/platform/os.h>

#define TEST_FILE thrtest
#define TEST_FUNCS thrtest_funcs
#include "common.h"

int thrtest1[16] = {0};
intptr thrtest2[16] = {0};

static int thrproc1(Thread *self)
{
    int32 slot, count;

    if (!(stvlNext(&self->args, int32, &slot) &&
          stvlNext(&self->args, int32, &count)))
        return 0;

    thrtest2[slot] = thrCurrent();

    for (int i = 0; i < count; i++) {
        thrtest1[slot]++;
    }

    return 0;
}

#define BASIC_THREADS 16
static int test_basic()
{
    Thread *threads[BASIC_THREADS];
    int ret = 0;
    int i;

    for (i = 0; i < BASIC_THREADS; i++) {
        threads[i] = thrCreate(thrproc1, stvar(int32, i), stvar(int32, 1000000 + i*100000));
    }

    for (i = 0; i < BASIC_THREADS; i++) {
        thrWait(threads[i], timeForever);
        thrDestroy(&threads[i]);
        if (thrtest1[i] != 1000000 + i * 100000)
            ret = 1;
    }

    return ret;
}

static Semaphore testsem;

static int thrproc2(Thread *self)
{
    bool dec;
    int32 count;

    if (!(stvlNext(&self->args, uint8, &dec) &&
          stvlNext(&self->args, int32, &count)))
        return 0;

    for (int i = 0; i < count; i++) {
        if (dec)
            semaDec(&testsem);
        else
            semaInc(&testsem, 1);
    }

    return 0;
}

#define SEMA_PRODUCERS 4
#define SEMA_CONSUMERS 16
#define SEMA_COUNT 1048576
static int test_sema()
{
    int ret = 0;
    if (!semaInit(&testsem, 0))
        return 1;

    int i;
    Thread *producers[SEMA_PRODUCERS];
    Thread *consumers[SEMA_CONSUMERS];
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        consumers[i] = thrCreate(thrproc2, stvar(uint8, 1), stvar(int32, SEMA_COUNT / SEMA_CONSUMERS));
    }
    for (i = 0; i < SEMA_PRODUCERS; i++) {
        producers[i] = thrCreate(thrproc2, stvar(uint8, 0), stvar(int32, SEMA_COUNT / SEMA_PRODUCERS));
    }

    for (i = 0; i < SEMA_PRODUCERS; i++) {
        thrWait(producers[i], timeForever);
        thrDestroy(&producers[i]);
    }
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        thrWait(consumers[i], timeForever);
        thrDestroy(&consumers[i]);
    }

    if (atomicLoad(int32, &testsem.count, Acquire) != 0)
        ret = 1;

    if (!semaDestroy(&testsem))
        ret = 1;

    return ret;
}

static atomic(bool) fail;
static int64 testint1;
static int64 testint2;
static int64 testint3;
static Mutex testmtx;

static int thrproc3(Thread *self)
{
    int32 count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        mutexAcquire(&testmtx);
        testint1++;
        testint2++;
        testint3++;
        mutexRelease(&testmtx);

        mutexAcquire(&testmtx);
        if (testint1 != testint2 || testint2 != testint3)
            atomicStore(bool, &fail, true, Release);
        mutexRelease(&testmtx);
    }

    return 0;
}

#define MTX_THREADS 32
#define MTX_COUNT 1048576
static int test_mutex()
{
    atomicStore(bool, &fail, false, Release);
    testint1 = 0;
    testint2 = 0;
    testint3 = 0;
    mutexInit(&testmtx);

    int i;
    Thread *threads[MTX_THREADS];

    for (i = 0; i < MTX_THREADS; i++) {
        threads[i] = thrCreate(thrproc3, stvar(int32, MTX_COUNT / MTX_THREADS));
    }

    for (i = 0; i < MTX_THREADS; i++) {
        thrWait(threads[i], timeForever);
        thrDestroy(&threads[i]);
    }

    int ret = atomicLoad(bool, &fail, Acquire) ? 1 : 0;
    if (testint1 != MTX_COUNT || testint2 != MTX_COUNT || testint3 != MTX_COUNT)
        ret = 1;

    mutexDestroy(&testmtx);

    return ret;
}

static RWLock testrw;
static atomic(bool) rthread_exit = atomicInit(false);

static int thrproc4r(Thread *self)
{
    while (!atomicLoad(bool, &rthread_exit, Acquire)) {
        rwlockAcquireRead(&testrw);
        for (int i = 0; i < 16; i++) {
            if (testint1 != testint2 || testint2 != testint3)
                atomicStore(bool, &fail, true, Release);

        }
        rwlockReleaseRead(&testrw);
        osYield();
    }

    return 0;
}

static int thrproc4w(Thread *self)
{
    int32 count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        rwlockAcquireWrite(&testrw);
        testint1++;
        testint2++;
        testint3++;
        rwlockReleaseWrite(&testrw);
    }

    return 0;
}

#define RW_WTHREADS 4
#define RW_RTHREADS 16
#define RW_COUNT 262144
static int test_rwlock()
{
    atomicStore(bool, &fail, false, Release);
    testint1 = 0;
    testint2 = 0;
    testint3 = 0;
    rwlockInit(&testrw);

    int i;
    Thread *rthreads[RW_RTHREADS];
    Thread *wthreads[RW_WTHREADS];

    for (i = 0; i < RW_RTHREADS; i++) {
        rthreads[i] = thrCreate(thrproc4r, stvar(int32, 0));
    }
    for (i = 0; i < RW_WTHREADS; i++) {
        wthreads[i] = thrCreate(thrproc4w, stvar(int32, RW_COUNT / RW_WTHREADS));
    }

    for (i = 0; i < RW_WTHREADS; i++) {
        thrWait(wthreads[i], timeForever);
        thrDestroy(&wthreads[i]);
    }
    atomicStore(bool, &rthread_exit, true, Release);
    for (i = 0; i < RW_RTHREADS; i++) {
        thrWait(rthreads[i], timeForever);
        thrDestroy(&rthreads[i]);
    }

    rwlockDestroy(&testrw);

    int ret = atomicLoad(bool, &fail, Acquire) ? 1 : 0;
    if (testint1 != RW_COUNT || testint2 != RW_COUNT || testint3 != RW_COUNT)
        ret = 1;

    return ret;
}

static Event testev;

#define EVENT_CONSUMERS 32
#define EVENT_PRODUCERS 4
#define EVENT_COUNT 2097152

static int32 evthrcount[EVENT_CONSUMERS];
static atomic(int32) evwork;
static atomic(int32) evdone;

static int thrproc5c(Thread *self)
{
    int thrid;
    if (!stvlNext(&self->args, int32, &thrid))
        return 0;

    int32 work;
    for (;;) {
        eventWait(&testev);

        do {
            work = atomicLoad(int32, &evwork, Relaxed);
            if (work > 0 && atomicCompareExchange(int32, strong, &evwork, &work, work - 1, Acquire, Relaxed)) {
                evthrcount[thrid]++;
                atomicFetchAdd(int32, &evdone, 1, Release);
            }
        } while (work > 0);

        if (atomicLoad(bool, &rthread_exit, Acquire))
            break;
    }

    return 0;
}

static int thrproc5p(Thread *self)
{
    int count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        atomicFetchAdd(int32, &evwork, 1, Acquire);
        eventSignal(&testev);
    }

    return 0;
}

static int test_event_sub(bool spin)
{
    atomicStore(bool, &rthread_exit, false, Release);

    if (spin)
        eventInit(&testev, Spin);
    else
        eventInit(&testev);

    int i;
    Thread *cthreads[EVENT_CONSUMERS];
    Thread *pthreads[EVENT_PRODUCERS];

    for (i = 0; i < EVENT_CONSUMERS; i++) {
        cthreads[i] = thrCreate(thrproc5c, stvar(int32, i));
    }
    for (i = 0; i < EVENT_PRODUCERS; i++) {
        pthreads[i] = thrCreate(thrproc5p, stvar(int32, EVENT_COUNT / EVENT_PRODUCERS));
    }

    for (i = 0; i < EVENT_PRODUCERS; i++) {
        thrWait(pthreads[i], timeForever);
        thrDestroy(&pthreads[i]);
    }

    while (atomicLoad(int32, &evwork, Relaxed) > 0) {
        osYield();
    }

    atomicStore(bool, &rthread_exit, true, Release);
    eventSignalLock(&testev);
    for (i = 0; i < EVENT_CONSUMERS; i++) {
        thrWait(cthreads[i], timeForever);
        thrDestroy(&cthreads[i]);
    }

    eventDestroy(&testev);

    int tcount = 0;
    for (int i = 0; i < EVENT_CONSUMERS; i++) {
        tcount += evthrcount[i];
    }

    if (atomicLoad(int32, &evdone, Acquire) != EVENT_COUNT)
        return 1;

    if (tcount != EVENT_COUNT)
        return 1;

    return 0;
}

static int test_event()
{
    return test_event_sub(false);
}

static int test_event_s()
{
    return test_event_sub(true);
}

static int thrproc6(Thread *self)
{
    // first test should take less than half a second
    if (!semaTryDecTimeout(&testsem, timeFromMsec(500)))
        atomicStore(bool, &fail, true, Release);

    // second test should take more than half a second
    if (semaTryDecTimeout(&testsem, timeFromMsec(500)))
        atomicStore(bool, &fail, true, Release);

    return 0;
}

static int test_timeout()
{
    atomicStore(bool, &fail, false, Release);
    semaInit(&testsem, 0);

    Thread *testthr = thrCreate(thrproc6, stvNone);

    osSleep(timeFromMsec(250));
    semaInc(&testsem, 1);

    osSleep(timeFromMsec(750));
    semaInc(&testsem, 1);

    thrWait(testthr, timeForever);
    thrDestroy(&testthr);

    int ret = atomicLoad(bool, &fail, Acquire) ? 1 : 0;
    return ret;
}

testfunc thrtest_funcs[] = {
    { "basic", test_basic },
    { "sema", test_sema },
    { "mutex", test_mutex },
    { "rwlock", test_rwlock },
    { "event", test_event },
    { "event_s", test_event_s },
    { "timeout", test_timeout },
    { 0, 0 }
};

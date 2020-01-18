#include <cx/thread.h>
#include <cx/string/strtest.h>
#include <cx/container.h>
#include <cx/thread/sema.h>
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
    if (saSize(&self->args) != 2 || !stEq(self->args[0].type, stType(int32)) || !stEq(self->args[1].type, stType(int32)))
        return 0;

    int slot = stGenVal(int32, self->args[0].data);
    int count = stGenVal(int32, self->args[1].data);

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
    if (saSize(&self->args) != 2 || !stEq(self->args[0].type, stType(uint8)) || !stEq(self->args[1].type, stType(int32)))
        return 0;

    bool dec = stGenVal(uint8, self->args[0].data);
    int count = stGenVal(int32, self->args[1].data);

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
    if (saSize(&self->args) != 1 || !stEq(self->args[0].type, stType(int32)))
        return 0;

    int count = stGenVal(int32, self->args[0].data);

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

static int test_rwlock()
{
    return 0;
}

testfunc thrtest_funcs[] = {
    { "basic", test_basic },
    { "sema", test_sema },
    { "mutex", test_mutex },
    { "rwlock", test_rwlock },
    { 0, 0 }
};

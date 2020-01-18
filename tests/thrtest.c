#include <cx/thread.h>
#include <cx/string/strtest.h>
#include <cx/container.h>
#include <cx/thread/sema.h>

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

static int test_basic()
{
    Thread *threads[16];
    int ret = 0;
    int i;

    for (i = 0; i < 16; i++) {
        threads[i] = thrCreate(thrproc1, stvar(int32, i), stvar(int32, 1000000 + i*100000));
    }

    for (i = 0; i < 16; i++) {
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
static int test_sema()
{
    int ret = 0;
    if (!semaInit(&testsem, 0))
        return 1;

    int i;
    Thread *producers[SEMA_PRODUCERS];
    Thread *consumers[SEMA_CONSUMERS];
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        consumers[i] = thrCreate(thrproc2, stvar(uint8, 1), stvar(int32, 1048576 / SEMA_CONSUMERS));
    }
    for (i = 0; i < SEMA_PRODUCERS; i++) {
        producers[i] = thrCreate(thrproc2, stvar(uint8, 0), stvar(int32, 1048576 / SEMA_PRODUCERS));
    }

    for (i = 0; i < SEMA_PRODUCERS; i++) {
        thrWait(producers[i], timeForever);
        thrDestroy(&producers[i]);
    }
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        thrWait(consumers[i], timeForever);
        thrDestroy(&consumers[i]);
    }

    if (atomic_load_int32(&testsem.count, ATOMIC_ACQUIRE) != 0)
        ret = 1;

    if (!semaDestroy(&testsem))
        ret = 1;

    return ret;
}

testfunc thrtest_funcs[] = {
    { "basic", test_basic },
    { "sema", test_sema },
    { 0, 0 }
};

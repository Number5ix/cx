#include <cx/thread.h>
#include <cx/string/strtest.h>
#include <cx/container.h>

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

testfunc thrtest_funcs[] = {
    { "basic", test_basic },
    { 0, 0 }
};

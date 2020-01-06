#include <cx/container/sarray.h>
#include <cx/string.h>
#include <cx/string/strtest.h>

#include <cx/static_config.h>

#define TEST_FILE sarraytest
#define TEST_FUNCS sarraytest_funcs
#include "common.h"

static int test_int()
{
    int32_t *t1 = 0;
    int64_t *t2 = 0;
    int64_t i;

    t1 = saCreate(int32, 10, 0);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i, 0);
    }

    if (saSize(&t1) != 51)
        return 1;

    if (t1[0] != 500)
        return 1;
    if (t1[50] != 0)
        return 1;
    if (t1[40] != 100)
        return 1;

    t2 = saCreate(int64, 10, 0);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i, 0);
    }

    if (saSize(&t2) != 51)
        return 1;

    if (t2[0] != 500)
        return 1;
    if (t2[50] != 0)
        return 1;
    if (t2[40] != 100)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

static int test_sorted_int()
{
    int32_t *t1 = 0;
    int64_t *t2 = 0;
    int64_t i;

    t1 = saCreate(int32, 10, SA_Sorted);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i, 0);
    }

    if (saSize(&t1) != 51)
        return 1;

    if (t1[0] != 0)
        return 1;
    if (t1[50] != 500)
        return 1;
    if (t1[40] != 400)
        return 1;

    if (saFind(&t1, int32, 320, 0) != 32)
        return 1;

    t2 = saCreate(int64, 10, SA_Sorted);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i, 0);
    }

    if (saSize(&t2) != 51)
        return 1;

    if (t2[0] != 0)
        return 1;
    if (t2[50] != 500)
        return 1;
    if (t2[40] != 400)
        return 1;

    if (saFind(&t2, int64, 320, 0) != 32)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

static int test_string()
{
    string *t1 = 0;
    string st1 = 0;
    string st2 = 0;
    string st3 = 0;
    int i;

    t1 = saCreate(string, 10, 0);

    strCopy(&st1, "This is a test");
    strCopy(&st2, "This is also a test");
    strCopy(&st3, "Test Test Test");

    for (i = 0; i < 50; i++) {
        saPush(&t1, string, st1, 0);
        saPush(&t1, string, st2, 0);
        saPush(&t1, string, st3, 0);
    }

    if (saSize(&t1) != 150)
        return 1;

    if (strTestRefCount(st1) != 51)
        return 1;
    if (strTestRefCount(st2) != 51)
        return 1;
    if (strTestRefCount(st3) != 51)
        return 1;

    if (saFind(&t1, string, st1, 0) != 0)
        return 1;
    if (saFind(&t1, string, st2, 0) != 1)
        return 1;
    if (saFind(&t1, string, st3, 0) != 2)
        return 1;

    saClear(&t1);
    if (saSize(&t1) != 0)
        return 1;

    if (strTestRefCount(st1) != 1)
        return 1;
    if (strTestRefCount(st2) != 1)
        return 1;
    if (strTestRefCount(st3) != 1)
        return 1;

    saDestroy(&t1);
    t1 = saCreate(string, 10, SA_Sorted);

    saPush(&t1, string, st1, 0);
    saPush(&t1, string, st2, 0);
    saPush(&t1, string, st3, 0);

    if (saFind(&t1, string, st1, 0) != 1)
        return 1;
    if (saFind(&t1, string, st2, 0) != 2)
        return 1;
    if (saFind(&t1, string, st3, 0) != 0)
        return 1;

    saDestroy(&t1);

    if (strTestRefCount(st1) != 1)
        return 1;
    if (strTestRefCount(st2) != 1)
        return 1;
    if (strTestRefCount(st3) != 1)
        return 1;

    strDestroy(&st1);
    strDestroy(&st2);
    strDestroy(&st3);

    return 0;
}

static int test_sort()
{
    int32_t *t1 = 0;
    int64_t *t2 = 0;
    int64_t i;

    t1 = saCreate(int32, 10, 0);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i, 0);
    }

    if (saSize(&t1) != 51)
        return 1;

    if (t1[0] != 500)
        return 1;
    if (t1[50] != 0)
        return 1;
    if (t1[40] != 100)
        return 1;

    saSort(&t1, true);

    if (t1[0] != 0)
        return 1;
    if (t1[50] != 500)
        return 1;
    if (t1[40] != 400)
        return 1;

    if (saFind(&t1, int32, 320, 0) != 32)
        return 1;

    t2 = saCreate(int64, 10, 0);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i, 0);
    }

    if (saSize(&t2) != 51)
        return 1;

    if (t2[0] != 500)
        return 1;
    if (t2[50] != 0)
        return 1;
    if (t2[40] != 100)
        return 1;

    saSort(&t2, true);

    if (t2[0] != 0)
        return 1;
    if (t2[50] != 500)
        return 1;
    if (t2[40] != 400)
        return 1;

    if (saFind(&t2, int64, 320, 0) != 32)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

static int test_string_sort()
{
    string *t1 = 0;
    string st1 = 0;
    string st2 = 0;
    string st3 = 0;

    t1 = saCreate(string, 10, 0);

    strCopy(&st1, "This is a test");
    strCopy(&st2, "This is also a test");
    strCopy(&st3, "Test Test Test");

    saPush(&t1, string, st1, 0);
    saPush(&t1, string, st2, 0);
    saPush(&t1, string, st3, 0);

    if (saFind(&t1, string, st1, 0) != 0)
        return 1;
    if (saFind(&t1, string, st2, 0) != 1)
        return 1;
    if (saFind(&t1, string, st3, 0) != 2)
        return 1;

    saSort(&t1, true);

    if (saFind(&t1, string, st1, 0) != 1)
        return 1;
    if (saFind(&t1, string, st2, 0) != 2)
        return 1;
    if (saFind(&t1, string, st3, 0) != 0)
        return 1;

    saDestroy(&t1);

    strDestroy(&st1);
    strDestroy(&st2);
    strDestroy(&st3);

    return 0;
}

testfunc sarraytest_funcs[] = {
    { "int", test_int },
    { "sorted_int", test_sorted_int },
    { "string", test_string },
    { "sort", test_sort },
    { "string_sort", test_string_sort },
    { 0, 0 }
};

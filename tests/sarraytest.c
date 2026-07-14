#include <cx/container/sarray.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/stype/stvar.h>

#define TEST_FILE sarraytest
#define TEST_FUNCS sarraytest_funcs
#include "common.h"

static int test_int()
{
    sa_int32 t1;
    sa_int64 t2;
    int64 i;

    saInit(&t1, int32, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i);
    }

    if (saSize(t1) != 51)
        return 1;

    if (t1.a[0] != 500)
        return 1;
    if (t1.a[50] != 0)
        return 1;
    if (t1.a[40] != 100)
        return 1;

    saInit(&t2, int64, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        return 1;

    if (t2.a[0] != 500)
        return 1;
    if (t2.a[50] != 0)
        return 1;
    if (t2.a[40] != 100)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

static int test_sorted_int()
{
    sa_int32 t1;
    sa_int64 t2;
    int64 i;

    saInit(&t1, int32, 10, SA_Sorted);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i);
    }

    if (saSize(t1) != 51)
        return 1;

    if (t1.a[0] != 0)
        return 1;
    if (t1.a[50] != 500)
        return 1;
    if (t1.a[40] != 400)
        return 1;

    if (saFind(t1, int32, 320) != 32)
        return 1;

    saInit(&t2, int64, 10, SA_Sorted);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        return 1;

    if (t2.a[0] != 0)
        return 1;
    if (t2.a[50] != 500)
        return 1;
    if (t2.a[40] != 400)
        return 1;

    if (saFind(t2, int64, 320) != 32)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

typedef struct OpaqueStruct {
    int32 a;
    int64 b;
    int32 c;
} OpaqueStruct;
saDeclare(OpaqueStruct);

static int test_opaque()
{
    sa_OpaqueStruct t1;
    int32 i;

    saInit(&t1, opaque(OpaqueStruct), 10);
    for (i = 500; i >= 0; i -= 10) {
        OpaqueStruct tmp;
        tmp.a = (int32)i;
        tmp.b = (int64)i * 1000;
        tmp.c = i - 5;
        saPush(&t1, opaque, tmp);
    }

    if (saSize(t1) != 51)
        return 1;

    if (t1.a[0].a != 500)
        return 1;
    if (t1.a[0].b != 500000)
        return 1;
    if (t1.a[0].c != 495)
        return 1;

    if (t1.a[50].a != 0)
        return 1;
    if (t1.a[50].b != 0)
        return 1;
    if (t1.a[50].c != -5)
        return 1;

    if (t1.a[40].a != 100)
        return 1;
    if (t1.a[40].b != 100000)
        return 1;
    if (t1.a[40].c != 95)
        return 1;

    saDestroy(&t1);

    return 0;
}

static int test_string()
{
    sa_string t1;
    string st1 = 0;
    string st2 = 0;
    string st3 = 0;
    int i;

    saInit(&t1, string, 10);

    strCopy(&st1, (string)"This is a test");
    strCopy(&st2, (string)"This is also a test");
    strCopy(&st3, (string)"Test Test Test");

    for (i = 0; i < 50; i++) {
        saPush(&t1, string, st1);
        saPush(&t1, string, st2);
        saPush(&t1, string, st3);
    }

    if (saSize(t1) != 150)
        return 1;

    if (strTestRefCount(st1) != 51)
        return 1;
    if (strTestRefCount(st2) != 51)
        return 1;
    if (strTestRefCount(st3) != 51)
        return 1;

    if (saFind(t1, string, st1) != 0)
        return 1;
    if (saFind(t1, string, st2) != 1)
        return 1;
    if (saFind(t1, string, st3) != 2)
        return 1;

    saClear(&t1);
    if (saSize(t1) != 0)
        return 1;

    if (strTestRefCount(st1) != 1)
        return 1;
    if (strTestRefCount(st2) != 1)
        return 1;
    if (strTestRefCount(st3) != 1)
        return 1;

    saDestroy(&t1);
    saInit(&t1, string, 10, SA_Sorted);

    saPush(&t1, string, st1);
    saPush(&t1, string, st2);
    saPush(&t1, string, st3);

    if (saFind(t1, string, st1) != 1)
        return 1;
    if (saFind(t1, string, st2) != 2)
        return 1;
    if (saFind(t1, string, st3) != 0)
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
    sa_int32 t1;
    sa_int64 t2;
    int64 i;

    saInit(&t1, int32, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t1, int32, (int32)i);
    }

    if (saSize(t1) != 51)
        return 1;

    if (t1.a[0] != 500)
        return 1;
    if (t1.a[50] != 0)
        return 1;
    if (t1.a[40] != 100)
        return 1;

    saSort(&t1, true);

    if (t1.a[0] != 0)
        return 1;
    if (t1.a[50] != 500)
        return 1;
    if (t1.a[40] != 400)
        return 1;

    if (saFind(t1, int32, 320) != 32)
        return 1;

    saInit(&t2, int64, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        return 1;

    if (t2.a[0] != 500)
        return 1;
    if (t2.a[50] != 0)
        return 1;
    if (t2.a[40] != 100)
        return 1;

    saSort(&t2, true);

    if (t2.a[0] != 0)
        return 1;
    if (t2.a[50] != 500)
        return 1;
    if (t2.a[40] != 400)
        return 1;

    if (saFind(t2, int64, 320) != 32)
        return 1;

    saDestroy(&t1);
    saDestroy(&t2);

    return 0;
}

static int test_string_sort()
{
    sa_string t1;
    string st1 = 0;
    string st2 = 0;
    string st3 = 0;

    saInit(&t1, string, 10);

    strCopy(&st1, _S"This is a test");
    strCopy(&st2, _S"This is also a test");
    strCopy(&st3, _S"Test Test Test");

    saPush(&t1, string, st1);
    saPush(&t1, string, st2);
    saPush(&t1, string, st3);

    if (saFind(t1, string, st1) != 0)
        return 1;
    if (saFind(t1, string, st2) != 1)
        return 1;
    if (saFind(t1, string, st3) != 2)
        return 1;

    saSort(&t1, true);

    if (saFind(t1, string, st1) != 1)
        return 1;
    if (saFind(t1, string, st2) != 2)
        return 1;
    if (saFind(t1, string, st3) != 0)
        return 1;

    saDestroy(&t1);

    strDestroy(&st1);
    strDestroy(&st2);
    strDestroy(&st3);

    return 0;
}

static int test_stvar_consume()
{
    sa_stvar arr = saInitNone;
    string s     = 0;
    stvar v;

    // Create a heap-allocated string so we can verify ownership via refcount.
    // strCopy forces a copy
    strCopy(&s, _SL("hello stvar consume test"));
    if (strTestRefCount(s) != 1)
        return 1;

    // Copy into an owning stvar — stCopy_string increments the refcount
    stvarCopy(&v, stvar(string, s));
    if (strTestRefCount(s) != 2)
        return 2;

    // Consume v into the array; the source stvar must be zeroed afterward
    saPushC(&arr, stvar, &v);

    // Source stvar must have been zeroed (PassPtr memset clears all fields)
    if (v.type != NULL || v.data.st_string != NULL)
        return 3;

    // Array must have one element
    if (saSize(arr) != 1)
        return 4;

    // Array element must be a string stvar pointing to the same string
    if (!stvarIs(&arr.a[0], string))
        return 5;
    if (arr.a[0].data.st_string != s)
        return 6;

    // Refcount: s owns one reference, the stvar in the array owns one
    if (strTestRefCount(s) != 2)
        return 7;

    // Destroying the array must release the stvar's string reference
    saDestroy(&arr);
    if (strTestRefCount(s) != 1)
        return 8;

    strDestroy(&s);
    return 0;
}

testfunc sarraytest_funcs[] = {
    { "int",           test_int           },
    { "sorted_int",    test_sorted_int    },
    { "opaque",        test_opaque        },
    { "string",        test_string        },
    { "sort",          test_sort          },
    { "string_sort",   test_string_sort   },
    { "stvar_consume", test_stvar_consume },
    { 0,               0                  }
};

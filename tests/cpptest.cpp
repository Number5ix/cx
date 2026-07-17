#include <cx/cx.h>
#include <cx/string.h>
#include <cx/container.h>
#include <cx/stype/stvar.h>
#include <cx/obj.h>
#include "objtestobj.h"
#include <cx/string/strtest.h>

#define TEST_FILE cpptest
#define TEST_FUNCS cpptest_funcs
#include "common.h"

// ---------------------------------------------------------------------------
// sarray
// ---------------------------------------------------------------------------

typedef struct CppPod {
    int a;
    int b;
} CppPod;
saDeclare(CppPod);

static int test_sarray()
{
    sa_int32 t1;
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

    if (saFind(t1, int32, 320) != 32)
        return 1;

    // foreach iteration - sum check
    int32 sum = 0;
    foreach(sarray, idx, int32, elem, t1) {
        sum += elem;
    }
    int32 expect = 0;
    for (i = 0; i <= 500; i += 10)
        expect += (int32)i;
    if (sum != expect)
        return 1;

    saDestroy(&t1);

    // saInitNone
    sa_int32 z = saInitNone;
    if (saSize(z) != 0)
        return 1;
    if (z.a != NULL)
        return 1;

    // sa_string with _SL literals
    sa_string ts;
    string s1 = 0;
    string s2 = 0;
    saInit(&ts, string, 4);

    strCopy(&s1, _SL("hello"));
    strCopy(&s2, _SL("world"));

    saPush(&ts, string, s1);
    saPush(&ts, string, s2);

    if (saSize(ts) != 2)
        return 1;
    if (!strEq(ts.a[0], _SL("hello")))
        return 1;
    if (strCmp(ts.a[1], _SL("world")) != 0)
        return 1;

    saDestroy(&ts);
    strDestroy(&s1);
    strDestroy(&s2);

    // saMerge of two int arrays
    sa_int32 arr1;
    sa_int32 arr2;
    sa_int32 merged;

    saInit(&arr1, int32, 4);
    saInit(&arr2, int32, 4);
    saPush(&arr1, int32, 1);
    saPush(&arr1, int32, 2);
    saPush(&arr2, int32, 3);
    saPush(&arr2, int32, 4);

    saMerge(&merged, arr1, arr2);

    if (saSize(merged) != 4)
        return 1;
    if (merged.a[0] != 1 || merged.a[1] != 2 || merged.a[2] != 3 || merged.a[3] != 4)
        return 1;

    saDestroy(&arr1);
    saDestroy(&arr2);
    saDestroy(&merged);

    return 0;
}

// ---------------------------------------------------------------------------
// hashtable
// ---------------------------------------------------------------------------

static int test_hashtable()
{
    hashtable ht = 0;
    htInit(&ht, string, int32, 8);

    htInsert(&ht, string, _S"one", int32, 1);
    htInsert(&ht, string, _S"two", int32, 2);
    htInsert(&ht, string, _S"three", int32, 3);

    if (htSize(ht) != 3)
        return 1;

    int32 val = 0;
    if (!htFind(ht, string, _S"two", int32, &val))
        return 1;
    if (val != 2)
        return 1;

    if (!htHasKey(ht, string, _S"three"))
        return 1;
    if (htHasKey(ht, string, _S"four"))
        return 1;

    if (!htRemove(&ht, string, _S"one"))
        return 1;
    if (htSize(ht) != 2)
        return 1;
    if (htHasKey(ht, string, _S"one"))
        return 1;

    // foreach iteration
    htInsert(&ht, string, _S"four", int32, 4);

    int count = 0;
    int32 sum = 0;
    foreach(hashtable, it, ht) {
        string key = htiKey(string, it);
        int32 v    = htiVal(int32, it);
        if (strEmpty(key))
            return 1;
        sum += v;
        count++;
    }

    if (count != 3)
        return 1;
    if (sum != 2 + 3 + 4)
        return 1;

    htDestroy(&ht);

    return 0;
}

// ---------------------------------------------------------------------------
// stype
// ---------------------------------------------------------------------------

static int test_stype()
{
    // stCmp/stHash on int32
    if (stCmp(int32, 5, 10) >= 0)
        return 1;
    if (stCmp(int32, 10, 10) != 0)
        return 1;
    if (stHash(int32, 42) != stHash(int32, 42))
        return 1;

    // stCmp/stHash on string
    string a = 0;
    string b = 0;
    strCopy(&a, _SL("alpha"));
    strCopy(&b, _SL("beta"));

    if (stCmp(string, a, b) >= 0)
        return 1;
    if (stHash(string, a) != stHash(string, a))
        return 1;

    // stCopy + stDestroy on string, mirroring COW semantics
    string dest = 0;
    stCopy(string, &dest, a);
    if (!strEq(dest, _SL("alpha")))
        return 1;
    if (strTestRefCount(a) != 2)
        return 1;

    stDestroy(string, &dest);
    if (strTestRefCount(a) != 1)
        return 1;

    strDestroy(&a);
    strDestroy(&b);

    // opaque struct sarray
    sa_CppPod arr;
    saInit(&arr, opaque(CppPod), 8);

    for (int i = 0; i < 5; i++) {
        CppPod p;
        p.a = i;
        p.b = i * 10;
        saPush(&arr, opaque, p);
    }

    if (saSize(arr) != 5)
        return 1;
    if (arr.a[3].a != 3 || arr.a[3].b != 30)
        return 1;

    if (!stEq(stType(opaque(CppPod)), saElemType(arr)))
        return 1;

    saDestroy(&arr);

    return 0;
}

// ---------------------------------------------------------------------------
// stvar
// ---------------------------------------------------------------------------

static int test_stvar()
{
    stvar v1 = stvar(int32, 42);
    if (!stvarIs(&v1, int32))
        return 1;
    if (v1.data.st_int32 != 42)
        return 1;

    string s = 0;
    strCopy(&s, _SL("stvar string"));

    stvar v2 = stvNone;
    stvarCopy(&v2, stvar(string, s));
    if (!stvarIs(&v2, string))
        return 1;
    if (strTestRefCount(s) != 2)
        return 1;

    stvar v3 = stvNone;
    stvarSet(&v3, int32, 7);
    if (!stvarIs(&v3, int32) || v3.data.st_int32 != 7)
        return 1;

    // push stvars into sa_stvar
    sa_stvar arr = saInitNone;
    stvar v4 = stvNone;
    stvarCopy(&v4, stvar(int32, 99));
    saPushC(&arr, stvar, &v4);

    if (saSize(arr) != 1)
        return 1;
    if (!stvarIs(&arr.a[0], int32) || arr.a[0].data.st_int32 != 99)
        return 1;

    saDestroy(&arr);

    stvarDestroy(&v2);
    if (strTestRefCount(s) != 1)
        return 1;

    stvarDestroy(&v3);
    strDestroy(&s);

    return 0;
}

// ---------------------------------------------------------------------------
// object
// ---------------------------------------------------------------------------

static int test_object()
{
    TestCls1 *cls1 = TestCls1_create();
    if (!cls1)
        return 1;
    cls1->data = 42;

    TestIf1 *ifptr = objInstIf(cls1, TestIf1);
    if (!ifptr)
        return 1;
    if (ifptr->testfunc(cls1) != 42)
        return 1;

    TestCls1 *cls1ref = objAcquire(cls1);
    if (atomicLoad(uintptr, &cls1->_ref, Acquire) != 2)
        return 1;
    objRelease(&cls1ref);
    if (atomicLoad(uintptr, &cls1->_ref, Acquire) != 1)
        return 1;

    objRelease(&cls1);

    // dynamic cast + weak references, mirroring objtest.c
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        return 1;

    cls4->data  = 12;
    cls4->data2 = 99;
    cls4->data3 = 15;
    cls4->data4 = 33;
    cls4->data5 = 73;

    TestCls3 *cls3 = TestCls3(cls4);
    TestCls1 *cls1b = objDynCast(TestCls1, cls3);
    if (!cls1b)
        return 1;
    if (cls1b->data != 12)
        return 1;

    Weak(TestCls3) *cls3w = objGetWeak(TestCls3, cls3);
    if (atomicLoad(uintptr, &cls4->_ref, Acquire) != 1)
        return 1;

    TestCls3 *cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if (!cls3a)
        return 1;
    if (atomicLoad(uintptr, &cls3a->_ref, Acquire) != 2)
        return 1;
    if (testcls3Testfunc2(cls3a) != 99)
        return 1;

    objRelease(&cls3a);

    objRelease(&cls4);

    cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if (cls3a)
        return 1;

    objDestroyWeak(&cls3w);

    return 0;
}

testfunc cpptest_funcs[] = {
    { "sarray",    test_sarray    },
    { "hashtable", test_hashtable },
    { "stype",     test_stype     },
    { "stvar",     test_stvar     },
    { "object",    test_object    },
    { 0,           0              }
};

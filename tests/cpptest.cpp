#include <cx/cx.h>
#include <cx/string.h>
#include <cx/container.h>
#include <cx/parse.h>
#include <cx/stype/stvar.h>
#include <cx/obj.h>
#include "objtestobj.h"
#include <cx/string/strtest.h>

#define TEST_FILE cpptest
#define TEST_FUNCS cpptest_funcs
#include "common.h"

// common.h binds LOG_CHANNEL to the raw cxTestLogChan global, which isn't visible to C++ builds
// (see testharness.h) since LogChannel's atomic(...) fields aren't the same type under C++'s
// std::atomic as under C's _Atomic -- rebind to the accessor function instead.
#undef LOG_CHANNEL
#define LOG_CHANNEL cxTestLogChanGet()

// TEST_FAIL (common.h) expands to logFmt(), whose variadic macro takes the address of an
// anonymous stvar compound literal -- valid C, not valid C++ (same restriction test_stvar()
// documents below for _strParse's dests[] array). Build the array as a named local first, then
// call the same non-variadic logger TEST_FAIL uses underneath.
#define TEST_FAIL_CPP(code, fmt, ...)                                     \
    do {                                                                  \
        stvar _cppArgs[] = { __VA_ARGS__ };                               \
        _logFmt(LOG_Error, -1, LOG_CHANNEL, NULL, fmt,                    \
                (int)(sizeof(_cppArgs) / sizeof(_cppArgs[0])), _cppArgs); \
        return (code);                                                   \
    } while (0)

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
        TEST_FAIL_CPP(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint32, saSize(t1)));
    if (t1.a[0] != 500)
        TEST_FAIL_CPP(1, _SL("t1.a[0]=${int} != 500"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 0)
        TEST_FAIL_CPP(1, _SL("t1.a[50]=${int} != 0"), stvar(int32, t1.a[50]));
    if (t1.a[40] != 100)
        TEST_FAIL_CPP(1, _SL("t1.a[40]=${int} != 100"), stvar(int32, t1.a[40]));

    saSort(&t1, true);
    if (t1.a[0] != 0)
        TEST_FAIL_CPP(1, _SL("after sort: t1.a[0]=${int} != 0"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 500)
        TEST_FAIL_CPP(1, _SL("after sort: t1.a[50]=${int} != 500"), stvar(int32, t1.a[50]));

    int32 foundIdx = saFind(t1, int32, 320);
    if (foundIdx != 32)
        TEST_FAIL_CPP(1, _SL("saFind(t1, int32, 320)=${int} != 32"), stvar(int32, foundIdx));

    // foreach iteration - sum check
    int32 sum = 0;
    foreach(sarray, idx, int32, elem, t1) {
        sum += elem;
    }
    int32 expect = 0;
    for (i = 0; i <= 500; i += 10)
        expect += (int32)i;
    if (sum != expect)
        TEST_FAIL_CPP(1, _SL("sum=${int} != expect=${int}"), stvar(int32, sum), stvar(int32, expect));

    saDestroy(&t1);

    // saInitNone
    sa_int32 z = saInitNone;
    if (saSize(z) != 0)
        TEST_FAIL_CPP(1, _SL("saSize(z)=${uint} != 0"), stvar(uint32, saSize(z)));
    if (z.a != NULL)
        TEST_FAIL_CPP(1, _SL("z.a != NULL"), stvNone);

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
        TEST_FAIL_CPP(1, _SL("saSize(ts)=${uint} != 2"), stvar(uint32, saSize(ts)));
    if (!strEq(ts.a[0], _SL("hello")))
        TEST_FAIL_CPP(1, _SL("ts.a[0]='${string}' != 'hello'"), stvar(strref, ts.a[0]));
    if (strCmp(ts.a[1], _SL("world")) != 0)
        TEST_FAIL_CPP(1, _SL("ts.a[1]='${string}' != 'world'"), stvar(strref, ts.a[1]));

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
        TEST_FAIL_CPP(1, _SL("saSize(merged)=${uint} != 4"), stvar(uint32, saSize(merged)));
    if (merged.a[0] != 1 || merged.a[1] != 2 || merged.a[2] != 3 || merged.a[3] != 4)
        TEST_FAIL_CPP(1, _SL("merged=[${int},${int},${int},${int}] != [1,2,3,4]"),
                      stvar(int32, merged.a[0]), stvar(int32, merged.a[1]),
                      stvar(int32, merged.a[2]), stvar(int32, merged.a[3]));

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
        TEST_FAIL_CPP(1, _SL("htSize(ht)=${uint} != 3"), stvar(uint32, htSize(ht)));

    int32 val = 0;
    if (!htFind(ht, string, _S"two", int32, &val))
        TEST_FAIL_CPP(1, _SL("htFind(ht, \"two\") failed"), stvNone);
    if (val != 2)
        TEST_FAIL_CPP(1, _SL("val=${int} != 2"), stvar(int32, val));

    if (!htHasKey(ht, string, _S"three"))
        TEST_FAIL_CPP(1, _SL("htHasKey(ht, \"three\") returned false"), stvNone);
    if (htHasKey(ht, string, _S"four"))
        TEST_FAIL_CPP(1, _SL("htHasKey(ht, \"four\") returned true, expected false"), stvNone);

    if (!htRemove(&ht, string, _S"one"))
        TEST_FAIL_CPP(1, _SL("htRemove(ht, \"one\") failed"), stvNone);
    if (htSize(ht) != 2)
        TEST_FAIL_CPP(1, _SL("htSize(ht) after remove=${uint} != 2"), stvar(uint32, htSize(ht)));
    if (htHasKey(ht, string, _S"one"))
        TEST_FAIL_CPP(1, _SL("htHasKey(ht, \"one\") returned true after remove"), stvNone);

    // foreach iteration
    htInsert(&ht, string, _S"four", int32, 4);

    int count = 0;
    int32 sum = 0;
    foreach(hashtable, it, ht) {
        string key = htiKey(string, it);
        int32 v    = htiVal(int32, it);
        if (strEmpty(key))
            TEST_FAIL_CPP(1, _SL("empty key encountered during iteration"), stvNone);
        sum += v;
        count++;
    }

    if (count != 3)
        TEST_FAIL_CPP(1, _SL("count=${int} != 3"), stvar(int32, count));
    if (sum != 2 + 3 + 4)
        TEST_FAIL_CPP(1, _SL("sum=${int} != 9"), stvar(int32, sum));

    htDestroy(&ht);

    return 0;
}

// ---------------------------------------------------------------------------
// stype
// ---------------------------------------------------------------------------

static int test_stype()
{
    // stCmp/stHash on int32
    intptr cmp1 = stCmp(int32, 5, 10);
    if (cmp1 >= 0)
        TEST_FAIL_CPP(1, _SL("stCmp(int32, 5, 10)=${int} >= 0"), stvar(int64, (int64)cmp1));
    intptr cmp2 = stCmp(int32, 10, 10);
    if (cmp2 != 0)
        TEST_FAIL_CPP(1, _SL("stCmp(int32, 10, 10)=${int} != 0"), stvar(int64, (int64)cmp2));
    uint32 h1a = stHash(int32, 42), h1b = stHash(int32, 42);
    if (h1a != h1b)
        TEST_FAIL_CPP(1, _SL("stHash(int32,42) not stable: ${uint} != ${uint}"),
                      stvar(uint32, h1a), stvar(uint32, h1b));

    // stCmp/stHash on string
    string a = 0;
    string b = 0;
    strCopy(&a, _SL("alpha"));
    strCopy(&b, _SL("beta"));

    intptr cmp3 = stCmp(string, a, b);
    if (cmp3 >= 0)
        TEST_FAIL_CPP(1, _SL("stCmp(string, '${string}', '${string}')=${int} >= 0"),
                      stvar(strref, a), stvar(strref, b), stvar(int64, (int64)cmp3));
    uint32 h2a = stHash(string, a), h2b = stHash(string, a);
    if (h2a != h2b)
        TEST_FAIL_CPP(1, _SL("stHash(string, '${string}') not stable: ${uint} != ${uint}"),
                      stvar(strref, a), stvar(uint32, h2a), stvar(uint32, h2b));

    // stCopy + stDestroy on string, mirroring COW semantics
    string dest = 0;
    stCopy(string, &dest, a);
    if (!strEq(dest, _SL("alpha")))
        TEST_FAIL_CPP(1, _SL("dest='${string}' != 'alpha'"), stvar(strref, dest));
    uint32 rc1 = strTestRefCount(a);
    if (rc1 != 2)
        TEST_FAIL_CPP(1, _SL("strTestRefCount(a)=${uint} != 2"), stvar(uint32, rc1));

    stDestroy(string, &dest);
    uint32 rc2 = strTestRefCount(a);
    if (rc2 != 1)
        TEST_FAIL_CPP(1, _SL("strTestRefCount(a) after destroy=${uint} != 1"), stvar(uint32, rc2));

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
        TEST_FAIL_CPP(1, _SL("saSize(arr)=${uint} != 5"), stvar(uint32, saSize(arr)));
    if (arr.a[3].a != 3 || arr.a[3].b != 30)
        TEST_FAIL_CPP(1, _SL("arr.a[3]={a=${int},b=${int}} != {3,30}"),
                      stvar(int32, arr.a[3].a), stvar(int32, arr.a[3].b));

    if (!stEq(stType(opaque(CppPod)), saElemType(arr)))
        TEST_FAIL_CPP(1, _SL("saElemType(arr) != stType(opaque(CppPod))"), stvNone);

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
        TEST_FAIL_CPP(1, _SL("stvarIs(&v1, int32) is false"), stvNone);
    if (v1.data.st_int32 != 42)
        TEST_FAIL_CPP(1, _SL("v1.data.st_int32=${int} != 42"), stvar(int32, v1.data.st_int32));

    string s = 0;
    strCopy(&s, _SL("stvar string"));

    stvar v2 = stvNone;
    stvarCopy(&v2, stvar(string, s));
    if (!stvarIs(&v2, string))
        TEST_FAIL_CPP(1, _SL("stvarIs(&v2, string) is false"), stvNone);
    uint32 rc1 = strTestRefCount(s);
    if (rc1 != 2)
        TEST_FAIL_CPP(1, _SL("strTestRefCount(s)=${uint} != 2"), stvar(uint32, rc1));

    stvar v3 = stvNone;
    stvarSet(&v3, int32, 7);
    if (!stvarIs(&v3, int32) || v3.data.st_int32 != 7)
        TEST_FAIL_CPP(1, _SL("v3 type/value check failed: st_int32=${int}"), stvar(int32, v3.data.st_int32));

    // push stvars into sa_stvar
    sa_stvar arr = saInitNone;
    stvar v4 = stvNone;
    stvarCopy(&v4, stvar(int32, 99));
    saPushC(&arr, stvar, &v4);

    if (saSize(arr) != 1)
        TEST_FAIL_CPP(1, _SL("saSize(arr)=${uint} != 1"), stvar(uint32, saSize(arr)));
    if (!stvarIs(&arr.a[0], int32) || arr.a[0].data.st_int32 != 99)
        TEST_FAIL_CPP(1, _SL("arr.a[0] type/value check failed: st_int32=${int}"),
                      stvar(int32, arr.a[0].data.st_int32));

    saDestroy(&arr);

    stvarDestroy(&v2);
    uint32 rc2 = strTestRefCount(s);
    if (rc2 != 1)
        TEST_FAIL_CPP(1, _SL("strTestRefCount(s) after destroy=${uint} != 1"), stvar(uint32, rc2));

    stvarDestroy(&v3);
    strDestroy(&s);

    // destination bindings, which take the C++ arm of the same macros
    int32 dest = 0;
    stvp d1    = stvp(int32, &dest);
    stvp d2    = stvpk(port, int32, &dest);
    stvp d3    = stvpNone;

    if (!stEq(d1.type, stType(int32)) || d1.ptr != (stgeneric*)&dest || d1.key != NULL)
        TEST_FAIL_CPP(1, _SL("d1 type/ptr/key check failed"), stvNone);
    if (!d2.key || strcmp(d2.key, "port") != 0)
        TEST_FAIL_CPP(1, _SL("d2.key != \"port\""), stvNone);
    if (d3.type != NULL || d3.ptr != NULL)
        TEST_FAIL_CPP(1, _SL("d3 (stvpNone) is not fully NULL"), stvNone);

    // The variadic call macros themselves stay C-only: a C++ compiler will not let the
    // address of a compound-literal array escape the full expression, which is exactly what
    // strFormat and strPatternMatch do. Building the array by hand is the C++ way in.
    uint32 x = 0, y = 0;
    stvp dests[2] = { stvpk(x, uint32, &x), stvpk(y, uint32, &y) };
    if (!_strParse(_SL("4-5"), _SL("${uint:x}-${uint:y}"), 2, dests) || x != 4 || y != 5)
        TEST_FAIL_CPP(1, _SL("_strParse result: x=${uint}, y=${uint}"), stvar(uint32, x), stvar(uint32, y));

    return 0;
}

// ---------------------------------------------------------------------------
// object
// ---------------------------------------------------------------------------

static int test_object()
{
    TestCls1 *cls1 = TestCls1_create();
    if (!cls1)
        TEST_FAIL_CPP(1, _SL("TestCls1_create() returned NULL"), stvNone);
    cls1->data = 42;

    TestIf1 *ifptr = objInstIf(cls1, TestIf1);
    if (!ifptr)
        TEST_FAIL_CPP(1, _SL("objInstIf(cls1, TestIf1) returned NULL"), stvNone);
    int32 tf = ifptr->testfunc(cls1);
    if (tf != 42)
        TEST_FAIL_CPP(1, _SL("ifptr->testfunc(cls1)=${int} != 42"), stvar(int32, tf));

    TestCls1 *cls1ref = objAcquire(cls1);
    uint64 ref1 = (uint64)atomicLoad(uintptr, &cls1->_ref, Acquire);
    if (ref1 != 2)
        TEST_FAIL_CPP(1, _SL("cls1->_ref after acquire=${uint} != 2"), stvar(uint64, ref1));
    objRelease(&cls1ref);
    uint64 ref2 = (uint64)atomicLoad(uintptr, &cls1->_ref, Acquire);
    if (ref2 != 1)
        TEST_FAIL_CPP(1, _SL("cls1->_ref after release=${uint} != 1"), stvar(uint64, ref2));

    objRelease(&cls1);

    // dynamic cast + weak references, mirroring objtest.c
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        TEST_FAIL_CPP(1, _SL("TestCls4b_create() returned NULL"), stvNone);

    cls4->data  = 12;
    cls4->data2 = 99;
    cls4->data3 = 15;
    cls4->data4 = 33;
    cls4->data5 = 73;

    TestCls3 *cls3 = TestCls3(cls4);
    TestCls1 *cls1b = objDynCast(TestCls1, cls3);
    if (!cls1b)
        TEST_FAIL_CPP(1, _SL("objDynCast(TestCls1, cls3) returned NULL"), stvNone);
    if (cls1b->data != 12)
        TEST_FAIL_CPP(1, _SL("cls1b->data=${int} != 12"), stvar(int32, cls1b->data));

    Weak(TestCls3) *cls3w = objGetWeak(TestCls3, cls3);
    uint64 ref3 = (uint64)atomicLoad(uintptr, &cls4->_ref, Acquire);
    if (ref3 != 1)
        TEST_FAIL_CPP(1, _SL("cls4->_ref after objGetWeak=${uint} != 1"), stvar(uint64, ref3));

    TestCls3 *cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if (!cls3a)
        TEST_FAIL_CPP(1, _SL("objAcquireFromWeak(TestCls3, cls3w) returned NULL"), stvNone);
    uint64 ref4 = (uint64)atomicLoad(uintptr, &cls3a->_ref, Acquire);
    if (ref4 != 2)
        TEST_FAIL_CPP(1, _SL("cls3a->_ref=${uint} != 2"), stvar(uint64, ref4));
    int32 tf2 = testcls3Testfunc2(cls3a);
    if (tf2 != 99)
        TEST_FAIL_CPP(1, _SL("testcls3Testfunc2(cls3a)=${int} != 99"), stvar(int32, tf2));

    objRelease(&cls3a);

    objRelease(&cls4);

    cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if (cls3a)
        TEST_FAIL_CPP(1, _SL("objAcquireFromWeak() after final release returned non-NULL: ${ptr}"),
                      stvar(ptr, cls3a));

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

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
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint64, (uint64)saSize(t1)));

    if (t1.a[0] != 500)
        TEST_FAIL(1, _SL("t1.a[0]=${int} != 500"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 0)
        TEST_FAIL(1, _SL("t1.a[50]=${int} != 0"), stvar(int32, t1.a[50]));
    if (t1.a[40] != 100)
        TEST_FAIL(1, _SL("t1.a[40]=${int} != 100"), stvar(int32, t1.a[40]));

    saInit(&t2, int64, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        TEST_FAIL(1, _SL("saSize(t2)=${uint} != 51"), stvar(uint64, (uint64)saSize(t2)));

    if (t2.a[0] != 500)
        TEST_FAIL(1, _SL("t2.a[0]=${int} != 500"), stvar(int64, t2.a[0]));
    if (t2.a[50] != 0)
        TEST_FAIL(1, _SL("t2.a[50]=${int} != 0"), stvar(int64, t2.a[50]));
    if (t2.a[40] != 100)
        TEST_FAIL(1, _SL("t2.a[40]=${int} != 100"), stvar(int64, t2.a[40]));

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
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint64, (uint64)saSize(t1)));

    if (t1.a[0] != 0)
        TEST_FAIL(1, _SL("sorted t1.a[0]=${int} != 0"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 500)
        TEST_FAIL(1, _SL("sorted t1.a[50]=${int} != 500"), stvar(int32, t1.a[50]));
    if (t1.a[40] != 400)
        TEST_FAIL(1, _SL("sorted t1.a[40]=${int} != 400"), stvar(int32, t1.a[40]));

    if (saFind(t1, int32, 320) != 32)
        TEST_FAIL(1, _SL("saFind(t1, 320)=${int} != 32"), stvar(int32, (int32)saFind(t1, int32, 320)));

    saInit(&t2, int64, 10, SA_Sorted);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        TEST_FAIL(1, _SL("saSize(t2)=${uint} != 51"), stvar(uint64, (uint64)saSize(t2)));

    if (t2.a[0] != 0)
        TEST_FAIL(1, _SL("sorted t2.a[0]=${int} != 0"), stvar(int64, t2.a[0]));
    if (t2.a[50] != 500)
        TEST_FAIL(1, _SL("sorted t2.a[50]=${int} != 500"), stvar(int64, t2.a[50]));
    if (t2.a[40] != 400)
        TEST_FAIL(1, _SL("sorted t2.a[40]=${int} != 400"), stvar(int64, t2.a[40]));

    if (saFind(t2, int64, 320) != 32)
        TEST_FAIL(1, _SL("saFind(t2, 320)=${int} != 32"), stvar(int32, (int32)saFind(t2, int64, 320)));

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
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint64, (uint64)saSize(t1)));

    if (t1.a[0].a != 500)
        TEST_FAIL(1, _SL("t1.a[0].a=${int} != 500"), stvar(int32, t1.a[0].a));
    if (t1.a[0].b != 500000)
        TEST_FAIL(1, _SL("t1.a[0].b=${int} != 500000"), stvar(int64, t1.a[0].b));
    if (t1.a[0].c != 495)
        TEST_FAIL(1, _SL("t1.a[0].c=${int} != 495"), stvar(int32, t1.a[0].c));

    if (t1.a[50].a != 0)
        TEST_FAIL(1, _SL("t1.a[50].a=${int} != 0"), stvar(int32, t1.a[50].a));
    if (t1.a[50].b != 0)
        TEST_FAIL(1, _SL("t1.a[50].b=${int} != 0"), stvar(int64, t1.a[50].b));
    if (t1.a[50].c != -5)
        TEST_FAIL(1, _SL("t1.a[50].c=${int} != -5"), stvar(int32, t1.a[50].c));

    if (t1.a[40].a != 100)
        TEST_FAIL(1, _SL("t1.a[40].a=${int} != 100"), stvar(int32, t1.a[40].a));
    if (t1.a[40].b != 100000)
        TEST_FAIL(1, _SL("t1.a[40].b=${int} != 100000"), stvar(int64, t1.a[40].b));
    if (t1.a[40].c != 95)
        TEST_FAIL(1, _SL("t1.a[40].c=${int} != 95"), stvar(int32, t1.a[40].c));

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
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 150"), stvar(uint64, (uint64)saSize(t1)));

    if (strTestRefCount(st1) != 51)
        TEST_FAIL(1, _SL("strTestRefCount(st1)=${int} != 51"), stvar(int32, strTestRefCount(st1)));
    if (strTestRefCount(st2) != 51)
        TEST_FAIL(1, _SL("strTestRefCount(st2)=${int} != 51"), stvar(int32, strTestRefCount(st2)));
    if (strTestRefCount(st3) != 51)
        TEST_FAIL(1, _SL("strTestRefCount(st3)=${int} != 51"), stvar(int32, strTestRefCount(st3)));

    if (saFind(t1, string, st1) != 0)
        TEST_FAIL(1, _SL("saFind(t1, st1)=${int} != 0"), stvar(int32, (int32)saFind(t1, string, st1)));
    if (saFind(t1, string, st2) != 1)
        TEST_FAIL(1, _SL("saFind(t1, st2)=${int} != 1"), stvar(int32, (int32)saFind(t1, string, st2)));
    if (saFind(t1, string, st3) != 2)
        TEST_FAIL(1, _SL("saFind(t1, st3)=${int} != 2"), stvar(int32, (int32)saFind(t1, string, st3)));

    saClear(&t1);
    if (saSize(t1) != 0)
        TEST_FAIL(1, _SL("saSize(t1) after clear=${uint} != 0"), stvar(uint64, (uint64)saSize(t1)));

    if (strTestRefCount(st1) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st1) after clear=${int} != 1"), stvar(int32, strTestRefCount(st1)));
    if (strTestRefCount(st2) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st2) after clear=${int} != 1"), stvar(int32, strTestRefCount(st2)));
    if (strTestRefCount(st3) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st3) after clear=${int} != 1"), stvar(int32, strTestRefCount(st3)));

    saDestroy(&t1);
    saInit(&t1, string, 10, SA_Sorted);

    saPush(&t1, string, st1);
    saPush(&t1, string, st2);
    saPush(&t1, string, st3);

    if (saFind(t1, string, st1) != 1)
        TEST_FAIL(1, _SL("sorted saFind(t1, st1)=${int} != 1"), stvar(int32, (int32)saFind(t1, string, st1)));
    if (saFind(t1, string, st2) != 2)
        TEST_FAIL(1, _SL("sorted saFind(t1, st2)=${int} != 2"), stvar(int32, (int32)saFind(t1, string, st2)));
    if (saFind(t1, string, st3) != 0)
        TEST_FAIL(1, _SL("sorted saFind(t1, st3)=${int} != 0"), stvar(int32, (int32)saFind(t1, string, st3)));

    saDestroy(&t1);

    if (strTestRefCount(st1) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st1) after destroy=${int} != 1"), stvar(int32, strTestRefCount(st1)));
    if (strTestRefCount(st2) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st2) after destroy=${int} != 1"), stvar(int32, strTestRefCount(st2)));
    if (strTestRefCount(st3) != 1)
        TEST_FAIL(1, _SL("strTestRefCount(st3) after destroy=${int} != 1"), stvar(int32, strTestRefCount(st3)));

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
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint64, (uint64)saSize(t1)));

    if (t1.a[0] != 500)
        TEST_FAIL(1, _SL("t1.a[0]=${int} != 500"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 0)
        TEST_FAIL(1, _SL("t1.a[50]=${int} != 0"), stvar(int32, t1.a[50]));
    if (t1.a[40] != 100)
        TEST_FAIL(1, _SL("t1.a[40]=${int} != 100"), stvar(int32, t1.a[40]));

    saSort(&t1, true);

    if (t1.a[0] != 0)
        TEST_FAIL(1, _SL("sorted t1.a[0]=${int} != 0"), stvar(int32, t1.a[0]));
    if (t1.a[50] != 500)
        TEST_FAIL(1, _SL("sorted t1.a[50]=${int} != 500"), stvar(int32, t1.a[50]));
    if (t1.a[40] != 400)
        TEST_FAIL(1, _SL("sorted t1.a[40]=${int} != 400"), stvar(int32, t1.a[40]));

    if (saFind(t1, int32, 320) != 32)
        TEST_FAIL(1, _SL("saFind(t1, 320)=${int} != 32"), stvar(int32, (int32)saFind(t1, int32, 320)));

    saInit(&t2, int64, 10);
    for (i = 500; i >= 0; i -= 10) {
        saPush(&t2, int64, i);
    }

    if (saSize(t2) != 51)
        TEST_FAIL(1, _SL("saSize(t2)=${uint} != 51"), stvar(uint64, (uint64)saSize(t2)));

    if (t2.a[0] != 500)
        TEST_FAIL(1, _SL("t2.a[0]=${int} != 500"), stvar(int64, t2.a[0]));
    if (t2.a[50] != 0)
        TEST_FAIL(1, _SL("t2.a[50]=${int} != 0"), stvar(int64, t2.a[50]));
    if (t2.a[40] != 100)
        TEST_FAIL(1, _SL("t2.a[40]=${int} != 100"), stvar(int64, t2.a[40]));

    saSort(&t2, true);

    if (t2.a[0] != 0)
        TEST_FAIL(1, _SL("sorted t2.a[0]=${int} != 0"), stvar(int64, t2.a[0]));
    if (t2.a[50] != 500)
        TEST_FAIL(1, _SL("sorted t2.a[50]=${int} != 500"), stvar(int64, t2.a[50]));
    if (t2.a[40] != 400)
        TEST_FAIL(1, _SL("sorted t2.a[40]=${int} != 400"), stvar(int64, t2.a[40]));

    if (saFind(t2, int64, 320) != 32)
        TEST_FAIL(1, _SL("saFind(t2, 320)=${int} != 32"), stvar(int32, (int32)saFind(t2, int64, 320)));

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
        TEST_FAIL(1, _SL("saFind(t1, st1)=${int} != 0"), stvar(int32, (int32)saFind(t1, string, st1)));
    if (saFind(t1, string, st2) != 1)
        TEST_FAIL(1, _SL("saFind(t1, st2)=${int} != 1"), stvar(int32, (int32)saFind(t1, string, st2)));
    if (saFind(t1, string, st3) != 2)
        TEST_FAIL(1, _SL("saFind(t1, st3)=${int} != 2"), stvar(int32, (int32)saFind(t1, string, st3)));

    saSort(&t1, true);

    if (saFind(t1, string, st1) != 1)
        TEST_FAIL(1, _SL("sorted saFind(t1, st1)=${int} != 1"), stvar(int32, (int32)saFind(t1, string, st1)));
    if (saFind(t1, string, st2) != 2)
        TEST_FAIL(1, _SL("sorted saFind(t1, st2)=${int} != 2"), stvar(int32, (int32)saFind(t1, string, st2)));
    if (saFind(t1, string, st3) != 0)
        TEST_FAIL(1, _SL("sorted saFind(t1, st3)=${int} != 0"), stvar(int32, (int32)saFind(t1, string, st3)));

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
        TEST_FAIL(1, _SL("strTestRefCount(s)=${int} != 1"), stvar(int32, strTestRefCount(s)));

    // Copy into an owning stvar — stCopy_string increments the refcount
    stvarCopy(&v, stvar(string, s));
    if (strTestRefCount(s) != 2)
        TEST_FAIL(2, _SL("after stvarCopy: strTestRefCount(s)=${int} != 2"), stvar(int32, strTestRefCount(s)));

    // Consume v into the array; the source stvar must be zeroed afterward
    saPushC(&arr, stvar, &v);

    // Source stvar must have been zeroed (PassPtr memset clears all fields)
    if (stvarType(&v) != NULL || v.data.st_string != NULL)
        TEST_FAIL(3, _SL("source stvar not zeroed after consume: type=${ptr} data.st_string=${ptr}"), stvar(ptr, (void*)stvarType(&v)), stvar(ptr, v.data.st_string));

    // Array must have one element
    if (saSize(arr) != 1)
        TEST_FAIL(4, _SL("saSize(arr)=${uint} != 1"), stvar(uint64, (uint64)saSize(arr)));

    // Array element must be a string stvar pointing to the same string
    if (!stvarIs(&arr.a[0], string))
        TEST_FAIL(5, _SL("array element is not a string stvar"), stvNone);
    if (arr.a[0].data.st_string != s)
        TEST_FAIL(6, _SL("array element string=${ptr} != source s=${ptr}"), stvar(ptr, arr.a[0].data.st_string), stvar(ptr, s));

    // Refcount: s owns one reference, the stvar in the array owns one
    if (strTestRefCount(s) != 2)
        TEST_FAIL(7, _SL("before destroy: strTestRefCount(s)=${int} != 2"), stvar(int32, strTestRefCount(s)));

    // Destroying the array must release the stvar's string reference
    saDestroy(&arr);
    if (strTestRefCount(s) != 1)
        TEST_FAIL(8, _SL("after array destroy: strTestRefCount(s)=${int} != 1"), stvar(int32, strTestRefCount(s)));

    strDestroy(&s);
    return 0;
}

// Orders int32 elements ascending or descending depending on the context pointer
static intptr cmp_int32_dir(stype st, stgeneric gen1, stgeneric gen2, flags_t flags, void* ctx)
{
    bool desc  = *(bool*)ctx;
    intptr ret = (intptr)gen1.st_int32 - (intptr)gen2.st_int32;

    return desc ? -ret : ret;
}

// Orders strings by length, shortest first
static intptr cmp_string_len(stype st, stgeneric gen1, stgeneric gen2, flags_t flags, void* ctx)
{
    return (intptr)strLen(gen1.st_string) - (intptr)strLen(gen2.st_string);
}

static int test_custom_sort()
{
    sa_int32 t1;
    sa_string t2;
    bool desc = true;
    int32 i;

    // Large enough to exercise the median-of-nine pivot selection path
    saInit(&t1, int32, 10, SA_Sorted);
    for (i = 0; i <= 500; i += 10) {
        saPush(&t1, int32, i);
    }

    if (saSize(t1) != 51)
        TEST_FAIL(1, _SL("saSize(t1)=${uint} != 51"), stvar(uint64, (uint64)saSize(t1)));

    saSortCustom(&t1, cmp_int32_dir, &desc);

    for (i = 0; i < 51; i++) {
        if (t1.a[i] != (50 - i) * 10)
            TEST_FAIL(2, _SL("descending sort t1.a[${int}]=${int} != ${int}"), stvar(int32, i), stvar(int32, t1.a[i]), stvar(int32, (50 - i) * 10));
    }

    // SA_Sorted must have been cleared, otherwise saFind would binary search a descending
    // array and come up with the wrong answer
    for (i = 0; i < 51; i++) {
        if (saFind(t1, int32, (50 - i) * 10) != i)
            TEST_FAIL(3, _SL("saFind after descending custom sort: found=${int} != ${int}"), stvar(int32, (int32)saFind(t1, int32, (50 - i) * 10)), stvar(int32, i));
    }

    // The same comparator must follow the context back the other way
    desc = false;
    saSortCustom(&t1, cmp_int32_dir, &desc);

    for (i = 0; i < 51; i++) {
        if (t1.a[i] != i * 10)
            TEST_FAIL(4, _SL("ascending sort t1.a[${int}]=${int} != ${int}"), stvar(int32, i), stvar(int32, t1.a[i]), stvar(int32, i * 10));
    }

    // Small arrays take the insertion sort path
    saSetSize(&t1, 3);
    t1.a[0] = 30;
    t1.a[1] = 10;
    t1.a[2] = 20;
    desc    = true;
    saSortCustom(&t1, cmp_int32_dir, &desc);
    if (t1.a[0] != 30 || t1.a[1] != 20 || t1.a[2] != 10)
        TEST_FAIL(5, _SL("insertion-sort path: t1.a={${int},${int},${int}} != {30,20,10}"), stvar(int32, t1.a[0]), stvar(int32, t1.a[1]), stvar(int32, t1.a[2]));

    saSetSize(&t1, 1);
    saSortCustom(&t1, cmp_int32_dir, &desc);
    if (t1.a[0] != 30)
        TEST_FAIL(6, _SL("single-element sort: t1.a[0]=${int} != 30"), stvar(int32, t1.a[0]));

    saClear(&t1);
    saSortCustom(&t1, cmp_int32_dir, &desc);
    if (saSize(t1) != 0)
        TEST_FAIL(7, _SL("empty-array sort: saSize(t1)=${uint} != 0"), stvar(uint64, (uint64)saSize(t1)));

    saDestroy(&t1);

    // Elements passed by pointer, with a comparator that ignores the context entirely
    saInit(&t2, string, 10);
    saPush(&t2, string, _S"This is a test");
    saPush(&t2, string, _S"Test");
    saPush(&t2, string, _S"This is also a test");

    saSortCustom(&t2, cmp_string_len, NULL);

    if (!strEq(t2.a[0], _S"Test"))
        TEST_FAIL(8, _SL("t2.a[0]='${string}' != 'Test'"), stvar(strref, t2.a[0]));
    if (!strEq(t2.a[1], _S"This is a test"))
        TEST_FAIL(9, _SL("t2.a[1]='${string}' != 'This is a test'"), stvar(strref, t2.a[1]));
    if (!strEq(t2.a[2], _S"This is also a test"))
        TEST_FAIL(10, _SL("t2.a[2]='${string}' != 'This is also a test'"), stvar(strref, t2.a[2]));

    saDestroy(&t2);

    return 0;
}

testfunc sarraytest_funcs[] = {
    { "int",           test_int           },
    { "sorted_int",    test_sorted_int    },
    { "opaque",        test_opaque        },
    { "string",        test_string        },
    { "sort",          test_sort          },
    { "string_sort",   test_string_sort   },
    { "custom_sort",   test_custom_sort   },
    { "stvar_consume", test_stvar_consume },
    { 0,               0                  }
};

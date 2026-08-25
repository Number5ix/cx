#include "objtestobj.h"
#include <cx/container/sarray.h>

#define TEST_FILE objtest
#define TEST_FUNCS objtest_funcs
#include "common.h"

static int test_iface()
{
    TestCls1 *cls1 = TestCls1_create();
    if (!cls1)
        TEST_FAIL(1, _SL("assertion failed: !cls1"), stvNone);

    cls1->data = 42;

    TestIf1 *ifptr = objInstIf(cls1, TestIf1);
    if (!ifptr)
        TEST_FAIL(1, _SL("assertion failed: !ifptr"), stvNone);

    if (ifptr->testfunc(cls1) != 42)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc(cls1) != 42 (ifptr->testfunc(cls1)=${int})"), stvar(int32, ifptr->testfunc(cls1)));

    objRelease(&cls1);

    return 0;
}

static int test_inherit()
{
    TestCls2 *cls2 = TestCls2_create();
    if (!cls2)
        TEST_FAIL(1, _SL("assertion failed: !cls2"), stvNone);

    cls2->data = 42;

    TestIf1 *ifptr = objInstIf(cls2, TestIf1);
    if (!ifptr)
        TEST_FAIL(1, _SL("assertion failed: !ifptr"), stvNone);

    if (ifptr->testfunc(cls2) != 42)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc(cls2) != 42 (ifptr->testfunc(cls2)=${int})"), stvar(int32, ifptr->testfunc(cls2)));

    objRelease(&cls2);

    return 0;
}

static int test_ifinherit()
{
    TestCls3 *cls3 = TestCls3_create();
    if (!cls3)
        TEST_FAIL(1, _SL("assertion failed: !cls3"), stvNone);

    cls3->data = 42;
    cls3->data2 = 69;

    TestIf2 *ifptr = objInstIf(cls3, TestIf2);
    if (!ifptr)
        TEST_FAIL(1, _SL("assertion failed: !ifptr"), stvNone);

    if (ifptr->testfunc(cls3) != 42)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc(cls3) != 42 (ifptr->testfunc(cls3)=${int})"), stvar(int32, ifptr->testfunc(cls3)));
    if (ifptr->testfunc2(cls3) != 69)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc2(cls3) != 69 (ifptr->testfunc2(cls3)=${int})"), stvar(int32, ifptr->testfunc2(cls3)));

    objRelease(&cls3);

    return 0;
}

static int test_override()
{
    TestCls4 *cls4 = TestCls4_create();
    if (!cls4)
        TEST_FAIL(1, _SL("assertion failed: !cls4"), stvNone);

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;

    TestIf2 *ifptr = objInstIf(cls4, TestIf2);
    if (!ifptr)
        TEST_FAIL(1, _SL("assertion failed: !ifptr"), stvNone);

    if (ifptr->testfunc(cls4) != 35)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc(cls4) != 35 (ifptr->testfunc(cls4)=${int})"), stvar(int32, ifptr->testfunc(cls4)));
    if (ifptr->testfunc2(cls4) != 69)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc2(cls4) != 69 (ifptr->testfunc2(cls4)=${int})"), stvar(int32, ifptr->testfunc2(cls4)));

    objRelease(&cls4);

    return 0;
}

static int test_abstract()
{
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        TEST_FAIL(1, _SL("assertion failed: !cls4"), stvNone);

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;
    cls4->data4 = 71;
    cls4->data5 = 99;

    TestIf2 *ifptr = objInstIf(cls4, TestIf2);
    if (!ifptr)
        TEST_FAIL(1, _SL("assertion failed: !ifptr"), stvNone);
    TestIf3 *ifptr3 = objInstIf(cls4, TestIf3);
    if (!ifptr3)
        TEST_FAIL(1, _SL("assertion failed: !ifptr3"), stvNone);

    if (ifptr->testfunc(cls4) != 71)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc(cls4) != 71 (ifptr->testfunc(cls4)=${int})"), stvar(int32, ifptr->testfunc(cls4)));
    if (ifptr->testfunc2(cls4) != 69)
        TEST_FAIL(1, _SL("assertion failed: ifptr->testfunc2(cls4) != 69 (ifptr->testfunc2(cls4)=${int})"), stvar(int32, ifptr->testfunc2(cls4)));
    if (ifptr3->testfunc3(cls4) != 99)
        TEST_FAIL(1, _SL("assertion failed: ifptr3->testfunc3(cls4) != 99 (ifptr3->testfunc3(cls4)=${int})"), stvar(int32, ifptr3->testfunc3(cls4)));

    if (cls4->_->testfunc(cls4) != 71)
        TEST_FAIL(1, _SL("assertion failed: cls4->_->testfunc(cls4) != 71 (cls4->_->testfunc(cls4)=${int})"), stvar(int32, cls4->_->testfunc(cls4)));
    if (cls4->_->testfunc2(cls4) != 69)
        TEST_FAIL(1, _SL("assertion failed: cls4->_->testfunc2(cls4) != 69 (cls4->_->testfunc2(cls4)=${int})"), stvar(int32, cls4->_->testfunc2(cls4)));
    if (cls4->_->testfunc3(cls4) != 99)
        TEST_FAIL(1, _SL("assertion failed: cls4->_->testfunc3(cls4) != 99 (cls4->_->testfunc3(cls4)=${int})"), stvar(int32, cls4->_->testfunc3(cls4)));

    objRelease(&cls4);

    return 0;
}

static TestCls4b *getptr(TestCls4b *ptr, int *counter)
{
    (*counter)++;
    return ptr;
}

static int test_cast()
{
    TestCls4b *cls4b = TestCls4b_create();

    // counter is to test for side effects from the macro evaluating the expression more than once
    int counter = 0;
    TestCls4a *cls4a = TestCls4a(getptr(cls4b, &counter));
    if ((uintptr)cls4a != (uintptr)cls4b || counter != 1)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls4a != (uintptr)cls4b || counter != 1 (counter=${int})"), stvar(int32, counter));

    TestCls4 *cls4 = TestCls4(getptr(cls4b, &counter));
    if ((uintptr)cls4 != (uintptr)cls4a || counter != 2)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls4 != (uintptr)cls4a || counter != 2 (counter=${int})"), stvar(int32, counter));

    TestCls3 *cls3 = TestCls3(getptr(cls4b, &counter));
    if ((uintptr)cls3 != (uintptr)cls4b || counter != 3)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls3 != (uintptr)cls4b || counter != 3 (counter=${int})"), stvar(int32, counter));

    TestCls2 *cls2 = TestCls2(getptr(cls4b, &counter));
    if ((uintptr)cls2 != (uintptr)cls4b || counter != 4)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls2 != (uintptr)cls4b || counter != 4 (counter=${int})"), stvar(int32, counter));

    TestCls1 *cls1 = TestCls1(getptr(cls4b, &counter));
    if ((uintptr)cls1 != (uintptr)cls4b || counter != 5)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls1 != (uintptr)cls4b || counter != 5 (counter=${int})"), stvar(int32, counter));

    // Remove ifdefs for full test; this should fail to compile
#if 0
    TestCls1 *cls2a = TestCls2(cls1);
    if ((uintptr)cls2a != (uintptr)cls4b || counter != 5)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)cls2a != (uintptr)cls4b || counter != 5 (counter=${int})"), stvar(int32, counter));
#endif

    ObjInst *obj = ObjInst(getptr(cls4b, &counter));
    if ((uintptr)obj != (uintptr)cls4b || counter != 6)
        TEST_FAIL(1, _SL("assertion failed: (uintptr)obj != (uintptr)cls4b || counter != 6 (counter=${int})"), stvar(int32, counter));

    objRelease(&cls4);
    return 0;
}

static int test_dyncast()
{
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        TEST_FAIL(1, _SL("assertion failed: !cls4"), stvNone);

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;
    cls4->data4 = 71;
    cls4->data5 = 99;

    TestCls1 *cls1 = objDynCast(TestCls1, cls4);
    if (!cls1)
        TEST_FAIL(1, _SL("assertion failed: !cls1"), stvNone);

    if (cls1->data != 42)
        TEST_FAIL(1, _SL("assertion failed: cls1->data != 42"), stvNone);

    if (testcls1Testfunc(cls1) != 71)
        TEST_FAIL(1, _SL("assertion failed: testcls1Testfunc(cls1) != 71"), stvNone);

    objRelease(&cls4);

    return 0;
}


static int test_obj_array()
{
    TestCls5 *cls5 = TestCls5_create();
    if (!cls5)
        TEST_FAIL(1, _SL("assertion failed: !cls5"), stvNone);

    cls5->data = 42;

    sa_TestCls5 arr;
    // test using strong type alias
    saInit(&arr, TestCls5, 10);

    for (int i = 0; i < 50; i++) {
        saPush(&arr, TestCls5, cls5);
    }

    if (atomicLoad(uintptr, &cls5->_ref, Acquire) != 51)
        TEST_FAIL(1, _SL("assertion failed: atomicLoad(uintptr, &cls5->_ref, Acquire) != 51"), stvNone);

    TestIf1 *if1 = objInstIf(arr.a[49], TestIf1);
    if (!if1)
        TEST_FAIL(1, _SL("assertion failed: !if1"), stvNone);
    if (if1->testfunc(arr.a[49]) != 42)
        TEST_FAIL(1, _SL("assertion failed: if1->testfunc(arr.a[49]) != 42 (if1->testfunc(arr.a[49])=${int})"), stvar(int32, if1->testfunc(arr.a[49])));

    saDestroy(&arr);

    if (atomicLoad(uintptr, &cls5->_ref, Acquire) != 1)
        TEST_FAIL(1, _SL("assertion failed: atomicLoad(uintptr, &cls5->_ref, Acquire) != 1"), stvNone);

    saInit(&arr, object, 10);

    for (int i = 0; i < 50; i++) {
        saPush(&arr, object, cls5, SA_Unique);
    }

    if (atomicLoad(uintptr, &cls5->_ref, Acquire) != 2)
        TEST_FAIL(1, _SL("assertion failed: atomicLoad(uintptr, &cls5->_ref, Acquire) != 2"), stvNone);

    saDestroy(&arr);
    objRelease(&cls5);

    return 0;
}

static int test_obj_weakref()
{
    TestCls4b *cls4 = TestCls4b_create();
    int ret = 0;
    if (!cls4)
        TEST_FAIL(1, _SL("assertion failed: !cls4"), stvNone);

    cls4->data = 12;
    cls4->data2 = 99;
    cls4->data3 = 15;
    cls4->data4 = 33;
    cls4->data5 = 73;

    TestCls3 *cls3 = TestCls3(cls4);
    Weak(TestCls3) *cls3w = objGetWeak(TestCls3, cls3);
    uint64 ref1 = (uint64)atomicLoad(uintptr, &cls4->_ref, Acquire);
    if (ref1 != 1)
        TEST_FAILV(ret, 1, _SL("cls4->_ref after objGetWeak=${uint} != 1"), stvar(uint64, ref1));

    TestCls3 *cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if(cls3a) {
        uint64 ref2 = (uint64)atomicLoad(uintptr, &cls3a->_ref, Acquire);
        if (ref2 != 2)
            TEST_FAILV(ret, 1, _SL("cls3a->_ref=${uint} != 2"), stvar(uint64, ref2));

        int32 tf2 = testcls3Testfunc2(cls3);
        if (tf2 != 99)
            TEST_FAILV(ret, 1, _SL("testcls3Testfunc2(cls3)=${int} != 99"), stvar(int32, tf2));
    } else {
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeak(TestCls3, cls3w) returned NULL"), stvNone);
    }

    TestCls1 *cls1 = objAcquireFromWeak(TestCls1, cls3w);
    if(cls1) {
        uint64 ref3 = (uint64)atomicLoad(uintptr, &cls1->_ref, Acquire);
        if (ref3 != 3)
            TEST_FAILV(ret, 1, _SL("cls1->_ref=${uint} != 3"), stvar(uint64, ref3));

        int32 tf1 = testcls1Testfunc(cls1);
        if (cls1->data != 12 || tf1 != 33)
            TEST_FAILV(ret, 1, _SL("cls1->data=${int} != 12 || testcls1Testfunc(cls1)=${int} != 33"),
                       stvar(int32, cls1->data), stvar(int32, tf1));
    } else {
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeak(TestCls1, cls3w) returned NULL"), stvNone);
    }

    TestCls4b *cls4a = objAcquireFromWeakDyn(TestCls4b, cls3w);
    if(cls4a) {
        uint64 ref4 = (uint64)atomicLoad(uintptr, &cls4a->_ref, Acquire);
        if (ref4 != 4)
            TEST_FAILV(ret, 1, _SL("cls4a->_ref=${uint} != 4"), stvar(uint64, ref4));

        int32 tf4 = testcls4Testfunc(cls4a);
        if (tf4 != 33)
            TEST_FAILV(ret, 1, _SL("testcls4Testfunc(cls4a)=${int} != 33"), stvar(int32, tf4));
    } else {
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeakDyn(TestCls4b, cls3w) returned NULL"), stvNone);
    }

    // test weak references after object has been destroyed

    objRelease(&cls4a);
    objRelease(&cls1);
    objRelease(&cls3a);

    uint64 ref5 = (uint64)atomicLoad(uintptr, &cls4->_ref, Acquire);
    if (ref5 != 1)
        TEST_FAILV(ret, 1, _SL("cls4->_ref before final release=${uint} != 1"), stvar(uint64, ref5));

    objRelease(&cls4);

    // these should all fail
    cls3a = objAcquireFromWeak(TestCls3, cls3w);
    if (cls3a)
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeak(TestCls3, cls3w) after final release returned non-NULL: ${ptr}"),
                   stvar(ptr, cls3a));

    cls1 = objAcquireFromWeak(TestCls1, cls3w);
    if (cls1)
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeak(TestCls1, cls3w) after final release returned non-NULL: ${ptr}"),
                   stvar(ptr, cls1));

    cls4a = objAcquireFromWeakDyn(TestCls4b, cls3w);
    if (cls4a)
        TEST_FAILV(ret, 1, _SL("objAcquireFromWeakDyn(TestCls4b, cls3w) after final release returned non-NULL: ${ptr}"),
                   stvar(ptr, cls4a));

    objDestroyWeak(&cls3w);

    return ret;
}

// ClassSet lookup, the mechanism a dynamic object slot resolves through. The entries are
// sorted by wire name at codegen so the lookup can be a binary search.
static int test_obj_classset()
{
    if (classSetFind(&SerClsSet_classset, _S"SerCls1") != &SerCls1_clsinfo)
        TEST_FAIL(1, _SL("assertion failed: classSetFind(&SerClsSet_classset, _S\"SerCls1\") != &SerCls1_clsinfo"), stvNone);
    if (classSetFind(&SerClsSet_classset, _S"SerCustom") != &SerCustom_clsinfo)
        TEST_FAIL(1, _SL("assertion failed: classSetFind(&SerClsSet_classset, _S\"SerCustom\") != &SerCustom_clsinfo"), stvNone);

    // A class that is not in the set, one that does not exist, and no name at all
    if (classSetFind(&SerClsSet_classset, _S"SerCls3") != NULL)
        TEST_FAIL(1, _SL("assertion failed: classSetFind(&SerClsSet_classset, _S\"SerCls3\") != NULL"), stvNone);
    if (classSetFind(&SerClsSet_classset, _S"NoSuchClass") != NULL)
        TEST_FAIL(1, _SL("assertion failed: classSetFind(&SerClsSet_classset, _S\"NoSuchClass\") != NULL"), stvNone);
    if (classSetFind(&SerClsSet_classset, NULL) != NULL)
        TEST_FAIL(1, _SL("assertion failed: classSetFind(&SerClsSet_classset, NULL) != NULL"), stvNone);

    return 0;
}

testfunc objtest_funcs[] = {
    { "iface", test_iface },
    { "inherit", test_inherit },
    { "ifinherit", test_ifinherit },
    { "override", test_override },
    { "abstract", test_abstract },
    { "cast", test_cast },
    { "dyncast", test_dyncast },
    { "obj_array", test_obj_array },
    { "weakref", test_obj_weakref },
    { "classset", test_obj_classset },
    { 0, 0 }
};

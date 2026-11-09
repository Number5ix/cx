#include "objtestobj.h"
#include <cx/container/sarray.h>
#include <cx/string/strtest.h>

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

// Every allowed type reads back what was written, and a read value is owned by the caller.
static int test_props_basic(void)
{
    int ret        = 0;
    PropBase* obj  = propbaseCreate();
    PropBase* peer = propbaseCreate();
    stvar v;

    if (!objPropSet(obj, _SL("num"), stvar(int32, -7)) || obj->vnum != -7)
        TEST_FAILV(ret, 1, _SL("num: vnum=${int} (want -7)"), stvar(int32, obj->vnum));
    if (!objPropSet(obj, _SL("flag"), stvar(bool, true)) || !obj->vflag)
        TEST_FAILV(ret, 1, _SL("flag: vflag=${int} (want 1)"), stvar(int32, obj->vflag));
    if (!objPropSet(obj, _SL("f32"), stvar(float32, 1.5f)) || obj->vf32 != 1.5f)
        TEST_FAILV(ret, 1, _SL("f32: vf32=${float} (want 1.5)"), stvar(float32, obj->vf32));
    if (!objPropSet(obj, _SL("ratio"), stvar(float64, 0.25)) || obj->vratio != 0.25)
        TEST_FAILV(ret, 1, _SL("ratio: vratio=${float} (want 0.25)"), stvar(float64, obj->vratio));
    if (!objPropSet(obj, _SL("big"), stvar(int64, 1LL << 40)) || obj->vbig != 1LL << 40)
        TEST_FAILV(ret, 1, _SL("big: vbig=${int}"), stvar(int64, obj->vbig));
    if (!objPropSet(obj, _SL("small"), stvar(uint8, 200)) || obj->vsmall != 200)
        TEST_FAILV(ret, 1, _SL("small: vsmall=${uint} (want 200)"), stvar(uint8, obj->vsmall));
    if (!objPropSet(obj, _SL("size"), stvar(size, 12345)) || obj->vsize != 12345)
        TEST_FAILV(ret, 1, _SL("size: vsize=${uint} (want 12345)"), stvar(size, obj->vsize));
    if (!objPropSet(obj, _SL("mode"), stvar(int32, 3)) || obj->vmode != 3)
        TEST_FAILV(ret, 1, _SL("mode: vmode=${int} (want 3)"), stvar(int32, obj->vmode));
    if (!objPropSet(obj, _SL("label"), stvar(strref, _SL("first"))) || !strEq(obj->vlabel, _SL("first")))
        TEST_FAILV(ret, 1, _SL("label: vlabel='${string}' (want 'first')"), stvar(string, obj->vlabel));
    if (!objPropSet(obj, _SL("peer"), stvar(object, peer)) || obj->vpeer != peer)
        TEST_FAILV(ret, 1, _SL("peer was not set"), stvNone);
    if (!objPropSet(obj, _SL("tag"), stvar(int32, 9)) || obj->vtag != 9)
        TEST_FAILV(ret, 1, _SL("tag, an unbound method's property: vtag=${int} (want 9)"), stvar(int32, obj->vtag));

    if (!objPropGet(obj, _SL("num"), &v) || !stEq(v._type, stType(int32)) || v.data.st_int32 != -7)
        TEST_FAILV(ret, 1, _SL("get num returned ${int} (want -7)"), stvar(int32, v.data.st_int32));
    stvarDestroy(&v);
    if (!objPropGet(obj, _SL("size"), &v) || !stEq(v._type, stType(size)) || v.data.st_size != 12345)
        TEST_FAILV(ret, 1, _SL("get size returned ${uint} (want 12345)"), stvar(size, v.data.st_size));
    stvarDestroy(&v);
    if (!objPropGet(obj, _SL("f32"), &v) || !stEq(v._type, stType(float32)) || v.data.st_float32 != 1.5f)
        TEST_FAILV(ret, 1, _SL("get f32 returned ${float} (want 1.5)"), stvar(float32, v.data.st_float32));
    stvarDestroy(&v);

    // a string read is owned: it survives the property changing and refers to its own copy
    if (!objPropGet(obj, _SL("label"), &v) || !stEq(v._type, stType(string)))
        TEST_FAILV(ret, 1, _SL("get label failed"), stvNone);
    objPropSet(obj, _SL("label"), stvar(strref, _SL("second")));
    if (!strEq(v.data.st_string, _SL("first")))
        TEST_FAILV(ret, 1, _SL("read label became '${string}' after a set (want 'first')"),
                   stvar(string, v.data.st_string));
    stvarDestroy(&v);

    // an object read holds a reference
    uintptr before = atomicLoad(uintptr, &peer->_ref, Acquire);
    if (!objPropGet(obj, _SL("peer"), &v) || v.data.st_object != objInstBase(peer))
        TEST_FAILV(ret, 1, _SL("get peer returned the wrong object"), stvNone);
    if (atomicLoad(uintptr, &peer->_ref, Acquire) != before + 1)
        TEST_FAILV(ret, 1, _SL("peer refcount=${uint} after get (want ${uint})"),
                   stvar(uint64, atomicLoad(uintptr, &peer->_ref, Acquire)), stvar(uint64, before + 1));
    stvarDestroy(&v);

    // [opt] object parameter: NULL clears it
    if (!objPropSet(obj, _SL("peer"), stvar(object, (ObjInst*)NULL)) || obj->vpeer)
        TEST_FAILV(ret, 1, _SL("setting peer to NULL failed"), stvNone);

    if (objPropGet(obj, _SL("nosuch"), &v) || !stEq(v._type, stType(none)))
        TEST_FAILV(ret, 1, _SL("get of an unknown property succeeded or left out set"), stvNone);
    if (objPropSet(obj, _SL("nosuch"), stvar(int32, 1)))
        TEST_FAILV(ret, 1, _SL("set of an unknown property succeeded"), stvNone);

    objRelease(&peer);
    objRelease(&obj);
    return ret;
}

// Values are converted to the property's type with range checks; bool and object types are
// stricter than stConvert.
static int test_props_convert(void)
{
    int ret        = 0;
    PropBase* obj  = propbaseCreate();
    PropOther* oth = propotherCreate();

    if (!objPropSet(obj, _SL("num"), stvar(int64, 12345)) || obj->vnum != 12345)
        TEST_FAILV(ret, 1, _SL("int64 in range into int32: vnum=${int}"), stvar(int32, obj->vnum));
    if (objPropSet(obj, _SL("num"), stvar(int64, 1LL << 40)) || obj->vnum != 12345)
        TEST_FAILV(ret, 1, _SL("int64 out of range into int32 was accepted: vnum=${int}"), stvar(int32, obj->vnum));
    if (objPropSet(obj, _SL("small"), stvar(int32, 300)))
        TEST_FAILV(ret, 1, _SL("300 into uint8 was accepted"), stvNone);
    if (!objPropSet(obj, _SL("f32"), stvar(float64, 2.5)) || obj->vf32 != 2.5f)
        TEST_FAILV(ret, 1, _SL("float64 into float32: vf32=${float}"), stvar(float32, obj->vf32));
    if (!objPropSet(obj, _SL("ratio"), stvar(int32, 3)) || obj->vratio != 3.0)
        TEST_FAILV(ret, 1, _SL("int32 into float64: vratio=${float}"), stvar(float64, obj->vratio));
    if (!objPropSet(obj, _SL("num"), stvar(strref, _SL("42"))) || obj->vnum != 42)
        TEST_FAILV(ret, 1, _SL("string into int32: vnum=${int}"), stvar(int32, obj->vnum));
    if (objPropSet(obj, _SL("num"), stvar(strref, _SL("forty"))))
        TEST_FAILV(ret, 1, _SL("non-numeric string into int32 was accepted"), stvNone);
    if (!objPropSet(obj, _SL("label"), stvar(int32, 7)) || !strEq(obj->vlabel, _SL("7")))
        TEST_FAILV(ret, 1, _SL("int32 into string: vlabel='${string}'"), stvar(string, obj->vlabel));

    obj->vflag = false;
    if (!objPropSet(obj, _SL("flag"), stvar(int32, 5)) || !obj->vflag)
        TEST_FAILV(ret, 1, _SL("nonzero int into bool did not set true"), stvNone);
    if (!objPropSet(obj, _SL("flag"), stvar(uint8, 0)) || obj->vflag)
        TEST_FAILV(ret, 1, _SL("zero into bool did not set false"), stvNone);
    if (objPropSet(obj, _SL("flag"), stvar(strref, _SL("true"))))
        TEST_FAILV(ret, 1, _SL("string into bool was accepted"), stvNone);
    if (objPropSet(obj, _SL("flag"), stvar(float64, 1.0)))
        TEST_FAILV(ret, 1, _SL("float into bool was accepted"), stvNone);

    if (objPropSet(obj, _SL("peer"), stvar(object, oth)) || obj->vpeer)
        TEST_FAILV(ret, 1, _SL("an object of the wrong class was accepted"), stvNone);
    if (objPropSet(obj, _SL("peer"), stvar(int32, 1)))
        TEST_FAILV(ret, 1, _SL("an integer into an object property was accepted"), stvNone);
    if (objPropSet(obj, _SL("num"), stvNone))
        TEST_FAILV(ret, 1, _SL("stvNone was accepted"), stvNone);

    objRelease(&oth);
    objRelease(&obj);
    return ret;
}

// A read-only property can be read but not written.
static int test_props_readonly(void)
{
    int ret       = 0;
    PropBase* obj = propbaseCreate();
    stvar v;

    objPropSet(obj, _SL("num"), stvar(int32, 1));
    objPropSet(obj, _SL("num"), stvar(int32, 2));
    if (objPropSet(obj, _SL("version"), stvar(int32, 99)))
        TEST_FAILV(ret, 1, _SL("set of a read-only property succeeded"), stvNone);
    if (!objPropGet(obj, _SL("version"), &v) || v.data.st_int32 != 2)
        TEST_FAILV(ret, 1, _SL("version=${int} (want 2)"), stvar(int32, v.data.st_int32));
    stvarDestroy(&v);

    const ObjPropDesc* pd = objPropFind(objClsInfo(obj), _SL("version"));
    if (!pd || pd->set || !(pd->flags & OBJPROP_ReadOnly))
        TEST_FAILV(ret, 1, _SL("version descriptor isn't marked read-only"), stvNone);
    pd = objPropFind(objClsInfo(obj), _SL("mode"));
    if (!pd || !(pd->flags & OBJPROP_Enum) || !stEq(pd->type, stType(int32)))
        TEST_FAILV(ret, 1, _SL("mode descriptor isn't an int32 enum"), stvNone);

    objRelease(&obj);
    return ret;
}

typedef struct PropEnumState {
    string names;
    int count;
    int stopafter;
} PropEnumState;

static bool propEnumCollect(const ObjPropDesc* pd, void* ctx)
{
    PropEnumState* st = ctx;
    strNConcat(&st->names, st->names, strEmpty(st->names) ? _S"" : _S",", pd->name);
    return ++st->count != st->stopafter;
}

// A child sees its parent's properties after its own, each once.
static int test_props_inherit(void)
{
    int ret         = 0;
    PropChild* obj  = propchildCreate();
    PropEnumState st = { 0 };
    stvar v;

    if (!objPropSet(obj, _SL("extra"), stvar(int32, 5)) || obj->vextra != 5)
        TEST_FAILV(ret, 1, _SL("child's own property: vextra=${int}"), stvar(int32, obj->vextra));
    if (!objPropSet(obj, _SL("ratio"), stvar(float64, 0.5)) || obj->vratio != 0.5)
        TEST_FAILV(ret, 1, _SL("inherited property: vratio=${float}"), stvar(float64, obj->vratio));
    if (!objPropGet(obj, _SL("ratio"), &v) || v.data.st_float64 != 0.5)
        TEST_FAILV(ret, 1, _SL("inherited get returned ${float}"), stvar(float64, v.data.st_float64));
    stvarDestroy(&v);

    if (objPropFind(objClsInfo(obj), _SL("ratio")) != objPropFind(&PropBase_clsinfo, _SL("ratio")))
        TEST_FAILV(ret, 1, _SL("the child doesn't share its parent's descriptor"), stvNone);
    if (objPropFind(&PropBase_clsinfo, _SL("extra")))
        TEST_FAILV(ret, 1, _SL("the parent found its child's property"), stvNone);

    objPropEnum(objClsInfo(obj), propEnumCollect, &st);
    if (!strEq(st.names, _SL("extra,big,f32,flag,label,limit,mode,num,peer,ratio,size,small,tag,version")))
        TEST_FAILV(ret, 1, _SL("enumerated '${string}'"), stvar(string, st.names));

    strClear(&st.names);
    st.count     = 0;
    st.stopafter = 3;
    if (objPropEnum(objClsInfo(obj), propEnumCollect, &st) || st.count != 3)
        TEST_FAILV(ret, 1, _SL("stopping early: count=${int} (want 3)"), stvar(int32, st.count));

    strDestroy(&st.names);
    objRelease(&obj);
    return ret;
}

// An interface's property appears on each implementing class and calls that class's method.
static int test_props_iface(void)
{
    int ret        = 0;
    PropBase* base = propbaseCreate();
    PropOther* oth = propotherCreate();

    objPropSet(base, _SL("label"), stvar(strref, _SL("x")));
    objPropSet(oth, _SL("label"), stvar(strref, _SL("x")));
    if (!strEq(base->vlabel, _SL("x")) || !strEq(oth->vlabel, _SL("other:x")))
        TEST_FAILV(ret, 1, _SL("labels '${string}' and '${string}' (want 'x' and 'other:x')"),
                   stvar(string, base->vlabel), stvar(string, oth->vlabel));

    const ObjPropDesc* pd = objPropFind(objClsInfo(oth), _SL("label"));
    if (!pd || !(pd->flags & OBJPROP_Iface) || !stEq(pd->type, stType(string)))
        TEST_FAILV(ret, 1, _SL("PropOther's label isn't an interface string property"), stvNone);
    if (pd == objPropFind(objClsInfo(base), _SL("label")))
        TEST_FAILV(ret, 1, _SL("the two classes share one label descriptor"), stvNone);
    if (objPropFind(objClsInfo(oth), _SL("num")))
        TEST_FAILV(ret, 1, _SL("PropOther has PropBase's num property"), stvNone);

    objRelease(&oth);
    objRelease(&base);
    return ret;
}

// A property declared on a parent, or on an abstract base, reaches the override.
static int test_props_override(void)
{
    int ret           = 0;
    PropChild* child  = propchildCreate();
    PropSquare* shape = propsquareCreate();
    stvar v;

    if (!objPropSet(child, _SL("num"), stvar(int32, 5)) || child->vnum != 10)
        TEST_FAILV(ret, 1, _SL("child num=${int} (want 10 from the override)"), stvar(int32, child->vnum));

    if (!objPropGet(shape, _SL("sides"), &v) || v.data.st_int32 != 4)
        TEST_FAILV(ret, 1, _SL("sides=${int} (want 4)"), stvar(int32, v.data.st_int32));
    stvarDestroy(&v);
    if (objPropFind(objClsInfo(shape), _SL("sides")) != objPropFind(&PropShape_clsinfo, _SL("sides")))
        TEST_FAILV(ret, 1, _SL("sides should come from the abstract base's table"), stvNone);

    objRelease(&shape);
    objRelease(&child);
    return ret;
}

// A setter returning false makes the set fail and leaves the value alone.
static int test_props_refuse(void)
{
    int ret       = 0;
    PropBase* obj = propbaseCreate();

    if (!objPropSet(obj, _SL("limit"), stvar(int32, 50)) || obj->vlimit != 50)
        TEST_FAILV(ret, 1, _SL("limit=${int} (want 50)"), stvar(int32, obj->vlimit));
    if (objPropSet(obj, _SL("limit"), stvar(int32, 200)) || obj->vlimit != 50)
        TEST_FAILV(ret, 1, _SL("refused set: limit=${int} (want 50 and failure)"), stvar(int32, obj->vlimit));

    // also through a cached descriptor, with conversion
    const ObjPropDesc* pd = objPropFind(objClsInfo(obj), _SL("limit"));
    if (!pd || objPropSetD(obj, pd, stvar(int64, 500)) || !objPropSetD(obj, pd, stvar(int64, 60)) ||
        obj->vlimit != 60)
        TEST_FAILV(ret, 1, _SL("descriptor set: limit=${int} (want 60)"), stvar(int32, obj->vlimit));

    objRelease(&obj);
    return ret;
}

static int test_grp_props(void)
{
    TEST_CHAIN(test_props_basic,
               test_props_convert,
               test_props_readonly,
               test_props_inherit,
               test_props_iface,
               test_props_override,
               test_props_refuse);
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
    { "props_basic", test_props_basic },
    { "props_convert", test_props_convert },
    { "props_readonly", test_props_readonly },
    { "props_inherit", test_props_inherit },
    { "props_iface", test_props_iface },
    { "props_override", test_props_override },
    { "props_refuse", test_props_refuse },
    { "grp_props", test_grp_props },
    { 0, 0 }
};

#include "objtestobj.h"
#include <cx/container/sarray.h>

#define TEST_FILE objtest
#define TEST_FUNCS objtest_funcs
#include "common.h"

static int test_iface()
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

    objRelease(cls1);

    return 0;
}

static int test_inherit()
{
    TestCls2 *cls2 = TestCls2_create();
    if (!cls2)
        return 1;

    cls2->data = 42;

    TestIf1 *ifptr = objInstIf(cls2, TestIf1);
    if (!ifptr)
        return 1;

    if (ifptr->testfunc(cls2) != 42)
        return 1;

    objRelease(cls2);

    return 0;
}

static int test_ifinherit()
{
    TestCls3 *cls3 = TestCls3_create();
    if (!cls3)
        return 1;

    cls3->data = 42;
    cls3->data2 = 69;

    TestIf2 *ifptr = objInstIf(cls3, TestIf2);
    if (!ifptr)
        return 1;

    if (ifptr->testfunc(cls3) != 42)
        return 1;
    if (ifptr->testfunc2(cls3) != 69)
        return 1;

    objRelease(cls3);

    return 0;
}

static int test_override()
{
    TestCls4 *cls4 = TestCls4_create();
    if (!cls4)
        return 1;

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;

    TestIf2 *ifptr = objInstIf(cls4, TestIf2);
    if (!ifptr)
        return 1;

    if (ifptr->testfunc(cls4) != 35)
        return 1;
    if (ifptr->testfunc2(cls4) != 69)
        return 1;

    objRelease(cls4);

    return 0;
}

static int test_abstract()
{
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        return 1;

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;
    cls4->data4 = 71;
    cls4->data5 = 99;

    TestIf2 *ifptr = objInstIf(cls4, TestIf2);
    if (!ifptr)
        return 1;
    TestIf3 *ifptr3 = objInstIf(cls4, TestIf3);
    if (!ifptr3)
        return 1;

    if (ifptr->testfunc(cls4) != 71)
        return 1;
    if (ifptr->testfunc2(cls4) != 69)
        return 1;
    if (ifptr3->testfunc3(cls4) != 99)
        return 1;

    if (cls4->_->testfunc(cls4) != 71)
        return 1;
    if (cls4->_->testfunc2(cls4) != 69)
        return 1;
    if (cls4->_->testfunc3(cls4) != 99)
        return 1;

    objRelease(cls4);

    return 0;
}

static int test_dyncast()
{
    TestCls4b *cls4 = TestCls4b_create();
    if (!cls4)
        return 1;

    cls4->data = 42;
    cls4->data2 = 69;
    cls4->data3 = 35;
    cls4->data4 = 71;
    cls4->data5 = 99;

    TestCls1 *cls1 = objDynCast(cls4, TestCls1);
    if (!cls1)
        return 1;

    if (cls1->data != 42)
        return 1;

    if (testCls1Testfunc(cls1) != 71)
        return 1;

    objRelease(cls4);

    return 0;
}


static int test_obj_array()
{
    TestCls5 *cls5 = TestCls5_create();
    if (!cls5)
        return 1;

    cls5->data = 42;

    TestCls5 **arr = saCreate(object, 10);

    for (int i = 0; i < 50; i++) {
        saPush(&arr, object, cls5);
    }

    if (atomicLoad(intptr, &cls5->_ref, AcqRel) != 51)
        return 1;

    TestIf1 *if1 = objInstIf(arr[49], TestIf1);
    if (!if1)
        return 1;
    if (if1->testfunc(arr[49]) != 42)
        return 1;

    saDestroy(&arr);

    if (atomicLoad(intptr, &cls5->_ref, AcqRel) != 1)
        return 1;

    arr = saCreate(object, 10);

    for (int i = 0; i < 50; i++) {
        saPush(&arr, object, cls5, Unique);
    }

    if (atomicLoad(intptr, &cls5->_ref, AcqRel) != 2)
        return 1;

    saDestroy(&arr);
    objRelease(cls5);

    return 0;
}

testfunc objtest_funcs[] = {
    { "iface", test_iface },
    { "inherit", test_inherit },
    { "ifinherit", test_ifinherit },
    { "override", test_override },
    { "abstract", test_abstract },
    { "dyncast", test_dyncast },
    { "obj_array", test_obj_array },
    { 0, 0 }
};

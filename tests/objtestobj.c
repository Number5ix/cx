// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "objtestobj.h"
// ==================== Auto-generated section ends ======================

TestCls1 *TestCls1_create()
{
    TestCls1 *ret;
    ret = objInstCreate(TestCls1);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

int TestCls1_testfunc(TestCls1 *self)
{
    return self->data;
}

TestCls2 *TestCls2_create()
{
    TestCls2 *ret;
    ret = objInstCreate(TestCls2);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

TestCls3 *TestCls3_create()
{
    TestCls3 *ret;
    ret = objInstCreate(TestCls3);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

int TestCls3_testfunc2(TestCls3 *self)
{
    return self->data2;
}

TestCls4 *TestCls4_create()
{
    TestCls4 *ret;
    ret = objInstCreate(TestCls4);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

extern int TestCls1_testfunc(TestCls1 *self); // parent
#define parent_testfunc() TestCls1_testfunc((TestCls1*)(self))
int TestCls4_testfunc(TestCls4 *self)
{
    return self->data3;
}

extern int TestCls4_testfunc(TestCls4 *self); // parent
#undef parent_testfunc
#define parent_testfunc() TestCls4_testfunc((TestCls4*)(self))
int TestCls4a_testfunc(TestCls4a *self)
{
    return self->data4;
}

int TestCls4b_testfunc3(TestCls4b *self)
{
    return self->data5;
}

TestCls5 *TestCls5_create()
{
    TestCls5 *ret;
    ret = objInstCreate(TestCls5);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

intptr TestCls5_cmp(TestCls5 *self, TestCls5 *other, uint32 flags)
{
    // Uncomment unless this function can compare different object classes
    devAssert(objClsInfo(self) == objClsInfo(other));

    return objDefaultCmp(self, other, flags);
}

TestCls4b *TestCls4b_create()
{
    TestCls4b *ret;
    ret = objInstCreate(TestCls4b);
    if (!objInstInit(ret))
        objRelease(&ret);
    return ret;
}

// Autogen begins -----
#include "objtestobj.auto.inc"
// Autogen ends -------

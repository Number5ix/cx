#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct TestCls1 TestCls1;
typedef struct TestCls2 TestCls2;
typedef struct TestCls3 TestCls3;
typedef struct TestCls4 TestCls4;
typedef struct TestCls4a TestCls4a;
typedef struct TestCls4b TestCls4b;
typedef struct TestCls5 TestCls5;
saDeclarePtr(TestCls1);
saDeclarePtr(TestCls2);
saDeclarePtr(TestCls3);
saDeclarePtr(TestCls4);
saDeclarePtr(TestCls4a);
saDeclarePtr(TestCls4b);
saDeclarePtr(TestCls5);

typedef struct TestIf1 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
} TestIf1;
extern TestIf1 TestIf1_tmpl;

typedef struct TestIf2 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    int (*testfunc2)(_Inout_ void *self);
} TestIf2;
extern TestIf2 TestIf2_tmpl;

typedef struct TestIf3 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc3)(_Inout_ void *self);
} TestIf3;
extern TestIf3 TestIf3_tmpl;

typedef struct TestCls1_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
} TestCls1_ClassIf;
extern TestCls1_ClassIf TestCls1_ClassIf_tmpl;

typedef struct TestCls2_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
} TestCls2_ClassIf;
extern TestCls2_ClassIf TestCls2_ClassIf_tmpl;

typedef struct TestCls3_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    int (*testfunc2)(_Inout_ void *self);
} TestCls3_ClassIf;
extern TestCls3_ClassIf TestCls3_ClassIf_tmpl;

typedef struct TestCls4_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    int (*testfunc2)(_Inout_ void *self);
} TestCls4_ClassIf;
extern TestCls4_ClassIf TestCls4_ClassIf_tmpl;

typedef struct TestCls4a_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    int (*testfunc2)(_Inout_ void *self);
    int (*testfunc3)(_Inout_ void *self);
} TestCls4a_ClassIf;
extern TestCls4a_ClassIf TestCls4a_ClassIf_tmpl;

typedef struct TestCls4b_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    int (*testfunc2)(_Inout_ void *self);
    int (*testfunc3)(_Inout_ void *self);
} TestCls4b_ClassIf;
extern TestCls4b_ClassIf TestCls4b_ClassIf_tmpl;

typedef struct TestCls5_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(_Inout_ void *self);
    intptr (*cmp)(_Inout_ void *self, void *other, uint32 flags);
} TestCls5_ClassIf;
extern TestCls5_ClassIf TestCls5_ClassIf_tmpl;

typedef struct TestCls1 {
    union {
        TestCls1_ClassIf *_;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
} TestCls1;
extern ObjClassInfo TestCls1_clsinfo;
#define TestCls1(inst) ((TestCls1*)(unused_noeval((inst) && &((inst)->_is_TestCls1)), (inst)))
#define TestCls1None ((TestCls1*)NULL)

_objfactory_guaranteed TestCls1 *TestCls1_create();
// TestCls1 *testcls1Create();
#define testcls1Create() TestCls1_create()

// int testcls1Testfunc(TestCls1 *self);
#define testcls1Testfunc(self) (self)->_->testfunc(TestCls1(self))

typedef struct TestCls2 {
    union {
        TestCls2_ClassIf *_;
        void *_is_TestCls2;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
} TestCls2;
extern ObjClassInfo TestCls2_clsinfo;
#define TestCls2(inst) ((TestCls2*)(unused_noeval((inst) && &((inst)->_is_TestCls2)), (inst)))
#define TestCls2None ((TestCls2*)NULL)

_objfactory_guaranteed TestCls2 *TestCls2_create();
// TestCls2 *testcls2Create();
#define testcls2Create() TestCls2_create()

// int testcls2Testfunc(TestCls2 *self);
#define testcls2Testfunc(self) (self)->_->testfunc(TestCls2(self))

typedef struct TestCls3 {
    union {
        TestCls3_ClassIf *_;
        void *_is_TestCls3;
        void *_is_TestCls2;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
    int data2;
} TestCls3;
extern ObjClassInfo TestCls3_clsinfo;
#define TestCls3(inst) ((TestCls3*)(unused_noeval((inst) && &((inst)->_is_TestCls3)), (inst)))
#define TestCls3None ((TestCls3*)NULL)

_objfactory_guaranteed TestCls3 *TestCls3_create();
// TestCls3 *testcls3Create();
#define testcls3Create() TestCls3_create()

// int testcls3Testfunc(TestCls3 *self);
#define testcls3Testfunc(self) (self)->_->testfunc(TestCls3(self))
// int testcls3Testfunc2(TestCls3 *self);
#define testcls3Testfunc2(self) (self)->_->testfunc2(TestCls3(self))

typedef struct TestCls4 {
    union {
        TestCls4_ClassIf *_;
        void *_is_TestCls4;
        void *_is_TestCls3;
        void *_is_TestCls2;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
    int data2;
    int data3;
} TestCls4;
extern ObjClassInfo TestCls4_clsinfo;
#define TestCls4(inst) ((TestCls4*)(unused_noeval((inst) && &((inst)->_is_TestCls4)), (inst)))
#define TestCls4None ((TestCls4*)NULL)

_objfactory_guaranteed TestCls4 *TestCls4_create();
// TestCls4 *testcls4Create();
#define testcls4Create() TestCls4_create()

// int testcls4Testfunc(TestCls4 *self);
#define testcls4Testfunc(self) (self)->_->testfunc(TestCls4(self))
// int testcls4Testfunc2(TestCls4 *self);
#define testcls4Testfunc2(self) (self)->_->testfunc2(TestCls4(self))

typedef struct TestCls4a {
    union {
        TestCls4a_ClassIf *_;
        void *_is_TestCls4a;
        void *_is_TestCls4;
        void *_is_TestCls3;
        void *_is_TestCls2;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
    int data2;
    int data3;
    int data4;
} TestCls4a;
extern ObjClassInfo TestCls4a_clsinfo;
#define TestCls4a(inst) ((TestCls4a*)(unused_noeval((inst) && &((inst)->_is_TestCls4a)), (inst)))
#define TestCls4aNone ((TestCls4a*)NULL)

// int testcls4aTestfunc(TestCls4a *self);
#define testcls4aTestfunc(self) (self)->_->testfunc(TestCls4a(self))
// int testcls4aTestfunc2(TestCls4a *self);
#define testcls4aTestfunc2(self) (self)->_->testfunc2(TestCls4a(self))
// int testcls4aTestfunc3(TestCls4a *self);
#define testcls4aTestfunc3(self) (self)->_->testfunc3(TestCls4a(self))

typedef struct TestCls4b {
    union {
        TestCls4b_ClassIf *_;
        void *_is_TestCls4b;
        void *_is_TestCls4a;
        void *_is_TestCls4;
        void *_is_TestCls3;
        void *_is_TestCls2;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
    int data2;
    int data3;
    int data4;
    int data5;
} TestCls4b;
extern ObjClassInfo TestCls4b_clsinfo;
#define TestCls4b(inst) ((TestCls4b*)(unused_noeval((inst) && &((inst)->_is_TestCls4b)), (inst)))
#define TestCls4bNone ((TestCls4b*)NULL)

_objfactory_guaranteed TestCls4b *TestCls4b_create();
// TestCls4b *testcls4bCreate();
#define testcls4bCreate() TestCls4b_create()

// int testcls4bTestfunc(TestCls4b *self);
#define testcls4bTestfunc(self) (self)->_->testfunc(TestCls4b(self))
// int testcls4bTestfunc2(TestCls4b *self);
#define testcls4bTestfunc2(self) (self)->_->testfunc2(TestCls4b(self))
// int testcls4bTestfunc3(TestCls4b *self);
#define testcls4bTestfunc3(self) (self)->_->testfunc3(TestCls4b(self))

typedef struct TestCls5 {
    union {
        TestCls5_ClassIf *_;
        void *_is_TestCls5;
        void *_is_TestCls1;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int data;
} TestCls5;
extern ObjClassInfo TestCls5_clsinfo;
#define TestCls5(inst) ((TestCls5*)(unused_noeval((inst) && &((inst)->_is_TestCls5)), (inst)))
#define TestCls5None ((TestCls5*)NULL)

_objfactory_guaranteed TestCls5 *TestCls5_create();
// TestCls5 *testcls5Create();
#define testcls5Create() TestCls5_create()

// int testcls5Testfunc(TestCls5 *self);
#define testcls5Testfunc(self) (self)->_->testfunc(TestCls5(self))
// intptr testcls5Cmp(TestCls5 *self, TestCls5 *other, uint32 flags);
#define testcls5Cmp(self, other, flags) (self)->_->cmp(TestCls5(self), other, flags)


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

typedef struct TestIf1 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
} TestIf1;
extern TestIf1 TestIf1_tmpl;

typedef struct TestIf2 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    int (*testfunc2)(void *self);
} TestIf2;
extern TestIf2 TestIf2_tmpl;

typedef struct TestIf3 {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc3)(void *self);
} TestIf3;
extern TestIf3 TestIf3_tmpl;

typedef struct TestCls1_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
} TestCls1_ClassIf;
extern TestCls1_ClassIf TestCls1_ClassIf_tmpl;

typedef struct TestCls2_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
} TestCls2_ClassIf;
extern TestCls2_ClassIf TestCls2_ClassIf_tmpl;

typedef struct TestCls3_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    int (*testfunc2)(void *self);
} TestCls3_ClassIf;
extern TestCls3_ClassIf TestCls3_ClassIf_tmpl;

typedef struct TestCls4_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    int (*testfunc2)(void *self);
} TestCls4_ClassIf;
extern TestCls4_ClassIf TestCls4_ClassIf_tmpl;

typedef struct TestCls4a_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    int (*testfunc2)(void *self);
} TestCls4a_ClassIf;
extern TestCls4a_ClassIf TestCls4a_ClassIf_tmpl;

typedef struct TestCls4b_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    int (*testfunc2)(void *self);
    int (*testfunc3)(void *self);
} TestCls4b_ClassIf;
extern TestCls4b_ClassIf TestCls4b_ClassIf_tmpl;

typedef struct TestCls5_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    int (*testfunc)(void *self);
    intptr (*cmp)(void *self, void *other, uint32 flags);
} TestCls5_ClassIf;
extern TestCls5_ClassIf TestCls5_ClassIf_tmpl;

typedef struct TestCls1 {
    TestCls1_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
} TestCls1;
extern ObjClassInfo TestCls1_clsinfo;

TestCls1 *TestCls1_create();
#define testCls1Create() TestCls1_create()
#define testCls1Testfunc(self) (self)->_->testfunc(objInstBase(self))

typedef struct TestCls2 {
    TestCls2_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
} TestCls2;
extern ObjClassInfo TestCls2_clsinfo;

TestCls2 *TestCls2_create();
#define testCls2Create() TestCls2_create()
#define testCls2Testfunc(self) (self)->_->testfunc(objInstBase(self))

typedef struct TestCls3 {
    TestCls3_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
    int data2;
} TestCls3;
extern ObjClassInfo TestCls3_clsinfo;

TestCls3 *TestCls3_create();
#define testCls3Create() TestCls3_create()
#define testCls3Testfunc(self) (self)->_->testfunc(objInstBase(self))
#define testCls3Testfunc2(self) (self)->_->testfunc2(objInstBase(self))

typedef struct TestCls4 {
    TestCls4_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
    int data2;
    int data3;
} TestCls4;
extern ObjClassInfo TestCls4_clsinfo;

TestCls4 *TestCls4_create();
#define testCls4Create() TestCls4_create()
#define testCls4Testfunc(self) (self)->_->testfunc(objInstBase(self))
#define testCls4Testfunc2(self) (self)->_->testfunc2(objInstBase(self))

typedef struct TestCls4a {
    TestCls4a_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
    int data2;
    int data3;
    int data4;
} TestCls4a;
extern ObjClassInfo TestCls4a_clsinfo;

#define testCls4aTestfunc(self) (self)->_->testfunc(objInstBase(self))
#define testCls4aTestfunc2(self) (self)->_->testfunc2(objInstBase(self))

typedef struct TestCls4b {
    TestCls4b_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
    int data2;
    int data3;
    int data4;
    int data5;
} TestCls4b;
extern ObjClassInfo TestCls4b_clsinfo;

TestCls4b *TestCls4b_create();
#define testCls4bCreate() TestCls4b_create()
#define testCls4bTestfunc(self) (self)->_->testfunc(objInstBase(self))
#define testCls4bTestfunc2(self) (self)->_->testfunc2(objInstBase(self))
#define testCls4bTestfunc3(self) (self)->_->testfunc3(objInstBase(self))

typedef struct TestCls5 {
    TestCls5_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    int data;
} TestCls5;
extern ObjClassInfo TestCls5_clsinfo;

TestCls5 *TestCls5_create();
#define testCls5Create() TestCls5_create()
#define testCls5Testfunc(self) (self)->_->testfunc(objInstBase(self))
#define testCls5Cmp(self, other, flags) (self)->_->cmp(objInstBase(self), other, flags)


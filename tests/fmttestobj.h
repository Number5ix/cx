#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/format/formattable.h>

typedef struct FmtTestClass FmtTestClass;
saDeclarePtr(FmtTestClass);

typedef struct FmtTestClass_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*format)(void *self, FMTVar *v, string *out);
} FmtTestClass_ClassIf;
extern FmtTestClass_ClassIf FmtTestClass_ClassIf_tmpl;

typedef struct FmtTestClass {
    FmtTestClass_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_FmtTestClass;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    int32 iv;
    string sv;
} FmtTestClass;
extern ObjClassInfo FmtTestClass_clsinfo;
#define FmtTestClass(inst) ((FmtTestClass*)((void)((inst) && &((inst)->_is_FmtTestClass)), (inst)))
#define FmtTestClassNone ((FmtTestClass*)NULL)

FmtTestClass *FmtTestClass_create(int32 ival, string sval);
#define fmttestclassCreate(ival, sval) FmtTestClass_create(ival, sval)
#define fmttestclassFormat(self, v, out) (self)->_->format(FmtTestClass(self), v, out)


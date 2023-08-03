#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct ConvertTestClass ConvertTestClass;
saDeclarePtr(ConvertTestClass);

typedef struct ConvertTestClass_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*convert)(void *self, stype st, stgeneric *dest, uint32 flags);
} ConvertTestClass_ClassIf;
extern ConvertTestClass_ClassIf ConvertTestClass_ClassIf_tmpl;

typedef struct ConvertTestClass {
    ConvertTestClass_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_ConvertTestClass;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    int32 ival;
    float64 fval;
    string sval;
} ConvertTestClass;
extern ObjClassInfo ConvertTestClass_clsinfo;
#define ConvertTestClass(inst) ((ConvertTestClass*)((void)((inst) && &((inst)->_is_ConvertTestClass)), (inst)))
#define ConvertTestClassNone ((ConvertTestClass*)NULL)

ConvertTestClass *ConvertTestClass_create(int32 ival, float64 fval, string sval);
// ConvertTestClass *converttestclassCreate(int32 ival, float64 fval, string sval);
#define converttestclassCreate(ival, fval, sval) ConvertTestClass_create(ival, fval, sval)

// bool converttestclassConvert(ConvertTestClass *self, stype st, stgeneric *dest, uint32 flags);
#define converttestclassConvert(self, st, dest, flags) (self)->_->convert(ConvertTestClass(self), st, dest, flags)


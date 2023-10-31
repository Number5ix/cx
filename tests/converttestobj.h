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

    // NOTE: While this is used by stConvert, the object interface is a higher level interface.
    // The normal convention of blindly overwriting the destination does not apply here. For
    // example, when called to convert to a string, the destination should be properly reused
    // or destroyed first.
    // The layer between stConvert and Convertible takes care of making sure the destination is
    // always initialized.
    bool (*convert)(_Inout_ void *self, stype st, stgeneric *dest, uint32 flags);
} ConvertTestClass_ClassIf;
extern ConvertTestClass_ClassIf ConvertTestClass_ClassIf_tmpl;

typedef struct ConvertTestClass {
    union {
        ConvertTestClass_ClassIf *_;
        void *_is_ConvertTestClass;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    int32 ival;
    float64 fval;
    string sval;
} ConvertTestClass;
extern ObjClassInfo ConvertTestClass_clsinfo;
#define ConvertTestClass(inst) ((ConvertTestClass*)(unused_noeval((inst) && &((inst)->_is_ConvertTestClass)), (inst)))
#define ConvertTestClassNone ((ConvertTestClass*)NULL)

_objfactory_guaranteed ConvertTestClass *ConvertTestClass_create(int32 ival, float64 fval, string sval);
// ConvertTestClass *converttestclassCreate(int32 ival, float64 fval, string sval);
#define converttestclassCreate(ival, fval, sval) ConvertTestClass_create(ival, fval, sval)

// bool converttestclassConvert(ConvertTestClass *self, stype st, stgeneric *dest, uint32 flags);
//
// NOTE: While this is used by stConvert, the object interface is a higher level interface.
// The normal convention of blindly overwriting the destination does not apply here. For
// example, when called to convert to a string, the destination should be properly reused
// or destroyed first.
// The layer between stConvert and Convertible takes care of making sure the destination is
// always initialized.
#define converttestclassConvert(self, st, dest, flags) (self)->_->convert(ConvertTestClass(self), st, dest, flags)


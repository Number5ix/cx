#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/format/formattable.h>

typedef struct FmtTestClass FmtTestClass;
typedef struct FmtTestClass2 FmtTestClass2;
saDeclarePtr(FmtTestClass);
saDeclarePtr(FmtTestClass2);

typedef struct FmtTestClass_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*format)(void *self, FMTVar *v, string *out);
} FmtTestClass_ClassIf;
extern FmtTestClass_ClassIf FmtTestClass_ClassIf_tmpl;

typedef struct FmtTestClass2_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*convert)(void *self, stype st, stgeneric *dest, uint32 flags);
} FmtTestClass2_ClassIf;
extern FmtTestClass2_ClassIf FmtTestClass2_ClassIf_tmpl;

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
// FmtTestClass *fmttestclassCreate(int32 ival, string sval);
#define fmttestclassCreate(ival, sval) FmtTestClass_create(ival, sval)

// bool fmttestclassFormat(FmtTestClass *self, FMTVar *v, string *out);
#define fmttestclassFormat(self, v, out) (self)->_->format(FmtTestClass(self), v, out)

typedef struct FmtTestClass2 {
    FmtTestClass2_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_FmtTestClass2;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    int32 iv;
    string sv;
} FmtTestClass2;
extern ObjClassInfo FmtTestClass2_clsinfo;
#define FmtTestClass2(inst) ((FmtTestClass2*)((void)((inst) && &((inst)->_is_FmtTestClass2)), (inst)))
#define FmtTestClass2None ((FmtTestClass2*)NULL)

FmtTestClass2 *FmtTestClass2_create(int32 ival, string sval);
// FmtTestClass2 *fmttestclass2Create(int32 ival, string sval);
#define fmttestclass2Create(ival, sval) FmtTestClass2_create(ival, sval)

// bool fmttestclass2Convert(FmtTestClass2 *self, stype st, stgeneric *dest, uint32 flags);
#define fmttestclass2Convert(self, st, dest, flags) (self)->_->convert(FmtTestClass2(self), st, dest, flags)


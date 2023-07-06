#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct Param Param;
typedef struct Method Method;
typedef struct Interface Interface;
typedef struct Member Member;
typedef struct Class Class;
typedef struct ComplexArrayType ComplexArrayType;
saDeclarePtr(Param);
saDeclarePtr(Method);
saDeclarePtr(Interface);
saDeclarePtr(Member);
saDeclarePtr(Class);
saDeclarePtr(ComplexArrayType);
saDeclareType(sarray_string, sa_string);

typedef struct Method_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    Method *(*clone)(void *self);
    intptr (*cmp)(void *self, void *other, uint32 flags);
} Method_ClassIf;
extern Method_ClassIf Method_ClassIf_tmpl;

typedef struct Interface_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    intptr (*cmp)(void *self, void *other, uint32 flags);
} Interface_ClassIf;
extern Interface_ClassIf Interface_ClassIf_tmpl;

typedef struct Member_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    intptr (*cmp)(void *self, void *other, uint32 flags);
} Member_ClassIf;
extern Member_ClassIf Member_ClassIf_tmpl;

typedef struct Class_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    intptr (*cmp)(void *self, void *other, uint32 flags);
} Class_ClassIf;
extern Class_ClassIf Class_ClassIf_tmpl;

typedef struct Param {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Param;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    string type;
    string predecr;
    string name;
    string postdecr;
} Param;
extern ObjClassInfo Param_clsinfo;
#define Param(inst) ((Param*)((void)((inst) && &((inst)->_is_Param)), (inst)))
#define ParamNone ((Param*)NULL)

Param *Param_create();
// Param *paramCreate();
#define paramCreate() Param_create()


typedef struct Method {
    Method_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Method;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    Class *srcclass;
    Interface *srcif;
    Class *mixinsrc;
    string srcfile;
    string returntype;
    string predecr;
    string name;
    sa_Param params;
    sa_string comments;
    sa_sarray_string annotations;
    bool isinit;
    bool isdestroy;
    bool isfactory;
    bool internal;
    bool unbound;
    bool standalone;
    bool mixin;
} Method;
extern ObjClassInfo Method_clsinfo;
#define Method(inst) ((Method*)((void)((inst) && &((inst)->_is_Method)), (inst)))
#define MethodNone ((Method*)NULL)

Method *Method_create();
// Method *methodCreate();
#define methodCreate() Method_create()

// Method *methodClone(Method *self);
#define methodClone(self) (self)->_->clone(Method(self))
// intptr methodCmp(Method *self, Method *other, uint32 flags);
#define methodCmp(self, other, flags) (self)->_->cmp(Method(self), other, flags)

typedef struct Interface {
    Interface_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Interface;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    string name;
    Interface *parent;
    sa_Method methods;
    bool included;
    bool processed;
    bool classif;
    sa_Method allmethods;
} Interface;
extern ObjClassInfo Interface_clsinfo;
#define Interface(inst) ((Interface*)((void)((inst) && &((inst)->_is_Interface)), (inst)))
#define InterfaceNone ((Interface*)NULL)

Interface *Interface_create();
// Interface *interfaceCreate();
#define interfaceCreate() Interface_create()

// intptr interfaceCmp(Interface *self, Interface *other, uint32 flags);
#define interfaceCmp(self, other, flags) (self)->_->cmp(Interface(self), other, flags)

typedef struct Member {
    Member_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Member;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    sa_string fulltype;
    string vartype;
    string predecr;
    string name;
    string postdecr;
    sa_string comments;
    sa_sarray_string annotations;
    Class *mixinsrc;
    string initstr;
    bool init;
    bool destroy;
} Member;
extern ObjClassInfo Member_clsinfo;
#define Member(inst) ((Member*)((void)((inst) && &((inst)->_is_Member)), (inst)))
#define MemberNone ((Member*)NULL)

Member *Member_create();
// Member *memberCreate();
#define memberCreate() Member_create()

// intptr memberCmp(Member *self, Member *other, uint32 flags);
#define memberCmp(self, other, flags) (self)->_->cmp(Member(self), other, flags)

typedef struct Class {
    Class_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Class;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    string name;
    Class *parent;
    Interface *classif;
    sa_Interface implements;
    sa_Class uses;
    sa_Member members;
    sa_Method methods;
    sa_string overrides;
    sa_sarray_string annotations;
    bool abstract;
    bool mixin;
    bool hasinit;
    bool hasdestroy;
    bool included;
    bool processed;
    bool hasautoinit;
    bool hasautodtors;
    string methodprefix;
    sa_Member allmembers;        // runtime stuff
    sa_Method allmethods;
} Class;
extern ObjClassInfo Class_clsinfo;
#define Class(inst) ((Class*)((void)((inst) && &((inst)->_is_Class)), (inst)))
#define ClassNone ((Class*)NULL)

Class *Class_create();
// Class *classCreate();
#define classCreate() Class_create()

// intptr classCmp(Class *self, Class *other, uint32 flags);
#define classCmp(self, other, flags) (self)->_->cmp(Class(self), other, flags)

typedef struct ComplexArrayType {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_ComplexArrayType;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    string tname;
    string tsubtype;
} ComplexArrayType;
extern ObjClassInfo ComplexArrayType_clsinfo;
#define ComplexArrayType(inst) ((ComplexArrayType*)((void)((inst) && &((inst)->_is_ComplexArrayType)), (inst)))
#define ComplexArrayTypeNone ((ComplexArrayType*)NULL)

ComplexArrayType *ComplexArrayType_create();
// ComplexArrayType *complexarraytypeCreate();
#define complexarraytypeCreate() ComplexArrayType_create()



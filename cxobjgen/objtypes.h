#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct Param Param;
typedef struct Method Method;
typedef struct Interface Interface;
typedef struct Member Member;
typedef struct Class Class;

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
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    string type;
    string predecr;
    string name;
    string postdecr;
} Param;
extern ObjClassInfo Param_clsinfo;

Param *Param_create();
#define paramCreate() Param_create()

typedef struct Method {
    Method_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    Class *srcclass;
    Interface *srcif;
    Class *mixinsrc;
    string srcfile;
    string returntype;
    string predecr;
    string name;
    Param **params;
    string **annotations;
    bool isinit;
    bool isdestroy;
    bool isfactory;
    bool internal;
    bool unbound;
    bool mixin;
} Method;
extern ObjClassInfo Method_clsinfo;

Method *Method_create();
#define methodCreate() Method_create()
#define methodClone(self) (self)->_->clone(objInstBase(self))
#define methodCmp(self, other, flags) (self)->_->cmp(objInstBase(self), other, flags)

typedef struct Interface {
    Interface_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    string name;
    Interface *parent;
    Method **methods;
    bool included;
    bool processed;
    Method **allmethods;
} Interface;
extern ObjClassInfo Interface_clsinfo;

Interface *Interface_create();
#define interfaceCreate() Interface_create()
#define interfaceCmp(self, other, flags) (self)->_->cmp(objInstBase(self), other, flags)

typedef struct Member {
    Member_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    string *fulltype;
    string vartype;
    string predecr;
    string name;
    string postdecr;
    string **annotations;
    Class *mixinsrc;
    string initstr;
    bool init;
    bool destroy;
} Member;
extern ObjClassInfo Member_clsinfo;

Member *Member_create();
#define memberCreate() Member_create()
#define memberCmp(self, other, flags) (self)->_->cmp(objInstBase(self), other, flags)

typedef struct Class {
    Class_ClassIf *_;
    ObjClassInfo *_clsinfo;
    atomic_intptr_t _ref;

    string name;
    Class *parent;
    Interface *classif;
    Interface **implements;
    Class **uses;
    Member **members;
    Method **methods;
    string *overrides;
    string **annotations;
    bool abstract;
    bool mixin;
    bool hasinit;
    bool hasdestroy;
    bool included;
    bool processed;
    bool hasautoinit;
    bool hasautodtors;
    string methodprefix;
    Member **allmembers;
    Method **allmethods;
} Class;
extern ObjClassInfo Class_clsinfo;

Class *Class_create();
#define classCreate() Class_create()
#define classCmp(self, other, flags) (self)->_->cmp(objInstBase(self), other, flags)


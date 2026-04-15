#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/struct.h>

typedef struct Param Param;
typedef struct Param_WeakRef Param_WeakRef;
typedef struct Method Method;
typedef struct Method_WeakRef Method_WeakRef;
typedef struct Interface Interface;
typedef struct Interface_WeakRef Interface_WeakRef;
typedef struct TypeNode TypeNode;
typedef struct TypeNode_WeakRef TypeNode_WeakRef;
typedef struct Member Member;
typedef struct Member_WeakRef Member_WeakRef;
typedef struct Class Class;
typedef struct Class_WeakRef Class_WeakRef;
typedef struct ComplexArrayType ComplexArrayType;
typedef struct ComplexArrayType_WeakRef ComplexArrayType_WeakRef;
typedef struct StructDef StructDef;
typedef struct StructDef_WeakRef StructDef_WeakRef;
typedef struct StructSetDef StructSetDef;
typedef struct StructSetDef_WeakRef StructSetDef_WeakRef;
saDeclarePtr(Param);
saDeclarePtr(Param_WeakRef);
#define _sti_Param _sti_object
#define SType_Param Param*
#define STStorageType_Param Param*
#define STypeArg_Param(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(Param, val))
#define STypeArgPtr_Param(type, val) (stgeneric*)objInstCheckClassPtr(Param, val)
#define STypeCheckedArg_Param(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_Param(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(Method);
saDeclarePtr(Method_WeakRef);
#define _sti_Method _sti_object
#define SType_Method Method*
#define STStorageType_Method Method*
#define STypeArg_Method(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(Method, val))
#define STypeArgPtr_Method(type, val) (stgeneric*)objInstCheckClassPtr(Method, val)
#define STypeCheckedArg_Method(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_Method(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(Interface);
saDeclarePtr(Interface_WeakRef);
#define _sti_Interface _sti_object
#define SType_Interface Interface*
#define STStorageType_Interface Interface*
#define STypeArg_Interface(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(Interface, val))
#define STypeArgPtr_Interface(type, val) (stgeneric*)objInstCheckClassPtr(Interface, val)
#define STypeCheckedArg_Interface(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_Interface(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(TypeNode);
saDeclarePtr(TypeNode_WeakRef);
#define _sti_TypeNode _sti_object
#define SType_TypeNode TypeNode*
#define STStorageType_TypeNode TypeNode*
#define STypeArg_TypeNode(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(TypeNode, val))
#define STypeArgPtr_TypeNode(type, val) (stgeneric*)objInstCheckClassPtr(TypeNode, val)
#define STypeCheckedArg_TypeNode(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_TypeNode(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(Member);
saDeclarePtr(Member_WeakRef);
#define _sti_Member _sti_object
#define SType_Member Member*
#define STStorageType_Member Member*
#define STypeArg_Member(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(Member, val))
#define STypeArgPtr_Member(type, val) (stgeneric*)objInstCheckClassPtr(Member, val)
#define STypeCheckedArg_Member(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_Member(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(Class);
saDeclarePtr(Class_WeakRef);
#define _sti_Class _sti_object
#define SType_Class Class*
#define STStorageType_Class Class*
#define STypeArg_Class(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(Class, val))
#define STypeArgPtr_Class(type, val) (stgeneric*)objInstCheckClassPtr(Class, val)
#define STypeCheckedArg_Class(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_Class(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(ComplexArrayType);
saDeclarePtr(ComplexArrayType_WeakRef);
#define _sti_ComplexArrayType _sti_object
#define SType_ComplexArrayType ComplexArrayType*
#define STStorageType_ComplexArrayType ComplexArrayType*
#define STypeArg_ComplexArrayType(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(ComplexArrayType, val))
#define STypeArgPtr_ComplexArrayType(type, val) (stgeneric*)objInstCheckClassPtr(ComplexArrayType, val)
#define STypeCheckedArg_ComplexArrayType(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_ComplexArrayType(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(StructDef);
saDeclarePtr(StructDef_WeakRef);
#define _sti_StructDef _sti_object
#define SType_StructDef StructDef*
#define STStorageType_StructDef StructDef*
#define STypeArg_StructDef(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(StructDef, val))
#define STypeArgPtr_StructDef(type, val) (stgeneric*)objInstCheckClassPtr(StructDef, val)
#define STypeCheckedArg_StructDef(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_StructDef(type, val) stType(type), stArgPtr(type, val)
saDeclarePtr(StructSetDef);
saDeclarePtr(StructSetDef_WeakRef);
#define _sti_StructSetDef _sti_object
#define SType_StructSetDef StructSetDef*
#define STStorageType_StructSetDef StructSetDef*
#define STypeArg_StructSetDef(type, val) stgeneric(object, (ObjInst*)objInstCheckClass(StructSetDef, val))
#define STypeArgPtr_StructSetDef(type, val) (stgeneric*)objInstCheckClassPtr(StructSetDef, val)
#define STypeCheckedArg_StructSetDef(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_StructSetDef(type, val) stType(type), stArgPtr(type, val)
saDeclareType(sarray_string, sa_string);

typedef struct Method_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    _Ret_valid_ Method* (*clone)(_In_ void* self);
    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} Method_ClassIf;
extern Method_ClassIf Method_ClassIf_tmpl;

typedef struct Interface_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} Interface_ClassIf;
extern Interface_ClassIf Interface_ClassIf_tmpl;

typedef struct Member_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} Member_ClassIf;
extern Member_ClassIf Member_ClassIf_tmpl;

typedef struct Class_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} Class_ClassIf;
extern Class_ClassIf Class_ClassIf_tmpl;

typedef struct StructDef_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} StructDef_ClassIf;
extern StructDef_ClassIf StructDef_ClassIf_tmpl;

typedef struct StructSetDef_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    intptr (*cmp)(_In_ void* self, void* other, uint32 flags);
} StructSetDef_ClassIf;
extern StructSetDef_ClassIf StructSetDef_ClassIf_tmpl;

typedef struct Param {
    union {
        ObjIface* _;
        void* _is_Param;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string type;
    string predecr;
    string name;
    string postdecr;
    sa_sarray_string annotations;
} Param;
extern ObjClassInfo Param_clsinfo;
#define Param(inst) objInstCheckClass(Param, inst)
#define ParamNone ((Param*)NULL)

typedef struct Param_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_Param_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} Param_WeakRef;
#define Param_WeakRef(inst) objWeakRefCheckClass(Param, inst)

_objfactory_guaranteed Param* Param_create();
// Param* paramCreate();
#define paramCreate() Param_create()


typedef struct Method {
    union {
        Method_ClassIf* _;
        void* _is_Method;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    Class* srcclass;
    Interface* srcif;
    Class* mixinsrc;
    string srcfile;
    string returntype;
    string predecr;
    string name;
    sa_Param params;
    sa_string comments;
    sa_string docs;
    sa_sarray_string annotations;
    bool isinit;
    bool isdestroy;
    bool isfactory;
    bool canfail;
    bool internal;
    bool unbound;
    bool standalone;
    bool mixin;
} Method;
extern ObjClassInfo Method_clsinfo;
#define Method(inst) objInstCheckClass(Method, inst)
#define MethodNone ((Method*)NULL)

typedef struct Method_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_Method_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} Method_WeakRef;
#define Method_WeakRef(inst) objWeakRefCheckClass(Method, inst)

_objfactory_guaranteed Method* Method_create();
// Method* methodCreate();
#define methodCreate() Method_create()

// Method* methodClone(Method* self);
#define methodClone(self) (self)->_->clone(Method(self))
// intptr methodCmp(Method* self, Method* other, uint32 flags);
#define methodCmp(self, other, flags) (self)->_->cmp(Method(self), other, flags)

typedef struct Interface {
    union {
        Interface_ClassIf* _;
        void* _is_Interface;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string name;
    Interface* parent;
    sa_Method methods;
    bool included;
    bool processed;
    bool classif;
    sa_Method allmethods;
} Interface;
extern ObjClassInfo Interface_clsinfo;
#define Interface(inst) objInstCheckClass(Interface, inst)
#define InterfaceNone ((Interface*)NULL)

typedef struct Interface_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_Interface_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} Interface_WeakRef;
#define Interface_WeakRef(inst) objWeakRefCheckClass(Interface, inst)

_objfactory_guaranteed Interface* Interface_create();
// Interface* interfaceCreate();
#define interfaceCreate() Interface_create()

// intptr interfaceCmp(Interface* self, Interface* other, uint32 flags);
#define interfaceCmp(self, other, flags) (self)->_->cmp(Interface(self), other, flags)

typedef struct TypeNode {
    union {
        ObjIface* _;
        void* _is_TypeNode;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string name;
    sa_TypeNode params;
} TypeNode;
extern ObjClassInfo TypeNode_clsinfo;
#define TypeNode(inst) objInstCheckClass(TypeNode, inst)
#define TypeNodeNone ((TypeNode*)NULL)

typedef struct TypeNode_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TypeNode_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} TypeNode_WeakRef;
#define TypeNode_WeakRef(inst) objWeakRefCheckClass(TypeNode, inst)

_objfactory_guaranteed TypeNode* TypeNode_create();
// TypeNode* typenodeCreate();
#define typenodeCreate() TypeNode_create()


typedef struct Member {
    union {
        Member_ClassIf* _;
        void* _is_Member;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    TypeNode* typenode;
    string vartype;
    string predecr;
    string name;
    string postdecr;
    sa_string comments;
    sa_string docs;
    sa_sarray_string annotations;
    Class* mixinsrc;
    string initstr;
    uint32 flags;
    bool init;
    bool destroy;
} Member;
extern ObjClassInfo Member_clsinfo;
#define Member(inst) objInstCheckClass(Member, inst)
#define MemberNone ((Member*)NULL)

typedef struct Member_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_Member_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} Member_WeakRef;
#define Member_WeakRef(inst) objWeakRefCheckClass(Member, inst)

_objfactory_guaranteed Member* Member_create();
// Member* memberCreate();
#define memberCreate() Member_create()

// intptr memberCmp(Member* self, Member* other, uint32 flags);
#define memberCmp(self, other, flags) (self)->_->cmp(Member(self), other, flags)

typedef struct Class {
    union {
        Class_ClassIf* _;
        void* _is_Class;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string name;
    Class* parent;
    Interface* classif;
    sa_Interface implements;
    sa_Class uses;
    sa_Member members;
    sa_Method methods;
    sa_string overrides;
    sa_string docs;
    sa_sarray_string annotations;
    bool abstract;
    bool mixin;
    bool hasinit;
    bool hasdestroy;
    bool initcanfail;
    bool included;
    bool processed;
    bool hasautoinit;
    bool hasautodtors;
    string methodprefix;
    sa_Member allmembers;        // runtime stuff
    sa_Method allmethods;
} Class;
extern ObjClassInfo Class_clsinfo;
#define Class(inst) objInstCheckClass(Class, inst)
#define ClassNone ((Class*)NULL)

typedef struct Class_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_Class_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} Class_WeakRef;
#define Class_WeakRef(inst) objWeakRefCheckClass(Class, inst)

_objfactory_guaranteed Class* Class_create();
// Class* classCreate();
#define classCreate() Class_create()

// intptr classCmp(Class* self, Class* other, uint32 flags);
#define classCmp(self, other, flags) (self)->_->cmp(Class(self), other, flags)

typedef struct ComplexArrayType {
    union {
        ObjIface* _;
        void* _is_ComplexArrayType;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string tname;
    string tsubtype;
} ComplexArrayType;
extern ObjClassInfo ComplexArrayType_clsinfo;
#define ComplexArrayType(inst) objInstCheckClass(ComplexArrayType, inst)
#define ComplexArrayTypeNone ((ComplexArrayType*)NULL)

typedef struct ComplexArrayType_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_ComplexArrayType_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} ComplexArrayType_WeakRef;
#define ComplexArrayType_WeakRef(inst) objWeakRefCheckClass(ComplexArrayType, inst)

_objfactory_guaranteed ComplexArrayType* ComplexArrayType_create();
// ComplexArrayType* complexarraytypeCreate();
#define complexarraytypeCreate() ComplexArrayType_create()


typedef struct StructDef {
    union {
        StructDef_ClassIf* _;
        void* _is_StructDef;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string name;
    sa_Member members;
    sa_string docs;
    sa_sarray_string annotations;
    bool hasinit;
    bool hasdestroy;
    bool included;
    bool processed;
} StructDef;
extern ObjClassInfo StructDef_clsinfo;
#define StructDef(inst) objInstCheckClass(StructDef, inst)
#define StructDefNone ((StructDef*)NULL)

typedef struct StructDef_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_StructDef_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} StructDef_WeakRef;
#define StructDef_WeakRef(inst) objWeakRefCheckClass(StructDef, inst)

_objfactory_guaranteed StructDef* StructDef_create();
// StructDef* structdefCreate();
#define structdefCreate() StructDef_create()

// intptr structdefCmp(StructDef* self, StructDef* other, uint32 flags);
#define structdefCmp(self, other, flags) (self)->_->cmp(StructDef(self), other, flags)

typedef struct StructSetDef {
    union {
        StructSetDef_ClassIf* _;
        void* _is_StructSetDef;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(uintptr) _ref;
    atomic(ptr) _weakref;

    string name;
    sa_string members;
    bool included;
} StructSetDef;
extern ObjClassInfo StructSetDef_clsinfo;
#define StructSetDef(inst) objInstCheckClass(StructSetDef, inst)
#define StructSetDefNone ((StructSetDef*)NULL)

typedef struct StructSetDef_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_StructSetDef_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(uintptr) _ref;
    RWLock _lock;
} StructSetDef_WeakRef;
#define StructSetDef_WeakRef(inst) objWeakRefCheckClass(StructSetDef, inst)

_objfactory_guaranteed StructSetDef* StructSetDef_create();
// StructSetDef* structsetdefCreate();
#define structsetdefCreate() StructSetDef_create()

// intptr structsetdefCmp(StructSetDef* self, StructSetDef* other, uint32 flags);
#define structsetdefCmp(self, other, flags) (self)->_->cmp(StructSetDef(self), other, flags)


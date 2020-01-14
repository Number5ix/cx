#pragma once

#include <cx/core/objiface.h>
#include <cx/container/hashtable.h>
#include <cx/thread/atomic.h>

// A class stores arbitrary per-instance data and may implement one or more interfaces.
// The class info structure contains runtime meta-information about the class.

CX_C_BEGIN

typedef struct ObjClassInfo ObjClassInfo;
typedef struct ObjInst ObjInst;
typedef struct ObjClassInfo {
    // ----- Statically defined in class info instance by class implementer -----
    size_t instsize;            // size of a class instance
    ObjClassInfo *parent;       // parent class
    ObjIface *classif;          // class interface
    ObjIface **ifimpl;          // static list of interface implementations, terminated by NULL pointer

    // init is required to be called at the end of the factory function, just before
    // returning the object to the caller, but after any members are initialized by
    // the factory. If init returns false, construction must aborted and the factory
    // should return NULL.
    // This is useful to have a final check or common object initialization that
    // applies regardless of which factory is used.
    bool(*init)(void *_self);

    // destroy is called just before the object's memory is deallocated
    void(*destroy)(void *_self);

    // this is an abstract class and cannot be instanced
    bool _abstract;

    // ----- Runtime use only, do not set manually! -----
    // internal cache of certain functions so the interface doesn't have to be looked up
    intptr (*_cmp)(void *self, void *other, uint32 flags);
    uint32 (*_hash)(void *self, uint32 flags);
    ObjIface **_impl;           // storage for hydrated implementations
    hashtable _tmpl;            // mapping of interface templates to implementations
} ObjClassInfo;

#define objClassInfoName(cname) cname##_clsinfo

// Generic class instance and implicit base for all dynamic objects
typedef struct ObjInst {
    ObjIface *_classif;         // basically vtable, this is called _ in class instances for less typing
    ObjClassInfo *_clsinfo;
    atomic_intptr _ref;         // reference count

    // user data members here
} ObjInst;

#define objInstCheck(inst) static_assert((inst->_clsinfo, offsetof(*(inst), _ref) == offsetof(ObjInst, _ref)), "Not an instance")
#define objInstBase(inst) ((ObjInst*)(&(inst)->_ref, &(inst)->_clsinfo, (inst)))
#define objClsInfo(inst) (inst->_clsinfo)

// DO NOT CALL DIRECTLY! Use objRelease instead
void _objDestroy(ObjInst *inst);

_meta_inline void _objAcquire(ObjInst *inst)
{
    if (inst)
        atomic_fetch_add_intptr(&inst->_ref, 1, ATOMIC_RELAXED);
}
#define objAcquire(inst) (_objAcquire(objInstBase(inst)), (inst))

_meta_inline void _objRelease(ObjInst *inst)
{
    if (atomic_fetch_sub_intptr(&inst->_ref, 1, ATOMIC_RELEASE) == 1) {
        atomic_fence(ATOMIC_ACQUIRE);
        _objDestroy(inst);
    }
}
#define objRelease(inst) { if (inst) { _objRelease(objInstBase(inst)); (inst) = NULL; } }

// Functions to get a populated interface from a class or instance
ObjIface *_objClassIf(ObjClassInfo *cls, ObjIface *iftmpl);
#define objClassIf(clsname, ifname) ((ifname*)_objClassIf(&objClassInfoName(clsname), objIfBase(&objIfTmplName(ifname))))
ObjIface *_objInstIf(ObjInst *inst, ObjIface *iftmpl);
#define objInstIf(inst, ifname) ((ifname*)_objInstIf(objInstBase(inst), objIfBase(&objIfTmplName(ifname))))

// Dynamic object casting within the hierarchy
// Returns instance pointer if it's safe to cast inst to the given class, NULL otherwise
ObjInst *_objDynCast(ObjInst *inst, ObjClassInfo *cls);
#define objDynCast(inst, clsname) ((clsname*)_objDynCast(objInstBase(inst), &objClassInfoName(clsname)))

CX_C_END

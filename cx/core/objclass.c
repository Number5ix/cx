#include "obj_private.h"
#include "cx/debug/assert.h"
#include "cx/thread/mutex.h"
#include "cx/utils/lazyinit.h"
#include "objstdif.h"

// simple mutex to prevent multiple threads from trying to init a class implemenation at
// the same time
static Mutex classMutex;
static LazyInitState classMutexState;

static void initClassMutex(void *unused)
{
    mutexInit(&classMutex);
}

static void hydrateIfaces(ObjClassInfo *cls, ObjIface ***impls, hashtable *impltbl)
{
    // Start with current class before moving on to parents.
    // The assumption is that classes deeper in the hierarchy will (usually)
    // implement interfaces that are the same or lower than their parents.
    for (ObjIface **iflistp = cls->ifimpl; *iflistp; ++iflistp) {
        _objHydrateIface(*iflistp, impls, impltbl);
    }

    // Hydrate skips functions that are already in the implementation table, so by
    // traversing children first we override parents. We recurse to here to populate
    // the child class's table with any parent class implementations that are not
    // overriden.
    if (cls->parent)
        hydrateIfaces(cls->parent, impls, impltbl);
}

bool _objCheckIfaces(ObjIface ***impls)
{
    for (int i = 0; i < saSize(impls); i++) {
        if (!_objCheckIface((*impls)[i]))
            return false;
    }
    return true;
}

static void classInitImpl(ObjClassInfo *cls, bool locked)
{
    lazyInit(&classMutexState, initClassMutex, NULL);

    if (!locked) {
        mutexAcquire(&classMutex);

        // check and make sure another thread didn't win the race
        if (cls->_impl) {
            mutexRelease(&classMutex);
            return;
        }
    }

    // Runtime class information is populated only once per class during the lifetime
    // of the program. It's okay for this to be moderately expensive in terms of
    // computation, as well as to allocate and never free memory.

    ObjIface **impl = saCreate(ptr, 4, Grow(Minimal));
    cls->_tmpl = htCreate(ptr, ptr, 8, Grow(At50));

    // Fully hydrated interface implementation tables include methods that are
    // implemented by the parent class, but not any children. Fill them in by recursing
    // the hierarchy.
    hydrateIfaces(cls, &impl, &cls->_tmpl);

    // Sanity check
    if (!cls->_abstract)
        devAssertMsg(_objCheckIfaces(&impl), "Interface not fully implemented");

    // Perform some slight of hand and swap the classif from the template to the hydrated
    // method table.
    if (cls->classif)
        relAssert(htFind(&cls->_tmpl, ptr, cls->classif, ptr, &cls->classif));

    // Go ahead and init parent class, in case child class needs to call parent
    // class functions by interface.
    if (cls->parent && !cls->parent->_impl)
        classInitImpl(cls->parent, true);

    // If this class implements Sortable or Hashable (even through a parent), cache the
    // function pointer to avoid having to check them again.
    Sortable *sortableIf;
    if (htFind(&cls->_tmpl, ptr, &Sortable_tmpl, ptr, &sortableIf))
        cls->_cmp = sortableIf->cmp;

    Hashable *hashableIf;
    if (htFind(&cls->_tmpl, ptr, &Hashable_tmpl, ptr, &hashableIf))
        cls->_hash = hashableIf->hash;

    // This must be done immediately before unlocking, since other threads check it
    // without holding the lock to determine if the class needs to be initialized.
    cls->_impl = impl;
    if (!locked)
        mutexRelease(&classMutex);
}

ObjInst *_objInstCreate(ObjClassInfo *cls)
{
    ObjInst *ret;

    if (cls->_abstract) {
        devFatalError("Tried to create an instance of an abstract class");
        return NULL;
    }

    ret = xaAlloc(cls->instsize, Zero);
    ret->_clsinfo = cls;
    atomicStore(intptr, &ret->_ref, 1, Relaxed);

    // Initialize interface implementation tables the first time the class is
    // instantiated
    if (!cls->_impl)
        classInitImpl(cls, false);

    ret->_classif = cls->classif;

    // Note we do NOT call init here. Init functions do not take parameters, so this
    // allows the factory function to set up some per-instance data before calling
    // objInstInit.

    return ret;
}

bool _objInstInit(ObjInst *inst, ObjClassInfo *cls)
{
    bool ret = true;

    // init parent classes first
    if (cls->parent)
        ret &= _objInstInit(inst, cls->parent);

    if (cls->init)
        ret &= cls->init(inst);

    return ret;
}

ObjIface *_objClassIf(ObjClassInfo *cls, ObjIface *iftmpl)
{
    ObjIface *ret = NULL;
    htFind(&cls->_tmpl, ptr, iftmpl, ptr, &ret);
    return ret;
}

ObjIface *_objInstIf(ObjInst *inst, ObjIface *iftmpl)
{
    if (!inst)
        return NULL;

    return _objClassIf(inst->_clsinfo, iftmpl);
}

ObjInst *_objDynCast(ObjInst *inst, ObjClassInfo *cls)
{
    if (!inst)
        return NULL;

    ObjClassInfo *test = inst->_clsinfo;
    while (test) {
        if (test == cls)
            return inst;
        test = test->parent;
    }

    return NULL;
}

static void instDtor(ObjInst *inst, ObjClassInfo *cls)
{
    // call destructors on child classes first
    if (cls->destroy)
        cls->destroy(inst);

    if (cls->parent)
        instDtor(inst, cls->parent);
}

void _objDestroy(ObjInst *inst)
{
    devAssert(atomicLoad(intptr, &inst->_ref, AcqRel) == 0);
    instDtor(inst, inst->_clsinfo);
    xaFree(inst);
}
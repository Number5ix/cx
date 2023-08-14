#include "cx/obj.h"
#include "cx/debug/assert.h"
#include "cx/utils/murmur.h"
#include "objstdif.h"

void stDtor_obj(stype st, stgeneric *gen, uint32 flags)
{
    objRelease(&gen->st_object);
}

void stCopy_obj(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    dest->st_object = objAcquire(src.st_object);
}

intptr stCmp_obj(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    ObjInst* inst1 = gen1.st_object;
    ObjInst* inst2 = gen2.st_object;

    if (inst1->_clsinfo->_cmp)
        return inst1->_clsinfo->_cmp(inst1, inst2, flags);

    // If they only care about equality, a pointer compare will suffice for non-sortable objects
    if (flags & ST_Equality)
        return (intptr)inst1 - (intptr)inst2;

    devFatalError("Tried to sort an unsortable object");
    return -1;
}

uint32 stHash_obj(stype st, stgeneric gen, uint32 flags)
{
    ObjInst *inst = gen.st_object;

    devAssert(inst->_clsinfo->_hash);
    if (inst->_clsinfo->_hash)
        return inst->_clsinfo->_hash(inst, flags);

    devFatalError("Tried to hash an unhashable object");
    return 0;
}

bool stConvert_obj(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags)
{
    ObjInst *inst = src.st_object;

    Convertible *cvtif = objInstIf(src.st_object, Convertible);
    if (cvtif) {
        // zero out dest first
        // stConvert may be used with an uninitialized destination,
        // but the Convertible interface cannot
        memset(dest, 0, stHasFlag(destst, PassPtr) ? sizeof(void *) : stGetSize(destst));
        return cvtif->convert(inst, destst, dest, flags);
    }

    return false;
}

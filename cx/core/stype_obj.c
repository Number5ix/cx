#include "cx/obj.h"
#include "cx/debug/assert.h"
#include "cx/utils/murmur.h"

void stDtor_obj(stype st, stgeneric *stgen, uint32 flags)
{
    ObjInst** inst = (ObjInst**)&stGenVal(object, *stgen);
    objRelease(*inst);
}

void stCopy_obj(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    objAcquire((ObjInst*)stGenVal(object, src));
    stGenVal(object, *dest) = stGenVal(object, src);
}

intptr stCmp_obj(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    ObjInst* inst1 = (ObjInst*)stGenVal(object, stgen1);
    ObjInst* inst2 = (ObjInst*)stGenVal(object, stgen2);

    if (inst1->_clsinfo->_cmp)
        return inst1->_clsinfo->_cmp(inst1, inst2, flags);

    devFatalError("Tried to sort an unsortable object");
    return -1;
}

uint32 stHash_obj(stype st, stgeneric stgen, uint32 flags)
{
    ObjInst *inst = (ObjInst*)stGenVal(object, stgen);

    devAssert(inst->_clsinfo->_hash);
    if (inst->_clsinfo->_hash)
        return inst->_clsinfo->_hash(inst, flags);

    devFatalError("Tried to hash an unhashable object");
    return 0;
}

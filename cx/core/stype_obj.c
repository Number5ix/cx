#include "cx/obj.h"
#include "cx/debug/assert.h"
#include "cx/utils/murmur.h"

void stDtor_obj(stype st, void *ptr, uint32 flags)
{
    objRelease(*(ObjInst**)ptr);
}

void stCopy_obj(stype st, void *dest, const void *src, uint32 flags)
{
    ObjInst** dstinstp = (ObjInst**)dest;
    ObjInst** srcinstp = (ObjInst**)src;
    objAcquire(*srcinstp);
    *dstinstp = *srcinstp;
}

intptr stCmp_obj(stype st, const void *ptr1, const void *ptr2, uint32 flags)
{
    ObjInst* inst1 = *(ObjInst**)ptr1;
    ObjInst* inst2 = *(ObjInst**)ptr2;

    if (inst1->_clsinfo->_cmp)
        return inst1->_clsinfo->_cmp(inst1, inst2, flags);

    devFatalError("Tried to sort an unsortable object");
    return -1;
}

uint32 stHash_obj(stype st, const void *ptr, uint32 flags)
{
    ObjInst *inst = *(ObjInst**)ptr;

    devAssert(inst->_clsinfo->_hash);
    if (inst->_clsinfo->_hash)
        return inst->_clsinfo->_hash(inst, flags);

    devFatalError("Tried to hash an unhashable object");
    return 0;
}

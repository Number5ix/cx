#include "objimpl.h"
#include "cx/utils/murmur.h"

intptr objDefaultCmp(void *_self, void *_other, uint32 flags)
{
    ObjInst *self = (ObjInst*)_self;
    ObjInst *other = (ObjInst*)_other;

    if (objClsInfo(self)->instsize != objClsInfo(other)->instsize)
        return objClsInfo(self)->instsize - objClsInfo(other)->instsize;

    return memcmp((uint8*)self + sizeof(ObjInst),
                  (uint8*)other + sizeof(ObjInst),
                  objClsInfo(self)->instsize);
}

uint32 objDefaultHash(void *_self, uint32 flags)
{
    ObjInst *self = (ObjInst*)_self;

    return hashMurmur3((uint8*)self + sizeof(ObjInst),
                       objClsInfo(self)->instsize);
}

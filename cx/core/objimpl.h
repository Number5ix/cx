#pragma once

#include <cx/core/objiface.h>
#include <cx/core/objclass.h>

// Only use the functions in this file for implementing a class or interface!

// Instance create and init must ONLY be called by factory functions associated
// with the class, not by outside parties.
ObjInst *_objInstCreate(ObjClassInfo *cls);
#define objInstCreate(clsname) (clsname*)_objInstCreate(&objClassInfoName(clsname))
bool _objInstInit(ObjInst *inst, ObjClassInfo *cls);
#define objInstInit(inst) _objInstInit(objInstBase(inst), (inst)->_clsinfo)

intptr objDefaultCmp(void *self, void *other, uint32 flags);
uint32 objDefaultHash(void *self, uint32 flags);

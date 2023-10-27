#pragma once

#include <cx/obj/objiface.h>
#include <cx/obj/objclass.h>

// Only use the functions in this file for implementing a class or interface!

// Instance create and init must ONLY be called by factory functions associated
// with the class, not by outside parties.
_Ret_notnull_ ObjInst *_objInstCreate(_In_ ObjClassInfo *cls);
#define objInstCreate(clsname) (clsname*)_objInstCreate(&objClassInfoName(clsname))
bool _objInstInit(_Inout_ ObjInst *inst, _In_ ObjClassInfo *cls);
#define objInstInit(inst) _objInstInit(objInstBase(inst), (inst)->_clsinfo)

intptr objDefaultCmp(_In_ void *self, _In_ void *other, uint32 flags);
uint32 objDefaultHash(_In_ void *self, uint32 flags);

#define _objfactory _Ret_opt_valid_ _Check_return_

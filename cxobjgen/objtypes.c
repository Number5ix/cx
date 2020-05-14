// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "objtypes.h"
// ==================== Auto-generated section ends ======================

void Param_destroy(Param *self)
{
    // Autogen begins -----
    strDestroy(&self->type);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    strDestroy(&self->postdecr);
    // Autogen ends -------
}

bool Method_init(Method *self)
{
    // Autogen begins -----
    self->params = saCreate(object, 1);
    return true;
    // Autogen ends -------
}

intptr Method_cmp(Method *self, Method *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Method_destroy(Method *self)
{
    // Autogen begins -----
    strDestroy(&self->srcfile);
    strDestroy(&self->returntype);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    saDestroy(&self->params);
    saDestroy(&self->annotations);
    // Autogen ends -------
}

bool Interface_init(Interface *self)
{
    // Autogen begins -----
    self->methods = saCreate(object, 1);
    self->allmethods = saCreate(object, 1);
    return true;
    // Autogen ends -------
}

intptr Interface_cmp(Interface *self, Interface *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Interface_destroy(Interface *self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    saDestroy(&self->methods);
    saDestroy(&self->allmethods);
    // Autogen ends -------
}

intptr Member_cmp(Member *self, Member *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Member_destroy(Member *self)
{
    // Autogen begins -----
    saDestroy(&self->fulltype);
    strDestroy(&self->vartype);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    strDestroy(&self->postdecr);
    saDestroy(&self->annotations);
    strDestroy(&self->initstr);
    // Autogen ends -------
}

bool Class_init(Class *self)
{
    // Autogen begins -----
    self->implements = saCreate(object, 1);
    self->uses = saCreate(object, 1);
    self->members = saCreate(object, 1);
    self->methods = saCreate(object, 1);
    self->overrides = saCreate(string, 1);
    self->allmembers = saCreate(object, 1);
    self->allmethods = saCreate(object, 1);
    return true;
    // Autogen ends -------
}

intptr Class_cmp(Class *self, Class *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Class_destroy(Class *self)
{
    // Autogen begins -----
    strDestroy(&self->name);
    objRelease(self->classif);
    saDestroy(&self->implements);
    saDestroy(&self->uses);
    saDestroy(&self->members);
    saDestroy(&self->methods);
    saDestroy(&self->overrides);
    saDestroy(&self->annotations);
    strDestroy(&self->methodprefix);
    saDestroy(&self->allmembers);
    saDestroy(&self->allmethods);
    // Autogen ends -------
}

Param *Param_create()
{
    Param *ret;
    ret = objInstCreate(Param);
    if (!objInstInit(ret)) {
        xaSFree(ret);
    }
    return ret;
}

Method *Method_create()
{
    Method *ret;
    ret = objInstCreate(Method);
    if (!objInstInit(ret)) {
        xaSFree(ret);
    }
    return ret;
}

Interface *Interface_create()
{
    Interface *ret;
    ret = objInstCreate(Interface);
    if (!objInstInit(ret)) {
        xaSFree(ret);
    }
    return ret;
}

Member *Member_create()
{
    Member *ret;
    ret = objInstCreate(Member);
    if (!objInstInit(ret)) {
        xaSFree(ret);
    }
    return ret;
}

Class *Class_create()
{
    Class *ret;
    ret = objInstCreate(Class);
    if (!objInstInit(ret)) {
        xaSFree(ret);
    }
    return ret;
}

Method *Method_clone(Method *self)
{
    Method *ret = methodCreate();
    ret->srcclass = self->srcclass;
    ret->srcif = self->srcif;
    ret->mixinsrc = self->mixinsrc;
    strDup(&ret->srcfile, self->srcfile);
    strDup(&ret->returntype, self->returntype);
    strDup(&ret->predecr, self->predecr);
    strDup(&ret->name, self->name);
    stCopy(sarray, &ret->params, self->params);
    if (self->annotations)
        stCopy(sarray, &ret->annotations, self->annotations);

    ret->isinit = self->isinit;
    ret->isdestroy = self->isdestroy;
    ret->isfactory = self->isfactory;
    ret->internal = self->internal;
    ret->unbound = self->unbound;
    ret->mixin = self->mixin;
    return ret;
}

// Autogen begins -----
#include "objtypes.auto.inc"
// Autogen ends -------

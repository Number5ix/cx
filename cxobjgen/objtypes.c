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
// ==================== Auto-generated section begins ====================
    strDestroy(&self->type);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    strDestroy(&self->postdecr);
// ==================== Auto-generated section ends ======================
}

bool Method_init(Method *self)
{
// ==================== Auto-generated section begins ====================
    self->params = saCreate(object, 1, 0);
    return true;
// ==================== Auto-generated section ends ======================
}

intptr Method_cmp(Method *self, Method *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Method_destroy(Method *self)
{
// ==================== Auto-generated section begins ====================
    strDestroy(&self->srcfile);
    strDestroy(&self->returntype);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    saDestroy(&self->params);
    saDestroy(&self->annotations);
// ==================== Auto-generated section ends ======================
}

bool Interface_init(Interface *self)
{
// ==================== Auto-generated section begins ====================
    self->methods = saCreate(object, 1, 0);
    self->allmethods = saCreate(object, 1, 0);
    return true;
// ==================== Auto-generated section ends ======================
}

intptr Interface_cmp(Interface *self, Interface *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Interface_destroy(Interface *self)
{
// ==================== Auto-generated section begins ====================
    strDestroy(&self->name);
    saDestroy(&self->methods);
    saDestroy(&self->allmethods);
// ==================== Auto-generated section ends ======================
}

intptr Member_cmp(Member *self, Member *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Member_destroy(Member *self)
{
// ==================== Auto-generated section begins ====================
    saDestroy(&self->fulltype);
    strDestroy(&self->vartype);
    strDestroy(&self->predecr);
    strDestroy(&self->name);
    strDestroy(&self->postdecr);
    saDestroy(&self->annotations);
    strDestroy(&self->initstr);
// ==================== Auto-generated section ends ======================
}

bool Class_init(Class *self)
{
// ==================== Auto-generated section begins ====================
    self->implements = saCreate(object, 1, 0);
    self->uses = saCreate(object, 1, 0);
    self->members = saCreate(object, 1, 0);
    self->methods = saCreate(object, 1, 0);
    self->overrides = saCreate(string, 1, 0);
    self->allmembers = saCreate(object, 1, 0);
    self->allmethods = saCreate(object, 1, 0);
    return true;
// ==================== Auto-generated section ends ======================
}

intptr Class_cmp(Class *self, Class *other, uint32 flags)
{
    // Uncomment unless this function can compare across different object classes
    devAssert(self->_clsinfo == other->_clsinfo);

    return strCmp(self->name, other->name);
}

void Class_destroy(Class *self)
{
// ==================== Auto-generated section begins ====================
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
// ==================== Auto-generated section ends ======================
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
    stCopy(sarray, &ret->params, self->params, 0);
    if (self->annotations)
        stCopy(sarray, &ret->annotations, self->annotations, 0);

    ret->isinit = self->isinit;
    ret->isdestroy = self->isdestroy;
    ret->isfactory = self->isfactory;
    ret->internal = self->internal;
    ret->unbound = self->unbound;
    ret->mixin = self->mixin;
    return ret;
}

// ==================== Auto-generated section begins ====================
Method_ClassIf Method_ClassIf_tmpl = {
    ._size = sizeof(Method_ClassIf),
};

Interface_ClassIf Interface_ClassIf_tmpl = {
    ._size = sizeof(Interface_ClassIf),
};

Member_ClassIf Member_ClassIf_tmpl = {
    ._size = sizeof(Member_ClassIf),
};

Class_ClassIf Class_ClassIf_tmpl = {
    ._size = sizeof(Class_ClassIf),
};

static ObjIface *_ifimpl_Param[] = {
    NULL
};

ObjClassInfo Param_clsinfo = {
    .instsize = sizeof(Param),
    .destroy = (void(*)(void*))Param_destroy,
    .ifimpl = _ifimpl_Param,
};

static Sortable _impl_Method_Sortable = {
    ._size = sizeof(Sortable),
    ._implements = (ObjIface*)&Sortable_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Method_cmp,
};

static Method_ClassIf _impl_Method_Method_ClassIf = {
    ._size = sizeof(Method_ClassIf),
    ._implements = (ObjIface*)&Method_ClassIf_tmpl,
    .clone = (Method *(*)(void*))Method_clone,
    .cmp = (intptr (*)(void*, void*, uint32))Method_cmp,
};

static ObjIface *_ifimpl_Method[] = {
    (ObjIface*)&_impl_Method_Sortable,
    (ObjIface*)&_impl_Method_Method_ClassIf,
    NULL
};

ObjClassInfo Method_clsinfo = {
    .instsize = sizeof(Method),
    .classif = (ObjIface*)&Method_ClassIf_tmpl,
    .init = (bool(*)(void*))Method_init,
    .destroy = (void(*)(void*))Method_destroy,
    .ifimpl = _ifimpl_Method,
};

static Sortable _impl_Interface_Sortable = {
    ._size = sizeof(Sortable),
    ._implements = (ObjIface*)&Sortable_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Interface_cmp,
};

static Interface_ClassIf _impl_Interface_Interface_ClassIf = {
    ._size = sizeof(Interface_ClassIf),
    ._implements = (ObjIface*)&Interface_ClassIf_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Interface_cmp,
};

static ObjIface *_ifimpl_Interface[] = {
    (ObjIface*)&_impl_Interface_Sortable,
    (ObjIface*)&_impl_Interface_Interface_ClassIf,
    NULL
};

ObjClassInfo Interface_clsinfo = {
    .instsize = sizeof(Interface),
    .classif = (ObjIface*)&Interface_ClassIf_tmpl,
    .init = (bool(*)(void*))Interface_init,
    .destroy = (void(*)(void*))Interface_destroy,
    .ifimpl = _ifimpl_Interface,
};

static Sortable _impl_Member_Sortable = {
    ._size = sizeof(Sortable),
    ._implements = (ObjIface*)&Sortable_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Member_cmp,
};

static Member_ClassIf _impl_Member_Member_ClassIf = {
    ._size = sizeof(Member_ClassIf),
    ._implements = (ObjIface*)&Member_ClassIf_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Member_cmp,
};

static ObjIface *_ifimpl_Member[] = {
    (ObjIface*)&_impl_Member_Sortable,
    (ObjIface*)&_impl_Member_Member_ClassIf,
    NULL
};

ObjClassInfo Member_clsinfo = {
    .instsize = sizeof(Member),
    .classif = (ObjIface*)&Member_ClassIf_tmpl,
    .destroy = (void(*)(void*))Member_destroy,
    .ifimpl = _ifimpl_Member,
};

static Sortable _impl_Class_Sortable = {
    ._size = sizeof(Sortable),
    ._implements = (ObjIface*)&Sortable_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Class_cmp,
};

static Class_ClassIf _impl_Class_Class_ClassIf = {
    ._size = sizeof(Class_ClassIf),
    ._implements = (ObjIface*)&Class_ClassIf_tmpl,
    .cmp = (intptr (*)(void*, void*, uint32))Class_cmp,
};

static ObjIface *_ifimpl_Class[] = {
    (ObjIface*)&_impl_Class_Sortable,
    (ObjIface*)&_impl_Class_Class_ClassIf,
    NULL
};

ObjClassInfo Class_clsinfo = {
    .instsize = sizeof(Class),
    .classif = (ObjIface*)&Class_ClassIf_tmpl,
    .init = (bool(*)(void*))Class_init,
    .destroy = (void(*)(void*))Class_destroy,
    .ifimpl = _ifimpl_Class,
};

// ==================== Auto-generated section ends ======================

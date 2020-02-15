// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsvfsdirsearch.h"
// ==================== Auto-generated section ends ======================

VFSVFSDirSearch *VFSVFSDirSearch_create(VFSDirSearch *ds)
{
    VFSVFSDirSearch *ret;
    ret = objInstCreate(VFSVFSDirSearch);
    ret->dirsearch = ds;
    if (!objInstInit(ret))
        objRelease(ret);
    return ret;
}

bool VFSVFSDirSearch_close(VFSVFSDirSearch *self)
{
    if (self->dirsearch)
        vfsSearchClose(self->dirsearch);
    self->dirsearch = NULL;
    return true;
}

FSDirEnt *VFSVFSDirSearch_next(VFSVFSDirSearch *self)
{
    if (!self->dirsearch)
        return NULL;
    return vfsSearchNext(self->dirsearch);
}

void VFSVFSDirSearch_destroy(VFSVFSDirSearch *self)
{
    VFSVFSDirSearch_close(self);
}

// ==================== Auto-generated section begins ====================
VFSVFSDirSearch_ClassIf VFSVFSDirSearch_ClassIf_tmpl = {
    ._size = sizeof(VFSVFSDirSearch_ClassIf),
};

static VFSDirSearchProvider _impl_VFSVFSDirSearch_VFSDirSearchProvider = {
    ._size = sizeof(VFSDirSearchProvider),
    ._implements = (ObjIface*)&VFSDirSearchProvider_tmpl,
    .close = (bool (*)(void*))VFSVFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSVFSDirSearch_next,
};

static VFSVFSDirSearch_ClassIf _impl_VFSVFSDirSearch_VFSVFSDirSearch_ClassIf = {
    ._size = sizeof(VFSVFSDirSearch_ClassIf),
    ._implements = (ObjIface*)&VFSVFSDirSearch_ClassIf_tmpl,
    .close = (bool (*)(void*))VFSVFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSVFSDirSearch_next,
};

static ObjIface *_ifimpl_VFSVFSDirSearch[] = {
    (ObjIface*)&_impl_VFSVFSDirSearch_VFSDirSearchProvider,
    (ObjIface*)&_impl_VFSVFSDirSearch_VFSVFSDirSearch_ClassIf,
    NULL
};

ObjClassInfo VFSVFSDirSearch_clsinfo = {
    .instsize = sizeof(VFSVFSDirSearch),
    .classif = (ObjIface*)&VFSVFSDirSearch_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSVFSDirSearch_destroy,
    .ifimpl = _ifimpl_VFSVFSDirSearch,
};

// ==================== Auto-generated section ends ======================

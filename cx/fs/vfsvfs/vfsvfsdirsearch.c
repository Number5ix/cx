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

// Autogen begins -----
#include "vfsvfsdirsearch.auto.inc"
// Autogen ends -------

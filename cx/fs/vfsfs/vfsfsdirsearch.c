// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsfsdirsearch.h"
// ==================== Auto-generated section ends ======================

VFSFSDirSearch *VFSFSDirSearch_create(FSDirSearch *ds)
{
    VFSFSDirSearch *ret;
    ret = objInstCreate(VFSFSDirSearch);
    ret->dirsearch = ds;
    if (!objInstInit(ret))
        objRelease(ret);
    return ret;
}

bool VFSFSDirSearch_close(VFSFSDirSearch *self)
{
    if (self->dirsearch)
        fsSearchClose(self->dirsearch);
    self->dirsearch = NULL;
    return true;
}

FSDirEnt *VFSFSDirSearch_next(VFSFSDirSearch *self)
{
    if (!self->dirsearch)
        return NULL;
    return fsSearchNext(self->dirsearch);
}

void VFSFSDirSearch_destroy(VFSFSDirSearch *self)
{
    VFSFSDirSearch_close(self);
}

// Autogen begins -----
#include "vfsfsdirsearch.auto.inc"
// Autogen ends -------

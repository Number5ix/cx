// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/core/objstdif.h>
#include <cx/debug/assert.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfsfsfile.h"
// ==================== Auto-generated section ends ======================

VFSFSFile *VFSFSFile_create(FSFile *f)
{
    VFSFSFile *ret;
    ret = objInstCreate(VFSFSFile);
    ret->file = f;
    if (!objInstInit(ret))
        objRelease(ret);
    return ret;
}

bool VFSFSFile_close(VFSFSFile *self)
{
    bool ret = false;
    if (self->file)
        ret = fsClose(self->file);
    self->file = NULL;
    return ret;
}

bool VFSFSFile_read(VFSFSFile *self, void *buf, size_t sz, size_t *bytesread)
{
    if (!self->file)
        return false;
    return fsRead(self->file, buf, sz, bytesread);
}

bool VFSFSFile_write(VFSFSFile *self, void *buf, size_t sz, size_t *byteswritten)
{
    if (!self->file)
        return false;
    return fsWrite(self->file, buf, sz, byteswritten);
}

int64 VFSFSFile_tell(VFSFSFile *self)
{
    if (!self->file)
        return -1;
    return fsTell(self->file);
}

int64 VFSFSFile_seek(VFSFSFile *self, int64 off, int seektype)
{
    if (!self->file)
        return -1;
    return fsSeek(self->file, off, seektype);
}

bool VFSFSFile_flush(VFSFSFile *self)
{
    if (!self->file)
        return false;
    return fsFlush(self->file);
}

void VFSFSFile_destroy(VFSFSFile *self)
{
    VFSFSFile_close(self);
}

// Autogen begins -----
#include "vfsfsfile.auto.inc"
// Autogen ends -------

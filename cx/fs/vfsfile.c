// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fs/vfsfile.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "vfs_private.h"
#include "cx/debug/error.h"

_Use_decl_annotations_
VFSFile* vfsOpen(VFS* vfs, strref path, flags_t flags)
{
    VFSFile* ret = 0;
    File* innerfile  = 0;

    string rpath       = 0;
    VFSMount* cowmount = 0;
    string cowrpath    = 0;
    uint32 pflags      = VFS_FindCache;

    // get the provider
    if (flags & (FS_Write | FS_Truncate | FS_Create))
        pflags |= VFS_FindWriteFile;
    if (flags & (FS_Truncate | FS_Create))
        pflags |= VFS_FindCreate;
    VFSMount* m = _vfsFindMount(vfs, &rpath, path, &cowmount, &cowrpath, pflags);
    if (!m) {
        cxerr = CX_FileNotFound;
        goto out;
    }

    if ((pflags & VFS_FindWriteFile) && (m->flags & VFS_ReadOnly) && !cowmount) {
        // we're trying to write to a read-only VFS and don't have a COW provider...
        cxerr = CX_ReadOnly;
        goto out;
    } else if (cowmount) {
        // initial open is read-only for COW files
        flags = FS_Read;
    }

    VFSProvider* provif = objInstIf(m->provider, VFSProvider);
    if (!provif) {
        cxerr = CX_InvalidArgument;
        goto out;
    }

    innerfile = provif->open(m->provider, rpath, flags);
    if (!innerfile) {
        // failed to actually open the file, cxerr set by provider
        _vfsInvalidateCache(vfs, path);
        goto out;
    }

    ret = vfsfileCreate(vfs, innerfile);
    if (cowmount) {
        ret->cowprov = objAcquire(cowmount->provider);
        strDup(&ret->cowrpath, cowrpath);
        rwlockAcquireRead(&vfs->vfslock);
        _vfsAbsPath(vfs, &ret->cowpath, path);
        rwlockReleaseRead(&vfs->vfslock);
    }

out:
    objRelease(&innerfile);
    strDestroy(&rpath);
    strDestroy(&cowrpath);
    objRelease(&m);
    objRelease(&cowmount);
    return ret;
}

_objfactory_guaranteed VFSFile* VFSFile_create(VFS* vfs, File* inner)
{
    VFSFile* self;
    self = objInstCreate(VFSFile);

    self->vfs   = objAcquire(vfs);
    self->inner = objAcquire(inner);

    objInstInit(self);
    return self;
}

bool VFSFile_close(_In_ VFSFile* self)
{
    if (!self->inner)
        return true;   // already closed

    // The provider's close is where buffered writes are flushed, so a failure there is the one
    // thing this function's return value has to carry.
    bool ret = fileClose(self->inner);
    objRelease(&self->inner);
    return ret;
}

bool VFSFile_read(_In_ VFSFile* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf,
                      size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread)
{
    if (!self->inner) {
        *bytesread = 0;
        return false;
    }

    return fileRead(self->inner, buf, sz, bytesread);
}

static void vfsCOWCreateAll(_Inout_ ObjInst* cowprov, _Inout_ VFSProvider* cowprovif,
                            _In_opt_ strref path)
{
    string parent = 0;
    pathParent(&parent, path);
    if (!strEmpty(parent) && cowprovif->stat(cowprov, parent, NULL) == FS_Nonexistent)
        vfsCOWCreateAll(cowprov, cowprovif, parent);
    strDestroy(&parent);

    if (cowprovif->stat(cowprov, path, NULL) == FS_Nonexistent)
        cowprovif->createDir(cowprov, path);
}

#define COWBLOCKSIZE 65536
static bool vfsCOWFile(_Inout_ VFSFile* file)
{
    File* cowfile          = 0;
    VFSProvider* cowprovif = 0;
    uint8* buf             = 0;
    bool ret               = false;
    size_t bytes;

    // No lock is taken here. The fields this touches -- inner and cowprov -- are per-file state
    // that every other file operation reads unlocked too, so a VFS-wide lock protected nothing
    // and stalled every other thread for the length of the copy. A file handle is single-owner;
    // see vfsOpen.

    if (!file->cowprov)
        return true;   // already copied

    buf       = xaAlloc(COWBLOCKSIZE);
    cowprovif = objInstIf(file->cowprov, VFSProvider);
    if (!cowprovif)
        goto out;

    // make sure the path exists
    string dirname = 0;
    pathParent(&dirname, file->cowrpath);
    vfsCOWCreateAll(file->cowprov, cowprovif, dirname);
    strDestroy(&dirname);

    // create the writable file
    cowfile = cowprovif->open(file->cowprov, file->cowrpath, FS_Write | FS_Create | FS_Truncate);
    if (!cowfile)
        goto out;

    int64 curpos = fileTell(file->inner);
    fileSeek(file->inner, 0, FS_Set);

    // copy contents to new file
    for (;;) {
        if (!fileRead(file->inner, buf, COWBLOCKSIZE, &bytes))
            goto out;
        if (bytes == 0)
            break;   // eof
        if (!fileWrite(cowfile, buf, bytes, NULL))
            goto out;
    }

    // file data is copied, now reset file pointer and swap the files around
    fileSeek(cowfile, curpos, FS_Set);
    fileClose(file->inner);
    objRelease(&file->inner);
    file->inner = cowfile;
    cowfile     = NULL;
    objRelease(&file->cowprov);
    ret = true;

out:
    if (!ret) {
        // Failed partway through, so throw away the half-written copy and the handle it was
        // going to replace. The file is left unusable rather than silently reading from the
        // layer the caller was trying to write past.
        if (cowfile) {
            objRelease(&cowfile);
            cowprovif->deleteFile(file->cowprov, file->cowrpath);
        }
        objRelease(&file->cowprov);
        objRelease(&file->inner);
    }
    xaFree(buf);

    if (ret)
        _vfsInvalidateCache(file->vfs, file->cowpath);
    return ret;
}

bool VFSFile_write(_In_ VFSFile* self, _In_reads_bytes_(sz) const void* buf, size_t sz,
                       _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten)
{
    if (!self->inner) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }

    if (self->cowprov) {
        if (!vfsCOWFile(self)) {
            if (byteswritten)
                *byteswritten = 0;
            return false;
        }
    }

    return fileWrite(self->inner, buf, sz, byteswritten);
}

int64 VFSFile_tell(_In_ VFSFile* self)
{
    if (!self->inner)
        return -1;
    return fileTell(self->inner);
}

int64 VFSFile_seek(_In_ VFSFile* self, int64 off, FSSeekType seektype)
{
    if (!self->inner)
        return -1;
    return fileSeek(self->inner, off, seektype);
}

bool VFSFile_flush(_In_ VFSFile* self)
{
    if (!self->inner)
        return false;
    return fileFlush(self->inner);
}

void VFSFile_destroy(_In_ VFSFile* self)
{
    VFSFile_close(self);

    // Autogen begins -----
    objRelease(&self->vfs);
    objRelease(&self->inner);
    objRelease(&self->cowprov);
    strDestroy(&self->cowpath);
    strDestroy(&self->cowrpath);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "fs/vfsfile.auto.inc"
// clang-format on
// Autogen ends -------

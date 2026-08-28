#include "vfs_private.h"
#include "cx/debug/error.h"

_Use_decl_annotations_
VFSFile* vfsOpen(VFS* vfs, strref path, flags_t flags)
{
    VFSFile* ret = 0;

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

    // finally actually set up the VFSFile structure
    ret             = xaAlloc(sizeof(VFSFile), XA_Zero);
    ret->fileprov   = provif->open(m->provider, rpath, flags);
    ret->fileprovif = objInstIf(ret->fileprov, VFSFileProvider);
    if (!(ret->fileprov && ret->fileprovif)) {
        // failed to actually open the file, cxerr set by provider
        xaDestroy(&ret);
        _vfsInvalidateCache(vfs, path);
        goto out;
    }
    if (cowmount) {
        ret->cowprov = objAcquire(cowmount->provider);
        strDup(&ret->cowrpath, cowrpath);
        rwlockAcquireRead(&vfs->vfslock);
        _vfsAbsPath(vfs, &ret->cowpath, path);
        rwlockReleaseRead(&vfs->vfslock);
    }
    ret->vfs = objAcquire(vfs);

out:
    strDestroy(&rpath);
    strDestroy(&cowrpath);
    objRelease(&m);
    objRelease(&cowmount);
    return ret;
}

_Use_decl_annotations_
bool vfsClose(VFSFile* file)
{
    if (!file)
        return false;

    // The provider's close is where buffered writes are flushed, so a failure there is the one
    // thing this function's return value has to carry.
    bool ret = true;
    if (file->fileprov)
        ret = file->fileprovif->close(file->fileprov);

    objRelease(&file->fileprov);
    objRelease(&file->cowprov);
    objRelease(&file->vfs);
    strDestroy(&file->cowpath);
    strDestroy(&file->cowrpath);
    xaFree(file);
    return ret;
}

_Use_decl_annotations_
bool vfsRead(VFSFile* file, void* buf, size_t sz, size_t* bytesread)
{
    if (!(file && file->fileprov)) {
        *bytesread = 0;
        return false;
    }

    return file->fileprovif->read(file->fileprov, buf, sz, bytesread);
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
    ObjInst* cowfile       = 0;
    VFSProvider* cowprovif = 0;
    uint8* buf             = 0;
    bool ret               = false;
    size_t bytes;

    // No lock is taken here. The fields this touches -- fileprov, cowprov and the provider
    // interfaces beside them -- are per-VFSFile state that every other file operation reads
    // unlocked too, so a VFS-wide lock protected nothing and stalled every other thread for the
    // length of the copy. A VFSFile is single-owner; see vfsOpen.

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
    VFSFileProvider* cowfileif = objInstIf(cowfile, VFSFileProvider);
    if (!cowfileif)
        goto out;

    int64 curpos = file->fileprovif->tell(file->fileprov);
    file->fileprovif->seek(file->fileprov, 0, FS_Set);

    // copy contents to new file
    for (;;) {
        if (!file->fileprovif->read(file->fileprov, buf, COWBLOCKSIZE, &bytes))
            goto out;
        if (bytes == 0)
            break;   // eof
        if (!cowfileif->write(cowfile, buf, bytes, NULL))
            goto out;
    }

    // file data is copied, now reset file pointer and swap the providers around
    cowfileif->seek(cowfile, curpos, FS_Set);
    file->fileprovif->close(file->fileprov);
    objRelease(&file->fileprov);
    file->fileprov   = cowfile;
    file->fileprovif = cowfileif;
    cowfile          = NULL;
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
        objRelease(&file->fileprov);
        file->fileprovif = NULL;
    }
    xaFree(buf);

    if (ret)
        _vfsInvalidateCache(file->vfs, file->cowpath);
    return ret;
}

_Use_decl_annotations_
bool vfsWrite(VFSFile* file, const void* buf, size_t sz, size_t* byteswritten)
{
    if (!(file && file->fileprov)) {
        if (byteswritten)
            *byteswritten = 0;
        return false;
    }

    if (file->cowprov) {
        if (!vfsCOWFile(file)) {
            if (byteswritten)
                *byteswritten = 0;
            return false;
        }
    }

    return file->fileprovif->write(file->fileprov, buf, sz, byteswritten);
}

_Use_decl_annotations_
bool vfsWriteString(VFSFile* file, strref str, size_t* byteswritten)
{
    size_t written = 0, wstep = 0;
    bool ret = true;

    striter iter;
    striBorrow(&iter, str);
    while (iter.len > 0) {
        if (!vfsWrite(file, iter.bytes, iter.len, &wstep)) {
            ret = false;
            break;
        }
        written += wstep;
        striNext(&iter);
    }

    if (byteswritten)
        *byteswritten = written;
    return ret;
}

_Use_decl_annotations_
int64 vfsTell(VFSFile* file)
{
    if (!(file && file->fileprov))
        return -1;
    return file->fileprovif->tell(file->fileprov);
}

_Use_decl_annotations_
int64 vfsSeek(VFSFile* file, int64 off, FSSeekType seektype)
{
    if (!(file && file->fileprov))
        return -1;
    return file->fileprovif->seek(file->fileprov, off, seektype);
}

_Use_decl_annotations_
bool vfsFlush(VFSFile* file)
{
    if (!(file && file->fileprov))
        return false;
    return file->fileprovif->flush(file->fileprov);
}

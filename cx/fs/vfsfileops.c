#include "vfs_private.h"
#include "cx/core/error.h"

VFSFile *_vfsOpen(VFS *vfs, strref path, int flags)
{
    VFSFile *ret = 0;

    string rpath = 0;
    VFSMount *cowmount = 0;
    string cowrpath = 0;
    uint32 pflags = VFS_FindCache;

    // get the provider
    if (flags & (FS_Write | FS_Truncate | FS_Create))
        pflags |= VFS_FindWriteFile;
    if (flags & FS_Truncate)
        pflags |= VFS_FindCreate;
    VFSMount *m = _vfsFindMount(vfs, &rpath, path, &cowmount, &cowrpath, pflags);
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

    VFSProvider *provif = objInstIf(m->provider, VFSProvider);
    if (!provif) {
        cxerr = CX_InvalidArgument;
        goto out;
    }

    // finally actually set up the VFSFile structure
    ret = xaAlloc(sizeof(VFSFile), Zero);
    ret->fileprov = provif->open(m->provider, rpath, flags);
    ret->fileprovif = objInstIf(ret->fileprov, VFSFileProvider);
    if (!(ret->fileprov && ret->fileprovif)) {
        // failed to actually open the file, cxerr set by provider
        xaSFree(ret);
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
    return ret;
}

bool vfsClose(VFSFile *file)
{
    if (!file)
        return false;

    objRelease(&file->fileprov);
    objRelease(&file->cowprov);
    objRelease(&file->vfs);
    strDestroy(&file->cowpath);
    strDestroy(&file->cowrpath);
    xaFree(file);
    return true;
}

bool vfsRead(VFSFile *file, void *buf, size_t sz, size_t *bytesread)
{
    if (!(file && file->fileprov))
        return false;

    return file->fileprovif->read(file->fileprov, buf, sz, bytesread);
}

static void vfsCOWCreateAll(ObjInst *cowprov, VFSProvider *cowprovif, strref path)
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
static bool vfsCOWFile(VFSFile *file)
{
    ObjInst *cowfile = 0;
    size_t bytes;

    rwlockAcquireRead(&file->vfs->vfsdlock);
    rwlockAcquireWrite(&file->vfs->vfslock);

    if (!file->cowprov) {
        // another thread beat us to the lock
        rwlockReleaseWrite(&file->vfs->vfslock);
        rwlockReleaseRead(&file->vfs->vfsdlock);
        return true;
    }

    uint8 *buf = xaAlloc(COWBLOCKSIZE);
    VFSProvider *cowprovif = objInstIf(file->cowprov, VFSProvider);
    if (!cowprovif)
        goto error;

    // make sure the path exists
    string dirname = 0;
    pathParent(&dirname, file->cowrpath);
    vfsCOWCreateAll(file->cowprov, cowprovif, dirname);
    strDestroy(&dirname);

    // create the writable file
    cowfile = cowprovif->open(file->cowprov, file->cowrpath, FS_Write | FS_Create | FS_Truncate);
    if (!cowfile)
        goto error;
    VFSFileProvider *cowfileif = objInstIf(cowfile, VFSFileProvider);

    int64 curpos = file->fileprovif->tell(file->fileprov);
    file->fileprovif->seek(file->fileprov, 0, FS_Set);

    // copy contents to new file
    for (;;) {
        if (!file->fileprovif->read(file->fileprov, buf, COWBLOCKSIZE, &bytes))
            goto error;
        if (bytes == 0)
            break;      // eof
        if (!cowfileif->write(cowfile, buf, bytes, NULL))
            goto error;
    }

    // file data is copied, now reset file pointer and swap the providers around
    cowfileif->seek(cowfile, curpos, FS_Set);
    file->fileprovif->close(file->fileprov);
    objRelease(&file->fileprov);
    file->fileprov = cowfile;
    file->fileprovif = cowfileif;
    objRelease(&file->cowprov);
    xaFree(buf);
    rwlockReleaseWrite(&file->vfs->vfslock);
    rwlockReleaseRead(&file->vfs->vfsdlock);

    _vfsInvalidateCache(file->vfs, file->cowpath);
    return true;

error:
    if (cowfile) {
        objRelease(&cowfile);
        if (cowprovif)
            cowprovif->deleteFile(file->cowprov, file->cowrpath);
    }
    objRelease(&file->cowprov);
    objRelease(&file->fileprov);
    xaFree(buf);
    return false;
}

bool vfsWrite(VFSFile *file, void *buf, size_t sz, size_t *byteswritten)
{
    if (!(file && file->fileprov))
        return false;

    if (file->cowprov) {
        if (!vfsCOWFile(file))
            return false;
    }

    return file->fileprovif->write(file->fileprov, buf, sz, byteswritten);
}

bool vfsWriteString(VFSFile *file, strref str, size_t *byteswritten)
{
    size_t written = 0, wstep = 0;
    bool ret = true;

    striter iter;
    striBorrow(&iter, str);
    while(iter.len > 0) {
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

int64 vfsTell(VFSFile *file)
{
    if (!(file && file->fileprov))
        return -1;
    return file->fileprovif->tell(file->fileprov);
}

int64 vfsSeek(VFSFile *file, int64 off, int seektype)
{
    if (!(file && file->fileprov))
        return -1;
    return file->fileprovif->seek(file->fileprov, off, seektype);
}

bool vfsFlush(VFSFile *file)
{
    if (!(file && file->fileprov))
        return false;
    return file->fileprovif->flush(file->fileprov);
}

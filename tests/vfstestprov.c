// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "vfstestprov.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include <cx/fs/path.h>
#include <cx/fs/vfs.h>
#include <cx/time/clock.h>

// Provider paths arrive relative to the mount point, with "" meaning the mount root. Strip a
// leading separator anyway so a test can spell a path either way.
static void tpPath(_Inout_ string* out, _In_opt_ strref path)
{
    if (strGetChar(path, 0) == '/')
        strSubStr(out, path, 1, strEnd);
    else
        strDup(out, path);
}

static bool tpCaseSensitive(_In_ VFSTestProv* self)
{
    return (self->provflags & VFS_CaseSensitive) != 0;
}

static bool tpEq(_In_ VFSTestProv* self, _In_opt_ strref a, _In_opt_ strref b)
{
    return tpCaseSensitive(self) ? strEq(a, b) : strEqi(a, b);
}

static void tpFillStat(_In_ VFSTestProv* self, _Out_ FSStat* stat, uint64 size)
{
    stat->size     = size;
    stat->created  = self->mtime;
    stat->modified = self->mtime;
    stat->accessed = self->mtime;
}

_objfactory_guaranteed VFSTestProv* VFSTestProv_create(uint32 provflags)
{
    VFSTestProv* self;
    self = objInstCreate(VFSTestProv);

    self->provflags = provflags;
    self->mtime     = 1000000;
    flags_t htflags = (provflags & VFS_CaseSensitive) ? 0 : HT_CaseInsensitive;
    htInit(&self->files, string, string, 8, htflags);
    htInit(&self->dirs, string, int32, 8, htflags);

    objInstInit(self);
    return self;
}

flags_t VFSTestProv_flags(_In_ VFSTestProv* self)
{
    return self->provflags;
}

_Ret_opt_valid_ ObjInst* VFSTestProv_open(_In_ VFSTestProv* self, _In_opt_ strref path, flags_t flags)
{
    string rpath = 0;
    tpPath(&rpath, path);

    bool writing = (flags & (FS_Write | FS_Create | FS_Truncate)) != 0;
    if (writing && (self->failmask & VFSTP_FailOpenWrite))
        goto fail;

    if (!htHasKey(self->files, string, rpath)) {
        if (!(flags & FS_Create))
            goto fail;
        htInsert(&self->files, string, rpath, string, _S);
    } else if (flags & FS_Truncate) {
        htInsert(&self->files, string, rpath, string, _S);
    }

    VFSTestProvFile* fileprov = vfstestprovfileCreate(self, rpath, writing);
    strDestroy(&rpath);
    return objInstBase(fileprov);

fail:
    strDestroy(&rpath);
    return NULL;
}

FSPathStat VFSTestProv_stat(_In_ VFSTestProv* self, _In_opt_ strref path, _When_(return != FS_Nonexistent, _Out_opt_) FSStat* stat)
{
    string rpath = 0, contents = 0;
    int ret      = FS_Nonexistent;
    tpPath(&rpath, path);

    if (htFind(self->files, string, rpath, string, &contents)) {
        ret = FS_File;
        if (stat)
            tpFillStat(self, stat, strLen(contents));
    } else if (strEmpty(rpath) || htHasKey(self->dirs, string, rpath)) {
        ret = FS_Directory;
        if (stat)
            tpFillStat(self, stat, 0);
    }

    strDestroy(&contents);
    strDestroy(&rpath);
    return ret;
}

bool VFSTestProv_setTimes(_In_ VFSTestProv* self, _In_opt_ strref path, int64 modified, int64 accessed)
{
    // One timestamp for the whole provider is all the tests need.
    self->mtime = modified;
    return true;
}

bool VFSTestProv_createDir(_In_ VFSTestProv* self, _In_opt_ strref path)
{
    if (self->failmask & VFSTP_FailCreateDir)
        return false;

    vfstestprovAddDir(self, path);
    return true;
}

bool VFSTestProv_removeDir(_In_ VFSTestProv* self, _In_opt_ strref path)
{
    string rpath = 0;
    tpPath(&rpath, path);
    bool ret = htRemove(&self->dirs, string, rpath);
    strDestroy(&rpath);
    return ret;
}

bool VFSTestProv_deleteFile(_In_ VFSTestProv* self, _In_opt_ strref path)
{
    string rpath = 0;
    tpPath(&rpath, path);
    bool ret = htRemove(&self->files, string, rpath);
    strDestroy(&rpath);
    return ret;
}

bool VFSTestProv_rename(_In_ VFSTestProv* self, _In_opt_ strref oldpath, _In_opt_ strref newpath)
{
    string rold = 0, rnew = 0, contents = 0;
    bool ret = false;

    tpPath(&rold, oldpath);
    tpPath(&rnew, newpath);

    if (htFind(self->files, string, rold, string, &contents)) {
        htRemove(&self->files, string, rold);
        htInsert(&self->files, string, rnew, string, contents);
        ret = true;
    }

    strDestroy(&contents);
    strDestroy(&rold);
    strDestroy(&rnew);
    return ret;
}

bool VFSTestProv_getFSPath(_In_ VFSTestProv* self, _Inout_ string* out, _In_opt_ strref path)
{
    // Not backed by the OS filesystem, so there is no real path to hand back.
    return false;
}

// searchInit positions the iterator on the first entry, so it needs searchNext.
bool VFSTestProv_searchNext(_In_ VFSTestProv* self, _Inout_ FSSearchIter* iter);

typedef struct VFSTPSearch {
    sa_string names;
    sa_int32 types;
    int32 idx;
} VFSTPSearch;

// Collects the immediate children of dpath out of one of the provider's tables.
static void tpCollect(_In_ VFSTestProv* self, _Inout_ VFSTPSearch* search, _In_ hashtable tbl,
                      int type, _In_opt_ strref dpath, _In_opt_ strref pattern)
{
    string parent = 0, name = 0;

    foreach (hashtable, hti, tbl) {
        strref key = htiKey(string, hti);

        strClear(&parent);
        pathParent(&parent, key);   // leaves parent empty for a top-level entry
        if (!tpEq(self, parent, dpath))
            continue;

        pathFilename(&name, key);
        // Providers match the pattern against the bare entry name, the way the platform
        // filesystem search does.
        if (!strEmpty(pattern) && !pathMatch(name, pattern, 0))
            continue;

        saPush(&search->names, string, name);
        saPush(&search->types, int32, type);
    }

    strDestroy(&parent);
    strDestroy(&name);
}

bool VFSTestProv_searchInit(_In_ VFSTestProv* self, _Out_ FSSearchIter* iter, _In_opt_ strref path, _In_opt_ strref pattern, bool stat)
{
    memset(iter, 0, sizeof(FSSearchIter));

    if (self->failmask & VFSTP_FailSearchInit)
        return false;

    string rpath = 0;
    tpPath(&rpath, path);

    if (!(strEmpty(rpath) || htHasKey(self->dirs, string, rpath))) {
        strDestroy(&rpath);
        return false;
    }

    VFSTPSearch* search = xaAlloc(sizeof(VFSTPSearch), XA_Zero);
    saInit(&search->names, string, 8);
    saInit(&search->types, int32, 8);
    tpCollect(self, search, self->dirs, FS_Directory, rpath, pattern);
    tpCollect(self, search, self->files, FS_File, rpath, pattern);
    strDestroy(&rpath);

    if (saSize(search->names) == 0) {
        saDestroy(&search->names);
        saDestroy(&search->types);
        xaFree(search);
        return false;
    }

    iter->_search = search;
    search->idx   = -1;
    return VFSTestProv_searchNext(self, iter);
}

bool VFSTestProv_searchValid(_In_ VFSTestProv* self, _In_ FSSearchIter* iter)
{
    return iter->name != NULL;
}

bool VFSTestProv_searchNext(_In_ VFSTestProv* self, _Inout_ FSSearchIter* iter)
{
    VFSTPSearch* search = (VFSTPSearch*)iter->_search;
    if (!search)
        return false;

    strDestroy(&iter->name);
    search->idx++;
    if (search->idx >= saSize(search->names))
        return false;

    strDup(&iter->name, search->names.a[search->idx]);
    iter->type = search->types.a[search->idx];

    string contents = 0;
    if (htFind(self->files, string, iter->name, string, &contents))
        tpFillStat(self, &iter->stat, strLen(contents));
    else
        tpFillStat(self, &iter->stat, 0);
    strDestroy(&contents);

    return true;
}

void VFSTestProv_searchFinish(_In_ VFSTestProv* self, _Inout_ FSSearchIter* iter)
{
    VFSTPSearch* search = (VFSTPSearch*)iter->_search;

    strDestroy(&iter->name);
    if (!search)
        return;

    saDestroy(&search->names);
    saDestroy(&search->types);
    xaDestroy(&iter->_search);
}

void VFSTestProv_destroy(_In_ VFSTestProv* self)
{
    // Autogen begins -----
    htDestroy(&self->files);
    htDestroy(&self->dirs);
    // Autogen ends -------
}

void VFSTestProv_addFile(_In_ VFSTestProv* self, _In_opt_ strref path, _In_opt_ strref contents)
{
    string rpath = 0, parent = 0;

    tpPath(&rpath, path);
    if (pathParent(&parent, rpath))
        vfstestprovAddDir(self, parent);
    htInsert(&self->files, string, rpath, strref, contents);

    strDestroy(&parent);
    strDestroy(&rpath);
}

void VFSTestProv_addDir(_In_ VFSTestProv* self, _In_opt_ strref path)
{
    string rpath = 0, parent = 0;

    tpPath(&rpath, path);
    if (strEmpty(rpath))
        goto out;   // the root always exists

    if (pathParent(&parent, rpath))
        vfstestprovAddDir(self, parent);
    htInsert(&self->dirs, string, rpath, int32, 1);

out:
    strDestroy(&parent);
    strDestroy(&rpath);
}

_objfactory_guaranteed VFSTestProvFile* VFSTestProvFile_create(VFSTestProv* prov, _In_opt_ strref path, bool writable)
{
    VFSTestProvFile* self;
    self = objInstCreate(VFSTestProvFile);

    self->prov     = objAcquire(prov);
    self->writable = writable;
    strDup(&self->path, path);

    objInstInit(self);
    return self;
}

bool VFSTestProvFile_close(_In_ VFSTestProvFile* self)
{
    return !(self->prov->failmask & VFSTP_FailClose);
}

bool VFSTestProvFile_read(_In_ VFSTestProvFile* self, _Out_writes_bytes_to_(sz, *bytesread) void* buf, size_t sz, _Out_ _Deref_out_range_(0, sz) size_t* bytesread)
{
    string contents = 0;
    *bytesread      = 0;

    if (self->prov->failmask & VFSTP_FailRead)
        return false;

    if (!htFind(self->prov->files, string, self->path, string, &contents))
        return false;

    uint32 len = strLen(contents);
    if (self->pos < len) {
        uint32 avail = len - (uint32)self->pos;
        *bytesread   = strCopyRaw(contents, (uint32)self->pos, (uint8*)buf,
                                  (uint32)min((size_t)avail, sz));
    }
    self->pos += *bytesread;

    strDestroy(&contents);
    return true;
}

bool VFSTestProvFile_write(_In_ VFSTestProvFile* self, _In_reads_bytes_(sz) const void* buf, size_t sz, _Out_opt_ _Deref_out_range_(0, sz) size_t* byteswritten)
{
    string contents = 0, pre = 0, mid = 0, post = 0, ncontents = 0;
    bool ret = false;

    if (byteswritten)
        *byteswritten = 0;

    if (!self->writable || (self->prov->failmask & VFSTP_FailWrite))
        return false;

    htFind(self->prov->files, string, self->path, string, &contents);
    uint32 len = strLen(contents);

    strSubStr(&pre, contents, 0, min((uint32)self->pos, len));
    strFromBytes(&mid, buf, (uint32)sz);
    if (self->pos + (int64)sz < (int64)len)
        strSubStr(&post, contents, (uint32)(self->pos + (int64)sz), strEnd);
    strNConcat(&ncontents, pre, mid, post);

    htInsert(&self->prov->files, string, self->path, string, ncontents);
    self->pos += (int64)sz;
    if (byteswritten)
        *byteswritten = sz;
    ret = true;

    strDestroy(&ncontents);
    strDestroy(&post);
    strDestroy(&mid);
    strDestroy(&pre);
    strDestroy(&contents);
    return ret;
}

int64 VFSTestProvFile_tell(_In_ VFSTestProvFile* self)
{
    return self->pos;
}

int64 VFSTestProvFile_seek(_In_ VFSTestProvFile* self, int64 off, FSSeekType seektype)
{
    string contents = 0;
    htFind(self->prov->files, string, self->path, string, &contents);

    switch (seektype) {
    case FS_Set:
        self->pos = off;
        break;
    case FS_Cur:
        self->pos += off;
        break;
    case FS_End:
        self->pos = strLen(contents) + off;
        break;
    }
    self->pos = clamplow(self->pos, 0);

    strDestroy(&contents);
    return self->pos;
}

bool VFSTestProvFile_flush(_In_ VFSTestProvFile* self)
{
    return true;   // writes go straight into the provider's table
}

void VFSTestProvFile_destroy(_In_ VFSTestProvFile* self)
{
    // Autogen begins -----
    objRelease(&self->prov);
    strDestroy(&self->path);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "vfstestprov.auto.inc"
// clang-format on
// Autogen ends -------

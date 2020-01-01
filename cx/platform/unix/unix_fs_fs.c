#include "unix_time.h"
#include "cx/fs/fs_private.h"
#include "cx/string.h"
#include "cx/utils/lazyinit.h"
#include "cx/container/sarray.h"
#include "cx/thread/rwlock.h"
#include "cx/platform/unix.h"

#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#ifdef _PLATFORM_FREEBSD
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <sys/stat.h>

static LazyInitState fsCurDirInit;
RWLock *_fsCurDirLock;
string _fsCurDir = 0;
string fsPlatformPathSepStr = _S"/";

static void initCurDir(void *data)
{
    char *buf = 0, *pcur = 0;
    size_t bufsz = PATH_MAX / 2;

    _fsCurDirLock = rwlockCreate();

    // POSIX doesn't actually limit this to PATH_MAX, so expand as necessary
    do {
        xaSFree(buf);
        bufsz *= 2;
        buf = xaAlloc(bufsz, 0);

        pcur = getcwd(buf, bufsz);
    } while (!pcur && errno == ERANGE);

    if (!pcur) {
        // failed??? punt...
        xaSFree(buf);
        strCopy(&_fsCurDir, _S"/");
        return;
    }

    pathFromPlatform(&_fsCurDir, buf);
    pathNormalize(&_fsCurDir);
    xaSFree(buf);
}

void pathFromPlatform(string *out, string platformpath)
{
    // not really much to do here...
    strDup(out, platformpath);
}

void pathToPlatform(string *out, string path)
{
    string ns = 0, rpath = 0;
    pathSplitNS(&ns, &rpath, path);
    // namespaces have no meaning in UNIX paths, so just
    // discard it and use the path part if there is one
    strDestroy(&ns);
    strDestroy(out);
    *out = rpath;
}

bool pathMakeAbsolute(string *out, string path)
{
    if (pathIsAbsolute(path)) {
        strDup(out, path);
        return true;
    }
    lazyInit(&fsCurDirInit, initCurDir, NULL);

    string tmp = 0;
    rwlockAcquireRead(_fsCurDirLock);
    pathJoin(&tmp, _fsCurDir, path);
    rwlockReleaseRead(_fsCurDirLock);
    strDestroy(out);
    *out = tmp;
    return true;
}

void fsCurDir(string *out)
{
    lazyInit(&fsCurDirInit, initCurDir, NULL);
    rwlockAcquireRead(_fsCurDirLock);
    strDup(out, _fsCurDir);
    rwlockReleaseRead(_fsCurDirLock);
}

bool fsSetCurDir(string cur)
{
    if (strEmpty(cur))
        return false;

    string ncur = 0, ppath = 0;
    strDup(&ncur, cur);

    // be paranoid about what we set our current directory to
    pathNormalize(&ncur);
    pathMakeAbsolute(&ncur, ncur);

    // We don't need to actually change the OS's current directory since our own path
    // normalization always results in absolute paths for everything, but it helps
    // keep things from being confusing if subprocesses are created.
    pathToPlatform(&ppath, ncur);
    rwlockAcquireWrite(_fsCurDirLock);
    if (chdir(strC(&ppath)) != 0) {
        unixMapErrno();
        strDestroy(&ncur);
	strDestroy(&ppath);
        rwlockReleaseWrite(_fsCurDirLock);
        return false;
    }

    strDestroy(&ppath);
    strDestroy(&_fsCurDir);
    _fsCurDir = ncur;
    rwlockReleaseWrite(_fsCurDirLock);
    return true;
}

static void fsExeInit(void *data)
{
    string *exepath = (string*)data;

#if defined(_PLATFORM_FREEBSD)
    int mib[4];
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PATHNAME;
    mib[3] = -1;
    size_t sz = 0;
    sysctl(mib, 4, NULL, &sz, NULL, 0);
    char *buf = xaAlloc(sz, 0);
    sysctl(mib, 4, buf, &sz, NULL, 0);
    pathFromPlatform(exepath, buf);
    xaFree(buf);
#elif defined(_PLATFORM_LINUX)
    size_t sz = 1024 / 2;
    char *buf = 0;
    ssize_t lsz;
    // loop until we're sure the buffer was big enough
    do {
        xaSFree(buf);
        sz *= 2;
        buf = xaAlloc(sz);
        lsz = readlink("/proc/self/exe", buf, sz);
    } while (lsz != -1 && lsz == sz);
    if (lsz == -1) {
        // panic
        strCopy(exepath, _S"/");
    } else {
        buf[lsz] = 0;
        pathFromPlatform(exepath, buf);
    }
    xaFree(buf);
#else
#error Unknown Platform
#endif
    pathNormalize(exepath);
}

void fsExe(string *out)
{
    static LazyInitState execache;
    static string exepath = 0;
    lazyInit(&execache, fsExeInit, &exepath);

    strDup(out, exepath);
}

void fsExeDir(string *out)
{
    fsExe(out);
    pathParent(out, *out);
}

int fsStat(string path, FSStat *fsstat)
{
    if (strEmpty(path))
        return FS_Nonexistent;

    struct stat sb;
    string ppath = 0;
    pathToPlatform(&ppath, path);
    if (stat(strC(&ppath), &sb) != 0) {
        if (fsstat)
            memset(fsstat, 0, sizeof(FSStat));
	strDestroy(&ppath);
        return FS_Nonexistent;
    }
    strDestroy(&ppath);

    if (fsstat) {
        fsstat->size = sb.st_size;
        fsstat->accessed = timeFromTimeSpec(&sb.st_atim);

        // unix ctime is inode change time, which is sometimes newer than mtime if the
        // metadata is touched
        fsstat->modified = max(timeFromTimeSpec(&sb.st_mtim), timeFromTimeSpec(&sb.st_ctim));

#ifdef _PLATFORM_FREEBSD
        fsstat->created = timeFromTimeSpec(&sb.st_birthtim);
#else
        // Linux has no file creation timestamp
        fsstat->created = fsstat->modified;
#endif
    }
    if (S_ISDIR(sb.st_mode))
        return FS_Directory;
    return FS_File;
}

bool fsCreateDir(string path)
{
    bool ret = true;
    if (strEmpty(path))
        return false;

    string ppath = 0;
    pathToPlatform(&ppath, path);
    if (mkdir(strC(&ppath), 0755) == -1 && errno != EEXIST)
        ret = unixMapErrno();

    strDestroy(&ppath);
    return ret;
}

bool fsCreateAll(string path)
{
    string parent = 0;
    pathParent(&parent, path);
    if (!strEmpty(parent) && !fsExist(parent))
        fsCreateAll(parent);
    strDestroy(&parent);

    return fsCreateDir(path);
}

bool fsRemoveDir(string path)
{
    bool ret = true;
    if (strEmpty(path))
        return false;

    string ppath = 0;
    pathToPlatform(&ppath, path);
    if (rmdir(strC(&ppath)) == -1)
        ret = unixMapErrno();

    strDestroy(&ppath);
    return ret;
}

bool fsDelete(string path)
{
    bool ret = true;
    if (strEmpty(path))
        return false;

    string ppath = 0;
    pathToPlatform(&ppath, path);
    if (unlink(strC(&ppath)) == -1)
        return unixMapErrno();

    strDestroy(&ppath);
    return ret;
}

bool fsRename(string from, string to)
{
    bool ret = true;
    if (strEmpty(from) || strEmpty(to))
        return false;

    string pfrom = 0, pto = 0;
    pathToPlatform(&pfrom, from);
    pathToPlatform(&pto, to);
    if (rename(strC(&pfrom), strC(&pto)) == -1)
        ret = unixMapErrno();

    strDestroy(&pfrom);
    strDestroy(&pto);
    return ret;
}

typedef struct FSDirSearch {
    DIR *d;
    FSDirEnt ent;
    string path;
    string pattern;
    bool stat;
} FSDirSearch;

FSDirSearch *fsSearchDir(string path, string pattern, bool stat)
{
    FSDirSearch *ret = 0;

    string ppath = 0;
    pathToPlatform(&ppath, path);

    ret = xaAlloc(sizeof(FSDirSearch), XA_ZERO);
    ret->d = opendir(strC(&ppath));
    strDestroy(&ppath);

    if (!ret->d) {
        unixMapErrno();
        xaFree(ret);
        return NULL;
    }

    strDup(&ret->path, path);
    strDup(&ret->pattern, pattern);
    ret->stat = stat;
    return ret;
}

FSDirEnt *fsSearchNext(FSDirSearch *search)
{
    if (!search)
        return NULL;

    struct dirent *de;

    for(;;) {
        de = readdir(search->d);
        if (!de)
            return NULL;

        if (strEmpty(search->pattern) || !pathMatch(de->d_name, search->pattern, 0)) {
            FSDirEnt *ret = &search->ent;
            strCopy(&ret->name, de->d_name);
            if (de->d_type == DT_DIR)
                ret->type = FS_Directory;
            else
                ret->type = FS_File;

            if (search->stat) {
                string tpath = 0;
                pathJoin(&tpath, search->path, ret->name);
                fsStat(tpath, &ret->stat);
                strDestroy(&tpath);
            }

            return ret;
        }
    }
}

void fsSearchClose(FSDirSearch *search)
{
    if (!search)
        return;

    strDestroy(&search->ent.name);
    closedir(search->d);
    strDestroy(&search->path);
    strDestroy(&search->pattern);
    xaFree(search);
}

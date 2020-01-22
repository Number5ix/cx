#include "win_fs.h"
#include "win_time.h"
#include "cx/string.h"
#include "cx/utils/lazyinit.h"
#include "cx/container/sarray.h"
#include "cx/thread/rwlock.h"
#include "cx/platform/win.h"

static LazyInitState fsCurDirInit;
RWLock _fsCurDirLock;
string _fsCurDir = 0;
string fsPlatformPathSepStr = _S"\\";

static void fsPathFromWinW(string *out, wchar_t *winpath)
{
    strFromUTF16(out, winpath, cstrLenw(winpath));
    pathFromPlatform(out, *out);
    pathNormalize(out);
}

static void initCurDir(void *data)
{
    rwlockInit(&_fsCurDirLock);

    DWORD sz = GetCurrentDirectoryW(0, NULL);
    wchar_t *p = xaAlloc(sz * sizeof(wchar_t));
    sz = GetCurrentDirectoryW(sz, p);
    strFromUTF16(&_fsCurDir, p, sz);
    xaFree(p);

    pathFromPlatform(&_fsCurDir, _fsCurDir);
    pathNormalize(&_fsCurDir);
}

static bool fsIsUNC(string path)
{
    striter it;
    striBorrow(&it, path);
    char ch;

    if (striChar(&it, &ch) && ch == '\\' && striChar(&it, &ch) && ch == '\\')
        return true;

    return false;
}

wchar_t* fsPathToNT(string path)
{
    string npath = 0, ntpath = 0;
    wchar_t *ret;
    pathMakeAbsolute(&npath, path);
    pathNormalize(&npath);
    pathToPlatform(&npath, npath);

    if (!fsIsUNC(npath)) {
        strConcat(&ntpath, _S"\\\\?\\", npath);
    } else {
        strSubStrI(&npath, 2, strEnd);
        strConcat(&ntpath, _S"\\\\?\\UNC\\", npath);
    }

    ret = strToUTF16S(ntpath);

    strDestroy(&npath);
    strDestroy(&ntpath);
    return ret;
}

void pathFromPlatform(string *out, string platformpath)
{
    string ns = 0;
    string rpath = 0;
    string ret = 0;

    strDup(&rpath, platformpath);

    // first, convert all backslashes to forward slashes
    int32 idx = 0;
    while ((idx = strFind(rpath, idx, _S"\\")) != -1)
        strSetChar(&rpath, idx, '/');

    char *buf = strBuffer(&rpath, 4);

    if (buf[0] == '/' && (buf[1] == '/' || buf[1] == '?') &&
        (buf[2] == '?' || buf[2] == '.') && buf[3] == '/') {
        // reserved prefix, can't use this!
        strSubStrI(&rpath, 4, strEnd);
        buf = strBuffer(&rpath, 4);
    }

    if (buf[1] == ':' && buf[2] == '/') {
        // absolute path
        strSubStr(&ns, rpath, 0, 1);
        strSubStrI(&rpath, 2, strEnd);
    } else if (buf[0] == '/' && buf[1] == '/') {
        // unc
        strDup(&ns, _S"unc");
        strSubStrI(&rpath, 1, strEnd);
    } else if (buf[0] == '/') {
        // starts with a slash, but not a UNC path...
        // must be drive relative, so get the drive letter from curdir
        lazyInit(&fsCurDirInit, initCurDir, NULL);
        rwlockAcquireRead(&_fsCurDirLock);
        strSubStr(&ns, _fsCurDir, 0, 1);
        rwlockReleaseRead(&_fsCurDirLock);
    }

    if (!strEmpty(ns)) {
        strLower(&ns);
        strNConcat(&ret, ns, fsNSSepStr, rpath);
    } else {
        strDup(&ret, rpath);
    }

    strDestroy(&ns);
    strDestroy(&rpath);
    strDestroy(out);
    *out = ret;
}

void pathToPlatform(string *out, string path)
{
    string ns = 0, rpath = 0;
    string ret = 0;
    pathSplitNS(&ns, &rpath, path);

    if (strEq(ns, _S"unc")) {
        strNConcat(&ret, _S"/", rpath);
    } else if (!strEmpty(ns)) {
        strNConcat(&ret, ns, _S":", rpath);
    } else {
        strDup(&ret, rpath);
    }

    // finally, convert all forward slahes to backslashes
    int32 idx = 0;
    while ((idx = strFind(ret, idx, _S"/")) != -1)
        strSetChar(&ret, idx, '\\');

    strDestroy(&ns);
    strDestroy(&rpath);

    strDestroy(out);
    *out = ret;
}

bool pathMakeAbsolute(string *out, string path)
{
    if (pathIsAbsolute(path)) {
        strDup(out, path);
        return true;
    }
    lazyInit(&fsCurDirInit, initCurDir, NULL);

    string tmp = 0;
    rwlockAcquireRead(&_fsCurDirLock);
    pathJoin(&tmp, _fsCurDir, path);
    rwlockReleaseRead(&_fsCurDirLock);
    strDestroy(out);
    *out = tmp;
    return true;
}

void fsCurDir(string *out)
{
    lazyInit(&fsCurDirInit, initCurDir, NULL);
    rwlockAcquireRead(&_fsCurDirLock);
    strDup(out, _fsCurDir);
    rwlockReleaseRead(&_fsCurDirLock);
}

bool fsSetCurDir(string cur)
{
    if (strEmpty(cur))
        return false;

    string ncur = 0;
    strDup(&ncur, cur);

    // be paranoid about what we set our current directory to
    pathNormalize(&ncur);
    pathMakeAbsolute(&ncur, ncur);

    // We don't need to actually change the OS's current directory since our own path
    // normalization always results in absolute paths for everything, but it helps
    // keep things from being confusing if subprocesses are created.
    rwlockAcquireWrite(&_fsCurDirLock);
    if (!SetCurrentDirectoryW(fsPathToNT(ncur))) {
        winMapLastError();
        strDestroy(&ncur);
        rwlockReleaseWrite(&_fsCurDirLock);
        return false;
    }

    strDestroy(&_fsCurDir);
    _fsCurDir = ncur;
    rwlockReleaseWrite(&_fsCurDirLock);
    return true;
}

static void fsExeInit(void *data)
{
    string *exepath = (string*)data;

    // windows API is really stupid here, has no way to get size first
    wchar_t *buf = xaAlloc(32768 * sizeof(wchar_t));
    GetModuleFileNameW(NULL, buf, 32768);
    buf[32767] = 0;             // it also doesn't null terminate when it truncates...
    fsPathFromWinW(exepath, buf);
    xaFree(buf);
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

int fsStat(string path, FSStat *stat)
{
    if (strEmpty(path))
        return FS_Nonexistent;

    if (!stat) {
        // fast path for if they don't care about times
        DWORD attr = GetFileAttributesW(fsPathToNT(path));
        // handle edge case where drive exists but is not ready
        if (attr == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_NOT_READY)
            return FS_Directory;

        if (attr == INVALID_FILE_ATTRIBUTES)
            return FS_Nonexistent;
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            return FS_Directory;
        return FS_File;
    }

    WIN32_FILE_ATTRIBUTE_DATA attrs;

    if (!GetFileAttributesExW(fsPathToNT(path), GetFileExInfoStandard, &attrs)) {
        memset(stat, 0, sizeof(FSStat));
        if (GetLastError() == ERROR_NOT_READY)
            return FS_Directory;
        return FS_Nonexistent;
    }

    stat->size = (uint64)attrs.nFileSizeHigh << 32 | attrs.nFileSizeLow;
    stat->accessed = timeFromFileTime(&attrs.ftLastAccessTime);
    stat->created = timeFromFileTime(&attrs.ftCreationTime);
    stat->modified = timeFromFileTime(&attrs.ftLastWriteTime);
    if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return FS_Directory;
    return FS_File;
}

bool fsCreateDir(string path)
{
    if (strEmpty(path))
        return false;

    if (!CreateDirectoryW(fsPathToNT(path), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return winMapLastError();

    return true;
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
    if (strEmpty(path))
        return false;

    if (!RemoveDirectoryW(fsPathToNT(path)))
        return winMapLastError();

    return true;
}

bool fsDelete(string path)
{
    if (strEmpty(path))
        return false;

    if (!DeleteFileW(fsPathToNT(path)))
        return winMapLastError();

    return true;
}

bool fsRename(string from, string to)
{
    if (strEmpty(from) || strEmpty(to))
        return false;

    if (!MoveFileW(fsPathToNT(from), fsPathToNT(to)))
        return winMapLastError();

    return true;
}

typedef struct FSDirSearch {
    HANDLE h;
    FSDirEnt ent;
    WIN32_FIND_DATAW first;
} FSDirSearch;

FSDirSearch *fsSearchDir(string path, string pattern, bool stat)
{
    FSDirSearch *ret;
    string spath = 0;

    // stat is ignored for Windows since the API always returns file
    // size and timestamps

    ret = xaAlloc(sizeof(FSDirSearch), Zero);
    pathJoin(&spath, path, strEmpty(pattern) ? _S"*" : pattern);

    ret->h = FindFirstFileW(fsPathToNT(spath), &ret->first);
    strDestroy(&spath);

    if (ret->h == INVALID_HANDLE_VALUE) {
        winMapLastError();
        xaFree(ret);
        return NULL;
    }

    return ret;
}

FSDirEnt *fsSearchNext(FSDirSearch *search)
{
    WIN32_FIND_DATAW data;

    do {
        // If we still have the first file from FindFirstFileW, use it
        if (search->first.dwFileAttributes != 0xFFFFFFFF) {
            memcpy(&data, &search->first, sizeof(WIN32_FIND_DATAW));
            search->first.dwFileAttributes = 0xFFFFFFFF;
        } else {
            if (!FindNextFileW(search->h, &data))
                return NULL;
        }

        // loop until we have something we're actually interested in
    } while (cstrLenw(data.cFileName) == 0 ||
             !wcscmp(data.cFileName, L".") ||
             !wcscmp(data.cFileName, L".."));

    FSDirEnt *ret = &search->ent;
    strFromUTF16(&ret->name, data.cFileName, cstrLenw(data.cFileName));
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        ret->type = FS_Directory;
    else
        ret->type = FS_File;

    ret->stat.size = (uint64)data.nFileSizeHigh << 32 | data.nFileSizeLow;
    ret->stat.accessed = timeFromFileTime(&data.ftLastAccessTime);
    ret->stat.created = timeFromFileTime(&data.ftCreationTime);
    ret->stat.modified = timeFromFileTime(&data.ftLastWriteTime);

    return ret;
}

void fsSearchClose(FSDirSearch *search)
{
    strDestroy(&search->ent.name);
    FindClose(search->h);
    xaFree(search);
}

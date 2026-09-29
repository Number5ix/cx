// ReadDirectoryChangesW backend for native watches.
//
// Every directory handle is associated with one private I/O completion port, which only the I/O
// thread waits on, and every read is issued from that thread. Keeping all I/O on one long-lived
// thread matters before Vista: there, closing the handle is the only way to cancel a read, and
// a read is cancelled anyway if the thread that issued it exits.

#include "cx/fs/fswatch_private.h"

#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/fs/path.h"
#include "cx/platform/win.h"
#include "cx/platform/win/win_fs.h"
#include "cx/string.h"
#include "cx/time/time.h"

// 64 KB is the most a read can return for a directory on a network share, so it is used for
// every directory rather than guessing which ones are remote.
#define WINW_BUFSZ 65536

// Completion keys that are not a WinWatch.
#define WINW_KEY_WAKE ((ULONG_PTR)1)

typedef struct WinWatch {
    OVERLAPPED ov;
    FSWDir* d;       // NULL once the core is done with it and the handle is closing
    HANDLE h;
    DWORD filter;
    BOOL subtree;
    bool pending;    // a read is outstanding, or its completion is being handled right now
    uint8* buf;

    // Set when the core replaced this watch with a new handle for the same directory, so a batch
    // being handled when that happened can carry on through the new one.
    struct WinWatch* next;
} WinWatch;

static HANDLE iocp;

bool _fsWatchPlatformInit(void)
{
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    return iocp != NULL;
}

static DWORD dirFilter(FSWDir* d)
{
    DWORD f = 0;
    if (d->mask & FSW_Names)
        f |= FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    if (d->mask & FSW_Contents)
        f |= FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    // Timestamps are attributes to a watch, but Windows reports them under their own filters.
    if (d->mask & FSW_Attributes)
        f |= FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY |
             FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    return f;
}

static void winwFree(WinWatch* ww)
{
    xaFree(ww->buf);
    xaFree(ww);
}

// Starts the next read. The kernel keeps collecting changes between reads, so nothing is lost
// in the gap.
static bool winwIssue(WinWatch* ww)
{
    memset(&ww->ov, 0, sizeof(ww->ov));
    if (!ReadDirectoryChangesW(ww->h, ww->buf, WINW_BUFSZ, ww->subtree, ww->filter, NULL, &ww->ov,
                               NULL)) {
        ww->pending = false;
        return winMapLastError();
    }
    ww->pending = true;
    return true;
}

// Hands a watch over to be freed once its outstanding read, if any, has come back.
static void winwClose(WinWatch* ww)
{
    ww->d = NULL;
    // Completes an outstanding read with a zero-length success, which the wait loop recognizes
    // by d being NULL. Closing rather than CancelIoEx, which XP does not have.
    CloseHandle(ww->h);
    ww->h = INVALID_HANDLE_VALUE;
    if (!ww->pending)
        winwFree(ww);
}

static WinWatch* winwOpen(FSWDir* d)
{
    HANDLE h = CreateFileW(fsPathToNT(d->path),
                           FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        winMapLastError();
        return NULL;
    }

    WinWatch* ww = xaAllocStruct(WinWatch, XA_Zero);
    ww->d        = d;
    ww->h        = h;
    ww->filter   = dirFilter(d);
    ww->subtree  = d->recursive;
    ww->buf      = xaAlloc(WINW_BUFSZ, XA_Align(3));

    if (!CreateIoCompletionPort(h, iocp, (ULONG_PTR)ww, 0) || !winwIssue(ww)) {
        winMapLastError();
        CloseHandle(h);
        winwFree(ww);
        return NULL;
    }

    return ww;
}

_Use_decl_annotations_
bool _fsWatchPlatformAddDir(FSWDir* d)
{
    WinWatch* ww = winwOpen(d);
    if (!ww)
        return false;
    d->os = ww;
    return true;
}

_Use_decl_annotations_
bool _fsWatchPlatformUpdateDir(FSWDir* d)
{
    WinWatch* old = d->os;
    if (old && old->filter == dirFilter(d) && old->subtree == (BOOL)d->recursive)
        return true;

    // The filter is fixed by the first read on a handle, so a different one needs a new handle.
    // Open it before closing the old one, so there is no moment with neither.
    WinWatch* ww = winwOpen(d);
    if (!ww)
        return false;
    d->os = ww;
    if (old) {
        old->next = ww;
        winwClose(old);
    }
    return true;
}

_Use_decl_annotations_
void _fsWatchPlatformRemoveDir(FSWDir* d)
{
    WinWatch* ww = d->os;
    d->os        = NULL;
    if (ww)
        winwClose(ww);
}

_Use_decl_annotations_
bool _fsWatchPlatformList(strref path, FSWListCB cb, void* ctx)
{
    string pattern = 0;
    pathJoin(&pattern, path, _S"*");

    WIN32_FIND_DATAW data;
    HANDLE fh = FindFirstFileW(fsPathToNT(pattern), &data);
    strDestroy(&pattern);
    if (fh == INVALID_HANDLE_VALUE)
        return winMapLastError();

    string name = 0;
    do {
        if (data.cFileName[0] == L'.' &&
            (data.cFileName[1] == 0 || (data.cFileName[1] == L'.' && data.cFileName[2] == 0)))
            continue;

        // A junction or symlink to a directory is not walked into.
        bool isdir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                     !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        strFromUTF16(&name, data.cFileName, cstrLenw(data.cFileName));
        cb(ctx, name, isdir);
    } while (FindNextFileW(fh, &data));

    strDestroy(&name);
    FindClose(fh);
    return true;
}

// 1 or 0 if the path is or is not a directory, -1 if it is not there to ask.
static int pathIsDir(strref path)
{
    DWORD attr = GetFileAttributesW(fsPathToNT(path));
    if (attr == INVALID_FILE_ATTRIBUTES)
        return -1;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

// Absolute cx path for a name the kernel reported relative to a watched directory. A name with
// a '~' in it may be a short 8.3 alias; while the entry still exists, the long name can be had.
static void eventPath(string* out, FSWDir* d, FILE_NOTIFY_INFORMATION* fni)
{
    string rel = 0;
    strFromUTF16(&rel, fni->FileName, fni->FileNameLength / sizeof(WCHAR));
    pathFromPlatform(&rel, rel);
    pathJoin(out, d->path, rel);

    if (strFind(rel, 0, _S"~") >= 0) {
        wchar_t longbuf[MAX_PATH * 2];
        DWORD n = GetLongPathNameW(fsPathToNT(*out), longbuf, sizeof(longbuf) / sizeof(longbuf[0]));
        if (n > 0 && n < sizeof(longbuf) / sizeof(longbuf[0])) {
            strFromUTF16(out, longbuf, n);
            // Drop the \\?\ prefix fsPathToNT added.
            if (strBeginsWith(*out, _S"\\\\?\\UNC\\")) {
                strSubStrI(out, 8, strEnd);
                strPrepend(_S"\\\\", out);
            } else if (strBeginsWith(*out, _S"\\\\?\\")) {
                strSubStrI(out, 4, strEnd);
            }
            pathFromPlatform(out, *out);
            pathNormalize(out);
        }
    }

    strDestroy(&rel);
}

// The directory a watch now reports for: its own, or its replacement's if the core swapped the
// handle, or NULL if the core is done with it. Replacements are never freed while a completion
// is being handled, since each has a read outstanding.
static FSWDir* winwDir(WinWatch* ww)
{
    while (ww && !ww->d) ww = ww->next;
    return ww ? ww->d : NULL;
}

static void handleRecords(WinWatch* ww, DWORD bytes)
{
    string path = 0, oldpath = 0;
    uint8* p    = ww->buf;

    for (;;) {
        // The core can drop this directory while handling an event.
        FSWDir* d = winwDir(ww);
        if (!d)
            break;

        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)p;
        eventPath(&path, d, fni);

        switch (fni->Action) {
        case FILE_ACTION_ADDED:
            _fsWatchRaw(FSWE_Created, path, NULL, pathIsDir(path));
            break;
        case FILE_ACTION_REMOVED:
            _fsWatchRaw(FSWE_Removed, path, NULL, -1);
            break;
        case FILE_ACTION_MODIFIED: {
            // One action covers contents and attributes alike, and directories report it when
            // their entries change; the latter is not a change of their own.
            if (pathIsDir(path) == 1)
                break;
            bool contents = (d->mask & FSW_Contents) || !(d->mask & FSW_Attributes);
            _fsWatchRaw(contents ? FSWE_Modified : FSWE_Attributes, path, NULL, 0);
            break;
        }
        case FILE_ACTION_RENAMED_OLD_NAME:
            if (oldpath)
                _fsWatchRaw(FSWE_Removed, oldpath, NULL, -1);
            strDup(&oldpath, path);
            break;
        case FILE_ACTION_RENAMED_NEW_NAME:
            if (oldpath)
                _fsWatchRaw(FSWE_Renamed, path, oldpath, pathIsDir(path));
            else
                _fsWatchRaw(FSWE_Created, path, NULL, pathIsDir(path));
            strDestroy(&oldpath);
            break;
        }

        if (fni->NextEntryOffset == 0 || (DWORD)(p - ww->buf) + fni->NextEntryOffset >= bytes)
            break;
        p += fni->NextEntryOffset;
    }

    // The new name belongs in the same batch; without it, the old one just went away.
    if (oldpath && winwDir(ww))
        _fsWatchRaw(FSWE_Removed, oldpath, NULL, -1);

    strDestroy(&path);
    strDestroy(&oldpath);
}

static void handleCompletion(WinWatch* ww, BOOL ok, DWORD bytes)
{
    // pending stays set while this runs, so a close from inside the core -- which frees a watch
    // with no read outstanding -- leaves the free to the end of this function instead.

    // Closed while a read was outstanding; this is that read coming back.
    if (!ww->d) {
        winwFree(ww);
        return;
    }

    FSWDir* d   = ww->d;
    bool reopen = false;

    // A handle follows its directory when it is renamed, and nothing reports that unless the
    // parent is watched too. So check it is still where the watch thinks it is before believing
    // any paths from it; if not, it has moved or been deleted, and the watch on it is over.
    if (ok && pathIsDir(d->path) != 1) {
        ww->pending = false;
        _fsWatchDirGone(d);   // closes, and so frees, ww
        return;
    }

    if (!ok) {
        if (GetLastError() == ERROR_NOTIFY_ENUM_DIR)
            _fsWatchOverflow(d);
        else
            reopen = true;
    } else if (bytes == 0) {
        // More changed than fit in the buffer.
        _fsWatchOverflow(d);
    } else {
        handleRecords(ww, bytes);
    }

    // Handling the records can close this watch.
    if (!ww->d) {
        winwFree(ww);
        return;
    }

    if (!reopen && winwIssue(ww))
        return;

    // The handle is no good. Usually the directory was deleted; if it is still there, start over
    // with a new handle and say changes may have been missed.
    ww->pending = false;
    if (pathIsDir(d->path) != 1) {
        _fsWatchDirGone(d);   // closes, and so frees, ww
        return;
    }

    WinWatch* nw = winwOpen(d);
    if (!nw) {
        _fsWatchDirGone(d);
        return;
    }
    d->os = nw;
    winwClose(ww);
    _fsWatchOverflow(d);
}

_Use_decl_annotations_
void _fsWatchPlatformWait(int64 timeout)
{
    DWORD ms = (DWORD)timeToMsec(timeout);

    for (;;) {
        DWORD bytes         = 0;
        ULONG_PTR key       = 0;
        LPOVERLAPPED ov     = NULL;
        BOOL ok             = GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, ms);

        if (!ov) {
            // Timed out, or a wake-up.
            return;
        }

        handleCompletion((WinWatch*)key, ok, bytes);

        // Drain whatever else is ready, then go back for commands.
        ms = 0;
    }
}

void _fsWatchPlatformWake(void)
{
    PostQueuedCompletionStatus(iocp, 0, WINW_KEY_WAKE, NULL);
}

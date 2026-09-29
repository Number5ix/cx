// inotify backend for native watches. One inotify instance serves every watch in the process;
// the kernel limits instances per user far more tightly than it limits watches.

#include "cx/fs/fswatch_private.h"

#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/container/foreach.h"
#include "cx/string.h"
#include "cx/time/time.h"

#include <sys/inotify.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

// One kernel watch. The kernel keeps a single watch per directory no matter how many paths lead
// to it, so two FSWDirs can share one of these -- the same directory reached through a symlink
// and through its real path, say. Its mask is always the union of what they want.
typedef struct InoWatch {
    int wd;
    uint32 mask;   // what the kernel has now
    sa_ptr dirs;   // FSWDir*
} InoWatch;

static int inoFd       = -1;
static int wakePipe[2] = { -1, -1 };
static hashtable wdMap;   // wd -> InoWatch*

// Room for plenty of events; each is a small header plus a name.
#define INO_BUFSZ 65536
static uint8* readBuf;

// A move is reported as a MOVED_FROM and a MOVED_TO sharing a cookie, which can be split across
// reads. An unmatched FROM waits here until the pair can be completed or given up on.
typedef struct InoPending {
    uint32 cookie;
    string path;
    int isdir;
} InoPending;
static InoPending pending[32];
static int32 npending;

static void mapErrno(void)
{
    switch (errno) {
    case ENOSPC:
    case ENOMEM:
        cxerr = CX_ResourceLimit;
        break;
    case ENOTDIR:
        cxerr = CX_FileNotFound;
        break;
    default:
        unixMapErrno();
    }
}

bool _fsWatchPlatformInit(void)
{
    inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inoFd < 0)
        return false;

    if (pipe2(wakePipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        close(inoFd);
        inoFd = -1;
        return false;
    }

    htInit(&wdMap, int32, ptr, 16);
    readBuf = xaAlloc(INO_BUFSZ);
    return true;
}

static InoWatch* findWatch(int wd)
{
    void* iw = NULL;
    htFind(wdMap, int32, wd, ptr, &iw);
    return iw;
}

static uint32 dirMask(FSWDir* d)
{
    uint32 m = IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_EXCL_UNLINK;
    if (d->mask & FSW_Names)
        m |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    if (d->mask & FSW_Contents)
        m |= IN_MODIFY;
    if (d->mask & FSW_Attributes)
        m |= IN_ATTRIB;
    if (d->nofollow)
        m |= IN_DONT_FOLLOW;
    return m;
}

_Use_decl_annotations_
bool _fsWatchPlatformAddDir(FSWDir* d)
{
    string ppath = 0;
    pathToPlatform(&ppath, d->path);

    // IN_MASK_ADD, because if this directory is already watched through another path, the
    // kernel would otherwise replace that path's mask with this one.
    uint32 m = dirMask(d);
    int wd   = inotify_add_watch(inoFd, strC(ppath), m | IN_MASK_ADD);
    strDestroy(&ppath);

    if (wd < 0) {
        mapErrno();
        return false;
    }

    InoWatch* iw = findWatch(wd);
    if (!iw) {
        iw     = xaAllocStruct(InoWatch, XA_Zero);
        iw->wd = wd;
        saInit(&iw->dirs, ptr, 1);
        htInsert(&wdMap, int32, wd, ptr, iw);
    }
    iw->mask |= m;
    saPush(&iw->dirs, ptr, d);
    d->os = iw;
    return true;
}

_Use_decl_annotations_
bool _fsWatchPlatformUpdateDir(FSWDir* d)
{
    InoWatch* iw = d->os;

    uint32 m = 0;
    foreach (sarray, i, FSWDir*, od, iw->dirs) {
        m |= dirMask(od);
    }
    if (m == iw->mask)
        return true;

    string ppath = 0;
    pathToPlatform(&ppath, d->path);
    int wd = inotify_add_watch(inoFd, strC(ppath), m);
    strDestroy(&ppath);

    if (wd < 0) {
        mapErrno();
        return false;
    }

    if (wd != iw->wd) {
        // The path leads somewhere else now. Leave that directory alone; the event reporting
        // the replacement is on its way and will sort this one out.
        if (!findWatch(wd))
            inotify_rm_watch(inoFd, wd);
        cxerr = CX_FileNotFound;
        return false;
    }

    iw->mask = m;
    return true;
}

_Use_decl_annotations_
void _fsWatchPlatformRemoveDir(FSWDir* d)
{
    InoWatch* iw = d->os;
    d->os        = NULL;
    if (!iw)
        return;

    saFindRemove(&iw->dirs, ptr, d);
    if (saSize(iw->dirs) > 0)
        return;

    // Fails harmlessly when the kernel has already dropped the watch.
    inotify_rm_watch(inoFd, iw->wd);
    htRemove(&wdMap, int32, iw->wd);
    saDestroy(&iw->dirs);
    xaFree(iw);
}

_Use_decl_annotations_
bool _fsWatchPlatformList(strref path, FSWListCB cb, void* ctx)
{
    string ppath = 0;
    pathToPlatform(&ppath, path);
    DIR* dir = opendir(strC(ppath));
    strDestroy(&ppath);

    if (!dir)
        return unixMapErrno();

    struct dirent* de;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        bool isdir = false;
        if (de->d_type == DT_DIR) {
            isdir = true;
        } else if (de->d_type == DT_UNKNOWN) {
            struct stat st;
            if (fstatat(dirfd(dir), de->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0)
                isdir = S_ISDIR(st.st_mode);
        }

        cb(ctx, (strref)de->d_name, isdir);
    }

    closedir(dir);
    return true;
}

static void flushPending(void)
{
    // Moved somewhere nothing is watching.
    for (int32 i = 0; i < npending; i++) {
        _fsWatchRaw(FSWE_Removed, pending[i].path, NULL, pending[i].isdir);
        strDestroy(&pending[i].path);
    }
    npending = 0;
}

static void addPending(uint32 cookie, strref path, int isdir)
{
    if (npending == sizeof(pending) / sizeof(pending[0])) {
        // Too many moves in flight to pair up; report the oldest as a plain removal.
        _fsWatchRaw(FSWE_Removed, pending[0].path, NULL, pending[0].isdir);
        strDestroy(&pending[0].path);
        memmove(&pending[0], &pending[1], sizeof(pending[0]) * (--npending));
    }

    pending[npending].cookie = cookie;
    pending[npending].path   = 0;
    strDup(&pending[npending].path, path);
    pending[npending].isdir = isdir;
    npending++;
}

// Hands the FROM path for a cookie to out and forgets it.
static bool takePending(uint32 cookie, string* out)
{
    for (int32 i = 0; i < npending; i++) {
        if (pending[i].cookie == cookie) {
            strDestroy(out);
            *out = pending[i].path;
            memmove(&pending[i], &pending[i + 1], sizeof(pending[0]) * (npending - i - 1));
            npending--;
            return true;
        }
    }
    return false;
}

// Tell the core a watched directory is gone, for every path it was watched through.
static void watchGone(int wd)
{
    for (;;) {
        // Re-fetched every time: each call can free the watch.
        InoWatch* iw = findWatch(wd);
        if (!iw || saSize(iw->dirs) == 0)
            return;
        _fsWatchDirGone(iw->dirs.a[0]);
    }
}

static void handleEvent(struct inotify_event* ev)
{
    if (ev->wd < 0) {
        if (ev->mask & IN_Q_OVERFLOW)
            _fsWatchOverflow(NULL);
        return;
    }

    if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED)) {
        watchGone(ev->wd);
        return;
    }

    int isdir   = (ev->mask & IN_ISDIR) ? 1 : 0;
    string path = 0, old = 0;

    // Report it through each path the directory is watched by. Re-fetched every time, because
    // the core may change or free the watch while handling an event.
    for (int32 i = 0;; i++) {
        InoWatch* iw = findWatch(ev->wd);
        if (!iw || i >= saSize(iw->dirs))
            break;
        FSWDir* d = iw->dirs.a[i];

        if (ev->len > 0 && ev->name[0])
            pathJoin(&path, d->path, (strref)ev->name);
        else
            strDup(&path, d->path);

        if (ev->mask & IN_CREATE) {
            _fsWatchRaw(FSWE_Created, path, NULL, isdir);
        } else if (ev->mask & IN_DELETE) {
            _fsWatchRaw(FSWE_Removed, path, NULL, isdir);
        } else if (ev->mask & IN_MOVED_FROM) {
            addPending(ev->cookie, path, isdir);
        } else if (ev->mask & IN_MOVED_TO) {
            if (takePending(ev->cookie, &old))
                _fsWatchRaw(FSWE_Renamed, path, old, isdir);
            else
                _fsWatchRaw(FSWE_Created, path, NULL, isdir);
        } else if (ev->mask & IN_MODIFY) {
            _fsWatchRaw(FSWE_Modified, path, NULL, isdir);
        } else if (ev->mask & IN_ATTRIB) {
            _fsWatchRaw(FSWE_Attributes, path, NULL, ev->len > 0 ? isdir : 1);
        }
    }

    strDestroy(&path);
    strDestroy(&old);
}

// Read and handle whatever is queued. Returns false if nothing was.
static bool readEvents(void)
{
    ssize_t n = read(inoFd, readBuf, INO_BUFSZ);
    if (n <= 0)
        return false;

    for (ssize_t off = 0; off + (ssize_t)sizeof(struct inotify_event) <= n;) {
        struct inotify_event* ev = (struct inotify_event*)(readBuf + off);
        handleEvent(ev);
        off += sizeof(struct inotify_event) + ev->len;
    }
    return true;
}

_Use_decl_annotations_
void _fsWatchPlatformWait(int64 timeout)
{
    struct pollfd pfd[2] = {
        { .fd = inoFd, .events = POLLIN },
        { .fd = wakePipe[0], .events = POLLIN },
    };

    if (poll(pfd, 2, (int)timeToMsec(timeout)) <= 0)
        return;

    if (pfd[1].revents & POLLIN) {
        char drain[64];
        while (read(wakePipe[0], drain, sizeof(drain)) > 0) {}
    }

    if (!(pfd[0].revents & POLLIN))
        return;

    while (readEvents()) {}

    // The other half of a move is usually right behind it. Give it a moment before deciding
    // the thing moved out of sight.
    for (int tries = 0; npending > 0 && tries < 4; tries++) {
        struct pollfd ipfd = { .fd = inoFd, .events = POLLIN };
        if (poll(&ipfd, 1, 10) <= 0)
            break;
        while (readEvents()) {}
    }
    flushPending();
}

void _fsWatchPlatformWake(void)
{
    if (wakePipe[1] >= 0) {
        char c = 0;
        (void)!write(wakePipe[1], &c, 1);
    }
}

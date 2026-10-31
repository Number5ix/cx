// FSEvents backend for native watches.
//
// One stream covers every watched directory: its paths are the outermost of them, since a stream
// always reports everything below its paths, and anything under a directory that is not watched
// with a subtree is filtered out by the core. The stream is replaced when that set changes. It
// reports real paths (/tmp is /private/tmp), so each event is mapped back through every watched
// directory above it to the paths the core knows.
//
// An event's flags are everything that has happened to that path for some time, not what just
// happened: a file that was created keeps reporting ItemCreated on every later write. So what an
// event means is decided by looking at the file now, against what was last seen of it:
// - Created when the inode at the path is new to us. A path seen for the first time counts only
//   if the file was born (or, if it moved in, renamed) after the watch started.
// - Modified when the modification time or size moved, Attributes when the change time did.
// - A rename is two ItemRenamed events for the same inode: the name it left, where it is no longer
//   found, and the name it took. A half without its partner is a move in or out of sight.
//
// The stream calls back on a dispatch queue. The callback only copies the events out; they are
// handled on the I/O thread inside _fsWatchPlatformWait, as the core requires.

// Keep AssertMacros.h from defining check(), verify() and require() as plain macros.
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

#include "cx/fs/fswatch_private.h"

#include "cx/container/foreach.h"
#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/string.h"
#include "cx/thread/event.h"
#include "cx/thread/mutex.h"
#include "cx/time/time.h"

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// How long the stream gathers events before delivering them. The first event after a quiet spell
// is delivered at once (kFSEventStreamCreateFlagNoDefer).
#define FSE_LATENCY 0.02

// How many later events to search for the other half of a rename.
#define FSE_PAIR_WINDOW 8

// Files remembered; past this the memory is dropped, which can only cost a repeated Created.
#define FSE_KNOWN_MAX 16384

#define FSE_ITEM_FLAGS                                                                            \
    (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved |                    \
     kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagItemModified | FSE_META_FLAGS)
#define FSE_META_FLAGS                                                                            \
    (kFSEventStreamEventFlagItemInodeMetaMod | kFSEventStreamEventFlagItemChangeOwner |           \
     kFSEventStreamEventFlagItemXattrMod | kFSEventStreamEventFlagItemFinderInfoMod)
#define FSE_LOST_FLAGS                                                                            \
    (kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped |                \
     kFSEventStreamEventFlagKernelDropped)

typedef struct FseDir {
    FSWDir* d;
    string real;              // what the stream reports it as
    uint64 addedId;           // events up to this one happened before it was watched
    int64 addedAt;            // when it was added, in nanoseconds since the Unix epoch
    dev_t dev;
    ino_t ino;
    struct FseDir* nextSame;  // next with the same real path
} FseDir;

typedef struct FseEvent {
    string path;   // real path
    uint32 flags;
    uint64 id;
    uint64 ino;    // 0 if the stream did not say
    bool done;     // already handled as the second half of a rename
} FseEvent;

// What a path was when last seen.
typedef struct FseKnown {
    uint64 ino;
    int64 mtime;
    int64 ctime;
    int64 size;
} FseKnown;

static dispatch_queue_t queue;
static FSEventStreamRef stream;
static sa_string streamRoots;
static bool rootsDirty;
static hashtable realDirs;   // real path -> FseDir*, the head of a nextSame chain
static hashtable known;      // real path -> FseKnown*

// Filled by the stream callback, drained by the I/O thread.
static Mutex pendLock;
static FseEvent* pend;
static int32 npend;
static int32 cappend;
static Event wakeEv;

// The name a file left, held for a moment in case the name it took is in the next delivery.
static struct {
    string path;
    uint64 ino;
    uint64 id;
    int isdir;
} held;

static int64 tsNanos(struct timespec ts)
{
    return (int64)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int64 nowNanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return tsNanos(ts);
}

// ---- the stream ----------------------------------------------------------------------------

static void pendPush(strref path, uint32 flags, uint64 id, uint64 ino)
{
    if (npend == cappend) {
        cappend = cappend ? cappend * 2 : 64;
        xaResize(&pend, sizeof(FseEvent) * cappend);
    }
    FseEvent* e = &pend[npend++];
    *e          = (FseEvent) { .flags = flags, .id = id, .ino = ino };
    strDup(&e->path, path);
}

static void streamCallback(ConstFSEventStreamRef s, void* ctx, size_t n, void* paths,
                           const FSEventStreamEventFlags flags[], const FSEventStreamEventId ids[])
{
    (void)s;
    (void)ctx;
    CFArrayRef arr = paths;
    char buf[PATH_MAX * 4];

    mutexAcquire(&pendLock);
    for (size_t i = 0; i < n; i++) {
        CFDictionaryRef info = CFArrayGetValueAtIndex(arr, (CFIndex)i);
        CFStringRef cfpath   = CFDictionaryGetValue(info, kFSEventStreamEventExtendedDataPathKey);
        CFNumberRef cfino    = CFDictionaryGetValue(info, kFSEventStreamEventExtendedFileIDKey);

        long long ino = 0;
        if (cfino)
            CFNumberGetValue(cfino, kCFNumberLongLongType, &ino);

        if (!cfpath || !CFStringGetFileSystemRepresentation(cfpath, buf, sizeof(buf))) {
            // A path too long to take is still a change that would go unreported.
            pendPush(NULL, kFSEventStreamEventFlagMustScanSubDirs, ids[i], 0);
            continue;
        }
        pendPush((strref)buf, (uint32)flags[i], ids[i], (uint64)ino);
    }
    mutexRelease(&pendLock);

    eventSignal(&wakeEv);
}

static FSEventStreamRef streamCreate(sa_string roots)
{
    CFMutableArrayRef arr = CFArrayCreateMutable(NULL, saSize(roots), &kCFTypeArrayCallBacks);
    foreach (sarray, i, string, root, roots) {
        CFStringRef s = CFStringCreateWithFileSystemRepresentation(NULL, strC(root));
        CFArrayAppendValue(arr, s);
        CFRelease(s);
    }

    FSEventStreamRef s = FSEventStreamCreate(NULL,
                                             streamCallback,
                                             NULL,
                                             arr,
                                             kFSEventStreamEventIdSinceNow,
                                             FSE_LATENCY,
                                             kFSEventStreamCreateFlagUseCFTypes |
                                                 kFSEventStreamCreateFlagUseExtendedData |
                                                 kFSEventStreamCreateFlagFileEvents |
                                                 kFSEventStreamCreateFlagNoDefer);
    CFRelease(arr);
    if (!s)
        return NULL;

    FSEventStreamSetDispatchQueue(s, queue);
    if (!FSEventStreamStart(s)) {
        FSEventStreamInvalidate(s);
        FSEventStreamRelease(s);
        return NULL;
    }
    return s;
}

static void streamDestroy(FSEventStreamRef s)
{
    // Whatever it already has is still worth reporting.
    FSEventStreamFlushSync(s);
    FSEventStreamStop(s);
    FSEventStreamInvalidate(s);
    FSEventStreamRelease(s);
}

static FseDir* realDirsHead(strref real);

static int cmpStrings(const void* a, const void* b)
{
    return strCmp(*(string*)a, *(string*)b);
}

// Is a watched directory strictly above this one?
static bool hasWatchedParent(strref real)
{
    string cur = 0, parent = 0;
    strDup(&cur, real);
    bool found = false;
    while (!found && pathParent(&parent, cur) && !strEq(parent, cur)) {
        found = realDirsHead(parent) != NULL;
        strDup(&cur, parent);
    }
    strDestroy(&cur);
    strDestroy(&parent);
    return found;
}

// The outermost watched directories: every other one is below one of them. Sorted, so that an
// unchanged set compares equal.
static void computeRoots(sa_string* out)
{
    saInit(out, string, 8);
    foreach (hashtable, hti, realDirs) {
        if (!hasWatchedParent(htiKey(string, hti)))
            saPush(out, strref, htiKey(string, hti));
    }
    qsort(out->a, saSize(*out), sizeof(string), cmpStrings);
}

static bool rootsEqual(sa_string a, sa_string b)
{
    if (saSize(a) != saSize(b))
        return false;
    for (int32 i = 0; i < saSize(a); i++) {
        if (!strEq(a.a[i], b.a[i]))
            return false;
    }
    return true;
}

// Point the stream at the current set of directories. The new stream starts before the old one
// stops, so nothing falls in between; what both report is only a duplicate.
static bool streamUpdate(void)
{
    rootsDirty = false;

    sa_string roots;
    computeRoots(&roots);
    if (stream && rootsEqual(roots, streamRoots)) {
        saDestroy(&roots);
        return true;
    }

    FSEventStreamRef ns = NULL;
    if (saSize(roots) > 0) {
        ns = streamCreate(roots);
        if (!ns) {
            saDestroy(&roots);
            cxerr = CX_Unspecified;
            return false;
        }
    }

    if (stream)
        streamDestroy(stream);
    stream = ns;
    saDestroy(&streamRoots);
    streamRoots = roots;
    return true;
}

// ---- watched directories -------------------------------------------------------------------

static FseDir* realDirsHead(strref real)
{
    void* head = NULL;
    htFind(realDirs, strref, real, ptr, &head);
    return head;
}

static void realDirsLink(FseDir* fd)
{
    fd->nextSame = realDirsHead(fd->real);
    htInsert(&realDirs, string, fd->real, ptr, fd);
}

static void realDirsUnlink(FseDir* fd)
{
    FseDir* head = realDirsHead(fd->real);
    if (head == fd) {
        if (fd->nextSame)
            htInsert(&realDirs, string, fd->real, ptr, fd->nextSame);
        else
            htRemove(&realDirs, string, fd->real);
        return;
    }
    for (FseDir* p = head; p; p = p->nextSame) {
        if (p->nextSame == fd) {
            p->nextSame = fd->nextSame;
            return;
        }
    }
}

bool _fsWatchPlatformInit(void)
{
    queue = dispatch_queue_create("cx fswatch", DISPATCH_QUEUE_SERIAL);
    if (!queue)
        return false;

    mutexInit(&pendLock);
    eventInit(&wakeEv);
    htInit(&realDirs, string, ptr, 16);
    htInit(&known, string, ptr, 64);
    saInit(&streamRoots, string, 8);
    return true;
}

_Use_decl_annotations_
bool _fsWatchPlatformAddDir(FSWDir* d)
{
    string ppath = 0;
    pathToPlatform(&ppath, d->path);
    char real[PATH_MAX];
    struct stat st;
    bool ok = realpath(strC(ppath), real) && stat(real, &st) == 0;
    strDestroy(&ppath);
    if (!ok)
        return unixMapErrno();

    FseDir* fd  = xaAllocStruct(FseDir, XA_Zero);
    fd->d       = d;
    fd->addedId = FSEventsGetCurrentEventId();
    fd->addedAt = nowNanos();
    fd->dev     = st.st_dev;
    fd->ino     = st.st_ino;
    strDup(&fd->real, (strref)real);

    d->os = fd;
    realDirsLink(fd);

    if (!streamUpdate()) {
        realDirsUnlink(fd);
        strDestroy(&fd->real);
        xaFree(fd);
        d->os = NULL;
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _fsWatchPlatformUpdateDir(FSWDir* d)
{
    // A stream reports everything below its paths whatever the directory asked for.
    (void)d;
    return true;
}

_Use_decl_annotations_
void _fsWatchPlatformRemoveDir(FSWDir* d)
{
    FseDir* fd = d->os;
    d->os      = NULL;
    if (!fd)
        return;

    realDirsUnlink(fd);
    strDestroy(&fd->real);
    xaFree(fd);

    // Narrowing the stream can wait until the core is done changing things; until then the extra
    // events are filtered out.
    rootsDirty = true;
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

// ---- mapping paths back --------------------------------------------------------------------

// Every path the core knows a real path by: one through each watched directory above it (or at
// it) that was added before the event happened. Returns the earliest of their start times, or -1
// if there are none.
static int64 translate(sa_string* out, strref real, uint64 id)
{
    saClear(out);
    int64 since = -1;

    string cur = 0, rel = 0, mapped = 0, parent = 0;
    strDup(&cur, real);
    for (;;) {
        for (FseDir* fd = realDirsHead(cur); fd; fd = fd->nextSame) {
            if (id != 0 && id <= fd->addedId)
                continue;

            if (strLen(cur) == strLen(real)) {
                strDup(&mapped, fd->d->path);
            } else {
                int32 skip = strLen(cur) + (strEq(cur, _S"/") ? 0 : 1);
                strSubStr(&rel, real, skip, strEnd);
                pathJoin(&mapped, fd->d->path, rel);
            }
            if (saFind(*out, string, mapped) < 0)
                saPush(out, string, mapped);
            if (since < 0 || fd->addedAt < since)
                since = fd->addedAt;
        }

        if (!pathParent(&parent, cur) || strEq(parent, cur))
            break;
        strDup(&cur, parent);
    }

    strDestroy(&cur);
    strDestroy(&rel);
    strDestroy(&mapped);
    strDestroy(&parent);
    return since;
}

static void emit(FSWatchEventKind kind, strref real, int isdir, uint64 id)
{
    sa_string paths;
    saInit(&paths, string, 2);
    translate(&paths, real, id);
    foreach (sarray, i, string, p, paths) {
        _fsWatchRaw(kind, p, NULL, isdir);
    }
    saDestroy(&paths);
}

// A rename is only reported as one when both names are in sight.
static void emitRename(strref realNew, strref realOld, int isdir, uint64 id, uint64 oldid)
{
    sa_string np, op;
    saInit(&np, string, 2);
    saInit(&op, string, 2);
    translate(&np, realNew, id);
    translate(&op, realOld, oldid);

    if (saSize(np) > 0 && saSize(op) > 0) {
        foreach (sarray, i, string, p, np) {
            _fsWatchRaw(FSWE_Renamed, p, op.a[0], isdir);
        }
    } else {
        foreach (sarray, i, string, p, op) {
            _fsWatchRaw(FSWE_Removed, p, NULL, isdir);
        }
        foreach (sarray, i, string, p, np) {
            _fsWatchRaw(FSWE_Created, p, NULL, isdir);
        }
    }

    saDestroy(&np);
    saDestroy(&op);
}

// ---- what was last seen of each path -------------------------------------------------------

static FseKnown* knownFind(strref real)
{
    void* k = NULL;
    htFind(known, strref, real, ptr, &k);
    return k;
}

static void knownDrop(strref real)
{
    FseKnown* k = knownFind(real);
    if (k) {
        htRemove(&known, strref, real);
        xaFree(k);
    }
}

static void knownClear(void)
{
    foreach (hashtable, hti, known) {
        xaFree(htiVal(ptr, hti));
    }
    htClear(&known);
}

static void knownSet(strref real, const struct stat* st)
{
    FseKnown* k = knownFind(real);
    if (!k) {
        if (htSize(known) >= FSE_KNOWN_MAX)
            knownClear();
        k = xaAllocStruct(FseKnown);
        htInsert(&known, strref, real, ptr, k);
    }
    k->ino   = st->st_ino;
    k->mtime = tsNanos(st->st_mtimespec);
    k->ctime = tsNanos(st->st_ctimespec);
    k->size  = st->st_size;
}

// ---- handling events -----------------------------------------------------------------------

static int flagsIsDir(uint32 flags)
{
    if (flags & kFSEventStreamEventFlagItemIsDir)
        return 1;
    if (flags & (kFSEventStreamEventFlagItemIsFile | kFSEventStreamEventFlagItemIsSymlink))
        return 0;
    return -1;
}

static void heldFlush(void)
{
    if (!held.path)
        return;
    emit(FSWE_Removed, held.path, held.isdir, held.id);
    knownDrop(held.path);
    strDestroy(&held.path);
}

static void holdOldName(FseEvent* e)
{
    heldFlush();
    strDup(&held.path, e->path);
    held.ino   = e->ino;
    held.id    = e->id;
    held.isdir = flagsIsDir(e->flags);
}

// e is the name a file left. Look for the name it took among the events right after it.
static bool pairForward(FseEvent* ev, int32 n, int32 i)
{
    FseEvent* e = &ev[i];
    if (e->ino == 0)
        return false;

    for (int32 j = i + 1; j < n && j <= i + FSE_PAIR_WINDOW; j++) {
        FseEvent* p = &ev[j];
        if (p->done || !(p->flags & kFSEventStreamEventFlagItemRenamed) || p->ino != e->ino)
            continue;

        struct stat st;
        if (lstat(strC(p->path), &st) != 0 || st.st_ino != e->ino)
            continue;

        emitRename(p->path, e->path, S_ISDIR(st.st_mode) ? 1 : 0, p->id, e->id);
        knownDrop(e->path);
        knownSet(p->path, &st);
        p->done = true;
        return true;
    }
    return false;
}

static void handleItem(FseEvent* ev, int32 n, int32 i)
{
    FseEvent* e  = &ev[i];
    uint32 flags = e->flags;

    sa_string paths;
    saInit(&paths, string, 2);
    int64 since = translate(&paths, e->path, e->id);
    saDestroy(&paths);
    if (since < 0)
        return;

    struct stat st;
    if (lstat(strC(e->path), &st) != 0) {
        // Gone: deleted, or the name a file was renamed away from.
        if ((flags & kFSEventStreamEventFlagItemRenamed) && pairForward(ev, n, i))
            return;
        if ((flags & kFSEventStreamEventFlagItemRenamed) &&
            !(flags & kFSEventStreamEventFlagItemRemoved)) {
            holdOldName(e);
            return;
        }
        if (flags & (kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagItemRenamed))
            emit(FSWE_Removed, e->path, flagsIsDir(flags), e->id);
        knownDrop(e->path);
        return;
    }

    int isdir = S_ISDIR(st.st_mode) ? 1 : 0;

    // The name a file took, whose old name came in an earlier delivery.
    if (held.path && (flags & kFSEventStreamEventFlagItemRenamed) && held.ino == st.st_ino) {
        emitRename(e->path, held.path, isdir, e->id, held.id);
        knownDrop(held.path);
        strDestroy(&held.path);
        knownSet(e->path, &st);
        return;
    }

    FseKnown* k  = knownFind(e->path);
    bool same    = k && k->ino == st.st_ino;
    int64 mtime  = tsNanos(st.st_mtimespec);
    int64 ctime  = tsNanos(st.st_ctimespec);
    bool appeared = false;

    if (flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed)) {
        if (k)
            appeared = !same;
        else
            appeared = tsNanos(st.st_birthtimespec) >= since ||
                ((flags & kFSEventStreamEventFlagItemRenamed) && ctime >= since);
    }

    // Something else was here before.
    if (k && !same && (flags & kFSEventStreamEventFlagItemRemoved))
        emit(FSWE_Removed, e->path, isdir, e->id);
    if (appeared)
        emit(FSWE_Created, e->path, isdir, e->id);

    if ((flags & kFSEventStreamEventFlagItemModified) && !isdir &&
        (same ? (mtime != k->mtime || st.st_size != k->size) : mtime >= since))
        emit(FSWE_Modified, e->path, isdir, e->id);

    if ((flags & FSE_META_FLAGS) && (same ? ctime != k->ctime : ctime >= since))
        emit(FSWE_Attributes, e->path, isdir, e->id);

    knownSet(e->path, &st);
}

// Is the watched directory still the one that was added?
static bool dirStillThere(FseDir* fd)
{
    struct stat st;
    return stat(strC(fd->real), &st) == 0 && S_ISDIR(st.st_mode) && st.st_dev == fd->dev &&
        st.st_ino == fd->ino;
}

// A volume was mounted or unmounted at path. Stop every watched directory at or below it that is
// no longer there, and rescan the rest that it touches.
static void mountChanged(strref real)
{
    for (;;) {
        // Looked for again every time, since each one stopped can free others.
        FseDir* gone = NULL;
        foreach (hashtable, hti, realDirs) {
            for (FseDir* fd = htiVal(ptr, hti); fd; fd = fd->nextSame) {
                if (_fsWatchPathWithin(fd->real, real, false) && !dirStillThere(fd)) {
                    gone = fd;
                    break;
                }
            }
            if (gone)
                break;
        }
        if (!gone)
            break;
        _fsWatchDirGone(gone->d);
    }

    // What is mounted at or below a watched directory changes what is in it.
    sa_ptr hit;
    saInit(&hit, ptr, 4);
    foreach (hashtable, hti, realDirs) {
        for (FseDir* fd = htiVal(ptr, hti); fd; fd = fd->nextSame) {
            if (_fsWatchPathWithin(fd->real, real, false) || _fsWatchPathWithin(real, fd->real, false))
                saPush(&hit, ptr, fd->d);
        }
    }
    foreach (sarray, i, FSWDir*, d, hit) {
        _fsWatchOverflow(d);
    }
    saDestroy(&hit);
}

static int cmpEvents(const void* a, const void* b)
{
    const FseEvent* ea = a;
    const FseEvent* eb = b;
    if (ea->id != eb->id)
        return ea->id < eb->id ? -1 : 1;
    return strCmp(ea->path, eb->path);
}

// Handle everything the stream has delivered so far.
static void drain(void)
{
    mutexAcquire(&pendLock);
    FseEvent* ev = pend;
    int32 n      = npend;
    pend         = NULL;
    npend = cappend = 0;
    mutexRelease(&pendLock);

    if (n == 0)
        return;

    // Two streams overlap while the stream is being replaced, so the same event can arrive twice.
    qsort(ev, n, sizeof(FseEvent), cmpEvents);

    bool lost = false;
    for (int32 i = 0; i < n; i++) {
        FseEvent* e = &ev[i];
        if (e->done)
            continue;
        if (i > 0 && e->id != 0 && e->id == ev[i - 1].id && strEq(e->path, ev[i - 1].path))
            continue;

        if (e->flags & FSE_LOST_FLAGS) {
            // One rescan of everything covers however many of these there are.
            if (!lost)
                _fsWatchOverflow(NULL);
            lost = true;
        } else if (e->flags & (kFSEventStreamEventFlagMount | kFSEventStreamEventFlagUnmount)) {
            mountChanged(e->path);
        } else if (e->flags & FSE_ITEM_FLAGS) {
            handleItem(ev, n, i);
        }
    }

    for (int32 i = 0; i < n; i++) strDestroy(&ev[i].path);
    xaFree(ev);
}

_Use_decl_annotations_
void _fsWatchPlatformWait(int64 timeout)
{
    if (rootsDirty)
        streamUpdate();

    eventWaitTimeout(&wakeEv, timeout);
    drain();

    // The name a file took is usually right behind the name it left. Give it a moment before
    // deciding the file moved out of sight.
    for (int tries = 0; held.path && tries < 4; tries++) {
        if (!eventWaitTimeout(&wakeEv, timeMS(10)))
            break;
        drain();
    }
    heldFlush();
}

void _fsWatchPlatformWake(void)
{
    eventSignal(&wakeEv);
}

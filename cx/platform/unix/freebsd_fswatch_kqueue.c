// kqueue backend for native watches, for FreeBSD releases without inotify.
//
// kqueue watches open descriptors rather than names, and a directory's knote only says that
// something in it changed, not what. So each watched directory keeps a listing of its entries,
// and a change is found by listing it again and comparing: a name that appears is Created, one
// that disappears is Removed, and a disappearing and an appearing name with the same inode are
// one Renamed. Changes to a file's contents are only seen by watching the file itself, so a
// directory whose users want them holds a descriptor for every regular file in it as well.

#include "cx/fs/fswatch_private.h"

#include "cx/container/foreach.h"
#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/string.h"
#include "cx/time/time.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// EVFILT_USER ident for waking the I/O thread.
#define KQ_WAKE_IDENT 1

typedef struct KqEntry {
    uint64 ino;
    bool isdir;
    bool isreg;
    int fd;   // this file's own descriptor, when its contents or attributes are watched; else -1
    int64 id; // knote id for that descriptor
} KqEntry;

typedef struct KqDir {
    int64 id;           // knote id for the directory's own descriptor
    FSWDir* d;
    int fd;
    hashtable entries;  // name -> KqEntry* (opaque pointer)
    bool dirty;         // something in it changed; list it again after this batch
    bool perfile;       // hold a descriptor for each regular file
} KqDir;

// What a knote's udata leads back to. Looked up by id rather than pointed at, because events
// already fetched in a batch can be for a descriptor that handling an earlier one just closed.
typedef struct KqNote {
    KqDir* dir;
    string name;   // NULL for the directory itself
} KqNote;

static int kq = -1;
static hashtable notes;   // int64 id -> KqNote*
static int64 nextNoteId;
static sa_ptr dirtyDirs;  // KqDir*, listed again at the end of the batch

static void mapErrno(void)
{
    switch (errno) {
    case EMFILE:
    case ENFILE:
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
    kq = kqueue();
    if (kq < 0)
        return false;

    struct kevent kev;
    EV_SET(&kev, KQ_WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
        close(kq);
        kq = -1;
        return false;
    }

    htInit(&notes, int64, ptr, 64);
    saInit(&dirtyDirs, ptr, 8);
    return true;
}

// ---- knotes --------------------------------------------------------------------------------

static int64 noteAdd(int fd, uint32 fflags, KqDir* dir, strref name)
{
    int64 id = ++nextNoteId;

    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, (void*)(intptr)id);
    if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
        mapErrno();
        return 0;
    }

    KqNote* n = xaAllocStruct(KqNote, XA_Zero);
    n->dir    = dir;
    if (name)
        strDup(&n->name, name);
    htInsert(&notes, int64, id, ptr, n);
    return id;
}

// Closing the descriptor removes its knote, and any of its events not yet fetched, by itself.
static void noteRemove(int64 id)
{
    KqNote* n = NULL;
    if (htExtract(&notes, int64, id, ptr, &n) && n) {
        strDestroy(&n->name);
        xaFree(n);
    }
}

static KqNote* noteFind(int64 id)
{
    void* n = NULL;
    htFind(notes, int64, id, ptr, &n);
    return n;
}

// ---- per-file descriptors ------------------------------------------------------------------

static void entryWatch(KqDir* kd, strref name, KqEntry* e)
{
    if (!kd->perfile || !e->isreg || e->fd >= 0)
        return;

    // Never O_RDONLY without O_NONBLOCK on something that might have become a FIFO since it
    // was listed; opening one for reading would block until a writer shows up.
    string cname = 0;
    strDup(&cname, name);
    e->fd = openat(kd->fd, strC(cname), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    strDestroy(&cname);
    if (e->fd < 0)
        return;   // unreadable; its name changes are still seen through the directory

    e->id = noteAdd(e->fd, NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB, kd, name);
    if (!e->id) {
        close(e->fd);
        e->fd = -1;
    }
}

static void entryUnwatch(KqEntry* e)
{
    if (e->fd >= 0) {
        close(e->fd);
        e->fd = -1;
        noteRemove(e->id);
        e->id = 0;
    }
}

static void entryFree(KqEntry* e)
{
    entryUnwatch(e);
    xaFree(e);
}

// ---- listings ------------------------------------------------------------------------------

typedef struct KqListing {
    sa_string names;
    sa_int64 inos;
    sa_int32 types;   // 1 directory, 2 regular file, 0 anything else
} KqListing;

static void listingInit(KqListing* l)
{
    saInit(&l->names, string, 16);
    saInit(&l->inos, int64, 16);
    saInit(&l->types, int32, 16);
}

static void listingDestroy(KqListing* l)
{
    saDestroy(&l->names);
    saDestroy(&l->inos);
    saDestroy(&l->types);
}

// Lists through dfd, so a directory that was renamed is still read from where it is now.
static bool listFd(int dfd, KqListing* l)
{
    int fd = openat(dfd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    DIR* dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return false;
    }

    struct dirent* de;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        int type = 0;
        if (de->d_type == DT_DIR) {
            type = 1;
        } else if (de->d_type == DT_REG) {
            type = 2;
        } else if (de->d_type == DT_UNKNOWN) {
            struct stat st;
            if (fstatat(dirfd(dir), de->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0)
                type = S_ISDIR(st.st_mode) ? 1 : S_ISREG(st.st_mode) ? 2 : 0;
        }

        saPush(&l->names, strref, (strref)de->d_name);
        saPush(&l->inos, int64, (int64)de->d_fileno);
        saPush(&l->types, int32, type);
    }

    closedir(dir);   // closes fd too
    return true;
}

static KqEntry* entryFind(KqDir* kd, strref name)
{
    void* e = NULL;
    htFind(kd->entries, strref, name, ptr, &e);
    return e;
}

// Take a directory's listing as the baseline to compare against, without reporting anything.
static void baseline(KqDir* kd)
{
    KqListing l;
    listingInit(&l);
    listFd(kd->fd, &l);

    for (int32 i = 0; i < saSize(l.names); i++) {
        KqEntry* e = xaAllocStruct(KqEntry, XA_Zero);
        e->ino     = (uint64)l.inos.a[i];
        e->isdir   = l.types.a[i] == 1;
        e->isreg   = l.types.a[i] == 2;
        e->fd      = -1;
        htInsert(&kd->entries, string, l.names.a[i], ptr, e);
        entryWatch(kd, l.names.a[i], e);
    }

    listingDestroy(&l);
}

// One entry that appeared or disappeared in a batch, waiting to be paired with its other half.
typedef struct KqChange {
    string path;
    uint64 ino;
    bool isdir;
    bool paired;
} KqChange;

static void changesFree(sa_ptr* list)
{
    foreach (sarray, i, KqChange*, c, *list) {
        strDestroy(&c->path);
        xaFree(c);
    }
    saDestroy(list);
}

static void changePush(sa_ptr* list, strref dir, strref name, uint64 ino, bool isdir)
{
    KqChange* c = xaAllocStruct(KqChange, XA_Zero);
    pathJoin(&c->path, dir, name);
    c->ino   = ino;
    c->isdir = isdir;
    saPush(list, ptr, c);
}

// List a directory again and bring its entries up to date, collecting what went and came.
static void rescanDir(KqDir* kd, sa_ptr* gone, sa_ptr* came)
{
    KqListing l;
    listingInit(&l);
    if (!listFd(kd->fd, &l)) {
        listingDestroy(&l);
        return;
    }

    hashtable seen;
    htInit(&seen, string, int32, saSize(l.names) + 1);

    for (int32 i = 0; i < saSize(l.names); i++) {
        strref name = l.names.a[i];
        uint64 ino  = (uint64)l.inos.a[i];
        htInsert(&seen, strref, name, int32, 1);

        KqEntry* e = entryFind(kd, name);
        if (e && e->ino == ino)
            continue;

        if (e) {
            // Replaced by a different file under the same name.
            changePush(gone, kd->d->path, name, e->ino, e->isdir);
            entryUnwatch(e);
        } else {
            e = xaAllocStruct(KqEntry, XA_Zero);
            htInsert(&kd->entries, strref, name, ptr, e);
        }

        e->ino   = ino;
        e->isdir = l.types.a[i] == 1;
        e->isreg = l.types.a[i] == 2;
        e->fd    = -1;
        changePush(came, kd->d->path, name, ino, e->isdir);
        entryWatch(kd, name, e);
    }

    // Anything remembered that the listing no longer has went away.
    sa_string removed;
    saInit(&removed, string, 4);
    foreach (hashtable, hti, kd->entries) {
        strref name = htiKey(string, hti);
        if (!htHasKey(seen, strref, name)) {
            KqEntry* e = htiVal(ptr, hti);
            changePush(gone, kd->d->path, name, e->ino, e->isdir);
            saPush(&removed, strref, name);
        }
    }
    foreach (sarray, i, string, name, removed) {
        KqEntry* e = NULL;
        if (htExtract(&kd->entries, strref, name, ptr, &e))
            entryFree(e);
    }

    saDestroy(&removed);
    htDestroy(&seen);
    listingDestroy(&l);
}

// ---- directories ---------------------------------------------------------------------------

static uint32 dirNoteFlags(void)
{
    return NOTE_WRITE | NOTE_EXTEND | NOTE_LINK | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB |
           NOTE_REVOKE;
}

static bool wantsPerFile(FSWDir* d)
{
    return (d->mask & (FSW_Contents | FSW_Attributes)) != 0;
}

// Opens a directory to watch and registers its knote. Tries a descriptor that cannot be read
// through first, falling back to an ordinary one where kqueue will not take that.
static int openDirWatched(FSWDir* d, KqDir* kd)
{
    string ppath = 0;
    pathToPlatform(&ppath, d->path);
    int base = O_DIRECTORY | O_CLOEXEC | (d->nofollow ? O_NOFOLLOW : 0);
    int fd   = -1;

#if defined(O_PATH)
    fd = open(strC(ppath), base | O_PATH);
    if (fd >= 0) {
        kd->id = noteAdd(fd, dirNoteFlags(), kd, NULL);
        if (!kd->id) {
            close(fd);
            fd = -1;
        }
    }
#endif

    if (fd < 0) {
        fd = open(strC(ppath), base | O_RDONLY);
        if (fd < 0) {
            mapErrno();
        } else {
            kd->id = noteAdd(fd, dirNoteFlags(), kd, NULL);
            if (!kd->id) {
                int err = cxerr;
                close(fd);
                fd    = -1;
                cxerr = err;
            }
        }
    }

    strDestroy(&ppath);
    return fd;
}

_Use_decl_annotations_
bool _fsWatchPlatformAddDir(FSWDir* d)
{
    KqDir* kd   = xaAllocStruct(KqDir, XA_Zero);
    kd->d       = d;
    kd->perfile = wantsPerFile(d);

    kd->fd = openDirWatched(d, kd);
    if (kd->fd < 0) {
        xaFree(kd);
        return false;
    }

    htInit(&kd->entries, string, ptr, 16);
    d->os = kd;
    baseline(kd);
    return true;
}

_Use_decl_annotations_
bool _fsWatchPlatformUpdateDir(FSWDir* d)
{
    KqDir* kd  = d->os;
    bool want  = wantsPerFile(d);
    if (want == kd->perfile)
        return true;

    kd->perfile = want;
    foreach (hashtable, hti, kd->entries) {
        KqEntry* e = htiVal(ptr, hti);
        if (want)
            entryWatch(kd, htiKey(string, hti), e);
        else
            entryUnwatch(e);
    }
    return true;
}

_Use_decl_annotations_
void _fsWatchPlatformRemoveDir(FSWDir* d)
{
    KqDir* kd = d->os;
    d->os     = NULL;
    if (!kd)
        return;

    foreach (hashtable, hti, kd->entries) {
        entryFree(htiVal(ptr, hti));
    }
    htDestroy(&kd->entries);
    saFindRemove(&dirtyDirs, ptr, kd);

    close(kd->fd);
    noteRemove(kd->id);
    xaFree(kd);
}

_Use_decl_annotations_
bool _fsWatchPlatformList(strref path, FSWListCB cb, void* ctx)
{
    string ppath = 0;
    pathToPlatform(&ppath, path);
    int fd = open(strC(ppath), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    strDestroy(&ppath);
    if (fd < 0)
        return unixMapErrno();

    KqListing l;
    listingInit(&l);
    bool ret = listFd(fd, &l);
    close(fd);

    for (int32 i = 0; i < saSize(l.names); i++) cb(ctx, l.names.a[i], l.types.a[i] == 1);

    listingDestroy(&l);
    return ret;
}

// ---- events --------------------------------------------------------------------------------

static void markDirty(KqDir* kd)
{
    if (!kd->dirty) {
        kd->dirty = true;
        saPush(&dirtyDirs, ptr, kd);
    }
}

static void handleEvent(struct kevent* kev)
{
    KqNote* n = noteFind((int64)(intptr)kev->udata);
    if (!n)
        return;

    KqDir* kd = n->dir;

    if (n->name) {
        // A file's own knote.
        string path = 0;
        pathJoin(&path, kd->d->path, n->name);
        if (kev->fflags & (NOTE_WRITE | NOTE_EXTEND))
            _fsWatchRaw(FSWE_Modified, path, NULL, 0);
        if (kev->fflags & NOTE_ATTRIB)
            _fsWatchRaw(FSWE_Attributes, path, NULL, 0);
        strDestroy(&path);
        return;
    }

    // The directory itself.
    if (kev->fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) {
        // Anything it was waiting to report goes with it.
        saFindRemove(&dirtyDirs, ptr, kd);
        _fsWatchDirGone(kd->d);
        return;
    }
    if (kev->fflags & NOTE_ATTRIB)
        _fsWatchRaw(FSWE_Attributes, kd->d->path, NULL, 1);
    if (kev->fflags & (NOTE_WRITE | NOTE_EXTEND | NOTE_LINK))
        markDirty(kd);
}

// List every directory that changed in this batch, then report what went and came, pairing a
// departure with an arrival of the same inode -- in the same directory or another -- as a
// rename.
static void flushDirty(void)
{
    sa_ptr gone, came;
    saInit(&gone, ptr, 8);
    saInit(&came, ptr, 8);

    foreach (sarray, i, KqDir*, kd, dirtyDirs) {
        kd->dirty = false;
        rescanDir(kd, &gone, &came);
    }
    saClear(&dirtyDirs);

    // Reporting can free directories, so nothing below touches a KqDir.
    foreach (sarray, i, KqChange*, c, came) {
        foreach (sarray, j, KqChange*, g, gone) {
            if (!g->paired && g->ino == c->ino && !strEq(g->path, c->path)) {
                g->paired = c->paired = true;
                _fsWatchRaw(FSWE_Renamed, c->path, g->path, c->isdir);
                break;
            }
        }
    }

    foreach (sarray, i, KqChange*, g, gone) {
        if (g->paired)
            continue;
        // A name that went and came back as a different file was replaced; its arrival says so.
        bool replaced = false;
        foreach (sarray, j, KqChange*, c, came) {
            if (strEq(c->path, g->path))
                replaced = true;
        }
        if (!replaced)
            _fsWatchRaw(FSWE_Removed, g->path, NULL, g->isdir);
    }

    foreach (sarray, i, KqChange*, c, came) {
        if (!c->paired)
            _fsWatchRaw(FSWE_Created, c->path, NULL, c->isdir);
    }

    changesFree(&gone);
    changesFree(&came);
}

_Use_decl_annotations_
void _fsWatchPlatformWait(int64 timeout)
{
    struct kevent evs[64];
    struct timespec ts = { .tv_sec = timeout / timeS(1), .tv_nsec = (timeout % timeS(1)) * 1000 };

    int n = kevent(kq, NULL, 0, evs, 64, &ts);
    while (n > 0) {
        for (int i = 0; i < n; i++) {
            if (evs[i].filter == EVFILT_VNODE)
                handleEvent(&evs[i]);
        }
        if (n < 64)
            break;
        // A full batch: pick up the rest before comparing listings, so both halves of a rename
        // are likely to be in the same comparison.
        struct timespec zero = { 0 };
        n = kevent(kq, NULL, 0, evs, 64, &zero);
    }

    if (saSize(dirtyDirs) > 0)
        flushDirty();
}

void _fsWatchPlatformWake(void)
{
    if (kq < 0)
        return;
    struct kevent kev;
    EV_SET(&kev, KQ_WAKE_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(kq, &kev, 1, NULL, 0, NULL);
}

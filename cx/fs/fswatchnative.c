// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fs/fswatchnative.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "fswatch_private.h"

#include <cx/debug/error.h>
#include <cx/fs/fs.h>
#include <cx/fs/path.h>
#include <cx/thread/event.h>
#include <cx/thread/mutex.h>
#include <cx/thread/thread.h>
#include <cx/time/time.h>
#include <cx/utils/lazyinit.h>

STR_CONST(kIOThreadName, "cx fswatch io");

// The platform wait is woken directly for every command, so this only bounds how long a missed
// wakeup -- or a shutdown request, which does not wake it -- could go unnoticed.
#define FSWATCH_IO_IDLE timeMS(1000)

// ---- targets -------------------------------------------------------------------------------
//
// Everything from here to the command queue runs only on the I/O thread (or inline once it has
// exited, under cmdLock), so none of it takes a lock.

typedef struct FSWTarget FSWTarget;
struct FSWTarget {
    FSNativeWatch* owner;   // identity only; a target is always dropped before its owner is freed
    FSWatch_WeakRef* wref;
    string path;            // absolute cx path, as the watch knows it
    flags_t flags;          // FSW_Subtree, plus the filter bits, never none of them
    bool isdir;             // a directory target; otherwise watched by name through its parent
    sa_ptr dirs;            // FSWDir* this target keeps armed
    FSWTarget* nextSame;    // next target with the same path, in targetIndex
};

// A watch's targets, hung off FSNativeWatch.io.
typedef struct FSWOwner {
    sa_ptr targets;
} FSWOwner;

static hashtable dirTable;      // path -> FSWDir*
static hashtable targetIndex;   // path -> FSWTarget*, the head of a nextSame chain

static FSWTarget* indexHead(strref path)
{
    void* head = NULL;
    htFind(targetIndex, strref, path, ptr, &head);
    return head;
}

static void indexLink(FSWTarget* t)
{
    t->nextSame = indexHead(t->path);
    htInsert(&targetIndex, string, t->path, ptr, t);
}

static void indexUnlink(FSWTarget* t)
{
    FSWTarget* head = indexHead(t->path);
    if (head == t) {
        if (t->nextSame)
            htInsert(&targetIndex, string, t->path, ptr, t->nextSame);
        else
            htRemove(&targetIndex, string, t->path);
        return;
    }

    for (FSWTarget* p = head; p; p = p->nextSame) {
        if (p->nextSame == t) {
            p->nextSame = t->nextSame;
            return;
        }
    }
}

static FSWTarget* findTarget(FSNativeWatch* w, strref path)
{
    for (FSWTarget* t = indexHead(path); t; t = t->nextSame) {
        if (t->owner == w)
            return t;
    }
    return NULL;
}

static bool pathEq(strref a, strref b)
{
#if FSWATCH_CASEI
    return strEqi(a, b);
#else
    return strEq(a, b);
#endif
}

// Is path strictly below base?
static bool pathBelow(strref path, strref base)
{
    return _fsWatchPathWithin(path, base, FSWATCH_CASEI) && strLen(path) != strLen(base);
}

static bool targetIsSubtree(FSWTarget* t)
{
    return t->isdir && (t->flags & FSW_Subtree);
}

static void queueFor(FSWTarget* t, FSWatchEventKind kind, strref path, strref oldpath)
{
    _fsWatchQueue(FSWatch(t->owner), t->wref, kind, path, oldpath, t->path);
}

// ---- directories ---------------------------------------------------------------------------

static FSWDir* findDir(strref path)
{
    void* d = NULL;
    htFind(dirTable, strref, path, ptr, &d);
    return d;
}

static void dirRecompute(FSWDir* d)
{
    d->mask      = 0;
    d->recursive = false;

    foreach (sarray, i, FSWTarget*, t, d->users) {
        // A directory target using its parent only needs to hear about its own name.
        if (t->isdir && !_fsWatchPathWithin(d->path, t->path, FSWATCH_CASEI)) {
            d->mask |= FSW_Names;
            continue;
        }

        d->mask |= t->flags & FSW_Everything;

        // A subtree has to hear about directories appearing in it, whatever it reports.
        if (targetIsSubtree(t)) {
            d->mask |= FSW_Names;
            if (pathEq(t->path, d->path))
                d->recursive = true;
        }
    }
}

static void dirFree(FSWDir* d)
{
    strDestroy(&d->path);
    saDestroy(&d->users);
    xaFree(d);
}

// Make t a user of the directory at path, asking the OS to watch it if nobody was yet.
static bool dirUse(FSWTarget* t, strref path, bool nofollow)
{
    FSWDir* d = findDir(path);

    if (!d) {
        d = xaAllocStruct(FSWDir, XA_Zero);
        strDup(&d->path, path);
        saInit(&d->users, ptr, 2);
        d->nofollow = nofollow;
        saPush(&d->users, ptr, t);
        dirRecompute(d);

        if (!_fsWatchPlatformAddDir(d)) {
            dirFree(d);
            return false;
        }
        htInsert(&dirTable, string, d->path, ptr, d);
    } else {
        if (saFind(d->users, ptr, t) >= 0)
            return true;

        flags_t oldmask = d->mask;
        bool oldrec     = d->recursive;
        saPush(&d->users, ptr, t);
        dirRecompute(d);

        if ((d->mask != oldmask || d->recursive != oldrec) && !_fsWatchPlatformUpdateDir(d)) {
            saFindRemove(&d->users, ptr, t);
            d->mask      = oldmask;
            d->recursive = oldrec;
            return false;
        }
    }

    saPush(&t->dirs, ptr, d);
    return true;
}

// The reverse of dirUse. d may be freed.
static void dirUnuse(FSWTarget* t, FSWDir* d)
{
    saFindRemove(&t->dirs, ptr, d);
    if (!saFindRemove(&d->users, ptr, t))
        return;

    if (saSize(d->users) == 0) {
        htRemove(&dirTable, string, d->path);
        _fsWatchPlatformRemoveDir(d);
        dirFree(d);
        return;
    }

    flags_t oldmask = d->mask;
    bool oldrec     = d->recursive;
    dirRecompute(d);
    // Narrowing is only an optimization; if it fails, extra events are filtered out anyway.
    if (d->mask != oldmask || d->recursive != oldrec)
        _fsWatchPlatformUpdateDir(d);
}

static void targetFree(FSWTarget* t)
{
    while (saSize(t->dirs) > 0) dirUnuse(t, t->dirs.a[saSize(t->dirs) - 1]);
    objDestroyWeak(&t->wref);
    strDestroy(&t->path);
    saDestroy(&t->dirs);
    xaFree(t);
}

// Forget a target that was registered. With stopped set, first tell its watch it is gone.
static void dropTarget(FSWTarget* t, bool stopped)
{
    if (stopped) {
        // A name that lost its directory is gone too; a directory says so only with FSW_Self.
        if (!t->isdir || (t->flags & FSW_Self))
            queueFor(t, FSWE_Removed, t->path, NULL);
        queueFor(t, FSWE_Stopped, t->path, NULL);
    }

    indexUnlink(t);
    FSWOwner* o = t->owner->io;
    if (o)
        saFindRemove(&o->targets, ptr, t);
    _fsWatchTargetRemoved(FSWatch(t->owner), t->path);
    targetFree(t);
}

// Every registered target, for the rare events that have to look at all of them.
static void allTargets(sa_ptr* out)
{
    saInit(out, ptr, 16);
    foreach (hashtable, hti, targetIndex) {
        for (FSWTarget* t = htiVal(ptr, hti); t; t = t->nextSame) saPush(out, ptr, t);
    }
}

// ---- matching ------------------------------------------------------------------------------

// Would t report this kind of change at a path depth levels below it?
static bool targetCovers(FSWTarget* t, int depth, FSWatchEventKind kind)
{
    if (depth == 0) {
        // Watched by name: anything that happens to that name.
        if (!t->isdir)
            return true;
        // A watched directory reports on itself only when asked to, and then only when it goes
        // or its attributes change.
        return (t->flags & FSW_Self) &&
            (kind == FSWE_Removed || kind == FSWE_Renamed || kind == FSWE_Attributes);
    }

    if (!t->isdir)
        return false;
    return depth == 1 || (t->flags & FSW_Subtree);
}

typedef struct FSWMatch {
    FSNativeWatch* owner;
    FSWTarget* tnew;   // most specific target covering the path
    FSWTarget* told;   // most specific target covering the old path, for a rename
} FSWMatch;

typedef struct FSWMatches {
    FSWMatch* m;
    int32 n;
    int32 cap;
} FSWMatches;

static FSWMatch* matchFor(FSWMatches* ms, FSNativeWatch* owner)
{
    for (int32 i = 0; i < ms->n; i++) {
        if (ms->m[i].owner == owner)
            return &ms->m[i];
    }

    if (ms->n == ms->cap) {
        ms->cap = ms->cap ? ms->cap * 2 : 8;
        xaResize(&ms->m, sizeof(FSWMatch) * ms->cap);
    }
    FSWMatch* ret = &ms->m[ms->n++];
    *ret          = (FSWMatch) { .owner = owner };
    return ret;
}

// Find, for each watch, the most specific of its targets covering path. Walking up from the
// path itself means the first target found for a watch is its most specific one.
static void collectMatches(FSWMatches* ms, strref path, FSWatchEventKind kind, bool old)
{
    string cur = 0;
    strDup(&cur, path);

    for (int depth = 0;; depth++) {
        for (FSWTarget* t = indexHead(cur); t; t = t->nextSame) {
            if (!targetCovers(t, depth, kind))
                continue;
            FSWMatch* m      = matchFor(ms, t->owner);
            FSWTarget** slot = old ? &m->told : &m->tnew;
            if (!*slot)
                *slot = t;
        }

        string parent = 0;
        if (!pathParent(&parent, cur) || strEq(parent, cur)) {
            strDestroy(&parent);
            break;
        }
        strDestroy(&cur);
        cur = parent;
    }

    strDestroy(&cur);
}

// Report a change to every watch that covers it.
static void deliver(FSWatchEventKind kind, strref path, strref oldpath, int isdir)
{
    // Directories do not have contents of their own to report.
    if (kind == FSWE_Modified && isdir == 1)
        return;

    FSWMatches ms = { 0 };
    collectMatches(&ms, path, kind, false);
    if (kind == FSWE_Renamed)
        collectMatches(&ms, oldpath, kind, true);

    for (int32 i = 0; i < ms.n; i++) {
        FSWMatch* m = &ms.m[i];

        if (kind == FSWE_Renamed) {
            // A rename is only a rename if the watch can see both ends of it.
            if (m->tnew && m->told) {
                if (m->tnew->flags & FSW_Names)
                    queueFor(m->tnew, FSWE_Renamed, path, oldpath);
            } else if (m->told) {
                if (m->told->flags & FSW_Names)
                    queueFor(m->told, FSWE_Removed, oldpath, NULL);
            } else if (m->tnew->flags & FSW_Names) {
                queueFor(m->tnew, FSWE_Created, path, NULL);
            }
        } else if (m->tnew && (m->tnew->flags & _fsWatchKindFilter(kind))) {
            queueFor(m->tnew, kind, path, NULL);
        }
    }

    xaFree(ms.m);
}

// ---- subtrees ------------------------------------------------------------------------------

typedef struct FSWListing {
    sa_string names;
    sa_bool isdir;
} FSWListing;

static void listingAdd(void* ctx, strref name, bool isdir)
{
    FSWListing* l = ctx;
    saPush(&l->names, strref, name);
    saPush(&l->isdir, bool, isdir);
}

// Arm a directory and everything below it for each of ts, which are subtree targets covering
// it. With emit set, whatever is found is reported as created: it appeared after the watch
// started, or moved in. Arming before listing is what closes the race with an entry created
// in between -- it is then both listed and reported by the OS, which is merely a duplicate.
static bool walkArm(FSWTarget** ts, int32 n, strref path, bool emit)
{
    bool ret = true;

    for (int32 i = 0; i < n; i++) {
        if (!dirUse(ts[i], path, true))
            ret = false;
    }
    if (!ret)
        return false;

    FSWListing l;
    saInit(&l.names, string, 16);
    saInit(&l.isdir, bool, 16);
    _fsWatchPlatformList(path, listingAdd, &l);

    string child = 0;
    for (int32 i = 0; i < saSize(l.names); i++) {
        pathJoin(&child, path, l.names.a[i]);
        if (emit)
            deliver(FSWE_Created, child, NULL, l.isdir.a[i]);
        if (l.isdir.a[i] && !walkArm(ts, n, child, emit))
            ret = false;
    }
    strDestroy(&child);

    saDestroy(&l.names);
    saDestroy(&l.isdir);
    return ret;
}

// Subtree targets that path is strictly below.
static void subtreesAbove(sa_ptr* out, strref path)
{
    saInit(out, ptr, 4);

    string cur = 0;
    if (!pathParent(&cur, path) || strEq(cur, path)) {
        strDestroy(&cur);
        return;
    }

    for (;;) {
        for (FSWTarget* t = indexHead(cur); t; t = t->nextSame) {
            if (targetIsSubtree(t))
                saPush(out, ptr, t);
        }

        string parent = 0;
        if (!pathParent(&parent, cur) || strEq(parent, cur)) {
            strDestroy(&parent);
            break;
        }
        strDestroy(&cur);
        cur = parent;
    }
    strDestroy(&cur);
}

// A directory appeared at path, by being created or moved in.
static void dirAppeared(strref path)
{
#if !FSWATCH_NATIVE_RECURSIVE
    sa_ptr ts;
    subtreesAbove(&ts, path);

    if (saSize(ts) > 0 && !walkArm((FSWTarget**)ts.a, saSize(ts), path, true)) {
        // Usually the system limit on watches. Part of the subtree is now unwatched, so say so.
        foreach (sarray, i, FSWTarget*, t, ts) {
            queueFor(t, FSWE_Rescan, path, NULL);
        }
    }
    saDestroy(&ts);
#endif
}

// The directory at path is gone, by being deleted or moved away.
static void dirVanished(strref path)
{
    sa_ptr all;
    allTargets(&all);

    // Targets are paths, so any at or below it have lost what they were watching. One watched
    // by name directly inside it has lost its directory; one naming the directory itself has
    // not, and keeps watching its parent for it to come back.
    foreach (sarray, i, FSWTarget*, t, all) {
        if (t->isdir ? _fsWatchPathWithin(t->path, path, FSWATCH_CASEI) : pathBelow(t->path, path))
            dropTarget(t, true);
    }
    saDestroy(&all);

    // What is left using directories down there are subtrees from further up.
    sa_ptr gone;
    saInit(&gone, ptr, 8);
    foreach (hashtable, hti, dirTable) {
        FSWDir* d = htiVal(ptr, hti);
        if (_fsWatchPathWithin(d->path, path, FSWATCH_CASEI))
            saPush(&gone, ptr, d);
    }

    // Unusing one directory never frees another, so the pointers stay good.
    foreach (sarray, i, FSWDir*, d, gone) {
        while (saSize(d->users) > 1) dirUnuse(d->users.a[saSize(d->users) - 1], d);
        if (saSize(d->users) == 1)
            dirUnuse(d->users.a[0], d);
    }
    saDestroy(&gone);
}

// ---- backend callbacks ---------------------------------------------------------------------

_Use_decl_annotations_
void _fsWatchRaw(FSWatchEventKind kind, strref path, strref oldpath, int isdir)
{
    deliver(kind, path, oldpath, isdir);

    // When the OS does not say, whatever went away might have been a directory; treating it as
    // one only costs a look for targets below it.
    if (isdir == 0)
        return;

    switch (kind) {
    case FSWE_Created:
        if (isdir == 1)
            dirAppeared(path);
        break;
    case FSWE_Removed:
        dirVanished(path);
        break;
    case FSWE_Renamed:
        // Rather than re-key everything that was below it, stop watching the old place and walk
        // the new one. Its contents are reported as created at their new paths.
        dirVanished(oldpath);
        dirAppeared(path);
        break;
    default:
        break;
    }
}

_Use_decl_annotations_
void _fsWatchDirGone(FSWDir* d)
{
    sa_ptr users;
    saClone(&users, d->users);

    // The last user to let go frees d, so it is not touched after this loop.
    string dpath = 0;
    strDup(&dpath, d->path);

    foreach (sarray, i, FSWTarget*, t, users) {
        string parent = 0;
        bool own      = t->isdir ? pathEq(t->path, dpath) :
                                   (pathParent(&parent, t->path) && pathEq(parent, dpath));
        strDestroy(&parent);

        if (own)
            dropTarget(t, true);
        else
            dirUnuse(t, d);
    }

    strDestroy(&dpath);
    saDestroy(&users);
}

_Use_decl_annotations_
void _fsWatchOverflow(FSWDir* d)
{
    sa_ptr all;
    if (d)
        saClone(&all, d->users);
    else
        allTargets(&all);

    foreach (sarray, i, FSWTarget*, t, all) {
        queueFor(t, FSWE_Rescan, t->path, NULL);

        // Directories created while events were being lost were never armed.
        if (targetIsSubtree(t) && !FSWATCH_NATIVE_RECURSIVE)
            walkArm(&t, 1, t->path, false);
    }
    saDestroy(&all);
}

// ---- commands ------------------------------------------------------------------------------

static bool cmdAdd(FSNativeWatch* w, strref path, flags_t flags)
{
    if (atomicLoad(bool, &w->cancelled, Acquire)) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    if (!(flags & FSW_Everything))
        flags |= FSW_Everything;

    bool isdir = fsStat(path, NULL) == FS_Directory;
    if (!isdir) {
        if (flags & FSW_Subtree) {
            cxerr = CX_InvalidArgument;
            return false;
        }

        string parent = 0;
        bool ok       = pathParent(&parent, path) && fsStat(parent, NULL) == FS_Directory;
        strDestroy(&parent);
        if (!ok) {
            cxerr = CX_FileNotFound;
            return false;
        }
    }

    FSWTarget* t = xaAllocStruct(FSWTarget, XA_Zero);
    t->owner     = w;
    t->wref      = objGetWeak(FSWatch, w);
    t->flags     = flags;
    t->isdir     = isdir;
    strDup(&t->path, path);
    saInit(&t->dirs, ptr, 1);

    bool ok = true;
    if (isdir) {
        ok = dirUse(t, path, false);
#if !FSWATCH_NATIVE_RECURSIVE
        if (ok && (flags & FSW_Subtree))
            ok = walkArm(&t, 1, path, false);
#endif
#if FSWATCH_NO_SELF_EVENTS
        // The directory's own watch will not say when it is renamed; its parent's will. Only
        // worth a second watch when the caller asked to hear about the directory itself. A root
        // has no parent, and cannot be renamed anyway.
        string parent = 0;
        if (ok && (flags & FSW_Self) && pathParent(&parent, path) && !pathEq(parent, path))
            ok = dirUse(t, parent, false);
        strDestroy(&parent);
#endif
    } else {
        string parent = 0;
        pathParent(&parent, path);
        ok = dirUse(t, parent, false);
        strDestroy(&parent);
    }

    if (!ok) {
        targetFree(t);
        return false;
    }

    // Arm the new target before dropping one it replaces, so there is no gap in coverage.
    FSWTarget* old = findTarget(w, path);
    if (old) {
        _fsWatchTargetAdded(FSWatch(w), path);   // dropTarget takes one copy away
        dropTarget(old, false);
    } else {
        _fsWatchTargetAdded(FSWatch(w), path);
    }

    if (!w->io) {
        FSWOwner* o = xaAllocStruct(FSWOwner, XA_Zero);
        saInit(&o->targets, ptr, 4);
        w->io = o;
    }
    saPush(&((FSWOwner*)w->io)->targets, ptr, t);
    indexLink(t);
    return true;
}

static bool cmdRemove(FSNativeWatch* w, strref path)
{
    FSWTarget* t = findTarget(w, path);
    if (!t) {
        cxerr = CX_FileNotFound;
        return false;
    }

    dropTarget(t, false);
    return true;
}

static void cmdRemoveAll(FSNativeWatch* w)
{
    FSWOwner* o = w->io;
    if (!o)
        return;

    while (saSize(o->targets) > 0) dropTarget(o->targets.a[saSize(o->targets) - 1], false);

    saDestroy(&o->targets);
    xaFree(o);
    w->io = NULL;
}

typedef enum FSWCmdOp {
    FSWC_Add,
    FSWC_Remove,
    FSWC_RemoveAll,
} FSWCmdOp;

typedef struct FSWCmd FSWCmd;
struct FSWCmd {
    FSWCmd* next;
    FSWCmdOp op;
    FSNativeWatch* w;
    strref path;   // borrowed; the sender waits for the answer
    flags_t flags;
    bool result;
    int err;
    Event done;
};

static LazyInitState ioInitState;
static bool ioAvailable;
static Thread* ioThread;

// Guards the command list and ioStopped. Once the I/O thread has exited, commands run inline
// while holding it, which keeps them one at a time just as the thread did.
static Mutex cmdLock;
static FSWCmd* cmdHead;
static FSWCmd* cmdTail;
static bool ioStopped;

static void execCmd(FSWCmd* c)
{
    cxerr = CX_Success;

    switch (c->op) {
    case FSWC_Add:
        if (ioStopped) {
            // Shutting down; nothing would ever report on it.
            cxerr     = CX_NotSupported;
            c->result = false;
        } else {
            c->result = cmdAdd(c->w, c->path, c->flags);
        }
        break;
    case FSWC_Remove:
        c->result = cmdRemove(c->w, c->path);
        break;
    case FSWC_RemoveAll:
        cmdRemoveAll(c->w);
        c->result = true;
        break;
    }

    c->err = cxerr;
}

// Run a batch, then signal each sender. A sender's command lives on its stack, so nothing in
// it may be touched after its event is signaled.
static void execList(FSWCmd* c)
{
    while (c) {
        FSWCmd* next = c->next;
        execCmd(c);
        eventSignal(&c->done);
        c = next;
    }
}

static int ioThreadFunc(Thread* self)
{
    while (thrLoop(self)) {
        FSWCmd* batch;
        withMutex (&cmdLock) {
            batch   = cmdHead;
            cmdHead = cmdTail = NULL;
        }
        execList(batch);

        _fsWatchPlatformWait(FSWATCH_IO_IDLE);
    }

    // From here on commands run inline on whoever sends them. Anything already waiting is
    // handled now, still under the lock, so no sender is left waiting on a thread that is gone.
    withMutex (&cmdLock) {
        ioStopped = true;
        execList(cmdHead);
        cmdHead = cmdTail = NULL;
    }
    return 0;
}

static void ioInit(void* data)
{
    mutexInit(&cmdLock);
    htInit(&dirTable, string, ptr, 16, FSWATCH_CASEI ? HT_CaseInsensitive : 0);
    htInit(&targetIndex, string, ptr, 16, FSWATCH_CASEI ? HT_CaseInsensitive : 0);

    ioAvailable = _fsWatchPlatformInit();
    if (!ioAvailable)
        return;

    // See procwatch.c for why this is a system thread and why stvNone is needed.
    ioThread = thrCreate(ioThreadFunc, kIOThreadName, stvNone);
    thrRegisterSysThread(ioThread);
}

static bool sendCmd(FSWCmd* c)
{
    eventInit(&c->done);

    mutexAcquire(&cmdLock);
    if (ioStopped) {
        execCmd(c);
        mutexRelease(&cmdLock);
    } else {
        if (cmdTail)
            cmdTail->next = c;
        else
            cmdHead = c;
        cmdTail = c;
        mutexRelease(&cmdLock);

        _fsWatchPlatformWake();
        eventWait(&c->done);
    }

    eventDestroy(&c->done);
    if (!c->result)
        cxerr = c->err;
    return c->result;
}

// ---- FSNativeWatch -------------------------------------------------------------------------

_Use_decl_annotations_
FSWatch* fsWatchCreate(closure cls)
{
    return FSWatch(fsnativewatchCreate(cls));
}

_objfactory_guaranteed FSNativeWatch* FSNativeWatch_create(closure cls)
{
    FSNativeWatch* self;
    self = objInstCreate(FSNativeWatch);

    self->cls = cls;

    objInstInit(self);

    return self;
}

bool FSNativeWatch_add(_In_ FSNativeWatch* self, _In_opt_ strref path, flags_t flags)
{
    lazyInit(&ioInitState, ioInit, NULL);
    if (!ioAvailable) {
        cxerr = CX_NotSupported;
        return false;
    }

    string abspath = 0;
    pathMakeAbsolute(&abspath, path);

    FSWCmd c = { .op = FSWC_Add, .w = self, .path = abspath, .flags = flags };
    bool ret = sendCmd(&c);

    strDestroy(&abspath);
    return ret;
}

bool FSNativeWatch_remove(_In_ FSNativeWatch* self, _In_opt_ strref path)
{
    if (!ioAvailable) {
        cxerr = CX_FileNotFound;
        return false;
    }

    string abspath = 0;
    pathMakeAbsolute(&abspath, path);

    FSWCmd c = { .op = FSWC_Remove, .w = self, .path = abspath };
    bool ret = sendCmd(&c);

    strDestroy(&abspath);
    return ret;
}

void FSNativeWatch_stopSources(_In_ FSNativeWatch* self)
{
    // A watch that never had anything added has nothing on the I/O thread -- which may not
    // even exist -- to take down. ioAvailable is only ever set before the first add returns.
    if (!ioAvailable)
        return;

    FSWCmd c = { .op = FSWC_RemoveAll, .w = self };
    sendCmd(&c);
}

void FSNativeWatch_destroy(_In_ FSNativeWatch* self)
{
    // Its targets hold a bare pointer to it, so they must be gone before it is.
    FSNativeWatch_stopSources(self);
}

// Autogen begins -----
// clang-format off
#include "fs/fswatchnative.auto.inc"
// clang-format on
// Autogen ends -------

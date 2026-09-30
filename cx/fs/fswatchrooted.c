// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fs/fswatchrooted.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "fswatch_private.h"

#include <cx/debug/error.h>
#include <cx/fs/fs.h>
#include <cx/fs/path.h>

// A path added before it could be watched.
typedef struct FSWRPending {
    string path;    // as passed to add, relative to the root
    string full;    // absolute
    string anc;     // the existing directory pendw watches for it
    flags_t flags;
} FSWRPending;

static void pendFree(FSWRPending* e)
{
    strDestroy(&e->path);
    strDestroy(&e->full);
    strDestroy(&e->anc);
    xaFree(e);
}

_objfactory_guaranteed FSWatchRooted* FSWatchRooted_create(_In_opt_ strref root, bool casei, closure cls)
{
    FSWatchRooted* self;
    self = objInstCreate(FSWatchRooted);

    strDup(&self->root, root);
    self->casei = casei;
    self->cls   = cls;
    mutexInit(&self->plock);
    saInit(&self->pending, ptr, 1);

    objInstInit(self);

    return self;
}

// Events from the inner watch, rewritten relative to the root and passed on.
static void rootedForward(stvlist* cvars, FSWatch* inner, FSWatchEvent* ev)
{
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return;

    FSWatchRooted* self = objAcquireFromWeakDyn(FSWatchRooted, wref);
    if (!self)
        return;

    string path = 0, oldpath = 0, target = 0;
    _fsWatchStripRoot(&path, ev->path, self->root, self->casei);
    if (ev->oldpath)
        _fsWatchStripRoot(&oldpath, ev->oldpath, self->root, self->casei);
    _fsWatchStripRoot(&target, ev->target, self->root, self->casei);

    FSWatchEvent rev = { .kind = ev->kind, .path = path, .oldpath = oldpath, .target = target };
    _fsWatchDeliver(FSWatch(self), &rev);

    strDestroy(&path);
    strDestroy(&oldpath);
    strDestroy(&target);
    objRelease(&self);
}

_Use_decl_annotations_
closure _fsWatchRootedInnerCls(FSWatchRooted* self)
{
    Weak(FSWatchRooted)* wref = objGetWeak(FSWatchRooted, self);
    closure ret = closureCreateAs(FSWatchCB, rootedForward, stvar(weakref, objWeakRefBase(wref)));
    objDestroyWeak(&wref);
    return ret;
}

static bool pathEqC(FSWatchRooted* self, strref a, strref b)
{
    return self->casei ? strEqi(a, b) : strEq(a, b);
}

static void pendForward(stvlist* cvars, FSWatch* pendw, FSWatchEvent* ev);

// The nearest directory above full that exists, or empty if there is none.
static void nearestDir(string* out, strref full)
{
    string cur = 0, parent = 0;
    strDup(&cur, full);
    strClear(out);
    while (pathParent(&parent, cur) && !strEq(parent, cur)) {
        if (fsStat(parent, NULL) == FS_Directory) {
            strDup(out, parent);
            break;
        }
        strDup(&cur, parent);
    }
    strDestroy(&cur);
    strDestroy(&parent);
}

// Stop watching anc for waiting paths, unless another one still waits on it.
static void pendRelease(FSWatchRooted* self, strref anc)
{
    if (strEmpty(anc) || !self->pendw)
        return;
    foreach (sarray, i, FSWRPending*, e, self->pending) {
        if (pathEqC(self, e->anc, anc))
            return;
    }
    fsWatchRemove(self->pendw, anc);
}

enum { PEND_Waiting, PEND_Added, PEND_Failed };

// Try to add a waiting path for real, or move it to the nearest directory that exists now.
// Call with plock held; e must not be in the pending list while this runs.
static int pendEval(FSWatchRooted* self, FSWRPending* e)
{
    string anc = 0, again = 0;
    int ret = PEND_Failed;

    for (int tries = 0; tries < 16; tries++) {
        bool rootok = fsStat(self->root, NULL) == FS_Directory;
        if (rootok) {
            if (fsWatchAdd(self->inner, e->full, e->flags)) {
                ret = PEND_Added;
                break;
            }
            // There, but it cannot be watched for some other reason.
            if (fsStat(e->full, NULL) != FS_Nonexistent)
                break;
        }

        nearestDir(&anc, rootok ? e->full : self->root);
        if (strEmpty(anc))
            break;

        if (!pathEqC(self, anc, e->anc)) {
            if (!self->pendw) {
                Weak(FSWatchRooted)* wref = objGetWeak(FSWatchRooted, self);
                self->pendw               = fsWatchCreate(
                    closureCreateAs(FSWatchCB, pendForward, stvar(weakref, objWeakRefBase(wref))));
                objDestroyWeak(&wref);
            }
            if (!fsWatchAdd(self->pendw, anc, FSW_Names))
                break;
            string old = e->anc;
            e->anc     = anc;
            anc        = old;
            pendRelease(self, anc);
        }

        // Anything created before that watch took effect produced no event for it.
        nearestDir(&again, rootok ? e->full : self->root);
        if (pathEqC(self, again, e->anc) && rootok == (fsStat(self->root, NULL) == FS_Directory)) {
            ret = PEND_Waiting;
            break;
        }
    }

    if (ret != PEND_Waiting) {
        string old = e->anc;
        e->anc     = NULL;
        pendRelease(self, old);
        strDestroy(&old);
    }
    strDestroy(&anc);
    strDestroy(&again);
    return ret;
}

// Events for the directories waiting paths wait on. Anything at all happening there is reason
// to look again.
static void pendForward(stvlist* cvars, FSWatch* pendw, FSWatchEvent* ev)
{
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return;

    FSWatchRooted* self = objAcquireFromWeakDyn(FSWatchRooted, wref);
    if (!self)
        return;
    if (atomicLoad(bool, &self->cancelled, Acquire)) {
        objRelease(&self);
        return;
    }

    sa_string added;
    saInit(&added, string, 1);

    withMutex (&self->plock) {
        for (int32 i = saSize(self->pending) - 1; i >= 0; --i) {
            FSWRPending* e = self->pending.a[i];
            if (!pathEqC(self, e->anc, ev->target))
                continue;

            saRemove(&self->pending, i);
            int res = pendEval(self, e);
            if (res == PEND_Waiting) {
                saPush(&self->pending, ptr, e);
                continue;
            }
            if (res == PEND_Added)
                saPush(&added, string, e->path);
            // A path that can no longer be waited for stays a target, but quietly.
            pendFree(e);
        }
    }

    // Whatever appeared before it was watched was never reported.
    foreach (sarray, i, string, p, added) {
        FSWatchEvent rev = { .kind = FSWE_Rescan, .path = p, .target = p };
        _fsWatchDeliver(FSWatch(self), &rev);
    }

    saDestroy(&added);
    objRelease(&self);
}

// Drops a waiting entry for path, if there is one. Call with plock held.
static bool pendDrop(FSWatchRooted* self, strref path)
{
    foreach (sarray, i, FSWRPending*, e, self->pending) {
        if (pathEqC(self, e->path, path)) {
            saRemove(&self->pending, i);
            pendRelease(self, e->anc);
            pendFree(e);
            return true;
        }
    }
    return false;
}

bool FSWatchRooted_add(_In_ FSWatchRooted* self, _In_opt_ strref path, flags_t flags)
{
    string full = 0;
    pathJoin(&full, self->root, path);
    bool ret = false;

    withMutex (&self->plock) {
        pendDrop(self, path);

        bool rootok = !self->native || fsStat(self->root, NULL) == FS_Directory;
        if (rootok) {
            ret = fsWatchAdd(self->inner, full, flags);
            if (ret || !self->native || fsStat(full, NULL) != FS_Nonexistent)
                break;
        }

        // Not there yet: wait for it.
        FSWRPending* e = xaAllocStruct(FSWRPending, XA_Zero);
        strDup(&e->path, path);
        strDup(&e->full, full);
        e->flags = flags;

        cxerr   = CX_Success;
        int res = pendEval(self, e);
        if (res == PEND_Waiting)
            saPush(&self->pending, ptr, e);
        else
            pendFree(e);
        ret = res != PEND_Failed;
        if (!ret && cxerr == CX_Success)
            cxerr = CX_FileNotFound;
    }

    if (ret)
        _fsWatchTargetAdded(FSWatch(self), path);
    strDestroy(&full);
    return ret;
}

bool FSWatchRooted_remove(_In_ FSWatchRooted* self, _In_opt_ strref path)
{
    bool ret = false;
    withMutex (&self->plock) {
        ret = pendDrop(self, path);
    }

    if (!ret) {
        string full = 0;
        pathJoin(&full, self->root, path);
        ret = fsWatchRemove(self->inner, full);
        strDestroy(&full);
    }
    if (ret)
        _fsWatchTargetRemoved(FSWatch(self), path);
    return ret;
}

void FSWatchRooted_stopSources(_In_ FSWatchRooted* self)
{
    fsWatchCancel(self->inner);

    // Not under plock: cancelling waits for a running pendForward, which takes it.
    FSWatch* pendw = NULL;
    withMutex (&self->plock) {
        pendw = objAcquire(self->pendw);
    }
    fsWatchCancel(pendw);
    objRelease(&pendw);
}

void FSWatchRooted_destroy(_In_ FSWatchRooted* self)
{
    FSWatchRooted_stopSources(self);

    foreach (sarray, i, FSWRPending*, e, self->pending) {
        pendFree(e);
    }

    // Autogen begins -----
    objRelease(&self->inner);
    strDestroy(&self->root);
    mutexDestroy(&self->plock);
    objRelease(&self->pendw);
    saDestroy(&self->pending);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "fs/fswatchrooted.auto.inc"
// clang-format on
// Autogen ends -------

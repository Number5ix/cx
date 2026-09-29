// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fs/vfswatch.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "fswatch_private.h"
#include "vfs_private.h"

#include <cx/debug/error.h>

// A VFSVFS mounted inside its own VFS would otherwise have a subtree watch recurse forever.
#define VFSWATCH_MAX_DEPTH 8
static _Thread_local int32 vfsWatchDepth;

// One path added to a provider's watch. Several targets can need the same one -- a subtree and
// a directory inside it served by the same mount -- so it counts its users and is removed from
// the provider's watch only when the last one goes.
typedef struct VFSWPath {
    string relpath;
    flags_t flags;   // what the provider's watch was last given for it
    sa_ptr uses;     // VFSWUse*
} VFSWPath;

// One mounted provider this watch listens through.
typedef struct VFSWLayer {
    int64 id;           // what its provider watch's closure knows it by
    VFSMount* mount;    // holds a reference; also what identifies the layer
    string mountpath;   // absolute VFS path of the mount point
    FSWatch* inner;     // the provider's watch
    sa_ptr paths;       // VFSWPath*
} VFSWLayer;

typedef struct VFSWTarget VFSWTarget;

// A target's use of one path in one layer.
typedef struct VFSWUse {
    VFSWTarget* target;
    VFSWLayer* layer;
    VFSWPath* path;
    flags_t flags;
    bool stopped;   // the provider reported it gone
} VFSWUse;

struct VFSWTarget {
    string path;    // absolute VFS path
    flags_t flags;
    bool isdir;
    bool stopped;   // every layer it was watched through reported it gone
    sa_ptr uses;    // VFSWUse*
};

typedef struct VFSWState {
    sa_ptr targets;      // VFSWTarget*
    sa_ptr layers;       // VFSWLayer*
    sa_string hidden;    // mount points whose opaque mount hides the layers mounted above them
    int64 nextLayerId;
    bool attached;       // listening to the VFS for mount changes
} VFSWState;

// What one layer of the VFS would need to be asked to watch for a target.
typedef struct VFSWPlan {
    VFSMount* mount;   // holds a reference
    string mountpath;
    string relpath;
    flags_t flags;
} VFSWPlan;

static bool vfsCaseI(VFSWatch* self)
{
    return !(self->vfs->flags & VFS_CaseSensitive);
}

static bool vpathEq(VFSWatch* self, strref a, strref b)
{
    return vfsCaseI(self) ? strEqi(a, b) : strEq(a, b);
}

// ---- planning ------------------------------------------------------------------------------

static void planFree(sa_ptr* plan)
{
    foreach (sarray, i, VFSWPlan*, p, *plan) {
        objRelease(&p->mount);
        strDestroy(&p->mountpath);
        strDestroy(&p->relpath);
        xaFree(p);
    }
    saDestroy(plan);
}

static void planPush(sa_ptr* plan, VFSMount* m, strref mountpath, strref relpath, flags_t flags)
{
    VFSWPlan* p = xaAllocStruct(VFSWPlan, XA_Zero);
    p->mount    = objAcquire(m);
    strDup(&p->mountpath, mountpath);
    strDup(&p->relpath, relpath);
    p->flags = flags;
    saPush(plan, ptr, p);
}

// The directory node at abspath, if there is one. Never creates one. VFS locks must be held.
static VFSDir* findDirNode(VFS* vfs, strref abspath)
{
    VFSDir* dir = NULL;
    string ns   = 0;
    sa_string components;
    saInit(&components, string, 8);
    pathDecompose(&ns, &components, abspath);

    if (strEmpty(ns))
        dir = vfs->root;
    else if (!htFind(vfs->namespaces, string, ns, VFSDir, &dir))
        dir = NULL;

    for (int32 i = 0, n = saSize(components); dir && i < n; i++) {
        if (strEmpty(components.a[i]))
            continue;
        if (!htFind(dir->subdirs, string, components.a[i], VFSDir, &dir))
            dir = NULL;
    }

    strDestroy(&ns);
    saDestroy(&components);
    return dir;
}

// Every mount below dir, as the root of a subtree. An opaque mount hides the layers above it
// from everything underneath, which is recorded in hidden.
static void planChildMounts(sa_ptr* plan, sa_string* hidden, VFSDir* dir, strref dirpath,
                            flags_t flags)
{
    string cpath = 0;
    foreach (hashtable, hti, dir->subdirs) {
        VFSDir* child = (VFSDir*)htiVal(ptr, hti);
        pathJoin(&cpath, dirpath, child->name);

        for (int32 i = saSize(child->mounts) - 1; i >= 0; --i) {
            // A mount point is just another directory in the subtree, not the target itself.
            planPush(plan, child->mounts.a[i], cpath, _S"", (flags & ~FSW_Self) | FSW_Subtree);
            if (child->mounts.a[i]->flags & VFS_Opaque) {
                saPush(hidden, string, cpath);
                break;
            }
        }

        planChildMounts(plan, hidden, child, cpath, flags);
    }
    strDestroy(&cpath);
}

// Which layers a target is watched through right now, and the mount generation that is true
// for. Takes and releases the VFS locks; calls no provider.
static uint32 planTarget(VFSWatch* self, VFSWTarget* t, sa_ptr* plan, sa_string* hidden)
{
    VFS* vfs = self->vfs;
    saInit(plan, ptr, 4);
    saInit(hidden, string, 0);

    sa_VFSCand cands;
    saInit(&cands, VFSCand, 4);

    rwlockAcquireRead(&vfs->vfsdlock);
    rwlockAcquireRead(&vfs->vfslock);

    uint32 gen = vfs->mountgen;
    _vfsSnapshot(vfs, &cands, t->path, !t->isdir);
    foreach (sarray, i, VFSCand, c, cands) {
        planPush(plan, c.mount, c.mountpath, c.relpath, t->flags);
    }

    if (t->isdir && (t->flags & FSW_Subtree)) {
        VFSDir* dir = findDirNode(vfs, t->path);
        if (dir)
            planChildMounts(plan, hidden, dir, t->path, t->flags);
    }

    rwlockReleaseRead(&vfs->vfslock);
    rwlockReleaseRead(&vfs->vfsdlock);

    saDestroy(&cands);
    return gen;
}

static uint32 currentGen(VFS* vfs)
{
    uint32 gen;
    rwlockAcquireRead(&vfs->vfslock);
    gen = vfs->mountgen;
    rwlockReleaseRead(&vfs->vfslock);
    return gen;
}

// ---- layers and uses -----------------------------------------------------------------------
//
// Everything that changes these holds reglock, and takes lock around the change itself so the
// delivery path, which only holds lock, always sees them consistent.

static void vfsWatchForward(stvlist* cvars, FSWatch* inner, FSWatchEvent* ev);

static VFSWLayer* findLayerByMount(VFSWState* st, VFSMount* m)
{
    foreach (sarray, i, VFSWLayer*, l, st->layers) {
        if (l->mount == m)
            return l;
    }
    return NULL;
}

static VFSWLayer* findLayerById(VFSWState* st, int64 id)
{
    foreach (sarray, i, VFSWLayer*, l, st->layers) {
        if (l->id == id)
            return l;
    }
    return NULL;
}

static VFSWLayer* layerGet(VFSWatch* self, VFSWPlan* p)
{
    VFSWState* st = self->state;
    VFSWLayer* l  = findLayerByMount(st, p->mount);
    if (l)
        return l;

    VFSWatchable* wif = objInstIf(p->mount->provider, VFSWatchable);
    if (!wif) {
        cxerr = CX_NotSupported;
        return NULL;
    }

    l     = xaAllocStruct(VFSWLayer, XA_Zero);
    l->id = ++st->nextLayerId;

    Weak(VFSWatch)* wref = objGetWeak(VFSWatch, self);
    closure cls          = closureCreateAs(FSWatchCB,
                                  vfsWatchForward,
                                  stvar(weakref, objWeakRefBase(wref)),
                                  stvar(int64, l->id));
    objDestroyWeak(&wref);

    l->inner = wif->createWatch(p->mount->provider, cls);
    if (!l->inner) {
        xaFree(l);
        cxerr = CX_NotSupported;
        return NULL;
    }

    l->mount = objAcquire(p->mount);
    strDup(&l->mountpath, p->mountpath);
    saInit(&l->paths, ptr, 2);

    withMutex (&self->lock) {
        saPush(&st->layers, ptr, l);
    }
    return l;
}

static void layerDrop(VFSWatch* self, VFSWLayer* l)
{
    VFSWState* st = self->state;
    withMutex (&self->lock) {
        saFindRemove(&st->layers, ptr, l);
    }

    // Not fsWatchCancel: this can run under reglock, and waiting for a callback that is already
    // running would deadlock if that callback -- through to the user's -- wants reglock too. Not
    // waiting is safe because vfsWatchForward drops anything for a layer that is gone.
    _fsWatchStop(l->inner);
    objRelease(&l->inner);
    objRelease(&l->mount);
    strDestroy(&l->mountpath);
    saDestroy(&l->paths);
    xaFree(l);
}

static flags_t pathFlags(VFSWPath* p)
{
    flags_t f = 0;
    foreach (sarray, i, VFSWUse*, u, p->uses) {
        f |= u->flags;
    }
    return f;
}

// Watch relpath in a layer for a target. Returns false (with cxerr) if the layer cannot.
static bool useAdd(VFSWatch* self, VFSWTarget* t, VFSWPlan* p)
{
    VFSWLayer* l = layerGet(self, p);
    if (!l)
        return false;

    VFSWPath* path = NULL;
    foreach (sarray, i, VFSWPath*, lp, l->paths) {
        if (vpathEq(self, lp->relpath, p->relpath)) {
            path = lp;
            break;
        }
    }

    flags_t want = p->flags | (path ? path->flags : 0);
    if (!path || want != path->flags) {
        if (!fsWatchAdd(l->inner, p->relpath, want)) {
            if (saSize(l->paths) == 0)
                layerDrop(self, l);
            return false;
        }
    }

    VFSWUse* u = xaAllocStruct(VFSWUse, XA_Zero);
    u->target  = t;
    u->layer   = l;
    u->flags   = p->flags;

    withMutex (&self->lock) {
        if (!path) {
            path = xaAllocStruct(VFSWPath, XA_Zero);
            strDup(&path->relpath, p->relpath);
            saInit(&path->uses, ptr, 1);
            saPush(&l->paths, ptr, path);
        }
        path->flags = want;
        u->path     = path;
        saPush(&path->uses, ptr, u);
        saPush(&t->uses, ptr, u);
    }
    return true;
}

static void useDrop(VFSWatch* self, VFSWUse* u)
{
    VFSWLayer* l   = u->layer;
    VFSWPath* path = u->path;
    bool lastuse = false, lastpath = false;

    withMutex (&self->lock) {
        saFindRemove(&u->target->uses, ptr, u);
        saFindRemove(&path->uses, ptr, u);
        lastuse = saSize(path->uses) == 0;
        if (lastuse) {
            saFindRemove(&l->paths, ptr, path);
            lastpath = saSize(l->paths) == 0;
        }
    }

    if (lastpath) {
        // Dropping the layer cancels its watch, which takes the path with it.
        layerDrop(self, l);
    } else if (lastuse) {
        fsWatchRemove(l->inner, path->relpath);
    } else {
        flags_t want = pathFlags(path);
        if (want != path->flags && fsWatchAdd(l->inner, path->relpath, want))
            path->flags = want;
    }

    if (lastuse) {
        strDestroy(&path->relpath);
        saDestroy(&path->uses);
        xaFree(path);
    }
    xaFree(u);
}

static bool useMatches(VFSWatch* self, VFSWUse* u, VFSWPlan* p)
{
    return u->layer->mount == p->mount && vpathEq(self, u->path->relpath, p->relpath) &&
           u->flags == p->flags;
}

// Bring a target's uses in line with the layers that can see it now. Returns true if at least
// one layer is watching it; on false, cxerr says why the last one could not.
static bool targetArm(VFSWatch* self, VFSWTarget* t)
{
    VFSWState* st = self->state;
    bool ret      = false;

    for (int tries = 0; tries < 8; tries++) {
        sa_ptr plan;
        sa_string hidden;
        uint32 gen = planTarget(self, t, &plan, &hidden);

        // Stopped uses are gone from the provider already; drop them along with any that no
        // longer match a layer.
        for (int32 i = saSize(t->uses) - 1; i >= 0; --i) {
            VFSWUse* u = t->uses.a[i];
            bool keep  = !u->stopped;
            if (keep) {
                keep = false;
                foreach (sarray, j, VFSWPlan*, p, plan) {
                    if (useMatches(self, u, p)) {
                        keep = true;
                        break;
                    }
                }
            }
            if (!keep)
                useDrop(self, u);
        }

        foreach (sarray, j, VFSWPlan*, p, plan) {
            bool have = false;
            foreach (sarray, i, VFSWUse*, u, t->uses) {
                if (useMatches(self, u, p)) {
                    have = true;
                    break;
                }
            }
            // A layer that cannot watch, or where the path does not exist, is simply skipped.
            if (!have)
                useAdd(self, t, p);
        }

        withMutex (&self->lock) {
            foreach (sarray, i, string, h, hidden) {
                if (saFind(st->hidden, string, h) < 0)
                    saPush(&st->hidden, string, h);
            }
        }

        planFree(&plan);
        saDestroy(&hidden);

        ret = saSize(t->uses) > 0;
        if (currentGen(self->vfs) == gen)
            break;
    }

    return ret;
}

static void targetFree(VFSWatch* self, VFSWTarget* t)
{
    while (saSize(t->uses) > 0) useDrop(self, t->uses.a[saSize(t->uses) - 1]);
    strDestroy(&t->path);
    saDestroy(&t->uses);
    xaFree(t);
}

static VFSWTarget* findTarget(VFSWatch* self, strref path)
{
    VFSWState* st = self->state;
    foreach (sarray, i, VFSWTarget*, t, st->targets) {
        if (vpathEq(self, t->path, path))
            return t;
    }
    return NULL;
}

// ---- mount changes -------------------------------------------------------------------------

// Would a mount added or removed at mountpath change what target t sees?
static bool mountAffects(VFSWatch* self, VFSWTarget* t, strref mountpath)
{
    bool casei = vfsCaseI(self);
    if (!mountpath || _fsWatchPathWithin(t->path, mountpath, casei))
        return true;
    return t->isdir && _fsWatchCovers(t->path, t->flags, true, mountpath, FSWE_Created, casei);
}

static bool vfsWatchMountChanged(stvlist* cvars, stvlist* args)
{
    ObjInst_WeakRef* wref = NULL;
    strref mpath          = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return true;
    stvlNext(args, strref, &mpath);

    VFSWatch* self = objAcquireFromWeakDyn(VFSWatch, wref);
    if (!self)
        return true;

    string mountpath = 0;
    if (mpath) {
        strDup(&mountpath, mpath);
        pathNormalize(&mountpath);
    }

    VFSWState* st = self->state;
    sa_string rescans;
    saInit(&rescans, string, 2);

    withMutex (&self->reglock) {
        if (atomicLoad(bool, &self->cancelled, Acquire))
            break;

        foreach (sarray, i, VFSWTarget*, t, st->targets) {
            if (t->stopped || !mountAffects(self, t, mountpath))
                continue;

            targetArm(self, t);

            // The contents of the mount point changed wholesale; say where to look.
            bool below = mountpath && _fsWatchPathWithin(mountpath, t->path, vfsCaseI(self));
            saPush(&rescans, string, below ? mountpath : t->path);
            saPush(&rescans, string, t->path);
        }
    }

    // Pairs of (where to rescan, which target), queued with nothing held.
    for (int32 i = 0; i + 1 < saSize(rescans); i += 2) {
        _vfsInvalidatePath(self->vfs, rescans.a[i], true);
        FSWatchEvent ev = { .kind = FSWE_Rescan, .path = rescans.a[i], .target = rescans.a[i + 1] };
        _fsWatchDeliver(FSWatch(self), &ev);
    }

    saDestroy(&rescans);
    strDestroy(&mountpath);
    objRelease(&self);
    return true;
}

// ---- events --------------------------------------------------------------------------------

static bool isHidden(VFSWatch* self, VFSWState* st, strref vpath, strref layermount)
{
    bool casei = vfsCaseI(self);
    foreach (sarray, i, string, h, st->hidden) {
        // Hidden only from layers mounted above the opaque mount point, not from layers at or
        // below it.
        if (_fsWatchPathWithin(vpath, h, casei) && !_fsWatchPathWithin(layermount, h, casei))
            return true;
    }
    return false;
}

// The most specific live target covering path, for an event of this kind.
static VFSWTarget* bestTarget(VFSWatch* self, VFSWState* st, strref path, FSWatchEventKind kind)
{
    VFSWTarget* best = NULL;
    bool casei       = vfsCaseI(self);
    foreach (sarray, i, VFSWTarget*, t, st->targets) {
        if (t->stopped || !_fsWatchCovers(t->path, t->flags, t->isdir, path, kind, casei))
            continue;
        if (!best || strLen(t->path) > strLen(best->path))
            best = t;
    }
    return best;
}

// Events from a provider's watch: translate them into VFS paths, keep the VFS cache honest, and
// pass them on.
static void vfsWatchForward(stvlist* cvars, FSWatch* inner, FSWatchEvent* ev)
{
    ObjInst_WeakRef* wref = NULL;
    int64 layerid         = 0;
    if (!stvlNext(cvars, weakref, &wref) || !stvlNext(cvars, int64, &layerid))
        return;

    VFSWatch* self = objAcquireFromWeakDyn(VFSWatch, wref);
    if (!self)
        return;

    VFSWState* st  = self->state;
    string vpath = 0, vold = 0, target = 0, stoppedPath = 0;
    FSWatchEventKind kind = ev->kind;
    bool deliver          = false;

    withMutex (&self->lock) {
        VFSWLayer* l = findLayerById(st, layerid);
        if (!l)
            break;

        pathJoin(&vpath, l->mountpath, ev->path);
        if (ev->oldpath)
            pathJoin(&vold, l->mountpath, ev->oldpath);

        if (kind == FSWE_Stopped) {
            // The provider stopped watching one of its paths. A target is only over once every
            // layer it was watched through has said so; until then the Removed that came before
            // this is all it hears.
            foreach (sarray, i, VFSWPath*, p, l->paths) {
                if (!vpathEq(self, p->relpath, ev->target))
                    continue;
                foreach (sarray, j, VFSWUse*, u, p->uses) {
                    u->stopped   = true;
                    VFSWTarget* t = u->target;
                    bool alldone  = true;
                    foreach (sarray, k, VFSWUse*, tu, t->uses) {
                        if (!tu->stopped)
                            alldone = false;
                    }
                    if (alldone && !t->stopped) {
                        t->stopped = true;
                        strDup(&stoppedPath, t->path);
                    }
                }
            }
            break;
        }

        if (isHidden(self, st, vpath, l->mountpath))
            break;

        VFSWTarget* tnew = bestTarget(self, st, vpath, kind);
        if (kind == FSWE_Renamed) {
            VFSWTarget* told = isHidden(self, st, vold, l->mountpath)
                                   ? NULL
                                   : bestTarget(self, st, vold, kind);
            if (!tnew && told) {
                kind = FSWE_Removed;
                strDup(&vpath, vold);
                strDestroy(&vold);
                tnew = told;
            } else if (tnew && !told) {
                kind = FSWE_Created;
                strDestroy(&vold);
            }
        }

        if (tnew && (tnew->flags & _fsWatchKindFilter(kind))) {
            strDup(&target, tnew->path);
            deliver = true;
        }
    }

    // Whatever the VFS remembers about these paths may be wrong now. Forget it before the
    // callback runs, so a callback that looks gets the truth.
    if (vpath) {
        _vfsInvalidateCache(self->vfs, vpath);
        if (kind != FSWE_Modified && kind != FSWE_Attributes && kind != FSWE_Created)
            _vfsInvalidatePath(self->vfs, vpath, true);
    }
    if (vold) {
        _vfsInvalidateCache(self->vfs, vold);
        _vfsInvalidatePath(self->vfs, vold, true);
    }

    if (deliver) {
        FSWatchEvent out = { .kind = kind, .path = vpath, .oldpath = vold, .target = target };
        _fsWatchDeliver(FSWatch(self), &out);
    }
    if (stoppedPath) {
        FSWatchEvent out = { .kind = FSWE_Stopped, .path = stoppedPath, .target = stoppedPath };
        _fsWatchDeliver(FSWatch(self), &out);
        _fsWatchTargetRemoved(FSWatch(self), stoppedPath);
    }

    strDestroy(&vpath);
    strDestroy(&vold);
    strDestroy(&target);
    strDestroy(&stoppedPath);
    objRelease(&self);
}

// ---- VFSWatch ------------------------------------------------------------------------------

_Use_decl_annotations_
FSWatch* vfsWatchCreate(VFS* vfs, closure cls)
{
    return FSWatch(vfswatchCreate(vfs, cls));
}

_objfactory_guaranteed VFSWatch* VFSWatch_create(VFS* vfs, closure cls)
{
    VFSWatch* self;
    self = objInstCreate(VFSWatch);

    self->vfs = objAcquire(vfs);
    self->cls = cls;
    mutexInit(&self->reglock);
    mutexInit(&self->lock);

    VFSWState* st = xaAllocStruct(VFSWState, XA_Zero);
    saInit(&st->targets, ptr, 4);
    saInit(&st->layers, ptr, 4);
    saInit(&st->hidden, string, 0);
    self->state = st;

    objInstInit(self);

    return self;
}

static bool addLocked(VFSWatch* self, strref abspath, flags_t flags)
{
    VFSWState* st = self->state;

    if (atomicLoad(bool, &self->cancelled, Acquire)) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    if (!(flags & FSW_Everything))
        flags |= FSW_Everything;

    bool isdir = vfsStat(self->vfs, abspath, NULL) == FS_Directory;
    if (!isdir) {
        if (flags & FSW_Subtree) {
            cxerr = CX_InvalidArgument;
            return false;
        }

        string parent = 0;
        bool ok = pathParent(&parent, abspath) && vfsStat(self->vfs, parent, NULL) == FS_Directory;
        strDestroy(&parent);
        if (!ok) {
            cxerr = CX_FileNotFound;
            return false;
        }
    }

    if (!st->attached) {
        Weak(VFSWatch)* wref = objGetWeak(VFSWatch, self);
        cchainAttachToken(&self->vfs->onmountchange,
                          vfsWatchMountChanged,
                          (intptr)self,
                          stvar(weakref, objWeakRefBase(wref)));
        objDestroyWeak(&wref);
        st->attached = true;
    }

    VFSWTarget* t = xaAllocStruct(VFSWTarget, XA_Zero);
    strDup(&t->path, abspath);
    t->flags = flags;
    t->isdir = isdir;
    saInit(&t->uses, ptr, 2);

    if (!targetArm(self, t)) {
        // No layer could watch it. Say so as not supported unless a provider had a better reason.
        int err = cxerr;
        targetFree(self, t);
        cxerr = (err == CX_Success) ? CX_NotSupported : err;
        return false;
    }

    // Replace one already there only now, so coverage never lapses.
    VFSWTarget* old = findTarget(self, abspath);
    withMutex (&self->lock) {
        if (old)
            saFindRemove(&st->targets, ptr, old);
        saPush(&st->targets, ptr, t);
    }
    if (old)
        targetFree(self, old);
    else
        _fsWatchTargetAdded(FSWatch(self), abspath);

    return true;
}

bool VFSWatch_add(_In_ VFSWatch* self, _In_opt_ strref path, flags_t flags)
{
    if (vfsWatchDepth >= VFSWATCH_MAX_DEPTH) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    string abspath = 0;
    vfsAbsolutePath(self->vfs, &abspath, path);
    pathNormalize(&abspath);

    bool ret = false;
    vfsWatchDepth++;
    cxerr = CX_Success;
    withMutex (&self->reglock) {
        ret = addLocked(self, abspath, flags);
    }
    vfsWatchDepth--;

    strDestroy(&abspath);
    return ret;
}

bool VFSWatch_remove(_In_ VFSWatch* self, _In_opt_ strref path)
{
    VFSWState* st  = self->state;
    string abspath = 0;
    vfsAbsolutePath(self->vfs, &abspath, path);
    pathNormalize(&abspath);

    bool ret = false;
    withMutex (&self->reglock) {
        VFSWTarget* t = findTarget(self, abspath);
        // One that stopped on its own is already gone as far as the caller is concerned.
        if (t) {
            ret = !t->stopped;
            withMutex (&self->lock) {
                saFindRemove(&st->targets, ptr, t);
            }
            if (ret)
                _fsWatchTargetRemoved(FSWatch(self), t->path);
            targetFree(self, t);
        }
    }

    if (!ret)
        cxerr = CX_FileNotFound;
    strDestroy(&abspath);
    return ret;
}

void VFSWatch_stopSources(_In_ VFSWatch* self)
{
    VFSWState* st = self->state;
    if (!st)
        return;

    withMutex (&self->reglock) {
        if (st->attached) {
            cchainDetach(&self->vfs->onmountchange, vfsWatchMountChanged, (intptr)self);
            st->attached = false;
        }

        while (saSize(st->targets) > 0) {
            VFSWTarget* t = st->targets.a[saSize(st->targets) - 1];
            withMutex (&self->lock) {
                saRemove(&st->targets, saSize(st->targets) - 1);
            }
            targetFree(self, t);
        }
    }
}

void VFSWatch_destroy(_In_ VFSWatch* self)
{
    VFSWatch_stopSources(self);

    VFSWState* st = self->state;
    if (st) {
        saDestroy(&st->targets);
        saDestroy(&st->layers);
        saDestroy(&st->hidden);
        xaFree(st);
        self->state = NULL;
    }

    // Autogen begins -----
    objRelease(&self->vfs);
    mutexDestroy(&self->reglock);
    mutexDestroy(&self->lock);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "fs/vfswatch.auto.inc"
// clang-format on
// Autogen ends -------

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

#include <cx/fs/path.h>

_objfactory_guaranteed FSWatchRooted* FSWatchRooted_create(_In_opt_ strref root, bool casei, closure cls)
{
    FSWatchRooted* self;
    self = objInstCreate(FSWatchRooted);

    strDup(&self->root, root);
    self->casei = casei;
    self->cls   = cls;

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

bool FSWatchRooted_add(_In_ FSWatchRooted* self, _In_opt_ strref path, flags_t flags)
{
    string full = 0;
    pathJoin(&full, self->root, path);
    bool ret = fsWatchAdd(self->inner, full, flags);
    if (ret)
        _fsWatchTargetAdded(FSWatch(self), path);
    strDestroy(&full);
    return ret;
}

bool FSWatchRooted_remove(_In_ FSWatchRooted* self, _In_opt_ strref path)
{
    string full = 0;
    pathJoin(&full, self->root, path);
    bool ret = fsWatchRemove(self->inner, full);
    if (ret)
        _fsWatchTargetRemoved(FSWatch(self), path);
    strDestroy(&full);
    return ret;
}

void FSWatchRooted_stopSources(_In_ FSWatchRooted* self)
{
    fsWatchCancel(self->inner);
}

void FSWatchRooted_destroy(_In_ FSWatchRooted* self)
{
    FSWatchRooted_stopSources(self);

    // Autogen begins -----
    objRelease(&self->inner);
    strDestroy(&self->root);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "fs/fswatchrooted.auto.inc"
// clang-format on
// Autogen ends -------

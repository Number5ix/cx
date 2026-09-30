#include <cx/closure.h>
#include <cx/container.h>
#include <cx/debug/error.h>
#include <cx/format.h>
#include <cx/fs.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/thread.h>
#include <cx/time/time.h>

#include "vfstestprov.h"

#define TEST_FILE vfswatchtest
#define TEST_FUNCS vfswatchtest_funcs
#include "common.h"

#define WAIT_TIMEOUT timeS(10)
#define K(kind) (1u << (kind))

// Everything a watch reported, and what the test is currently waiting for.
typedef struct VRec {
    Mutex lock;
    sa_int32 kinds;
    sa_string paths;
    sa_string targets;
    uint32 wantKinds;
    string wantPath;
    Event hit;

    // For cache_invalidated_before_callback: read this file through this VFS inside the callback.
    VFS* readVfs;
    string readPath;
    string readResult;
    FSWatch* cancelSelf;

    // For unmount_during_callback: on the first event, signal entered, sleep, then add addPath.
    FSWatch* addTo;
    string addPath;
    Event entered;
    atomic(bool) addDone;
} VRec;

static void recInit(VRec* r)
{
    memset(r, 0, sizeof(*r));
    mutexInit(&r->lock);
    saInit(&r->kinds, int32, 16);
    saInit(&r->paths, string, 16);
    saInit(&r->targets, string, 16);
    eventInit(&r->hit);
    eventInit(&r->entered);
}

static void recDestroy(VRec* r)
{
    saDestroy(&r->kinds);
    saDestroy(&r->paths);
    saDestroy(&r->targets);
    strDestroy(&r->wantPath);
    strDestroy(&r->readPath);
    strDestroy(&r->readResult);
    strDestroy(&r->addPath);
    eventDestroy(&r->hit);
    eventDestroy(&r->entered);
    mutexDestroy(&r->lock);
}

static void readAllVfs(VFS* vfs, strref path, string* out)
{
    uint8 buf[256];
    size_t n = 0;

    strClear(out);
    VFSFile* f = vfsOpen(vfs, path, FS_Read);
    if (!f)
        return;
    if (fileRead(f, buf, sizeof(buf), &n))
        strFromBytes(out, buf, (uint32)n);
    fileClose(&f);
}

static void onEvent(stvlist* cvars, FSWatch* watch, FSWatchEvent* ev)
{
    VRec* r = stvlNextPtr(cvars);

    if (r->readVfs && ev->kind == FSWE_Created && strEq(ev->path, r->readPath))
        readAllVfs(r->readVfs, r->readPath, &r->readResult);
    if (r->cancelSelf)
        fsWatchCancel(r->cancelSelf);
    if (r->addTo && !atomicLoad(bool, &r->addDone, Acquire)) {
        eventSignalLock(&r->entered);
        osSleep(timeMS(200));
        fsWatchAdd(r->addTo, r->addPath, 0);
        atomicStore(bool, &r->addDone, true, Release);
    }

    withMutex (&r->lock) {
        saPush(&r->kinds, int32, ev->kind);
        saPush(&r->paths, strref, ev->path);
        saPush(&r->targets, strref, ev->target);
        if ((r->wantKinds & K(ev->kind)) && strEq(ev->path, r->wantPath))
            eventSignalLock(&r->hit);
    }
}

static FSWatch* recWatch(VRec* r, VFS* vfs)
{
    return vfsWatchCreate(vfs, closureCreateAs(FSWatchCB, onEvent, stvar(ptr, r)));
}

static bool recSawLocked(VRec* r, uint32 kinds, strref path)
{
    for (int32 i = 0; i < saSize(r->kinds); i++) {
        if ((kinds & K(r->kinds.a[i])) && strEq(r->paths.a[i], path))
            return true;
    }
    return false;
}

static bool recSaw(VRec* r, uint32 kinds, strref path)
{
    bool ret = false;
    withMutex (&r->lock) {
        ret = recSawLocked(r, kinds, path);
    }
    return ret;
}

static bool recWait(VRec* r, uint32 kinds, strref path)
{
    bool seen = false;
    withMutex (&r->lock) {
        seen = recSawLocked(r, kinds, path);
        if (!seen) {
            eventReset(&r->hit);
            r->wantKinds = kinds;
            strDup(&r->wantPath, path);
        }
    }
    return seen || eventWaitTimeout(&r->hit, WAIT_TIMEOUT);
}

static const char* kindNames[] = { "?",       "Created",    "Removed", "Modified",
                                   "Renamed", "Attributes", "Rescan",  "Stopped" };

static void recDump(VRec* r, string* out)
{
    strDup(out, _S"events:");
    withMutex (&r->lock) {
        for (int32 i = 0; i < saSize(r->kinds); i++) {
            int32 k = r->kinds.a[i];
            strAppend(out, _S" [");
            strAppend(out, (strref)((k > 0 && k <= FSWE_Stopped) ? kindNames[k] : kindNames[0]));
            strAppend(out, _S" ");
            strAppend(out, r->paths.a[i]);
            strAppend(out, _S"]");
        }
    }
}

#define WFAIL(ret, r, msg, path)                                                              \
    do {                                                                                      \
        string _dump = 0;                                                                     \
        recDump(r, &_dump);                                                                   \
        TEST_FAILV(ret, 1, _SL(msg " '${string}'; ${string}"), stvar(strref, path),           \
                   stvar(string, _dump));                                                     \
        strDestroy(&_dump);                                                                   \
    } while (0)

// ---- layers --------------------------------------------------------------------------------

static int test_vfswatch_inject_basic(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs         = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* p   = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(p, _S"sub");
    vfsMountProvider(vfs, p, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(p, FSWE_Created, _S"a.txt", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/a.txt"))
        WFAIL(ret, &r, "no Created for", _S"/m/a.txt");

    withMutex (&r.lock) {
        if (saSize(r.targets) > 0 && !strEq(r.targets.a[0], _S"/m"))
            TEST_FAILV(ret, 1, _SL("target '${string}', wanted '/m'"), stvar(string, r.targets.a[0]));
    }

    // Not a subtree, so something two levels down is not reported; and without FSW_Self,
    // neither is a change to the watched directory itself.
    vfstestprovInject(p, FSWE_Created, _S"sub/deep.txt", NULL);
    vfstestprovInject(p, FSWE_Attributes, _S"", NULL);
    vfstestprovInject(p, FSWE_Created, _S"sentinel", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/sentinel"))
        WFAIL(ret, &r, "no Created for sentinel", _S"/m/sentinel");
    if (recSaw(&r, K(FSWE_Created), _S"/m/sub/deep.txt"))
        WFAIL(ret, &r, "directory watch reported something two levels down", _S"/m/sub/deep.txt");
    if (recSaw(&r, K(FSWE_Attributes), _S"/m"))
        WFAIL(ret, &r, "directory reported its own attributes without FSW_Self:", _S"/m");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&p);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// Every layer that can see a path is watched.
static int test_vfswatch_stacked_layers(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(lower, _S"d");
    vfstestprovAddDir(upper, _S"d");
    vfsMountProvider(vfs, lower, _S"/m");
    vfsMountProvider(vfs, upper, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m/d", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m/d) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(lower, FSWE_Created, _S"d/low", NULL);
    vfstestprovInject(upper, FSWE_Created, _S"d/up", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/d/low"))
        WFAIL(ret, &r, "no Created from the lower layer for", _S"/m/d/low");
    if (!recWait(&r, K(FSWE_Created), _S"/m/d/up"))
        WFAIL(ret, &r, "no Created from the upper layer for", _S"/m/d/up");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// An opaque mount inside a watched subtree hides what the layers above it have there.
static int test_vfswatch_opaque_hides_lower(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* base = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* over = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(base, _S"sub");
    vfsMountProvider(vfs, base, _S"/m");
    vfsMountProvider(vfs, over, _S"/m/sub", VFS_Opaque);

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m", FSW_Subtree)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(base, FSWE_Created, _S"sub/hidden", NULL);
    vfstestprovInject(over, FSWE_Created, _S"shown", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/sub/shown"))
        WFAIL(ret, &r, "no Created from the opaque layer for", _S"/m/sub/shown");

    vfstestprovInject(base, FSWE_Created, _S"sentinel", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/sentinel"))
        WFAIL(ret, &r, "no Created for sentinel", _S"/m/sentinel");
    if (recSaw(&r, K(FSWE_Created), _S"/m/sub/hidden"))
        WFAIL(ret, &r, "change hidden by an opaque mount was reported", _S"/m/sub/hidden");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&base);
    objRelease(&over);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

static int test_vfswatch_nonwatchable_layer(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* blind = vfstestprovCreate(VFS_CaseSensitive);
    blind->failmask    = VFSTP_FailWatch;
    vfstestprovAddDir(blind, _S"d");
    vfsMountProvider(vfs, blind, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (fsWatchAdd(w, _S"/m/d", 0))
        TEST_FAILV(ret, 1, _SL("watching through only unwatchable providers succeeded"), stvNone);
    else if (cxerr != CX_NotSupported)
        TEST_FAILV(ret, 1, _SL("cxerr ${int}, wanted CX_NotSupported"), stvar(int32, cxerr));

    // With a watchable layer on top, the path is watched through that one.
    VFSTestProv* seeing = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(seeing, _S"d");
    vfsMountProvider(vfs, seeing, _S"/m");
    if (!fsWatchAdd(w, _S"/m/d", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd with one watchable layer failed: ${int}"),
                   stvar(int32, cxerr));
    } else {
        vfstestprovInject(seeing, FSWE_Created, _S"d/x", NULL);
        if (!recWait(&r, K(FSWE_Created), _S"/m/d/x"))
            WFAIL(ret, &r, "no Created for", _S"/m/d/x");
    }

    fsWatchCancel(w);

    objRelease(&w);
    objRelease(&blind);
    objRelease(&seeing);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// ---- mounts --------------------------------------------------------------------------------

static int test_vfswatch_mount_under_subtree(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* base = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* late = vfstestprovCreate(VFS_CaseSensitive);
    vfsMountProvider(vfs, base, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m", FSW_Subtree)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfsMountProvider(vfs, late, _S"/m/new");
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/new"))
        WFAIL(ret, &r, "no Rescan after mounting at", _S"/m/new");

    vfstestprovInject(late, FSWE_Created, _S"f", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/new/f"))
        WFAIL(ret, &r, "provider mounted after the watch started not watched;", _S"/m/new/f");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&base);
    objRelease(&late);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

static int test_vfswatch_unmount(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* base = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* gone = vfstestprovCreate(VFS_CaseSensitive);
    vfsMountProvider(vfs, base, _S"/m");
    vfsMountProvider(vfs, gone, _S"/m/g");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m", FSW_Subtree)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(gone, FSWE_Created, _S"before", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/g/before"))
        WFAIL(ret, &r, "no Created through the child mount for", _S"/m/g/before");

    vfsUnmount(vfs, _S"/m/g");
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/g"))
        WFAIL(ret, &r, "no Rescan after unmounting", _S"/m/g");

    vfstestprovInject(gone, FSWE_Created, _S"after", NULL);
    vfstestprovInject(base, FSWE_Created, _S"sentinel", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/sentinel"))
        WFAIL(ret, &r, "no Created for sentinel", _S"/m/sentinel");
    if (recSaw(&r, K(FSWE_Created), _S"/m/g/after"))
        WFAIL(ret, &r, "unmounted provider still reported", _S"/m/g/after");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&base);
    objRelease(&gone);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// Unmounting a provider while a callback reached through it is adding to the same watch. The
// unmount re-plans the watch and drops that provider's layer while the callback waits to add.
static int test_vfswatch_unmount_during_callback(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs          = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* base = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* gone = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(base, _S"other");
    vfsMountProvider(vfs, base, _S"/m");
    vfsMountProvider(vfs, gone, _S"/m/g");

    FSWatch* w = recWatch(&r, vfs);
    r.addTo    = w;
    strDup(&r.addPath, _S"/m/other");
    if (!fsWatchAdd(w, _S"/m", FSW_Subtree)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(gone, FSWE_Created, _S"x", NULL);
    if (!eventWaitTimeout(&r.entered, WAIT_TIMEOUT)) {
        WFAIL(ret, &r, "callback never ran for", _S"/m/g/x");
        goto out;
    }

    vfsUnmount(vfs, _S"/m/g");
    for (int i = 0; i < 1000 && !atomicLoad(bool, &r.addDone, Acquire); i++) osSleep(timeMS(10));
    if (!atomicLoad(bool, &r.addDone, Acquire))
        TEST_FAILV(ret, 1, _SL("callback never finished adding a path"), stvNone);

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&base);
    objRelease(&gone);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// A callback that looks at the VFS sees the change it is being told about, not a stale cache.
static int test_vfswatch_cache_invalidated(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddFile(lower, _S"f.txt", _S"lower");
    vfsMountProvider(vfs, lower, _S"/m");
    vfsMountProvider(vfs, upper, _S"/m");

    // Remember the file as coming from the lower layer.
    string contents = 0;
    readAllVfs(vfs, _S"/m/f.txt", &contents);
    if (!strEq(contents, _S"lower"))
        TEST_FAILV(ret, 1, _SL("read '${string}' before the change, wanted 'lower'"),
                   stvar(string, contents));

    r.readVfs = vfs;
    strDup(&r.readPath, _S"/m/f.txt");
    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    // The upper layer gains the file behind the VFS's back, and says so.
    vfstestprovAddFile(upper, _S"f.txt", _S"upper");
    vfstestprovInject(upper, FSWE_Created, _S"f.txt", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/f.txt"))
        WFAIL(ret, &r, "no Created for", _S"/m/f.txt");
    else if (!strEq(r.readResult, _S"upper"))
        TEST_FAILV(ret, 1, _SL("callback read '${string}', wanted 'upper'"),
                   stvar(string, r.readResult));

out:
    strDestroy(&contents);
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// ---- VFS in a VFS --------------------------------------------------------------------------

static int test_vfswatch_vfsvfs_forward(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* inner         = vfsCreate(VFS_CaseSensitive);
    VFS* outer         = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* p     = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* extra = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(p, _S"data");
    vfsMountProvider(inner, p, _S"/");
    vfsMountVFS(outer, _S"/x", inner, _S"/data");

    FSWatch* w = recWatch(&r, outer);
    if (!fsWatchAdd(w, _S"/x", FSW_Subtree)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/x) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(p, FSWE_Created, _S"data/f", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/x/f"))
        WFAIL(ret, &r, "change in the inner VFS not reported at", _S"/x/f");

    // A mount inside the inner VFS reaches the outer watch too.
    vfsMountProvider(inner, extra, _S"/data/sub");
    if (!recWait(&r, K(FSWE_Rescan), _S"/x/sub"))
        WFAIL(ret, &r, "no Rescan for a mount inside the inner VFS at", _S"/x/sub");
    vfstestprovInject(extra, FSWE_Created, _S"g", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/x/sub/g"))
        WFAIL(ret, &r, "no Created through the inner mount at", _S"/x/sub/g");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&p);
    objRelease(&extra);
    vfsDestroy(&outer);
    vfsDestroy(&inner);
    recDestroy(&r);
    return ret;
}

// A VFS mounted inside itself must not send a subtree watch round forever.
static int test_vfswatch_vfsvfs_loop_guard(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs       = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* p = vfstestprovCreate(VFS_CaseSensitive);
    vfsMountProvider(vfs, p, _S"/");
    vfsMountVFS(vfs, _S"/loop", vfs, _S"/");

    FSWatch* w = recWatch(&r, vfs);
    fsWatchAdd(w, _S"/", FSW_Subtree);   // succeeding or not; returning is the point

    vfstestprovInject(p, FSWE_Created, _S"f", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/f"))
        WFAIL(ret, &r, "no Created at the top of a looped VFS for", _S"/f");

    fsWatchCancel(w);

    objRelease(&w);
    objRelease(&p);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

static int test_vfswatch_cancel_in_callback(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs       = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* p = vfstestprovCreate(VFS_CaseSensitive);
    vfsMountProvider(vfs, p, _S"/m");

    FSWatch* w   = recWatch(&r, vfs);
    r.cancelSelf = w;
    if (!fsWatchAdd(w, _S"/m", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovInject(p, FSWE_Created, _S"a", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/a"))
        WFAIL(ret, &r, "no Created for", _S"/m/a");

    vfstestprovInject(p, FSWE_Created, _S"b", NULL);
    osSleep(timeMS(200));
    if (recSaw(&r, K(FSWE_Created), _S"/m/b"))
        WFAIL(ret, &r, "cancelled watch still reported", _S"/m/b");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&p);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// ---- paths appearing in a layer -------------------------------------------------------------

// Waits for a provider's watch to be armed on path, which happens off the test thread when it
// follows a change.
static bool waitWatching(VFSTestProv* p, strref path)
{
    for (int i = 0; i < 1000; i++) {
        if (vfstestprovWatching(p, path))
            return true;
        osSleep(timeMS(10));
    }
    return false;
}

// A directory watched through the lower layer, then created in the upper one, is watched there
// from then on.
static int test_vfswatch_upper_appears(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(lower, _S"d");
    vfsMountProvider(vfs, lower, _S"/m");
    vfsMountProvider(vfs, upper, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m/d", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m/d) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovAddDir(upper, _S"d");
    vfstestprovInject(upper, FSWE_Created, _S"d", NULL);
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/d"))
        WFAIL(ret, &r, "no Rescan once the upper layer had", _S"/m/d");

    vfstestprovAddFile(upper, _S"d/x", _S"x");
    vfstestprovInject(upper, FSWE_Created, _S"d/x", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/d/x"))
        WFAIL(ret, &r, "directory created in the upper layer not watched;", _S"/m/d/x");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// A subtree whose whole chain of directories appears in the upper layer at once, and a file
// whose parent directories do.
static int test_vfswatch_upper_appears_deep(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(lower, _S"d");
    vfstestprovAddFile(lower, _S"a/b/f.txt", _S"low");
    vfsMountProvider(vfs, lower, _S"/m");
    vfsMountProvider(vfs, upper, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m/d", FSW_Subtree) || !fsWatchAdd(w, _S"/m/a/b/f.txt", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    vfstestprovAddDir(upper, _S"d/e/f");
    vfstestprovInject(upper, FSWE_Created, _S"d", NULL);
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/d"))
        WFAIL(ret, &r, "no Rescan once the upper layer had", _S"/m/d");
    vfstestprovInject(upper, FSWE_Created, _S"d/e/f/g", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/d/e/f/g"))
        WFAIL(ret, &r, "subtree created in the upper layer not watched;", _S"/m/d/e/f/g");

    vfstestprovAddDir(upper, _S"a/b");
    vfstestprovInject(upper, FSWE_Created, _S"a", NULL);
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/a/b/f.txt"))
        WFAIL(ret, &r, "no Rescan once the upper layer had the parent of", _S"/m/a/b/f.txt");
    vfstestprovAddFile(upper, _S"a/b/f.txt", _S"up");
    vfstestprovInject(upper, FSWE_Modified, _S"a/b/f.txt", NULL);
    if (!recWait(&r, K(FSWE_Modified), _S"/m/a/b/f.txt"))
        WFAIL(ret, &r, "file in new upper-layer directories not watched;", _S"/m/a/b/f.txt");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// A directory removed from the upper layer while the lower one still has it keeps the watch
// alive, and is watched in the upper layer again once it comes back.
static int test_vfswatch_upper_recreated(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    VFS* vfs           = vfsCreate(VFS_CaseSensitive);
    VFSTestProv* lower = vfstestprovCreate(VFS_CaseSensitive);
    VFSTestProv* upper = vfstestprovCreate(VFS_CaseSensitive);
    vfstestprovAddDir(lower, _S"d");
    vfstestprovAddDir(upper, _S"d");
    vfsMountProvider(vfs, lower, _S"/m");
    vfsMountProvider(vfs, upper, _S"/m");

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/m/d", 0)) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd(/m/d) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    // The upper layer's directory goes away, as its watch would report it.
    vfstestprovRemoveDir(upper, _S"d");
    vfstestprovInject(upper, FSWE_Stopped, _S"d", NULL);
    if (!waitWatching(upper, _S"")) {
        TEST_FAILV(ret, 1, _SL("upper layer never watched for d to come back"), stvNone);
        goto out;
    }

    vfstestprovAddDir(upper, _S"d");
    vfstestprovInject(upper, FSWE_Created, _S"d", NULL);
    if (!recWait(&r, K(FSWE_Rescan), _S"/m/d"))
        WFAIL(ret, &r, "no Rescan once the upper layer had", _S"/m/d");
    vfstestprovInject(upper, FSWE_Created, _S"d/y", NULL);
    if (!recWait(&r, K(FSWE_Created), _S"/m/d/y"))
        WFAIL(ret, &r, "recreated upper-layer directory not watched;", _S"/m/d/y");
    if (recSaw(&r, K(FSWE_Stopped), _S"/m/d"))
        WFAIL(ret, &r, "Stopped while the lower layer still has", _S"/m/d");

out:
    fsWatchCancel(w);
    objRelease(&w);
    objRelease(&lower);
    objRelease(&upper);
    vfsDestroy(&vfs);
    recDestroy(&r);
    return ret;
}

// ---- end to end ----------------------------------------------------------------------------

static int test_vfswatch_vfsfs_end_to_end(void)
{
    int ret = 0;
    VRec r;
    recInit(&r);

    string dir = 0, f = 0;
    pathMakeAbsolute(&dir, _S"cx_vfswatchtest_e2e");
    fsCreateAll(dir);
    pathJoin(&f, dir, _S"real.txt");
    fsDelete(f);

    VFS* vfs = vfsCreate(0);
    vfsMountFS(vfs, _S"/root", dir);

    FSWatch* w = recWatch(&r, vfs);
    if (!fsWatchAdd(w, _S"/root", 0)) {
        if (cxerr == CX_NotSupported)
            TEST_INFO(_SL("native watches are not supported here; skipping"), stvNone);
        else
            TEST_FAILV(ret, 1, _SL("fsWatchAdd(/root) failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    FSFile* fh = fsOpen(f, FS_Overwrite);
    fileClose(&fh);
    if (!recWait(&r, K(FSWE_Created), _S"/root/real.txt"))
        WFAIL(ret, &r, "native change not reported at", _S"/root/real.txt");

out:
    fsWatchCancel(w);
    objRelease(&w);
    vfsDestroy(&vfs);
    fsDelete(f);
    fsRemoveDir(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// ---- groups --------------------------------------------------------------------------------

// Each group below runs several of the subtests above in one process. The individual subtests
// stay registered under their own names too, for running or debugging one in isolation.

int test_vfswatch_grp_layers(void)
{
    TEST_CHAIN(test_vfswatch_inject_basic, test_vfswatch_stacked_layers,
               test_vfswatch_opaque_hides_lower, test_vfswatch_nonwatchable_layer,
               test_vfswatch_cache_invalidated, test_vfswatch_cancel_in_callback);
}

int test_vfswatch_grp_mounts(void)
{
    TEST_CHAIN(test_vfswatch_mount_under_subtree, test_vfswatch_unmount,
               test_vfswatch_unmount_during_callback, test_vfswatch_vfsvfs_forward,
               test_vfswatch_vfsvfs_loop_guard);
}

int test_vfswatch_grp_appear(void)
{
    TEST_CHAIN(test_vfswatch_upper_appears, test_vfswatch_upper_appears_deep,
               test_vfswatch_upper_recreated);
}

int test_vfswatch_grp_e2e(void)
{
    TEST_CHAIN(test_vfswatch_vfsfs_end_to_end);
}

testfunc vfswatchtest_funcs[] = {
    { "inject_basic",      test_vfswatch_inject_basic        },
    { "stacked_layers",    test_vfswatch_stacked_layers      },
    { "opaque_hides_lower", test_vfswatch_opaque_hides_lower },
    { "nonwatchable_layer", test_vfswatch_nonwatchable_layer },
    { "cache_invalidated", test_vfswatch_cache_invalidated   },
    { "cancel_in_callback", test_vfswatch_cancel_in_callback },
    { "mount_under_subtree", test_vfswatch_mount_under_subtree },
    { "unmount",           test_vfswatch_unmount             },
    { "unmount_during_callback", test_vfswatch_unmount_during_callback },
    { "vfsvfs_forward",    test_vfswatch_vfsvfs_forward      },
    { "vfsvfs_loop_guard", test_vfswatch_vfsvfs_loop_guard   },
    { "vfsfs_end_to_end",  test_vfswatch_vfsfs_end_to_end    },
    { "upper_appears",     test_vfswatch_upper_appears       },
    { "upper_appears_deep", test_vfswatch_upper_appears_deep },
    { "upper_recreated",   test_vfswatch_upper_recreated     },
    { "grp_layers",        test_vfswatch_grp_layers          },
    { "grp_mounts",        test_vfswatch_grp_mounts          },
    { "grp_appear",        test_vfswatch_grp_appear          },
    { "grp_e2e",           test_vfswatch_grp_e2e             },
    { 0, 0 }
};

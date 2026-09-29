#include <cx/closure.h>
#include <cx/container.h>
#include <cx/debug/error.h>
#include <cx/format.h>
#include <cx/fs.h>
#include <cx/fs/fswatch_private.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/thread.h>
#include <cx/time/clock.h>
#include <cx/time/time.h>

#define TEST_FILE fswatchtest
#define TEST_FUNCS fswatchtest_funcs
#include "common.h"

// How long to wait for an event that should arrive. Generous, because a loaded CI machine can
// be slow to schedule the watcher threads; a passing run never waits this long.
#define WAIT_TIMEOUT timeS(10)

// Everything a watch reported, and what the test is currently waiting for.
typedef struct WRec {
    Mutex lock;
    sa_int32 kinds;
    sa_string paths;
    sa_string olds;
    sa_string targets;

    // Waiting for an event of one of these kinds (a mask of 1 << kind) at this path.
    uint32 wantKinds;
    string wantPath;
    Event hit;

    // Optional behavior, for the lifecycle tests.
    FSWatch* cancelSelf;    // cancel this watch from inside the first callback
    int64 sleepFor;         // sleep this long inside each callback
    atomic(bool) inCallback;
    atomic(bool) finished;  // set as a callback returns
    Event* block;           // wait on this inside the first callback
    Event entered;          // signaled when a callback starts
    FSWatch* addTo;         // on the first callback, add addPath to this watch
    string addPath;
    atomic(int32) calls;
} WRec;

static void recInit(WRec* r)
{
    memset(r, 0, sizeof(*r));
    mutexInit(&r->lock);
    saInit(&r->kinds, int32, 16);
    saInit(&r->paths, string, 16);
    saInit(&r->olds, string, 16);
    saInit(&r->targets, string, 16);
    eventInit(&r->hit);
    eventInit(&r->entered);
}

static void recDestroy(WRec* r)
{
    saDestroy(&r->kinds);
    saDestroy(&r->paths);
    saDestroy(&r->olds);
    saDestroy(&r->targets);
    strDestroy(&r->wantPath);
    strDestroy(&r->addPath);
    eventDestroy(&r->hit);
    eventDestroy(&r->entered);
    mutexDestroy(&r->lock);
}

static void onEvent(stvlist* cvars, FSWatch* watch, FSWatchEvent* ev)
{
    WRec* r = stvlNextPtr(cvars);
    int32 n = atomicFetchAdd(int32, &r->calls, 1, AcqRel);

    atomicStore(bool, &r->inCallback, true, Release);
    eventSignalLock(&r->entered);

    withMutex (&r->lock) {
        saPush(&r->kinds, int32, ev->kind);
        saPush(&r->paths, strref, ev->path);
        saPush(&r->olds, strref, ev->oldpath);
        saPush(&r->targets, strref, ev->target);

        if ((r->wantKinds & (1u << ev->kind)) && strEq(ev->path, r->wantPath))
            eventSignalLock(&r->hit);
    }

    if (n == 0 && r->block)
        eventWait(r->block);
    if (n == 0 && r->cancelSelf)
        fsWatchCancel(r->cancelSelf);
    if (n == 0 && r->addTo)
        fsWatchAdd(r->addTo, r->addPath, 0);
    if (r->sleepFor)
        osSleep(r->sleepFor);

    atomicStore(bool, &r->inCallback, false, Release);
    atomicStore(bool, &r->finished, true, Release);
}

static FSWatch* recWatch(WRec* r)
{
    return fsWatchCreate(closureCreateAs(FSWatchCB, onEvent, stvar(ptr, r)));
}

static bool recSawLocked(WRec* r, uint32 kinds, strref path)
{
    for (int32 i = 0; i < saSize(r->kinds); i++) {
        if ((kinds & (1u << r->kinds.a[i])) && strEq(r->paths.a[i], path))
            return true;
    }
    return false;
}

// Has an event of one of these kinds been seen at path?
static bool recSaw(WRec* r, uint32 kinds, strref path)
{
    bool ret = false;
    withMutex (&r->lock) {
        ret = recSawLocked(r, kinds, path);
    }
    return ret;
}

// Waits for an event of one of these kinds at path, whether it already arrived or not.
static bool recWait(WRec* r, uint32 kinds, strref path)
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

static const char* kindNames[] = { "?",      "Created", "Removed", "Modified",
                                   "Renamed", "Attributes", "Rescan", "Stopped" };

// Everything seen so far, for a failure message.
static void recDump(WRec* r, string* out)
{
    strDup(out, _S"events:");
    withMutex (&r->lock) {
        for (int32 i = 0; i < saSize(r->kinds); i++) {
            int32 k = r->kinds.a[i];
            strAppend(out, _S" [");
            strAppend(out, (strref)((k > 0 && k <= FSWE_Stopped) ? kindNames[k] : kindNames[0]));
            strAppend(out, _S" ");
            strAppend(out, r->paths.a[i]);
            if (r->olds.a[i]) {
                strAppend(out, _S" <- ");
                strAppend(out, r->olds.a[i]);
            }
            strAppend(out, _S"]");
        }
    }
}

#define K(kind) (1u << (kind))

// A test failure that shows what the watch actually reported.
#define WFAIL(ret, r, msg, path)                                                              \
    do {                                                                                      \
        string _dump = 0;                                                                     \
        recDump(r, &_dump);                                                                   \
        TEST_FAILV(ret, 1, _SL(msg " '${string}'; ${string}"), stvar(strref, path),           \
                   stvar(string, _dump));                                                     \
        strDestroy(&_dump);                                                                   \
    } while (0)

// ---- scratch directories -------------------------------------------------------------------

static void rmTree(strref path)
{
    if (fsStat(path, NULL) == FS_File) {
        fsDelete(path);
        return;
    }

    FSSearchIter it;
    sa_string names;
    saInit(&names, string, 8);
    for (fsSearchInit(&it, path, NULL, false); fsSearchValid(&it); fsSearchNext(&it))
        saPush(&names, string, it.name);
    fsSearchFinish(&it);

    string child = 0;
    foreach (sarray, i, string, name, names) {
        pathJoin(&child, path, name);
        rmTree(child);
    }
    strDestroy(&child);
    saDestroy(&names);
    fsRemoveDir(path);
}

// A fresh, empty, absolute scratch directory for one test.
static void scratchDir(string* out, strref name)
{
    string rel = 0;
    strConcat(&rel, _S"cx_fswatchtest_", name);
    pathMakeAbsolute(out, rel);
    strDestroy(&rel);

    rmTree(*out);
    fsCreateAll(*out);
}

static bool writeFile(strref path, strref contents)
{
    FSFile* f = fsOpen(path, FS_Overwrite);
    if (!f)
        return false;
    bool ret = fileWriteString(f, contents, NULL);
    return fileClose(&f) && ret;
}

// Join a scratch directory and a relative name.
static string sub(string* out, strref dir, strref name)
{
    pathJoin(out, dir, name);
    return *out;
}

// Returns true (and logs) when this platform cannot watch, so the test can pass as skipped.
static bool unsupported(bool added)
{
    if (!added && cxerr == CX_NotSupported) {
        TEST_INFO(_SL("native watches are not supported here; skipping"), stvNone);
        return true;
    }
    return false;
}

// ---- basic events --------------------------------------------------------------------------

static int test_fswatch_create_remove(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"create_remove");
    sub(&f, dir, _S"a.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, 0);
    if (unsupported(added))
        goto out;
    if (!added) {
        TEST_FAILV(ret, 1, _SL("fsWatchAdd failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    writeFile(f, _S"hello");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created for", f);

    fsDelete(f);
    if (!recWait(&r, K(FSWE_Removed), f))
        WFAIL(ret, &r, "no Removed for", f);

    withMutex (&r.lock) {
        for (int32 i = 0; i < saSize(r.targets); i++) {
            if (!strEq(r.targets.a[i], dir))
                TEST_FAILV(ret, 1, _SL("event target '${string}', wanted '${string}'"),
                           stvar(string, r.targets.a[i]), stvar(string, dir));
        }
    }

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_modify(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"modify");
    sub(&f, dir, _S"a.txt");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, 0);
    if (unsupported(added))
        goto out;

    FSFile* fh = fsOpen(f, FS_Write);
    fileWriteString(fh, _S"two", NULL);
    fileClose(&fh);
    if (!recWait(&r, K(FSWE_Modified), f))
        WFAIL(ret, &r, "no Modified for", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_attributes(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"attributes");
    sub(&f, dir, _S"a.txt");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Attributes);
    if (unsupported(added))
        goto out;

    // A day ago: cx times count from the Julian epoch, so a small number is a date no platform
    // can store.
    int64 when = clockWall() - timeS(86400);
    if (!fsSetTimes(f, when, when))
        TEST_FAILV(ret, 1, _SL("fsSetTimes('${string}') failed: ${int}"), stvar(string, f),
                   stvar(int32, cxerr));
    if (!recWait(&r, K(FSWE_Attributes), f))
        WFAIL(ret, &r, "no Attributes for", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// A watch that asked only for names hears nothing about contents.
static int test_fswatch_filter_names_only(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0, sentinel = 0;
    scratchDir(&dir, _S"filter");
    sub(&f, dir, _S"a.txt");
    sub(&sentinel, dir, _S"sentinel");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Names);
    if (unsupported(added))
        goto out;

    FSFile* fh = fsOpen(f, FS_Write);
    fileWriteString(fh, _S"two", NULL);
    fileClose(&fh);

    // Events arrive in order, so once the sentinel is seen anything about the write would have.
    writeFile(sentinel, _S"");
    if (!recWait(&r, K(FSWE_Created), sentinel))
        WFAIL(ret, &r, "no Created for sentinel", sentinel);
    if (recSaw(&r, K(FSWE_Modified), f))
        WFAIL(ret, &r, "names-only watch reported Modified for", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&sentinel);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_rename_same_dir(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, a = 0, b = 0;
    scratchDir(&dir, _S"rename_same");
    sub(&a, dir, _S"a.txt");
    sub(&b, dir, _S"b.txt");
    writeFile(a, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, 0);
    if (unsupported(added))
        goto out;

    fsRename(a, b);
    if (!recWait(&r, K(FSWE_Renamed), b)) {
        WFAIL(ret, &r, "no Renamed to", b);
    } else {
        withMutex (&r.lock) {
            for (int32 i = 0; i < saSize(r.kinds); i++) {
                if (r.kinds.a[i] == FSWE_Renamed && !strEq(r.olds.a[i], a))
                    TEST_FAILV(ret, 1, _SL("Renamed old path '${string}', wanted '${string}'"),
                               stvar(string, r.olds.a[i]), stvar(string, a));
            }
        }
    }

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&a);
    strDestroy(&b);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_rename_cross_dir(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d1 = 0, d2 = 0, a = 0, b = 0;
    scratchDir(&dir, _S"rename_cross");
    sub(&d1, dir, _S"one");
    sub(&d2, dir, _S"two");
    fsCreateDir(d1);
    fsCreateDir(d2);
    sub(&a, d1, _S"a.txt");
    sub(&b, d2, _S"a.txt");
    writeFile(a, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, d1, 0) && fsWatchAdd(w, d2, 0);
    if (unsupported(added))
        goto out;

    fsRename(a, b);
    // Either the whole rename, or its two halves.
    if (!recWait(&r, K(FSWE_Renamed) | K(FSWE_Created), b))
        WFAIL(ret, &r, "move not reported at", b);
    if (!recSaw(&r, K(FSWE_Renamed), b) && !recWait(&r, K(FSWE_Removed), a))
        WFAIL(ret, &r, "move not reported at", a);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&a);
    strDestroy(&b);
    strDestroy(&d1);
    strDestroy(&d2);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// ---- targets -------------------------------------------------------------------------------

// Saving by writing a temporary file and renaming it over the original is the common case a
// file watch has to survive.
static int test_fswatch_file_target_replace(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0, tmp = 0;
    scratchDir(&dir, _S"replace");
    sub(&f, dir, _S"config.txt");
    sub(&tmp, dir, _S"config.txt.tmp");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, f, 0);
    if (unsupported(added))
        goto out;

    writeFile(tmp, _S"two");
    // fsRename replaces an existing file on Unix but not on Windows, where saving goes through a
    // delete first. Either way the file is replaced under the watch's nose.
    if (!fsRename(tmp, f)) {
        fsDelete(f);
        if (!fsRename(tmp, f))
            TEST_FAILV(ret, 1, _SL("could not rename over '${string}'"), stvar(string, f));
    }
    if (!recWait(&r, K(FSWE_Created) | K(FSWE_Renamed), f))
        WFAIL(ret, &r, "replacing the file not reported at", f);

    // And it is still watched afterwards.
    FSFile* fh = fsOpen(f, FS_Write);
    fileWriteString(fh, _S"three", NULL);
    fileClose(&fh);
    if (!recWait(&r, K(FSWE_Modified), f))
        WFAIL(ret, &r, "no Modified after replacing", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&tmp);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// A file watched by name hears nothing about the other files next to it.
static int test_fswatch_file_target_siblings(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0, other = 0;
    scratchDir(&dir, _S"siblings");
    sub(&f, dir, _S"watched.txt");
    sub(&other, dir, _S"other.txt");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, f, 0);
    if (unsupported(added))
        goto out;

    writeFile(other, _S"x");
    fsDelete(other);

    FSFile* fh = fsOpen(f, FS_Write);
    fileWriteString(fh, _S"two", NULL);
    fileClose(&fh);
    if (!recWait(&r, K(FSWE_Modified), f))
        WFAIL(ret, &r, "no Modified for", f);

    withMutex (&r.lock) {
        for (int32 i = 0; i < saSize(r.paths); i++) {
            if (!strEq(r.paths.a[i], f))
                TEST_FAILV(ret, 1, _SL("file watch reported unrelated '${string}'"),
                           stvar(string, r.paths.a[i]));
        }
    }

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&other);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_nonexistent_entry(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"nonexistent");
    sub(&f, dir, _S"later.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, f, 0);
    if (unsupported(added))
        goto out;
    if (!added) {
        TEST_FAILV(ret, 1, _SL("watching a missing file failed: ${int}"), stvar(int32, cxerr));
        goto out;
    }

    writeFile(f, _S"now");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created for", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_nonexistent_parent(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"noparent");
    sub(&f, dir, _S"missing/later.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, f, 0);
    if (unsupported(added))
        goto out;
    if (added)
        TEST_FAILV(ret, 1, _SL("watching '${string}' succeeded without its directory"),
                   stvar(string, f));
    else if (cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("cxerr ${int}, wanted CX_FileNotFound"), stvar(int32, cxerr));

    sub(&f, dir, _S"file.txt");
    writeFile(f, _S"x");
    if (fsWatchAdd(w, f, FSW_Subtree) || cxerr != CX_InvalidArgument)
        TEST_FAILV(ret, 1, _SL("FSW_Subtree on a file: cxerr ${int}, wanted CX_InvalidArgument"),
                   stvar(int32, cxerr));

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// One watch, several targets, one callback -- and each event names the target it fell under.
static int test_fswatch_multi_target(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d1 = 0, d2 = 0, f1 = 0, f2 = 0;
    scratchDir(&dir, _S"multi");
    sub(&d1, dir, _S"one");
    sub(&d2, dir, _S"two");
    fsCreateDir(d1);
    fsCreateDir(d2);
    sub(&f1, d1, _S"a");
    sub(&f2, d2, _S"b");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, d1, 0) && fsWatchAdd(w, d2, 0);
    if (unsupported(added))
        goto out;

    writeFile(f1, _S"1");
    writeFile(f2, _S"2");
    if (!recWait(&r, K(FSWE_Created), f1))
        WFAIL(ret, &r, "no Created for", f1);
    if (!recWait(&r, K(FSWE_Created), f2))
        WFAIL(ret, &r, "no Created for", f2);

    withMutex (&r.lock) {
        for (int32 i = 0; i < saSize(r.paths); i++) {
            strref want = strEq(r.paths.a[i], f1) ? d1 : d2;
            if (!strEq(r.targets.a[i], want))
                TEST_FAILV(ret, 1, _SL("event for '${string}' has target '${string}', wanted '${string}'"),
                           stvar(string, r.paths.a[i]), stvar(string, r.targets.a[i]),
                           stvar(strref, want));
        }
    }

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f1);
    strDestroy(&f2);
    strDestroy(&d1);
    strDestroy(&d2);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_remove_target(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d1 = 0, d2 = 0, f1 = 0, sentinel = 0;
    scratchDir(&dir, _S"remove_target");
    sub(&d1, dir, _S"one");
    sub(&d2, dir, _S"two");
    fsCreateDir(d1);
    fsCreateDir(d2);
    sub(&f1, d1, _S"a");
    sub(&sentinel, d2, _S"sentinel");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, d1, 0) && fsWatchAdd(w, d2, 0);
    if (unsupported(added))
        goto out;

    if (!fsWatchRemove(w, d1))
        TEST_FAILV(ret, 1, _SL("fsWatchRemove('${string}') failed"), stvar(string, d1));
    if (fsWatchRemove(w, d1))
        TEST_FAILV(ret, 1, _SL("removing '${string}' twice succeeded"), stvar(string, d1));

    writeFile(f1, _S"1");
    writeFile(sentinel, _S"");
    if (!recWait(&r, K(FSWE_Created), sentinel))
        WFAIL(ret, &r, "no Created for sentinel", sentinel);
    if (recSaw(&r, K(FSWE_Created), f1))
        WFAIL(ret, &r, "removed target still reported", f1);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f1);
    strDestroy(&sentinel);
    strDestroy(&d1);
    strDestroy(&d2);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// Adding a path again replaces its flags.
static int test_fswatch_dup_add_updates_flags(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"dup_add");
    sub(&f, dir, _S"a.txt");
    writeFile(f, _S"one");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Names) && fsWatchAdd(w, dir, FSW_Contents);
    if (unsupported(added))
        goto out;

    FSFile* fh = fsOpen(f, FS_Write);
    fileWriteString(fh, _S"two", NULL);
    fileClose(&fh);
    if (!recWait(&r, K(FSWE_Modified), f))
        WFAIL(ret, &r, "re-adding with FSW_Contents did not report Modified for", f);

    // One target, so one removal empties the watch.
    if (!fsWatchRemove(w, dir) || fsWatchRemove(w, dir))
        TEST_FAILV(ret, 1, _SL("adding a path twice did not leave exactly one target"), stvNone);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// A watched directory that is deleted stops its target. Only with FSW_Self is the deletion
// itself reported too.
static int dirRemovedCase(bool self)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d = 0;
    scratchDir(&dir, self ? _S"dir_removed_self" : _S"dir_removed");
    sub(&d, dir, _S"victim");
    fsCreateDir(d);

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, d, self ? FSW_Self : 0);
    if (unsupported(added))
        goto out;

    if (!fsRemoveDir(d))
        TEST_FAILV(ret, 1, _SL("could not remove watched directory '${string}': ${int}"),
                   stvar(string, d), stvar(int32, cxerr));
    if (!recWait(&r, K(FSWE_Stopped), d))
        WFAIL(ret, &r, "no Stopped for", d);
    // Stopped is the last thing a target reports, so anything else about it has arrived.
    if (self && !recSaw(&r, K(FSWE_Removed), d))
        WFAIL(ret, &r, "no Removed with FSW_Self for", d);
    if (!self && recSaw(&r, K(FSWE_Removed) | K(FSWE_Renamed) | K(FSWE_Attributes), d))
        WFAIL(ret, &r, "directory reported on itself without FSW_Self:", d);
    if (fsWatchRemove(w, d))
        TEST_FAILV(ret, 1, _SL("stopped target '${string}' was still registered"), stvar(string, d));

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&d);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_target_dir_removed(void)
{
    return dirRemovedCase(false);
}

static int test_fswatch_target_dir_removed_self(void)
{
    return dirRemovedCase(true);
}

// A watched directory moved away stops its target, rather than going on reporting changes
// under a path that no longer exists. (On Windows, without FSW_Self, that is noticed when the
// next change inside it arrives -- which the file written below provides.)
static int test_fswatch_target_dir_renamed(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d = 0, moved = 0, f = 0, oldf = 0;
    scratchDir(&dir, _S"dir_renamed");
    sub(&d, dir, _S"before");
    sub(&moved, dir, _S"after");
    sub(&f, moved, _S"file.txt");
    sub(&oldf, d, _S"file.txt");
    fsCreateDir(d);

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, d, 0);
    if (unsupported(added))
        goto out;

    if (!fsRename(d, moved))
        TEST_FAILV(ret, 1, _SL("could not rename watched directory '${string}': ${int}"),
                   stvar(string, d), stvar(int32, cxerr));
    writeFile(f, _S"x");

    if (!recWait(&r, K(FSWE_Stopped), d))
        WFAIL(ret, &r, "no Stopped after moving", d);
    if (recSaw(&r, K(FSWE_Created) | K(FSWE_Modified), oldf))
        WFAIL(ret, &r, "moved directory still reported under its old path:", oldf);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&oldf);
    strDestroy(&moved);
    strDestroy(&d);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// The directory's own attribute changes are reported only with FSW_Self.
static int test_fswatch_self_attributes(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, plain = 0, withself = 0, sentinel = 0;
    scratchDir(&dir, _S"self_attributes");
    sub(&plain, dir, _S"plain");
    sub(&withself, dir, _S"self");
    sub(&sentinel, plain, _S"sentinel");
    fsCreateDir(plain);
    fsCreateDir(withself);

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, plain, FSW_Names | FSW_Attributes) &&
                 fsWatchAdd(w, withself, FSW_Self | FSW_Attributes);
    if (unsupported(added))
        goto out;

    int64 when = clockWall() - timeS(86400);
    if (!fsSetTimes(withself, when, when))
        TEST_FAILV(ret, 1, _SL("fsSetTimes('${string}') failed: ${int}"), stvar(string, withself),
                   stvar(int32, cxerr));
    if (!recWait(&r, K(FSWE_Attributes), withself))
        WFAIL(ret, &r, "no Attributes with FSW_Self for", withself);

    fsSetTimes(plain, when, when);
    writeFile(sentinel, _S"");
    if (!recWait(&r, K(FSWE_Created), sentinel))
        WFAIL(ret, &r, "no Created for sentinel", sentinel);
    if (recSaw(&r, K(FSWE_Attributes), plain))
        WFAIL(ret, &r, "directory reported its own attributes without FSW_Self:", plain);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&sentinel);
    strDestroy(&plain);
    strDestroy(&withself);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// ---- subtrees ------------------------------------------------------------------------------

static int test_fswatch_subtree_basic(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, deep = 0, f = 0;
    scratchDir(&dir, _S"subtree");
    sub(&deep, dir, _S"a/b/c");
    fsCreateAll(deep);
    sub(&f, deep, _S"file.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Subtree);
    if (unsupported(added))
        goto out;

    writeFile(f, _S"x");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created deep in the subtree for", f);

    // A plain directory watch does not see that far down.
    WRec r2;
    recInit(&r2);
    string a = 0, af = 0, sentinel = 0;
    sub(&a, dir, _S"a");
    sub(&af, deep, _S"other.txt");
    sub(&sentinel, a, _S"sentinel");
    FSWatch* w2 = recWatch(&r2);
    fsWatchAdd(w2, a, 0);
    writeFile(af, _S"x");
    writeFile(sentinel, _S"");
    if (!recWait(&r2, K(FSWE_Created), sentinel))
        WFAIL(ret, &r2, "no Created for sentinel", sentinel);
    if (recSaw(&r2, K(FSWE_Created), af))
        WFAIL(ret, &r2, "directory watch reported something two levels down", af);
    fsWatchCancel(w2);
    objRelease(&w2);
    recDestroy(&r2);
    strDestroy(&a);
    strDestroy(&af);
    strDestroy(&sentinel);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&deep);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// Directories created inside a subtree are watched too, even when something is created in them
// before the watcher has had a chance to notice them.
static int test_fswatch_subtree_new_dirs_race(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, deep = 0, f = 0, later = 0;
    scratchDir(&dir, _S"subtree_race");
    sub(&deep, dir, _S"new1/new2");
    sub(&f, deep, _S"file.txt");
    sub(&later, deep, _S"later.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Subtree);
    if (unsupported(added))
        goto out;

    fsCreateAll(deep);
    writeFile(f, _S"x");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created inside new directories for", f);

    writeFile(later, _S"y");
    if (!recWait(&r, K(FSWE_Created), later))
        WFAIL(ret, &r, "new directory not watched afterwards;", later);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&later);
    strDestroy(&deep);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static int test_fswatch_subtree_dir_rename(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, x = 0, y = 0, f = 0;
    scratchDir(&dir, _S"subtree_rename");
    sub(&x, dir, _S"x");
    sub(&y, dir, _S"y");
    fsCreateAll(x);
    sub(&f, y, _S"file.txt");

    FSWatch* w = recWatch(&r);
    bool added = fsWatchAdd(w, dir, FSW_Subtree);
    if (unsupported(added))
        goto out;

    fsRename(x, y);
    if (!recWait(&r, K(FSWE_Renamed) | K(FSWE_Created), y))
        WFAIL(ret, &r, "directory rename not reported at", y);

    writeFile(f, _S"x");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "renamed directory not watched at its new path;", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&x);
    strDestroy(&y);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

#define MANY_TARGETS 300

static int test_fswatch_many_targets(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d = 0, f = 0, name = 0;
    scratchDir(&dir, _S"many");

    FSWatch* w = recWatch(&r);
    for (int i = 0; i < MANY_TARGETS; i++) {
        strFormat(&name, _S"d${int}", stvar(int32, i));
        sub(&d, dir, name);
        fsCreateDir(d);
        bool added = fsWatchAdd(w, d, 0);
        if (unsupported(added))
            goto out;
        if (!added) {
            TEST_FAILV(ret, 1, _SL("adding target ${int} failed: ${int}"), stvar(int32, i),
                       stvar(int32, cxerr));
            goto out;
        }
    }

    sub(&f, d, _S"last.txt");
    writeFile(f, _S"x");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created in the last of many targets for", f);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&name);
    strDestroy(&f);
    strDestroy(&d);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// ---- lifecycle -----------------------------------------------------------------------------

static int test_fswatch_cancel_in_callback(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0, g = 0;
    scratchDir(&dir, _S"cancel_cb");
    sub(&f, dir, _S"a");
    sub(&g, dir, _S"b");

    FSWatch* w   = recWatch(&r);
    r.cancelSelf = w;
    bool added   = fsWatchAdd(w, dir, FSW_Names);
    if (unsupported(added))
        goto out;

    writeFile(f, _S"x");
    if (!recWait(&r, K(FSWE_Created), f))
        WFAIL(ret, &r, "no Created for", f);

    // The callback cancelled the watch -- without deadlocking -- so nothing more arrives.
    for (int i = 0; i < 1000 && !atomicLoad(bool, &r.finished, Acquire); i++) osSleep(timeMS(10));
    writeFile(g, _S"y");
    osSleep(timeMS(300));
    if (recSaw(&r, K(FSWE_Created), g))
        WFAIL(ret, &r, "cancelled watch still reported", g);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&g);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// Cancelling from another thread waits for a callback that is already running.
static int test_fswatch_cancel_waits(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, f = 0;
    scratchDir(&dir, _S"cancel_waits");
    sub(&f, dir, _S"a");

    FSWatch* w = recWatch(&r);
    r.sleepFor = timeMS(300);
    bool added = fsWatchAdd(w, dir, FSW_Names);
    if (unsupported(added))
        goto out;

    writeFile(f, _S"x");
    if (!eventWaitTimeout(&r.entered, WAIT_TIMEOUT)) {
        WFAIL(ret, &r, "callback never ran for", f);
        goto out;
    }

    fsWatchCancel(w);
    if (atomicLoad(bool, &r.inCallback, Acquire))
        TEST_FAILV(ret, 1, _SL("fsWatchCancel returned while a callback was still running"), stvNone);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

static atomic(int32) closureDestroyed;

static void noteDestroyed(stvlist* cvars)
{
    atomicFetchAdd(int32, &closureDestroyed, 1, Release);
}

// Releasing the last reference stops the watch and frees its closure.
static int test_fswatch_release_cancels(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0;
    scratchDir(&dir, _S"release");

    atomicStore(int32, &closureDestroyed, 0, Release);
    closure cls = closureCreateAs(FSWatchCB, onEvent, stvar(ptr, &r));
    closureSetDestroy(cls, noteDestroyed);

    FSWatch* w = fsWatchCreate(cls);
    bool added = fsWatchAdd(w, dir, 0);
    if (unsupported(added)) {
        objRelease(&w);
        goto out;
    }

    objRelease(&w);
    if (atomicLoad(int32, &closureDestroyed, Acquire) != 1)
        TEST_FAILV(ret, 1, _SL("closure destroyed ${int} times on release, wanted 1"),
                   stvar(int32, atomicLoad(int32, &closureDestroyed, Acquire)));

out:
    rmTree(dir);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// A callback may add paths to its own watch.
static int test_fswatch_add_from_callback(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    string dir = 0, d1 = 0, d2 = 0, f1 = 0, f2 = 0;
    scratchDir(&dir, _S"add_cb");
    sub(&d1, dir, _S"one");
    sub(&d2, dir, _S"two");
    fsCreateDir(d1);
    fsCreateDir(d2);
    sub(&f1, d1, _S"a");
    sub(&f2, d2, _S"b");

    FSWatch* w = recWatch(&r);
    r.addTo    = w;
    strDup(&r.addPath, d2);
    bool added = fsWatchAdd(w, d1, FSW_Names);
    if (unsupported(added))
        goto out;

    writeFile(f1, _S"x");
    if (!recWait(&r, K(FSWE_Created), f1))
        WFAIL(ret, &r, "no Created for", f1);
    for (int i = 0; i < 1000 && !atomicLoad(bool, &r.finished, Acquire); i++) osSleep(timeMS(10));

    writeFile(f2, _S"y");
    if (!recWait(&r, K(FSWE_Created), f2))
        WFAIL(ret, &r, "path added from a callback not watched;", f2);

out:
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&f1);
    strDestroy(&f2);
    strDestroy(&d1);
    strDestroy(&d2);
    strDestroy(&dir);
    recDestroy(&r);
    return ret;
}

// A watch whose callback falls far behind loses events, and is told to rescan instead.
static int test_fswatch_overflow_backlog(void)
{
    int ret = 0;
    WRec r;
    recInit(&r);
    Event gate;
    eventInit(&gate);
    string dir = 0, f = 0, name = 0;
    scratchDir(&dir, _S"overflow");

    int32 oldcap = _fsWatchSetBacklogCap(4);
    FSWatch* w   = recWatch(&r);
    r.block      = &gate;
    bool added   = fsWatchAdd(w, dir, FSW_Names);
    if (unsupported(added))
        goto out;

    for (int i = 0; i < 40; i++) {
        strFormat(&name, _S"f${int}", stvar(int32, i));
        sub(&f, dir, name);
        writeFile(f, _S"");
    }

    // Give the watcher time to queue what it can and drop the rest, then let the callback go.
    if (!eventWaitTimeout(&r.entered, WAIT_TIMEOUT))
        WFAIL(ret, &r, "callback never ran for", dir);
    osSleep(timeMS(500));
    eventSignalLock(&gate);

    if (!recWait(&r, K(FSWE_Rescan), dir))
        WFAIL(ret, &r, "no Rescan after overflowing the backlog of", dir);

out:
    _fsWatchSetBacklogCap(oldcap);
    eventSignalLock(&gate);
    fsWatchCancel(w);
    objRelease(&w);
    rmTree(dir);
    strDestroy(&name);
    strDestroy(&f);
    strDestroy(&dir);
    eventDestroy(&gate);
    recDestroy(&r);
    return ret;
}

// ---- groups --------------------------------------------------------------------------------

// Each group below runs several of the subtests above in one process. The individual subtests
// stay registered under their own names too, for running or debugging one in isolation.

int test_fswatch_grp_basic(void)
{
    TEST_CHAIN(test_fswatch_create_remove, test_fswatch_modify, test_fswatch_attributes,
               test_fswatch_filter_names_only, test_fswatch_rename_same_dir,
               test_fswatch_rename_cross_dir);
}

int test_fswatch_grp_targets(void)
{
    TEST_CHAIN(test_fswatch_file_target_replace, test_fswatch_file_target_siblings,
               test_fswatch_nonexistent_entry, test_fswatch_nonexistent_parent,
               test_fswatch_multi_target, test_fswatch_remove_target,
               test_fswatch_dup_add_updates_flags, test_fswatch_target_dir_removed,
               test_fswatch_target_dir_removed_self, test_fswatch_target_dir_renamed,
               test_fswatch_self_attributes);
}

int test_fswatch_grp_subtree(void)
{
    TEST_CHAIN(test_fswatch_subtree_basic, test_fswatch_subtree_new_dirs_race,
               test_fswatch_subtree_dir_rename, test_fswatch_many_targets);
}

int test_fswatch_grp_lifecycle(void)
{
    TEST_CHAIN(test_fswatch_cancel_in_callback, test_fswatch_cancel_waits,
               test_fswatch_release_cancels, test_fswatch_add_from_callback,
               test_fswatch_overflow_backlog);
}

testfunc fswatchtest_funcs[] = {
    { "create_remove",         test_fswatch_create_remove         },
    { "modify",                test_fswatch_modify                },
    { "attributes",            test_fswatch_attributes            },
    { "filter_names_only",     test_fswatch_filter_names_only     },
    { "rename_same_dir",       test_fswatch_rename_same_dir       },
    { "rename_cross_dir",      test_fswatch_rename_cross_dir      },
    { "file_target_replace",   test_fswatch_file_target_replace   },
    { "file_target_siblings",  test_fswatch_file_target_siblings  },
    { "nonexistent_entry",     test_fswatch_nonexistent_entry     },
    { "nonexistent_parent",    test_fswatch_nonexistent_parent    },
    { "multi_target",          test_fswatch_multi_target          },
    { "remove_target",         test_fswatch_remove_target         },
    { "dup_add_updates_flags", test_fswatch_dup_add_updates_flags },
    { "target_dir_removed",    test_fswatch_target_dir_removed    },
    { "target_dir_removed_self", test_fswatch_target_dir_removed_self },
    { "target_dir_renamed",    test_fswatch_target_dir_renamed    },
    { "self_attributes",       test_fswatch_self_attributes       },
    { "subtree_basic",         test_fswatch_subtree_basic         },
    { "subtree_new_dirs_race", test_fswatch_subtree_new_dirs_race },
    { "subtree_dir_rename",    test_fswatch_subtree_dir_rename    },
    { "many_targets",          test_fswatch_many_targets          },
    { "cancel_in_callback",    test_fswatch_cancel_in_callback    },
    { "cancel_waits",          test_fswatch_cancel_waits          },
    { "release_cancels",       test_fswatch_release_cancels       },
    { "add_from_callback",     test_fswatch_add_from_callback     },
    { "overflow_backlog",      test_fswatch_overflow_backlog      },
    { "grp_basic",             test_fswatch_grp_basic             },
    { "grp_targets",           test_fswatch_grp_targets           },
    { "grp_subtree",           test_fswatch_grp_subtree           },
    { "grp_lifecycle",         test_fswatch_grp_lifecycle         },
    { 0, 0 }
};

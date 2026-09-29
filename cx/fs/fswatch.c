#include "fswatch_private.h"

#include <cx/container/foreach.h>
#include <cx/fs/path.h>
#include <cx/string.h>
#include <cx/thread/atomic.h>
#include <cx/thread/mutex.h>
#include <cx/thread/thread.h>
#include <cx/time/time.h>
#include <cx/utils/lazyinit.h>

STR_CONST(kDispatchThreadName, "cx fswatch dispatch");

// The dispatch thread is woken whenever something is queued; this only bounds how long a
// missed wakeup could go unnoticed.
#define FSWATCH_DISPATCH_IDLE timeMS(1000)

typedef struct FSWQueued {
    struct FSWQueued* next;
    FSWatch_WeakRef* wref;
    FSWatchEventKind kind;
    string path;
    string oldpath;
    string target;
} FSWQueued;

static LazyInitState dispInitState;

// Guards the queue. A leaf lock: nothing else is taken while it is held.
static Mutex qLock;
static FSWQueued* qHead;
static FSWQueued* qTail;

static Thread* dispThread;
static atomic(uintptr) dispThreadId;
// Undelivered events one watch may have queued; 0 means FSWATCH_BACKLOG_DEFAULT.
#define FSWATCH_BACKLOG_DEFAULT 8192
static atomic(int32) backlogCap;

static int dispatchThreadFunc(Thread* self);

static void dispatchInit(void* data)
{
    mutexInit(&qLock);

    // Registered as a system thread so the existing atexit hook shuts it down with everything
    // else.
    dispThread = thrCreate(dispatchThreadFunc, kDispatchThreadName, stvNone);
    thrRegisterSysThread(dispThread);
}

void _fsWatchDispatchInit(void)
{
    lazyInit(&dispInitState, dispatchInit, NULL);
}

bool _fsWatchOnDispatchThread(void)
{
    uintptr id = atomicLoad(uintptr, &dispThreadId, Acquire);
    return id != 0 && id == (uintptr)thrCurrentOSThreadID();
}

static void freeQueued(FSWQueued* q)
{
    objDestroyWeak(&q->wref);
    strDestroy(&q->path);
    strDestroy(&q->oldpath);
    strDestroy(&q->target);
    xaFree(q);
}

static void invoke(FSWatch* w, FSWatchEvent* ev)
{
    // See FSWatch_cancel for the other half of this.
    atomicFetchAdd(int32, &w->busy, 1, SeqCst);
    if (!atomicLoad(bool, &w->cancelled, SeqCst) && w->cls)
        closureCallAs(FSWatchCB, w->cls, w, ev);
    atomicFetchSub(int32, &w->busy, 1, Release);
}

// Tell the callback to look at every target again, after events for it had to be dropped.
static void sendRescans(FSWatch* w)
{
    sa_string paths;
    withMutex (&w->tlock) {
        saClone(&paths, w->tpaths);
    }

    foreach (sarray, idx, string, p, paths) {
        FSWatchEvent ev = { .kind = FSWE_Rescan, .path = p, .target = p };
        invoke(w, &ev);
    }
    saDestroy(&paths);
}

static void dispatchQueued(void)
{
    for (;;) {
        FSWQueued* batch;
        withMutex (&qLock) {
            batch = qHead;
            qHead = qTail = NULL;
        }

        if (!batch)
            return;

        while (batch) {
            FSWQueued* q = batch;
            batch        = q->next;

            // A watch that was released while this sat in the queue is gone, and so is its
            // interest in the event.
            FSWatch* w = objAcquireFromWeak(FSWatch, q->wref);
            if (w) {
                FSWatchEvent ev = { .kind    = q->kind,
                                    .path    = q->path,
                                    .oldpath = q->oldpath,
                                    .target  = q->target };
                invoke(w, &ev);

                // The last queued event for a watch that overflowed is where the Rescan it is
                // owed goes out. _fsWatchQueue sets the flag before giving back its own count,
                // so it is always visible by the time the count reaches zero.
                if (atomicFetchSub(int32, &w->backlog, 1, AcqRel) == 1 &&
                    atomicExchange(bool, &w->overflowed, false, AcqRel))
                    sendRescans(w);

                objRelease(&w);
            }
            freeQueued(q);
        }
    }
}

static int dispatchThreadFunc(Thread* self)
{
    atomicStore(uintptr, &dispThreadId, (uintptr)thrCurrentOSThreadID(), Release);

    // Queueing signals the thread's own notify event, which is also what thrRequestExit
    // signals, so shutdown does not have to wait out the idle timeout.
    while (thrLoop(self)) {
        dispatchQueued();
        eventWaitTimeout(&self->notify, FSWATCH_DISPATCH_IDLE);
    }

    dispatchQueued();
    return 0;
}

_Use_decl_annotations_
void _fsWatchQueue(FSWatch* w, FSWatch_WeakRef* wref, FSWatchEventKind kind, strref path,
                   strref oldpath, strref target)
{
    if (atomicLoad(bool, &w->cancelled, Acquire))
        return;

    int32 cap = atomicLoad(int32, &backlogCap, Relaxed);
    if (cap <= 0)
        cap = FSWATCH_BACKLOG_DEFAULT;

    if (atomicFetchAdd(int32, &w->backlog, 1, AcqRel) >= cap) {
        // Full. Owe a Rescan instead; the flag must be set before the count is given back.
        atomicStore(bool, &w->overflowed, true, Release);
        atomicFetchSub(int32, &w->backlog, 1, AcqRel);
        return;
    }

    FSWQueued* q = xaAllocStruct(FSWQueued, XA_Zero);
    q->wref      = (FSWatch_WeakRef*)objCloneWeak(wref);
    q->kind      = kind;
    strDup(&q->path, path);
    strDup(&q->oldpath, oldpath);
    strDup(&q->target, target);

    withMutex (&qLock) {
        if (qTail)
            qTail->next = q;
        else
            qHead = q;
        qTail = q;
    }

    eventSignal(&dispThread->notify);
}

_Use_decl_annotations_
void _fsWatchDeliver(FSWatch* w, FSWatchEvent* ev)
{
    if (_fsWatchOnDispatchThread()) {
        invoke(w, ev);
        return;
    }

    FSWatch_WeakRef* wref = objGetWeak(FSWatch, w);
    _fsWatchQueue(w, wref, ev->kind, ev->path, ev->oldpath, ev->target);
    objDestroyWeak(&wref);
}

_Use_decl_annotations_
void _fsWatchStop(FSWatch* w)
{
    atomicStore(bool, &w->cancelled, true, SeqCst);
    fsWatchStopSources(w);
}

_Use_decl_annotations_
void _fsWatchTargetAdded(FSWatch* w, strref path)
{
    withMutex (&w->tlock) {
        saPush(&w->tpaths, strref, path);
    }
}

_Use_decl_annotations_
void _fsWatchTargetRemoved(FSWatch* w, strref path)
{
    withMutex (&w->tlock) {
        saFindRemove(&w->tpaths, strref, path);
    }
}

int32 _fsWatchSetBacklogCap(int32 cap)
{
    int32 prev = atomicExchange(int32, &backlogCap, cap, Relaxed);
    return prev > 0 ? prev : FSWATCH_BACKLOG_DEFAULT;
}

_Use_decl_annotations_
bool _fsWatchPathWithin(strref path, strref base, bool casei)
{
    if (!(casei ? strBeginsWithi(path, base) : strBeginsWith(path, base)))
        return false;

    uint32 blen = strLen(base);
    if (strLen(path) == blen || blen == 0)
        return true;

    // A root such as "/" or "c:/" already ends in the separator.
    if (blen > 0 && strGetChar(base, (int32)blen - 1) == '/')
        return true;
    return strGetChar(path, (int32)blen) == '/';
}

_Use_decl_annotations_
bool _fsWatchCovers(strref tpath, flags_t tflags, bool tisdir, strref path, FSWatchEventKind kind,
                    bool casei)
{
    if (!_fsWatchPathWithin(path, tpath, casei))
        return false;

    if (strLen(path) == strLen(tpath)) {
        // Watched by name: anything that happens to that name.
        if (!tisdir)
            return true;
        // A watched directory is always told when it stops or should be looked at again. Its own
        // departure and attributes are reported only with FSW_Self.
        if (kind == FSWE_Rescan || kind == FSWE_Stopped)
            return true;
        return (tflags & FSW_Self) &&
            (kind == FSWE_Removed || kind == FSWE_Renamed || kind == FSWE_Attributes);
    }

    if (!tisdir)
        return false;
    if (tflags & FSW_Subtree)
        return true;

    // Only the entries directly in it.
    string parent = 0;
    if (!pathParent(&parent, path))
        strClear(&parent);   // a relative path with one component is directly in ""
    bool ret = casei ? strEqi(parent, tpath) : strEq(parent, tpath);
    strDestroy(&parent);
    return ret;
}

flags_t _fsWatchKindFilter(FSWatchEventKind kind)
{
    switch (kind) {
    case FSWE_Created:
    case FSWE_Removed:
    case FSWE_Renamed:
        return FSW_Names;
    case FSWE_Modified:
        return FSW_Contents;
    case FSWE_Attributes:
        return FSW_Attributes;
    default:
        // Rescan and Stopped go to everyone.
        return FSW_Everything;
    }
}

_Use_decl_annotations_
void _fsWatchStripRoot(string* out, strref path, strref root, bool casei)
{
    if (!_fsWatchPathWithin(path, root, casei)) {
        strDup(out, path);
        return;
    }

    uint32 rlen = strLen(root);
    if (strLen(path) == rlen) {
        strClear(out);
        return;
    }

    // Skip the separator too, unless the root already ends in one.
    if (rlen == 0 || strGetChar(root, (int32)rlen - 1) != '/')
        rlen++;
    strSubStr(out, path, rlen, strEnd);
}

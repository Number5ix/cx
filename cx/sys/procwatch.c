#include "process_private.h"

#include <cx/container/sarray.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/thread/atomic.h>
#include <cx/thread/mutex.h>
#include <cx/thread/thread.h>
#include <cx/time/time.h>
#include <cx/utils/lazyinit.h>

STR_CONST(kWatchThreadName, "cx process watcher");

// How long the watcher blocks with nothing to do. The platform wait is woken directly whenever
// registrations change, so this only bounds how long a missed wakeup could go unnoticed.
#define PROCWATCH_IDLE timeMS(200)

static LazyInitState watchInitState;

// Guards both lists below.
static Mutex watchLock;

// Processes being watched, and those that have finished but whose callbacks have not run yet.
// Both hold a reference, which is what makes launch-and-forget safe: procLaunch() followed
// immediately by procRelease() still gets reaped and still fires its callbacks.
static sa_Process watched;
static sa_Process completed;

static Thread* watchThread;
static bool watchAvailable;

// The thread currently inside a user callback, or 0. procNotifyCancel consults this so that
// cancelling from inside a callback does not wait for itself to finish.
static atomic(uintptr) watchDispatchThread;

static int procWatchThread(Thread* self);

static void watchInit(void* data)
{
    mutexInit(&watchLock);
    saInit(&watched, object, 8);
    saInit(&completed, object, 8);

    watchAvailable = _procWatchPlatformInit();
    if (!watchAvailable)
        return;

    // Registered as a system thread so the existing atexit hook shuts it down with everything
    // else, rather than needing its own teardown path.
    // stvNone, not nothing: count_macro_args() yields 1 for an empty __VA_ARGS__, so a
    // zero-argument thrCreate hands Thread_create n=1 alongside an empty array and it
    // reads garbage off the end.
    watchThread = thrCreate(procWatchThread, kWatchThreadName, stvNone);
    thrRegisterSysThread(watchThread);
}

// Run the callbacks for everything that has finished since the last pass.
static void dispatchCompleted(void)
{
    sa_Process batch;
    saInit(&batch, object, 4);

    // Take the whole list and let the lock go before calling anything. A callback is free to
    // register a new watch, cancel one, or release its own handle, all of which come back
    // through this mutex.
    withMutex (&watchLock) {
        for (int32 i = 0; i < saSize(completed); i++) saPush(&batch, object, completed.a[i]);
        saClear(&completed);
    }

    if (saSize(batch) == 0) {
        saDestroy(&batch);
        return;
    }

    atomicStore(uintptr, &watchDispatchThread, (uintptr)thrCurrentOSThreadID(), Release);

    for (int32 i = 0; i < saSize(batch); i++) {
        Process* proc = batch.a[i];

        // Take the subscriber list off the object under its lock and call them with nothing
        // held. Exit happens once, so moving the list out is also what stops a second dispatch
        // from calling the same subscribers again.
        sa_closure subs = saInitNone;
        withMutex (&proc->lock) {
            subs = proc->onexit;
            saInit(&proc->onexit, closure, 2);
        }

        for (int32 j = 0; j < saSize(subs); j++)
            closureCall(subs.a[j], stvar(int64, proc->pid), stvar(int32, proc->exitcode));

        saDestroy(&subs);
    }

    atomicStore(uintptr, &watchDispatchThread, 0, Release);

    // Drops the references the registry was holding, which is where a forgotten child is
    // finally destroyed.
    saDestroy(&batch);
}

static int procWatchThread(Thread* self)
{
    while (!atomicLoad(bool, &self->requestExit, Relaxed)) {
        _procWatchPlatformWait(PROCWATCH_IDLE);
        dispatchCompleted();
    }

    // One last pass, so a process that finished during shutdown still gets its callbacks.
    dispatchCompleted();
    return 0;
}

_Use_decl_annotations_
void _procWatchCompleted(Process* proc)
{
    withMutex (&watchLock) {
        int32 idx = saFind(watched, object, proc);
        if (idx >= 0) {
            // Push before removing, so the refcount cannot reach zero while the lock is held --
            // destroying a Process re-enters this file.
            saPush(&completed, object, proc);
            saRemove(&watched, idx);
        }
    }

    _procWatchPlatformRemove(proc);
    _procWatchPlatformWake();
}

_Use_decl_annotations_
bool _procWatchRegister(Process* proc)
{
    lazyInit(&watchInitState, watchInit, NULL);

    // No watcher on this platform: the caller's synchronous sweep is the whole mechanism.
    if (!watchAvailable)
        return false;

    withMutex (&watchLock) {
        saPush(&watched, object, proc);
    }

    if (!_procWatchPlatformAdd(proc)) {
        // Could not watch it after all. Drop back to the sweep rather than holding a reference
        // to something nothing will ever collect.
        withMutex (&watchLock) {
            int32 idx = saFind(watched, object, proc);
            if (idx >= 0)
                saRemove(&watched, idx);
        }
        return false;
    }

    _procWatchPlatformWake();
    return true;
}

_Use_decl_annotations_
bool procNotifyExit(Process* proc, closure cls)
{
    if (!proc || !cls)
        return false;

    // Give the synchronous sweep a chance first, so a child that has already finished is known
    // to have finished even on a platform with no watcher.
    _procReapPending();

    bool firenow = false;

    // Deciding under the process lock is what closes the race: the exit is published under this
    // same lock, so either this sees it and fires below, or the publisher has not got there yet
    // and will fire the chain this closure has just joined.
    withMutex (&proc->lock) {
        if (atomicLoad(bool, &proc->exited, Acquire))
            firenow = true;
        else
            saPushC(&proc->onexit, closure, &cls);   // takes ownership; cls is cleared
    }

    if (firenow) {
        // Never with the lock held.
        closureCall(cls, stvar(int64, proc->pid), stvar(int32, proc->exitcode));
        closureDestroy(&cls);
    }

    return true;
}

_Use_decl_annotations_
void procNotifyCancel(Process* proc)
{
    if (!proc)
        return;

    withMutex (&proc->lock) {
        saClear(&proc->onexit);
    }

    // Called from inside a callback: waiting would be waiting on this thread.
    if ((uintptr)thrCurrentOSThreadID() == atomicLoad(uintptr, &watchDispatchThread, Acquire))
        return;

    // Wait for any callback still running to return, so once this call is done nothing the
    // callback touches is still in use. This waits for whatever dispatch is in flight rather
    // than only this process's, which costs a little extra waiting and needs no bookkeeping to
    // stay correct.
    while (atomicLoad(uintptr, &watchDispatchThread, Acquire) != 0) osSleep(timeMS(1));
}

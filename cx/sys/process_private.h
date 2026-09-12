#pragma once

#include <cx/platform/base.h>
#include <cx/sys/process.h>

// Contract every platform backend implements. Enumeration and lookup do not need a Process at
// all; the rest operate on one the platform layer created itself, so each backend can safely
// downcast to its own subclass.

// Fill out with a snapshot of every process on the machine. The array is already initialized.
// Returns false only if the process list could not be read at all -- processes that vanish
// while the snapshot is being built are skipped, and the call still succeeds.
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags);

// Fill out with one process's details. out is already initialized. Returns false if the
// process does not exist.
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags);

// Launch a program. Returns NULL and sets cxerr if it could not be started -- including when
// the executable itself failed to start, which the Unix backend learns through an error pipe
// rather than by discovering a child that immediately died.
_Ret_opt_valid_ Process* _procPlatformLaunch(strref exe, sa_string args, const ProcessOpts* opts);

// Wait for a process to finish, up to timeout microseconds. Only called when the outcome is not
// already cached on the object.
bool _procPlatformWait(Process* proc, int64 timeout);

// Ask a process to stop (force = false) or kill it outright (force = true).
bool _procPlatformTerminate(Process* proc, bool force);

// Report the exit code for a process whose status is not already cached. Platforms that can
// never answer for this process -- Unix, for anything cx did not fork -- set CX_NotSupported so
// the caller can tell "cannot know" from "not finished yet".
bool _procPlatformExitCode(Process* proc, int32* code);

// Collect any launched children that have finished and cache their status on their handles.
// Called from procLaunch, procWait and Process destroy. Until the watcher lands, this is the
// only thing that reaps, so a finished child stays a zombie until the next process API call.
void _procReapPending(void);

// Attach to an already-running process. Returns NULL and sets cxerr on failure.
_Ret_opt_valid_ Process* _procPlatformOpen(ProcessID pid);

// Grow an enumeration array by one and hand back the new slot, with the fields whose "unknown"
// value is not zero already set. Shared so every backend fills entries the same way.
ProcessInfo* _procInfoPush(sa_ProcessInfo* out);

// Ask the OS whether the process is still alive. Only called for a process whose outcome is
// not already cached on the object. A process cx did not launch that is found to be gone has
// that published on the handle, which is what lets procNotifyExit fire immediately for it.
bool _procPlatformRunning(Process* proc);

// ---- exit watcher -------------------------------------------------------------------------
//
// procwatch.c owns the registry, the single watcher thread and closure dispatch. Each platform
// supplies only the mechanism for learning that a process finished.

// Called by procwatch.c.

// One-time setup of the platform's waiting mechanism. Returning false means this platform has
// no watcher, and no thread is started -- callers fall back to the synchronous sweep.
bool _procWatchPlatformInit(void);

typedef enum ProcWatchAddResult {
    PROCWATCH_Failed = 0,   // could not watch it
    PROCWATCH_Armed,        // watching; the exit will be delivered through _procWatchCompleted
    PROCWATCH_Gone,         // a process cx did not launch has already finished; nothing to watch
} ProcWatchAddResult;

// Start or stop watching a process. The registry already holds a reference for the duration,
// and proc->watchid is assigned before Add is called. Remove is called exactly once per
// successful Add, by whichever of completion or cancellation takes the process off the
// registry first.
ProcWatchAddResult _procWatchPlatformAdd(Process* proc);
void _procWatchPlatformRemove(Process* proc);

// Block up to timeout microseconds for watched processes to finish, calling
// _procWatchCompleted() for each. Must return early when _procWatchPlatformWake() is called, or
// a newly registered process would not be waited on until this timed out.
void _procWatchPlatformWait(int64 timeout);

// Nudge a blocked _procWatchPlatformWait so it picks up a registration change.
void _procWatchPlatformWake(void);

// Called by the platform backends, from whatever thread noticed the exit. The exit status must
// already be cached on the object. Hands the process to the watcher thread for dispatch, so no
// user callback ever runs on a system thread.
void _procWatchCompleted(Process* proc);

// Looks up a watched process by registration id and returns it acquired, or NULL if it is no
// longer watched. Backends whose wait returns a token rather than a pinned pointer use this, so
// a registration cancelled while its event was in flight is dropped instead of touched.
_Ret_opt_valid_ Process* _procWatchAcquire(int64 watchid);

// Every watched process with this pid, acquired, appended to out (already initialized). For
// backends that key registrations on the pid, where several handles can share one.
void _procWatchAcquireByPid(sa_Process* out, int64 pid);

// Called by the platform launch paths for every child cx forks, whether or not anyone asked to
// be notified -- something has to reap it -- and by procNotifyExit for a process cx did not
// launch. Registering a process that is already watched does nothing and returns true. Returns
// true if the watcher took the process on, in which case the watcher is now its only reaper;
// false means the caller must fall back to the synchronous sweep. For a process cx did not
// launch that has already finished, publishes that and returns true.
bool _procWatchRegister(Process* proc);

// Publish a finished process's outcome on its handle. known is false when the process is gone
// but its status could not be collected; the exit code then reads as PROC_ExitCodeUnknown. The
// first publication wins, so this is safe to call from several places that noticed the exit.
void _procPublishExit(Process* proc, bool known, int32 exitcode, int32 termsignal);

#if defined(_PLATFORM_WIN)
#include "cx/platform/win/win_sys_process.h"
#elif defined(_PLATFORM_UNIX)
#include "cx/platform/unix/unix_sys_process.h"
#elif defined(_PLATFORM_WASM)
#include "cx/platform/wasm/wasm_sys_process.h"
#endif

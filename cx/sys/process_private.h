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

// Attach to an already-running process. Returns NULL and sets cxerr on failure.
_Ret_opt_valid_ Process* _procPlatformOpen(ProcessID pid);

// Grow an enumeration array by one and hand back the new slot, with the fields whose "unknown"
// value is not zero already set. Shared so every backend fills entries the same way.
ProcessInfo* _procInfoPush(sa_ProcessInfo* out);

// Ask the OS whether the process is still alive. Only called for a process whose outcome is
// not already cached on the object.
bool _procPlatformRunning(Process* proc);

#if defined(_PLATFORM_WIN)
#include "cx/platform/win/win_sys_process.h"
#elif defined(_PLATFORM_UNIX)
#include "cx/platform/unix/unix_sys_process.h"
#elif defined(_PLATFORM_WASM)
#include "cx/platform/wasm/wasm_sys_process.h"
#endif

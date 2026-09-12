#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/string.h"

#include <unistd.h>

// There are no other processes in a wasm sandbox, and nothing to launch one with.
//
// Enumeration deliberately succeeds and returns nothing, rather than failing. Code that lists
// processes and loops over the result then runs unchanged here instead of needing a platform
// branch around every call site. Everything that would have to invent a process fails instead,
// with CX_NotSupported.

_Use_decl_annotations_
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags)
{
    return true;
}

_Use_decl_annotations_
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
Process* _procPlatformOpen(ProcessID pid)
{
    cxerr = CX_NotSupported;
    return NULL;
}

bool _procPlatformRunning(Process* proc)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
Process* _procPlatformLaunch(strref exe, sa_string args, const ProcessOpts* opts)
{
    cxerr = CX_NotSupported;
    return NULL;
}

bool _procPlatformWait(Process* proc, int64 timeout)
{
    cxerr = CX_NotSupported;
    return false;
}

bool _procPlatformTerminate(Process* proc, bool force)
{
    cxerr = CX_NotSupported;
    return false;
}

bool _procPlatformExitCode(Process* proc, int32* code)
{
    cxerr = CX_NotSupported;
    return false;
}

void _procReapPending(void)
{
    // Nothing is ever launched here, so there is nothing to collect.
}

_Use_decl_annotations_
ProcessID procCurrentID(void)
{
    // Emscripten does provide a pid, and it is the one thing here with a real answer.
    return (ProcessID)getpid();
}

// No processes means nothing to watch. Init returns false, so procwatch.c starts no thread and
// arms no lazy initialization at all.
bool _procWatchPlatformInit(void)
{
    return false;
}

bool _procWatchPlatformAdd(Process* proc)
{
    return false;
}

void _procWatchPlatformRemove(Process* proc)
{
}

void _procWatchPlatformWait(int64 timeout)
{
}

void _procWatchPlatformWake(void)
{
}

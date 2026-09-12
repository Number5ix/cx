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
ProcessID procCurrentID(void)
{
    // Emscripten does provide a pid, and it is the one thing here with a real answer.
    return (ProcessID)getpid();
}

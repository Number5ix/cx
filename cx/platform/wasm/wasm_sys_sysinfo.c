#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/time/clock.h"

#include <emscripten/heap.h>

// A wasm sandbox has one linear memory and no view of the machine underneath it. The memory
// figures describe that sandbox, which is the only thing here a program can actually run out of.
// Everything else -- processor accounting, run queues, other processes -- belongs to a system
// this code cannot see, and there is no honest empty answer to give for those.

_Use_decl_annotations_
bool _sysPlatformMemInfo(SysMemInfo* out)
{
    size_t heap = emscripten_get_heap_size();
    size_t max  = emscripten_get_heap_max();

    out->physTotal = max > heap ? max : heap;
    out->physAvail = out->physTotal - heap;
    out->valid |= SYSMEM_Phys;

    out->pageSize = 65536;   // fixed by the WebAssembly specification
    out->valid |= SYSMEM_PageSize;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformCPUTimes(SysCPUTimes* out)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformUptime(SysUptime* out)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformLoadAvg(SysLoadAvg* out)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc)
{
    cxerr = CX_NotSupported;
    return false;
}

#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_process.h"
#include "cx/platform/unix/unix_sys_sysinfo.h"

#include <stdlib.h>

bool _sysUnixProcUsable(Process* proc)
{
    if (proc && !_procUnixSameProcess(proc)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformLoadAvg(SysLoadAvg* out)
{
    double avg[3];

    // Both Unixes keep this figure and expose it the same way, so there is nothing per-OS to do.
    int n = getloadavg(avg, 3);
    if (n <= 0)
        return unixMapErrno();

    if (n > 0) {
        out->load1 = avg[0];
        out->valid |= SYSLOAD_Load1;
    }
    if (n > 1) {
        out->load5 = avg[1];
        out->valid |= SYSLOAD_Load5;
    }
    if (n > 2) {
        out->load15 = avg[2];
        out->valid |= SYSLOAD_Load15;
    }

    return true;
}

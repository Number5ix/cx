#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_processobj.h"
#include "cx/string.h"

#include <errno.h>
#include <signal.h>
#include <unistd.h>

bool _procUnixAlive(ProcessID pid)
{
    if (pid <= 0)
        return false;

    // Signal 0 performs the permission and existence checks without sending anything. EPERM
    // means the process is there but belongs to someone else, which still answers the question.
    if (kill((pid_t)pid, 0) == 0)
        return true;

    return errno == EPERM;
}

_Use_decl_annotations_
Process* _procPlatformOpen(ProcessID pid)
{
    if (!_procUnixAlive(pid)) {
        cxerr = CX_FileNotFound;
        return NULL;
    }

    UnixProcess* uproc = _unixprocobjCreate();
    uproc->pid         = pid;

    // Not our child, so nothing here may waitpid() on it later and no exit code will ever be
    // available for it. Exit *notification* still works; see the header.
    uproc->ischild = false;

    return Process(uproc);
}

bool _procPlatformRunning(Process* proc)
{
    return _procUnixAlive(proc->pid);
}

_Use_decl_annotations_
ProcessID procCurrentID(void)
{
    return (ProcessID)getpid();
}

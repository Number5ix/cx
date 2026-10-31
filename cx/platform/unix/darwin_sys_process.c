#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/string.h"

#include <sys/types.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <libproc.h>
#include <string.h>
#include <unistd.h>

// Resolves a process's executable path. Only readable for the same user's processes (or as
// root), so failure here is routine.
static bool readExePath(string* out, ProcessID pid)
{
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    int len = proc_pidpath((int)pid, buf, sizeof(buf));
    if (len <= 0)
        return false;

    strFromBytes(out, buf, (uint32)len);
    return true;
}

// Copies one kinfo_proc record into a ProcessInfo.
static void fillFromKinfo(ProcessInfo* info, const struct kinfo_proc* kp, flags_t flags)
{
    info->pid  = (ProcessID)kp->kp_proc.p_pid;
    info->ppid = (ProcessID)kp->kp_eproc.e_ppid;

    const char* comm = kp->kp_proc.p_comm;
    strFromBytes(&info->name, comm, (uint32)strnlen(comm, sizeof(kp->kp_proc.p_comm)));

    if (flags & PROC_EnumFullPath) {
        if (readExePath(&info->exepath, info->pid)) {
            // p_comm is truncated by the kernel; the real path's filename is the full name.
            pathFilename(&info->name, info->exepath);
        }
    }
}

// sysctl rather than libproc's proc_pidinfo, because it reports every process regardless of
// which user owns it.
static bool readKinfo(ProcessID pid, struct kinfo_proc* kp)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    size_t len = sizeof(*kp);

    // A pid that doesn't exist succeeds with len == 0.
    return sysctl(mib, 4, kp, &len, NULL, 0) == 0 && len >= sizeof(*kp);
}

_Use_decl_annotations_
bool _procUnixStartTime(ProcessID pid, int64* out)
{
    struct kinfo_proc kp;
    if (!readKinfo(pid, &kp))
        return false;

    // Only ever compared against another reading for the same pid, so any stable encoding of
    // the same instant will do.
    *out = (int64)kp.kp_proc.p_starttime.tv_sec * 1000000 + (int64)kp.kp_proc.p_starttime.tv_usec;
    return true;
}

_Use_decl_annotations_
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t len = 0;

    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0)
        return unixMapErrno();

    // Processes can start between the sizing call and the real one, so ask for noticeably more
    // room than the kernel just said it needed.
    len += len / 8 + 16 * sizeof(struct kinfo_proc);

    struct kinfo_proc* kps = xaAlloc(len);
    if (sysctl(mib, 4, kps, &len, NULL, 0) != 0) {
        xaFree(kps);
        return unixMapErrno();
    }

    size_t count = len / sizeof(struct kinfo_proc);
    for (size_t i = 0; i < count; i++) {
        ProcessInfo* info = _procInfoPush(out);
        fillFromKinfo(info, &kps[i], flags);
    }

    xaFree(kps);
    return true;
}

_Use_decl_annotations_
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    if (pid <= 0) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    struct kinfo_proc kp;
    if (!readKinfo(pid, &kp)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    fillFromKinfo(out, &kp, flags);
    return true;
}

#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/string.h"

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>

#include <errno.h>
#include <unistd.h>

// Resolves a process's executable path. Permission-limited in the same way Linux's
// /proc/<pid>/exe is, so failure here is routine.
static bool readExePath(string* out, ProcessID pid)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, (int)pid };
    char buf[4096];
    size_t len = sizeof(buf);

    if (sysctl(mib, 4, buf, &len, NULL, 0) != 0 || len == 0)
        return false;

    // The returned length counts the NUL terminator.
    while (len > 0 && buf[len - 1] == 0) len--;
    if (len == 0)
        return false;

    strFromBytes(out, buf, (uint32)len);
    return true;
}

// Copies one kinfo_proc record into a ProcessInfo.
static void fillFromKinfo(ProcessInfo* info, const struct kinfo_proc* kp, flags_t flags)
{
    info->pid  = (ProcessID)kp->ki_pid;
    info->ppid = (ProcessID)kp->ki_ppid;

    strFromBytes(&info->name, kp->ki_comm, (uint32)strnlen(kp->ki_comm, sizeof(kp->ki_comm)));

    if (flags & PROC_EnumFullPath) {
        if (readExePath(&info->exepath, info->pid)) {
            // ki_comm is truncated by the kernel; the real path's filename is the full name.
            pathFilename(&info->name, info->exepath);
        }
    }
}

_Use_decl_annotations_
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags)
{
    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC };
    size_t len = 0;

    if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
        return unixMapErrno();

    // Processes can start between the sizing call and the real one, so ask for noticeably more
    // room than the kernel just said it needed.
    len += len / 8 + 8192;

    char* buf = xaAlloc(len);
    if (sysctl(mib, 3, buf, &len, NULL, 0) != 0) {
        xaFree(buf);
        return unixMapErrno();
    }

    // Walk by each record's own ki_structsize rather than by sizeof(struct kinfo_proc). A
    // kernel built from different sources than this binary can disagree about the struct's
    // size, and striding by the compiled-in size would then walk straight off the rails.
    char* p   = buf;
    char* end = buf + len;

    while (p + sizeof(int) <= end) {
        const struct kinfo_proc* kp = (const struct kinfo_proc*)p;
        int ssz                     = kp->ki_structsize;

        if (ssz <= 0 || p + ssz > end)
            break;

        // A record shorter than what this binary was compiled against cannot be read safely.
        if ((size_t)ssz >= sizeof(struct kinfo_proc)) {
            ProcessInfo* info = _procInfoPush(out);
            fillFromKinfo(info, kp, flags);
        }

        p += ssz;
    }

    xaFree(buf);
    return true;
}

_Use_decl_annotations_
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    if (pid <= 0) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len < sizeof(kp)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    fillFromKinfo(out, &kp, flags);
    return true;
}

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
bool _procUnixStartTime(ProcessID pid, int64* out)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, NULL, 0) != 0 || len < sizeof(kp))
        return false;

    // Only ever compared against another reading for the same pid, so any stable encoding of
    // the same instant will do.
    *out = (int64)kp.ki_start.tv_sec * 1000000 + (int64)kp.ki_start.tv_usec;
    return true;
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

// ---- exit watcher ------------------------------------------------------------------------
//
// kqueue can watch process exits directly with EVFILT_PROC/NOTE_EXIT, keyed on the pid. There
// is no pidfd equivalent in use here, so UnixProcess::waitfd stays -1 throughout.
//
// A knote is identified by its pid alone, so two handles to one process -- a launched child and
// a procOpen of the same pid -- share a single registration. The event therefore carries no
// pointer: the watcher looks up every watched handle with that pid and completes them all.

#include "cx/platform/unix/unix_sys_processobj.h"
#include "cx/thread/atomic.h"
#include "cx/thread/mutex.h"

#include <sys/event.h>
#include <sys/wait.h>

static int watchKq = -1;

// Identifier for the user event used to wake a blocked kevent() call.
#define PROCWATCH_WAKE_IDENT 1

bool _procWatchPlatformInit(void)
{
    watchKq = kqueue();
    if (watchKq < 0)
        return false;

    // EVFILT_USER is the kqueue-native wakeup, and it latches: a trigger raised while nothing
    // is waiting is still pending at the next kevent().
    struct kevent kev;
    EV_SET(&kev, PROCWATCH_WAKE_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (kevent(watchKq, &kev, 1, NULL, 0, NULL) != 0) {
        close(watchKq);
        watchKq = -1;
        return false;
    }

    return true;
}

// Collect a finished child and publish what was found. Blocking is fine: NOTE_EXIT is raised
// as the process exits, so it is collectable momentarily if it is not already.
static void reapChild(Process* proc)
{
    int status = 0;
    pid_t r;

    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r > 0)
        _procUnixPublishStatus(proc, status);
    else
        // ECHILD: something else in the program collected it first, so the status is gone.
        _procPublishExit(proc, false, 0, 0);
}

// Drop the knote for a pid, unless another watched handle still relies on it.
static void deleteKnoteIfUnused(int64 pid)
{
    sa_Process others;
    saInit(&others, object, 2);
    _procWatchAcquireByPid(&others, pid);

    if (saSize(others) == 0) {
        struct kevent kev;
        EV_SET(&kev, (uintptr_t)pid, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
        kevent(watchKq, &kev, 1, NULL, 0, NULL);   // ENOENT once it has fired; nothing to do
    }

    saDestroy(&others);
}

ProcWatchAddResult _procWatchPlatformAdd(Process* proc)
{
    if (watchKq < 0)
        return PROCWATCH_Failed;

    // Added even if another handle already watches this pid. Re-adding an existing knote just
    // updates it, and skipping it could race with that knote firing and being consumed.
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)proc->pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);

    if (kevent(watchKq, &kev, 1, NULL, 0, NULL) != 0) {
        if (errno != ESRCH)
            return PROCWATCH_Failed;

        if (!proc->ischild)
            return PROCWATCH_Gone;

        // ESRCH for a child means it exited between the fork and this registration, so there
        // is nothing to wait for -- collect it right now instead of losing the notification.
        int status = 0;
        if (waitpid((pid_t)proc->pid, &status, WNOHANG) > 0) {
            _procUnixPublishStatus(proc, status);
            _procWatchCompleted(proc);
            return PROCWATCH_Armed;
        }
        return PROCWATCH_Failed;
    }

    // Registering by pid can catch a different process that has since been given the same
    // number. Now that the knote exists, a start time that still matches proves it is the
    // right process: if the original exits after this point the knote fires.
    if (!proc->ischild && !_procUnixSameProcess(proc)) {
        // This handle is already on the registry, so it does not count as another user.
        sa_Process others;
        saInit(&others, object, 2);
        _procWatchAcquireByPid(&others, proc->pid);
        bool shared = saSize(others) > 1;
        saDestroy(&others);

        if (!shared) {
            EV_SET(&kev, (uintptr_t)proc->pid, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
            kevent(watchKq, &kev, 1, NULL, 0, NULL);
        }
        return PROCWATCH_Gone;
    }

    return PROCWATCH_Armed;
}

void _procWatchPlatformRemove(Process* proc)
{
    // A child's knote is EV_ONESHOT and removes itself as it fires, which is the only way a
    // child is ever removed. A process cx did not launch can also be removed by cancelling,
    // while its knote is still live.
    if (proc->ischild || watchKq < 0)
        return;

    deleteKnoteIfUnused(proc->pid);
}

void _procWatchPlatformWake(void)
{
    if (watchKq < 0)
        return;

    struct kevent kev;
    EV_SET(&kev, PROCWATCH_WAKE_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(watchKq, &kev, 1, NULL, 0, NULL);
}

void _procWatchPlatformWait(int64 timeout)
{
    if (watchKq < 0)
        return;

    struct kevent evs[32];
    struct timespec ts;
    ts.tv_sec  = (time_t)(timeout / 1000000);
    ts.tv_nsec = (long)((timeout % 1000000) * 1000);

    int n = kevent(watchKq, NULL, 0, evs, 32, &ts);

    for (int i = 0; i < n; i++) {
        if (evs[i].filter != EVFILT_PROC)
            continue;

        sa_Process procs;
        saInit(&procs, object, 2);
        _procWatchAcquireByPid(&procs, (int64)evs[i].ident);

        for (int32 j = 0; j < saSize(procs); j++) {
            Process* proc = procs.a[j];

            if (proc->ischild)
                reapChild(proc);
            else
                // Not ours to collect: that it exited is everything there is to learn.
                _procPublishExit(proc, false, 0, 0);

            _procWatchCompleted(proc);
        }

        saDestroy(&procs);
    }
}

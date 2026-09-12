#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_processobj.h"
#include "cx/string.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

// Reads a small /proc file whole. These are generated on read and always tiny, so one read is
// enough. Returns the byte count, or -1 if the file could not be read -- which for /proc
// usually means the process exited while we were looking at it.
static ssize_t readProcFile(const char* path, char* buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, sz - 1);
    close(fd);

    if (n < 0)
        return -1;

    buf[n] = 0;
    return n;
}

// Reads one numeric field from a /proc/<pid>/stat line, counting from the field after the
// executable name: index 0 is the state, 1 the parent pid, 19 the start time (fields 3, 4 and
// 22 of the line as documented in proc(5)).
//
// Field 2 is the executable name in parentheses, and it can itself contain both spaces and
// parentheses -- a process named "ev(il) name" is legal. Scanning for the LAST ')' is the only
// reliable way to find where the fixed-width fields begin; sscanf on the whole line gets this
// wrong for any such process.
static bool parseStatField(const char* stat, int idx, int64* out)
{
    const char* p = strrchr(stat, ')');
    if (!p)
        return false;

    p++;   // just past the comm field; what follows is " <state> <ppid> ..."

    for (int i = 0;; i++) {
        while (*p == ' ') p++;
        if (!*p)
            return false;
        if (i == idx)
            break;
        while (*p && *p != ' ') p++;
    }

    char* end   = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p)
        return false;

    *out = (int64)v;
    return true;
}

_Use_decl_annotations_
bool _procUnixStartTime(ProcessID pid, int64* out)
{
    char path[64], buf[1024];

    snprintf(path, sizeof(path), "/proc/%lld/stat", (long long)pid);
    if (readProcFile(path, buf, sizeof(buf)) < 0)
        return false;

    // Clock ticks since boot. The unit does not matter: this is only ever compared against
    // another reading for the same pid.
    return parseStatField(buf, 19, out);
}

// Resolves /proc/<pid>/exe. Fails with EACCES for processes belonging to other users, which is
// routine rather than an error worth reporting.
static bool readExePath(string* out, ProcessID pid)
{
    char link[64], buf[4096];

    snprintf(link, sizeof(link), "/proc/%lld/exe", (long long)pid);

    ssize_t n = readlink(link, buf, sizeof(buf) - 1);
    if (n <= 0)
        return false;

    strFromBytes(out, buf, (uint32)n);
    return true;
}

// Fills in one process. Returns false if it has gone away, which is expected during a walk of
// /proc and is not an error.
static bool fillProcInfo(ProcessInfo* info, ProcessID pid, flags_t flags)
{
    char path[64], buf[1024];

    info->pid = pid;

    snprintf(path, sizeof(path), "/proc/%lld/stat", (long long)pid);
    ssize_t n = readProcFile(path, buf, sizeof(buf));
    if (n < 0)
        return false;

    int64 ppid = 0;
    if (parseStatField(buf, 1, &ppid))
        info->ppid = (ProcessID)ppid;

    snprintf(path, sizeof(path), "/proc/%lld/comm", (long long)pid);
    n = readProcFile(path, buf, sizeof(buf));
    if (n > 0) {
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        strFromBytes(&info->name, buf, (uint32)n);
    }

    if (flags & PROC_EnumFullPath) {
        if (readExePath(&info->exepath, pid)) {
            // comm is truncated to 15 characters by the kernel. Where the real path is
            // readable, its filename is the untruncated name.
            pathFilename(&info->name, info->exepath);
        }
    }

    return !strEmpty(info->name);
}

_Use_decl_annotations_
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags)
{
    DIR* d = opendir("/proc");
    if (!d)
        return unixMapErrno();

    struct dirent* de;
    while ((de = readdir(d))) {
        // Only the all-numeric entries are processes; the rest of /proc is kernel state.
        char* end     = NULL;
        long long pid = strtoll(de->d_name, &end, 10);
        if (de->d_name[0] < '0' || de->d_name[0] > '9' || !end || *end || pid <= 0)
            continue;

        // Filled into a local and pushed only on success, so a process that exits mid-walk
        // never leaves a half-built entry in the array.
        ProcessInfo info;
        procInfoInit(&info);
        if (fillProcInfo(&info, (ProcessID)pid, flags))
            saPush(out, ProcessInfo, info);
        procInfoDestroy(&info);
    }

    closedir(d);
    return true;
}

_Use_decl_annotations_
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    if (pid <= 0) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    if (!fillProcInfo(out, pid, flags)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

// ---- exit watcher ------------------------------------------------------------------------
//
// A pidfd is pollable and refers to the process itself rather than to its id, so it cannot be
// confused by pid reuse the way a bare pid can. Watching them with epoll lets one thread wait
// on any number of children at once.

#include "cx/thread/atomic.h"
#include "cx/thread/mutex.h"

#include <sys/epoll.h>
#include <sys/syscall.h>

static int watchEpoll = -1;
static int watchWake[2] = { -1, -1 };

static int pidfdOpen(ProcessID pid)
{
#if defined(SYS_pidfd_open)
    return (int)syscall(SYS_pidfd_open, (pid_t)pid, 0);
#else
    return -1;
#endif
}

bool _procWatchPlatformInit(void)
{
#if !defined(SYS_pidfd_open)
    return false;
#else
    // Probe rather than trust the headers: pidfd_open is a 5.3 syscall, and cx may well be
    // running on an older kernel than it was built against.
    int probe = pidfdOpen((ProcessID)getpid());
    if (probe < 0)
        return false;
    close(probe);

    watchEpoll = epoll_create1(EPOLL_CLOEXEC);
    if (watchEpoll < 0)
        return false;

    // A sticky self-pipe, so a wake that arrives while the thread is not yet waiting is still
    // there when it gets to epoll_wait rather than being lost.
    if (pipe2(watchWake, O_CLOEXEC | O_NONBLOCK) != 0) {
        close(watchEpoll);
        watchEpoll = -1;
        return false;
    }

    struct epoll_event ev = { 0 };
    ev.events             = EPOLLIN;
    ev.data.ptr           = NULL;   // NULL marks the wake pipe rather than a process
    epoll_ctl(watchEpoll, EPOLL_CTL_ADD, watchWake[0], &ev);

    return true;
#endif
}

bool _procWatchPlatformAdd(Process* proc)
{
    UnixProcess* uproc = objDynCast(UnixProcess, proc);
    if (!uproc || watchEpoll < 0)
        return false;

    int fd = pidfdOpen(proc->pid);
    if (fd < 0)
        return false;

    uproc->waitfd = fd;

    struct epoll_event ev = { 0 };
    ev.events             = EPOLLIN;
    ev.data.ptr           = proc;

    if (epoll_ctl(watchEpoll, EPOLL_CTL_ADD, fd, &ev) != 0) {
        close(fd);
        uproc->waitfd = -1;
        return false;
    }

    return true;
}

void _procWatchPlatformRemove(Process* proc)
{
    UnixProcess* uproc = objDynCast(UnixProcess, proc);
    if (!uproc || uproc->waitfd < 0)
        return;

    if (watchEpoll >= 0)
        epoll_ctl(watchEpoll, EPOLL_CTL_DEL, uproc->waitfd, NULL);

    close(uproc->waitfd);
    uproc->waitfd = -1;
}

void _procWatchPlatformWake(void)
{
    if (watchWake[1] < 0)
        return;

    char b = 1;
    ssize_t ignored = write(watchWake[1], &b, 1);
    (void)ignored;
}

void _procWatchPlatformWait(int64 timeout)
{
    if (watchEpoll < 0)
        return;

    struct epoll_event evs[32];
    int n = epoll_wait(watchEpoll, evs, 32, (int)timeToMsec(timeout));

    for (int i = 0; i < n; i++) {
        if (!evs[i].data.ptr) {
            // The wake pipe. Drain it so it does not report readable forever.
            char buf[64];
            while (read(watchWake[0], buf, sizeof(buf)) > 0) {}
            continue;
        }

        Process* proc      = (Process*)evs[i].data.ptr;
        UnixProcess* uproc = objDynCast(UnixProcess, proc);
        if (!uproc || uproc->waitfd < 0)
            continue;

        // Reap through the pidfd, which collects the child and yields its status in one step
        // and cannot be confused by a reused pid.
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        if (waitid(P_PIDFD, (id_t)uproc->waitfd, &info, WEXITED) != 0)
            continue;

        mutexAcquire(&proc->lock);
        if (info.si_code == CLD_EXITED) {
            proc->exitcode   = info.si_status;
            proc->termsignal = 0;
        } else {
            proc->termsignal = info.si_status;
            proc->exitcode   = 128 + info.si_status;
        }
        atomicStore(bool, &proc->exited, true, Release);
        mutexRelease(&proc->lock);

        _procWatchCompleted(proc);
    }
}

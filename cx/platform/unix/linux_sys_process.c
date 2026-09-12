#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/fs/path.h"
#include "cx/platform/unix.h"
#include "cx/string.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

// Pulls the parent pid out of /proc/<pid>/stat.
//
// Field 2 of that line is the executable name in parentheses, and it can itself contain both
// spaces and parentheses -- a process named "ev(il) name" is legal. Scanning for the LAST ')'
// is the only reliable way to find where the fixed-width fields begin; sscanf on the whole line
// gets this wrong for any such process.
static bool parseStatPPid(const char* stat, ProcessID* ppid)
{
    const char* p = strrchr(stat, ')');
    if (!p)
        return false;

    p++;   // just past the comm field; what follows is " <state> <ppid> ..."

    while (*p == ' ') p++;
    while (*p && *p != ' ') p++;   // skip state
    while (*p == ' ') p++;

    if (*p < '0' || *p > '9')
        return false;

    *ppid = (ProcessID)strtoll(p, NULL, 10);
    return true;
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

    parseStatPPid(buf, &info->ppid);

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

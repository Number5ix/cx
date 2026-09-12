#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/linux_procfs.h"
#include "cx/platform/unix/unix_sys_sysinfo.h"
#include "cx/time/time.h"
#include "cx/utils/lazyinit.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// Everything /proc counts processor time in is clock ticks, whose length the C library reports
// and which is 10ms on every Linux in practice. Read once rather than on every sample.
static LazyInitState tickInitState;
static int64 tickHz;

static void tickInit(void* unused)
{
    long hz = sysconf(_SC_CLK_TCK);
    tickHz  = hz > 0 ? (int64)hz : 100;
}

static int64 ticksToUsec(int64 ticks)
{
    lazyInit(&tickInitState, tickInit, NULL);

    // Multiply before dividing: a tick rate that does not divide a million evenly would
    // otherwise lose most of the value.
    return ticks * 1000000 / tickHz;
}

// The "Key:  <n> kB" lines in /proc are kilobytes, and every caller here wants bytes.
static bool readKB(const char* buf, const char* key, uint64* out)
{
    int64 kb;
    if (!_linuxProcFileField(buf, key, &kb) || kb < 0)
        return false;

    *out = (uint64)kb * 1024;
    return true;
}

// Seconds since the Unix epoch when the machine booted, from /proc/stat's btime line. Fixed for
// the life of the machine, so it is read once.
static LazyInitState btimeInitState;
static int64 bootTime;

static void btimeInit(void* unused)
{
    // btime comes after one line per processor and after the interrupt line, which lists every
    // interrupt source on the machine. On a large server that is well past any fixed buffer, so
    // this one file is read whole rather than into a stack buffer.
    char* buf = _linuxReadProcFileAlloc("/proc/stat");
    if (!buf)
        return;

    int64 secs;
    if (_linuxProcFileField(buf, "btime", &secs))
        bootTime = timeFromTimeT((time_t)secs);

    xaFree(buf);
}

_Use_decl_annotations_
bool _sysPlatformMemInfo(SysMemInfo* out)
{
    char buf[8192];

    if (_linuxReadProcFile("/proc/meminfo", buf, sizeof(buf)) < 0)
        return unixMapErrno();

    if (readKB(buf, "MemTotal:", &out->physTotal)) {
        // MemAvailable is the kernel's own estimate of what can be handed out without paging,
        // and is what every tool reports as "available". Kernels before 3.14 do not have it, so
        // fall back to the sum it replaced.
        if (!readKB(buf, "MemAvailable:", &out->physAvail)) {
            uint64 freemem = 0, buffers = 0, cached = 0;
            readKB(buf, "MemFree:", &freemem);
            readKB(buf, "Buffers:", &buffers);
            readKB(buf, "Cached:", &cached);
            out->physAvail = freemem + buffers + cached;
        }
        out->valid |= SYSMEM_Phys;
    }

    uint64 committed = 0;
    if (readKB(buf, "CommitLimit:", &out->commitTotal) &&
        readKB(buf, "Committed_AS:", &committed)) {
        // Committed_AS can exceed the limit when overcommit is permissive, which would otherwise
        // wrap this unsigned subtraction into an enormous number.
        out->commitAvail = out->commitTotal > committed ? out->commitTotal - committed : 0;
        out->valid |= SYSMEM_Commit;
    }

    if (readKB(buf, "SwapTotal:", &out->swapTotal) && readKB(buf, "SwapFree:", &out->swapAvail))
        out->valid |= SYSMEM_Swap;

    long pagesz = sysconf(_SC_PAGESIZE);
    if (pagesz > 0) {
        out->pageSize = (uint64)pagesz;
        out->valid |= SYSMEM_PageSize;
    }

    if (out->valid == 0) {
        cxerr = CX_Unspecified;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformCPUTimes(SysCPUTimes* out)
{
    char buf[1024];

    // Only the leading aggregate "cpu" line is wanted; the per-processor lines that follow are
    // further into the file than this read goes, which is why a small buffer is enough.
    if (_linuxReadProcFile("/proc/stat", buf, sizeof(buf)) < 0)
        return unixMapErrno();

    if (strncmp(buf, "cpu ", 4) != 0) {
        cxerr = CX_Unspecified;
        return false;
    }

    // user nice system idle iowait irq softirq steal guest guest_nice. Older kernels stop short
    // of the later columns, so take however many are actually there.
    int64 v[10]  = { 0 };
    const char* p = buf + 4;
    int n         = 0;

    while (n < 10) {
        while (*p == ' ') p++;

        char* end   = NULL;
        long long x = strtoll(p, &end, 10);
        if (end == p)
            break;

        v[n++] = (int64)x;
        p      = end;
    }

    if (n < 4) {
        cxerr = CX_Unspecified;
        return false;
    }

    // Guest time is already counted inside user time by the kernel, so the last two columns are
    // deliberately left out rather than added again.
    out->user   = ticksToUsec(v[0] + v[1]);
    out->system = ticksToUsec(v[2]);
    out->idle   = ticksToUsec(v[3]);
    out->valid  = SYSCPU_User | SYSCPU_System | SYSCPU_Idle;

    if (n > 4) {
        out->iowait = ticksToUsec(v[4]);
        out->valid |= SYSCPU_IOWait;
    }
    if (n > 6) {
        out->irq = ticksToUsec(v[5] + v[6]);
        out->valid |= SYSCPU_IRQ;
    }
    if (n > 7) {
        out->steal = ticksToUsec(v[7]);
        out->valid |= SYSCPU_Steal;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformUptime(SysUptime* out)
{
    char buf[128];

    if (_linuxReadProcFile("/proc/uptime", buf, sizeof(buf)) < 0)
        return unixMapErrno();

    char* end     = NULL;
    double uptime = strtod(buf, &end);
    if (end == buf || uptime < 0) {
        cxerr = CX_Unspecified;
        return false;
    }

    out->uptime = (int64)(uptime * 1000000.0);
    out->valid |= SYSUP_Uptime;

    lazyInit(&btimeInitState, btimeInit, NULL);
    if (bootTime != 0) {
        // Taken straight from the kernel rather than derived from the uptime, so it does not
        // move every time this is called.
        out->boottime = bootTime;
        out->valid |= SYSUP_BootTime;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc)
{
    char path[64], buf[8192];

    if (!_sysUnixProcUsable(proc))
        return false;

    snprintf(path, sizeof(path), "/proc/%lld/status", (long long)pid);
    if (_linuxReadProcFile(path, buf, sizeof(buf)) < 0)
        return unixMapErrno();

    if (readKB(buf, "VmRSS:", &out->resident))
        out->valid |= PROCMEM_Resident;
    if (readKB(buf, "VmHWM:", &out->peakResident))
        out->valid |= PROCMEM_PeakResident;
    if (readKB(buf, "VmSize:", &out->virtualSize))
        out->valid |= PROCMEM_Virtual;
    if (readKB(buf, "VmPeak:", &out->peakVirtualSize))
        out->valid |= PROCMEM_PeakVirtual;
    if (readKB(buf, "VmSwap:", &out->swapped))
        out->valid |= PROCMEM_Swapped;

    // A kernel thread has no address space and so reports none of these lines. Nothing can be
    // said about its memory, which is a failure to report rather than a report of zero.
    if (out->valid == 0) {
        cxerr = CX_NotSupported;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    char path[64], buf[2048];

    if (!_sysUnixProcUsable(proc))
        return false;

    snprintf(path, sizeof(path), "/proc/%lld/stat", (long long)pid);
    if (_linuxReadProcFile(path, buf, sizeof(buf)) < 0)
        return unixMapErrno();

    int64 utime = 0, stime = 0, start = 0;

    if (_linuxParseStatField(buf, 11, &utime)) {
        out->user = ticksToUsec(utime);
        out->valid |= PROCCPU_User;
    }
    if (_linuxParseStatField(buf, 12, &stime)) {
        out->system = ticksToUsec(stime);
        out->valid |= PROCCPU_System;
    }

    lazyInit(&btimeInitState, btimeInit, NULL);
    if (bootTime != 0 && _linuxParseStatField(buf, 19, &start)) {
        // This field counts from boot, not from the epoch.
        out->started = bootTime + ticksToUsec(start);
        out->valid |= PROCCPU_Started;
    }

    if (out->valid == 0) {
        cxerr = CX_Unspecified;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc)
{
    char path[64], buf[1024];

    if (!_sysUnixProcUsable(proc))
        return false;

    // Readable only by the user the process belongs to. Asking for more than that would mean
    // tracing the process, so a refusal is reported as one rather than worked around.
    snprintf(path, sizeof(path), "/proc/%lld/io", (long long)pid);
    if (_linuxReadProcFile(path, buf, sizeof(buf)) < 0)
        return unixMapErrno();

    int64 rchar = 0, wchar_ = 0, syscr = 0, syscw = 0;

    // rchar and wchar count everything the process read and wrote, including from pipes,
    // sockets and the page cache, which is what the counters on the other platforms measure
    // too. read_bytes and write_bytes only count what actually reached a disk.
    if (_linuxProcFileField(buf, "rchar:", &rchar) &&
        _linuxProcFileField(buf, "wchar:", &wchar_)) {
        out->readBytes  = (uint64)rchar;
        out->writeBytes = (uint64)wchar_;
        out->valid |= PROCIO_Bytes;
    }

    if (_linuxProcFileField(buf, "syscr:", &syscr) &&
        _linuxProcFileField(buf, "syscw:", &syscw)) {
        out->readOps  = (uint64)syscr;
        out->writeOps = (uint64)syscw;
        out->valid |= PROCIO_Ops;
    }

    if (out->valid == 0) {
        cxerr = CX_Unspecified;
        return false;
    }

    return true;
}

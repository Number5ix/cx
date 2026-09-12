#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_sysinfo.h"
#include "cx/time/time.h"
#include "cx/utils/lazyinit.h"

#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>
#include <vm/vm_param.h>

#include <errno.h>
#include <unistd.h>

// Reads a sysctl of exactly the expected size. Anything else -- a name this kernel does not
// have, or a value of a different width than this binary was built against -- is treated as
// "not reported" rather than as an error, which is how the optional counters below drop out.
static bool sysctlExact(const char* name, void* out, size_t sz)
{
    size_t len = sz;
    return sysctlbyname(name, out, &len, NULL, 0) == 0 && len == sz;
}

// Reads a sysctl holding a single unsigned number, whatever width the kernel exports it as.
// hw.physmem and the page counters are not the same width on a 32-bit kernel as on a 64-bit one,
// and have changed between releases, so pinning a width here would quietly stop working.
static bool sysctlUNum(const char* name, uint64* out)
{
    uint64 buf = 0;
    size_t len = sizeof(buf);

    if (sysctlbyname(name, &buf, &len, NULL, 0) != 0)
        return false;

    if (len == sizeof(uint64)) {
        *out = buf;
        return true;
    }

    if (len == sizeof(uint32)) {
        // The kernel wrote the first four bytes of the buffer, which are not the low four bytes
        // of the value on a big-endian machine -- so copy them out rather than casting.
        uint32 v32;
        memcpy(&v32, &buf, sizeof(v32));
        *out = v32;
        return true;
    }

    return false;
}

static LazyInitState pageInitState;
static uint64 pageSize;

static void pageInit(void* unused)
{
    long sz  = sysconf(_SC_PAGESIZE);
    pageSize = sz > 0 ? (uint64)sz : 4096;
}

static uint64 sysPageSize(void)
{
    lazyInit(&pageInitState, pageInit, NULL);
    return pageSize;
}

// kern.cp_time counts in statistics-clock ticks, which is a different rate from the scheduler's
// hz and is what has to be divided out to get real time.
static LazyInitState tickInitState;
static int64 tickHz;

static void tickInit(void* unused)
{
    struct clockinfo ci;

    tickHz = 128;   // the historical stathz, used only if the kernel will not say
    if (sysctlExact("kern.clockrate", &ci, sizeof(ci))) {
        if (ci.stathz > 0)
            tickHz = ci.stathz;
        else if (ci.hz > 0)
            tickHz = ci.hz;
    }
}

static int64 ticksToUsec(int64 ticks)
{
    lazyInit(&tickInitState, tickInit, NULL);
    return ticks * 1000000 / tickHz;
}

// A timeval holding a point in time, such as when a process started.
static int64 timevalToCx(const struct timeval* tv)
{
    return timeFromTimeT((time_t)tv->tv_sec) + tv->tv_usec;
}

// A timeval holding a length of time, such as processor time consumed. No epoch shift applies.
static int64 timevalToDuration(const struct timeval* tv)
{
    return timeFromSeconds((int64)tv->tv_sec) + tv->tv_usec;
}

// Adds up every swap device the kernel has configured. This is the same walk swapinfo(8) does,
// and needs no privilege.
static bool sysSwapTotals(uint64* total, uint64* used)
{
    int mib[CTL_MAXNAME];
    size_t miblen = CTL_MAXNAME;

    if (sysctlnametomib("vm.swap_info", mib, &miblen) != 0)
        return false;

    uint64 blks = 0, inuse = 0;
    bool any = false;

    for (int n = 0;; n++) {
        struct xswdev xsw;
        size_t len = sizeof(xsw);

        mib[miblen] = n;
        if (sysctl(mib, (u_int)(miblen + 1), &xsw, &len, NULL, 0) != 0)
            break;   // one past the last device

        // A kernel built from different sources than this binary lays the structure out
        // differently, and reading it would produce nonsense rather than an error.
        if (xsw.xsw_version != XSWDEV_VERSION)
            return false;

        blks += (uint64)xsw.xsw_nblks;
        inuse += (uint64)xsw.xsw_used;
        any = true;
    }

    if (!any)
        return false;

    *total = blks * sysPageSize();
    *used  = inuse * sysPageSize();
    return true;
}

_Use_decl_annotations_
bool _sysPlatformMemInfo(SysMemInfo* out)
{
    uint64 physmem = 0;

    if (sysctlUNum("hw.physmem", &physmem)) {
        out->physTotal = physmem;

        // What a program can have without waiting: pages nobody holds, plus the ones the pager
        // will hand over on demand. The laundry queue replaced the cache queue in FreeBSD 12, so
        // whichever this kernel has contributes and the other simply does not exist.
        uint64 freepg = 0, inactive = 0, laundry = 0, cached = 0;
        sysctlUNum("vm.stats.vm.v_free_count", &freepg);
        sysctlUNum("vm.stats.vm.v_inactive_count", &inactive);
        sysctlUNum("vm.stats.vm.v_laundry_count", &laundry);
        sysctlUNum("vm.stats.vm.v_cache_count", &cached);

        out->physAvail = (freepg + inactive + laundry + cached) * sysPageSize();
        out->valid |= SYSMEM_Phys;
    }

    uint64 swtotal = 0, swused = 0;
    if (sysSwapTotals(&swtotal, &swused)) {
        out->swapTotal = swtotal;
        out->swapAvail = swtotal > swused ? swtotal - swused : 0;
        out->valid |= SYSMEM_Swap;
    }

    // There is no commit accounting to report: FreeBSD hands out address space freely and finds
    // out whether it can honour it when the memory is touched.

    out->pageSize = sysPageSize();
    out->valid |= SYSMEM_PageSize;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformCPUTimes(SysCPUTimes* out)
{
    long cp[CPUSTATES];

    if (!sysctlExact("kern.cp_time", cp, sizeof(cp)))
        return unixMapErrno();

    out->user   = ticksToUsec((int64)cp[CP_USER] + (int64)cp[CP_NICE]);
    out->system = ticksToUsec((int64)cp[CP_SYS]);
    out->irq    = ticksToUsec((int64)cp[CP_INTR]);
    out->idle   = ticksToUsec((int64)cp[CP_IDLE]);
    out->valid  = SYSCPU_User | SYSCPU_System | SYSCPU_IRQ | SYSCPU_Idle;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformUptime(SysUptime* out)
{
    struct timeval boot;

    if (!sysctlExact("kern.boottime", &boot, sizeof(boot)))
        return unixMapErrno();

    // The kernel knows when it started, not how long ago that was; the caller works out the
    // difference.
    out->boottime = timevalToCx(&boot);
    out->valid |= SYSUP_BootTime;
    return true;
}

// One sysctl answers every per-process question on this platform.
static bool sysProcKinfo(struct kinfo_proc* out, ProcessID pid, Process* proc)
{
    if (!_sysUnixProcUsable(proc))
        return false;

    if (pid <= 0) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    size_t len = sizeof(*out);

    if (sysctl(mib, 4, out, &len, NULL, 0) != 0 || len < sizeof(*out)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc)
{
    struct kinfo_proc kp;

    if (!sysProcKinfo(&kp, pid, proc))
        return false;

    out->resident = (uint64)kp.ki_rssize * sysPageSize();
    out->valid |= PROCMEM_Resident;

    out->virtualSize = (uint64)kp.ki_size;
    out->valid |= PROCMEM_Virtual;

    if (kp.ki_rusage.ru_maxrss > 0) {
        // ru_maxrss is in kilobytes, unlike everything else here.
        //
        // This kernel samples that high-water mark periodically rather than tracking every page,
        // so a process whose resident set grew quickly can show a peak permanently *below* its
        // current size -- and it never catches up, even once the process goes idle. A peak is by
        // definition at least the current value, so take whichever is larger: that is never less
        // accurate than the kernel's sample, and it keeps the field meaning what it says.
        uint64 peak       = (uint64)kp.ki_rusage.ru_maxrss * 1024;
        out->peakResident = peak > out->resident ? peak : out->resident;
        out->valid |= PROCMEM_PeakResident;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    struct kinfo_proc kp;

    if (!sysProcKinfo(&kp, pid, proc))
        return false;

    out->user   = timevalToDuration(&kp.ki_rusage.ru_utime);
    out->system = timevalToDuration(&kp.ki_rusage.ru_stime);
    out->valid  = PROCCPU_User | PROCCPU_System;

    out->started = timevalToCx(&kp.ki_start);
    out->valid |= PROCCPU_Started;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc)
{
    struct kinfo_proc kp;

    if (!sysProcKinfo(&kp, pid, proc))
        return false;

    // The kernel counts a process's block-device operations but not the bytes they moved, so
    // only the operation counts can be reported here.
    out->readOps  = (uint64)kp.ki_rusage.ru_inblock;
    out->writeOps = (uint64)kp.ki_rusage.ru_oublock;
    out->valid |= PROCIO_Ops;

    return true;
}

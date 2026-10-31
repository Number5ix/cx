#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_sysinfo.h"
#include "cx/time/time.h"
#include "cx/utils/lazyinit.h"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

// Reads a sysctl of exactly the expected size; anything else is treated as "not reported".
static bool sysctlExact(const char* name, void* out, size_t sz)
{
    size_t len = sz;
    return sysctlbyname(name, out, &len, NULL, 0) == 0 && len == sz;
}

static LazyInitState hostInitState;
static mach_port_t hostPort;
static uint64 pageSize;
static uint64 kernPageSize;
static int64 tickHz;
static uint64 timebaseNumer;
static uint64 timebaseDenom;

static void hostInit(void* unused)
{
    // mach_host_self() hands out a new port right on every call, so it is fetched once and kept.
    hostPort = mach_host_self();

    long sz  = sysconf(_SC_PAGESIZE);
    pageSize = sz > 0 ? (uint64)sz : 4096;

    // The kernel's VM counters are in its own pages, which are not the size a program sees when
    // it runs under Rosetta (16K kernel pages, 4K reported to the program).
    kernPageSize = vm_kernel_page_size ? (uint64)vm_kernel_page_size : pageSize;

    long hz = sysconf(_SC_CLK_TCK);
    tickHz  = hz > 0 ? hz : 100;

    // Process CPU times come back from the kernel in mach_absolute_time units. Under Rosetta,
    // mach_timebase_info describes the translated clock (1/1) rather than the kernel's own, which
    // on Apple Silicon is a fixed 24 MHz (125/3 ns per tick), so it must not be trusted there.
    int translated  = 0;
    size_t len      = sizeof(translated);
    if (sysctlbyname("sysctl.proc_translated", &translated, &len, NULL, 0) == 0 && translated) {
        timebaseNumer = 125;
        timebaseDenom = 3;
    } else {
        mach_timebase_info_data_t tb;
        if (mach_timebase_info(&tb) == KERN_SUCCESS && tb.denom != 0) {
            timebaseNumer = tb.numer;
            timebaseDenom = tb.denom;
        } else {
            timebaseNumer = 1;
            timebaseDenom = 1;
        }
    }
}

static void sysHostInit(void)
{
    lazyInit(&hostInitState, hostInit, NULL);
}

static int64 machToUsec(uint64 t)
{
    // Split to keep t * numer from overflowing for large values.
    uint64 ns = (t / timebaseDenom) * timebaseNumer + (t % timebaseDenom) * timebaseNumer / timebaseDenom;
    return (int64)(ns / 1000);
}

static int64 ticksToUsec(uint64 ticks)
{
    return (int64)(ticks * 1000000 / (uint64)tickHz);
}

// A timeval holding a point in time, such as when a process started.
static int64 timevalToCx(const struct timeval* tv)
{
    return timeFromTimeT((time_t)tv->tv_sec) + tv->tv_usec;
}

_Use_decl_annotations_
bool _sysPlatformMemInfo(SysMemInfo* out)
{
    sysHostInit();

    uint64 memsize = 0;
    if (sysctlExact("hw.memsize", &memsize, sizeof(memsize))) {
        out->physTotal = memsize;

        // What a program can have without waiting: free pages, plus inactive ones the pager can
        // hand over on demand. free_count already includes the speculative pages.
        vm_statistics64_data_t vm;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(hostPort, HOST_VM_INFO64, (host_info64_t)&vm, &count) ==
            KERN_SUCCESS) {
            out->physAvail = ((uint64)vm.free_count + (uint64)vm.inactive_count) * kernPageSize;
            out->valid |= SYSMEM_Phys;
        }
    }

    // Swap files are created and removed on demand, so a machine with none yet reports 0 of 0.
    struct xsw_usage swap;
    if (sysctlExact("vm.swapusage", &swap, sizeof(swap))) {
        out->swapTotal = swap.xsu_total;
        out->swapAvail = swap.xsu_avail;
        out->valid |= SYSMEM_Swap;
    }

    // There is no commit accounting to report: address space is handed out freely and backed
    // by the compressor and swap when it is touched.

    out->pageSize = pageSize;
    out->valid |= SYSMEM_PageSize;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformCPUTimes(SysCPUTimes* out)
{
    sysHostInit();

    // Per processor rather than HOST_CPU_LOAD_INFO, whose machine-wide 32-bit tick counters wrap
    // within weeks on a machine with many cores.
    natural_t ncpu                  = 0;
    processor_info_array_t info     = NULL;
    mach_msg_type_number_t infoCount = 0;

    if (host_processor_info(hostPort, PROCESSOR_CPU_LOAD_INFO, &ncpu, &info, &infoCount) !=
        KERN_SUCCESS) {
        cxerr = CX_Unspecified;
        return false;
    }

    uint64 user = 0, sys = 0, idle = 0;
    processor_cpu_load_info_t loads = (processor_cpu_load_info_t)info;
    for (natural_t i = 0; i < ncpu; i++) {
        user += (uint64)loads[i].cpu_ticks[CPU_STATE_USER] + (uint64)loads[i].cpu_ticks[CPU_STATE_NICE];
        sys += (uint64)loads[i].cpu_ticks[CPU_STATE_SYSTEM];
        idle += (uint64)loads[i].cpu_ticks[CPU_STATE_IDLE];
    }

    vm_deallocate(mach_task_self(), (vm_address_t)info, (vm_size_t)infoCount * sizeof(integer_t));

    out->user   = ticksToUsec(user);
    out->system = ticksToUsec(sys);
    out->idle   = ticksToUsec(idle);
    out->valid  = SYSCPU_User | SYSCPU_System | SYSCPU_Idle;

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

static bool sysProcCheck(ProcessID pid, Process* proc)
{
    if (!_sysUnixProcUsable(proc))
        return false;

    if (pid <= 0) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    sysHostInit();
    return true;
}

// Resource usage of a process. Readable for the same user's processes, or as root.
static bool sysProcRusage(struct rusage_info_v4* out, ProcessID pid)
{
    if (proc_pid_rusage((int)pid, RUSAGE_INFO_V4, (rusage_info_t*)out) != 0) {
        cxerr = (errno == ESRCH) ? CX_FileNotFound : CX_AccessDenied;
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc)
{
    if (!sysProcCheck(pid, proc))
        return false;

    struct proc_taskinfo ti;
    int r = proc_pidinfo((int)pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
    if (r == (int)sizeof(ti)) {
        out->resident = ti.pti_resident_size;
        out->valid |= PROCMEM_Resident;

        out->virtualSize = ti.pti_virtual_size;
        out->valid |= PROCMEM_Virtual;
    }

    // The phys footprint is the memory charged to this process alone: its dirty private pages,
    // including those compressed or swapped out.
    struct rusage_info_v4 ri;
    if (sysProcRusage(&ri, pid)) {
        out->privateSize = ri.ri_phys_footprint;
        out->valid |= PROCMEM_Private;
    }

    if (!out->valid) {
        if (r <= 0 && errno == ESRCH)
            cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    if (!sysProcCheck(pid, proc))
        return false;

    struct rusage_info_v4 ri;
    if (sysProcRusage(&ri, pid)) {
        out->user   = machToUsec(ri.ri_user_time);
        out->system = machToUsec(ri.ri_system_time);
        out->valid |= PROCCPU_User | PROCCPU_System;
    }

    // The start time is readable for every process, whoever owns it.
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    if (sysctl(mib, 4, &kp, &len, NULL, 0) == 0 && len >= sizeof(kp)) {
        out->started = timevalToCx(&kp.kp_proc.p_starttime);
        out->valid |= PROCCPU_Started;
    }

    if (!out->valid) {
        cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc)
{
    if (!sysProcCheck(pid, proc))
        return false;

    struct rusage_info_v4 ri;
    if (!sysProcRusage(&ri, pid))
        return false;

    // The kernel counts bytes but not how many separate operations moved them. Writes are taken
    // from the logical counter, which moves as soon as the process writes; the disk counter only
    // moves when the cache is flushed, possibly long after. Reads have only the disk counter.
    out->readBytes  = ri.ri_diskio_bytesread;
    out->writeBytes = ri.ri_logical_writes;
    out->valid |= PROCIO_Bytes;

    return true;
}

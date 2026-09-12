#include "sysinfo_private.h"

#include <cx/debug/error.h>
#include <cx/platform/os.h>
#include <cx/thread/atomic.h>
#include <cx/time/clock.h>

static int32 sysCPUCount(void)
{
    int n = osLogicalCPUs();
    return n > 0 ? (int32)n : 1;
}

// Turns a pair of counter readings into a percentage. Counters only ever move forwards, but a
// processor coming online or going offline between two readings can make them appear not to, so
// anything nonsensical reads as zero rather than as a wild number.
static float64 sysUsagePct(int64 used, int64 capacity)
{
    if (used <= 0 || capacity <= 0)
        return 0.0;

    float64 pct = (float64)used * 100.0 / (float64)capacity;
    return pct > 100.0 ? 100.0 : pct;
}

_Use_decl_annotations_
bool sysMemInfo(SysMemInfo* out)
{
    memset(out, 0, sizeof(SysMemInfo));
    return _sysPlatformMemInfo(out);
}

_Use_decl_annotations_
bool sysCPUTimes(SysCPUTimes* out)
{
    memset(out, 0, sizeof(SysCPUTimes));
    out->sampled = clockTimer();
    out->cpus    = sysCPUCount();

    if (!_sysPlatformCPUTimes(out))
        return false;

    // Everything the platform reported adds up to the time accounted for, and whatever is not
    // idle counts as busy. Working it out here rather than in each backend means a platform
    // that grows a new state later is included automatically.
    out->total = out->user + out->system + out->idle + out->iowait + out->irq + out->steal;
    out->busy  = out->total - out->idle;
    return true;
}

_Use_decl_annotations_
bool sysUptime(SysUptime* out)
{
    memset(out, 0, sizeof(SysUptime));

    if (!_sysPlatformUptime(out))
        return false;

    // Each platform knows one of these natively; the other follows from the current time.
    if ((out->valid & SYSUP_Uptime) && !(out->valid & SYSUP_BootTime)) {
        out->boottime = clockWall() - out->uptime;
        out->valid |= SYSUP_BootTime;
    } else if ((out->valid & SYSUP_BootTime) && !(out->valid & SYSUP_Uptime)) {
        out->uptime = clockWall() - out->boottime;
        out->valid |= SYSUP_Uptime;
    }

    return true;
}

_Use_decl_annotations_
bool sysLoadAvg(SysLoadAvg* out)
{
    memset(out, 0, sizeof(SysLoadAvg));
    return _sysPlatformLoadAvg(out);
}

_Use_decl_annotations_
float64 sysCPUUsage(const SysCPUTimes* first, const SysCPUTimes* second)
{
    return sysUsagePct(second->busy - first->busy, second->total - first->total);
}

_Use_decl_annotations_
bool sysCPUSample(SysCPUSampler* s, float64* pct)
{
    SysCPUTimes now;

    *pct = 0.0;
    if (!sysCPUTimes(&now))
        return false;

    bool ready = s->primed;
    if (ready)
        *pct = sysCPUUsage(&s->last, &now);

    s->last   = now;
    s->primed = true;
    return ready;
}

// Checks that a handle still refers to a live process before asking about it. A handle whose
// process has finished must report nothing rather than the numbers belonging to whatever now
// holds that process id.
static bool sysProcUsable(Process* proc)
{
    if (!proc) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    if (atomicLoad(bool, &proc->exited, Acquire)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool procMemInfo(ProcMemInfo* out, Process* proc)
{
    memset(out, 0, sizeof(ProcMemInfo));

    if (!sysProcUsable(proc))
        return false;

    return _sysPlatformProcMemInfo(out, proc->pid, proc);
}

_Use_decl_annotations_
bool procMemInfoByID(ProcMemInfo* out, ProcessID pid)
{
    memset(out, 0, sizeof(ProcMemInfo));

    if (pid == PROCESS_InvalidID) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    return _sysPlatformProcMemInfo(out, pid, NULL);
}

// Shared tail of the two processor-time entry points.
static bool sysProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    out->sampled = clockTimer();

    if (!_sysPlatformProcCPUTimes(out, pid, proc))
        return false;

    out->total = out->user + out->system;
    return true;
}

_Use_decl_annotations_
bool procCPUTimes(ProcCPUTimes* out, Process* proc)
{
    memset(out, 0, sizeof(ProcCPUTimes));

    if (!sysProcUsable(proc))
        return false;

    return sysProcCPUTimes(out, proc->pid, proc);
}

_Use_decl_annotations_
bool procCPUTimesByID(ProcCPUTimes* out, ProcessID pid)
{
    memset(out, 0, sizeof(ProcCPUTimes));

    if (pid == PROCESS_InvalidID) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    return sysProcCPUTimes(out, pid, NULL);
}

_Use_decl_annotations_
bool procIOInfo(ProcIOInfo* out, Process* proc)
{
    memset(out, 0, sizeof(ProcIOInfo));

    if (!sysProcUsable(proc))
        return false;

    return _sysPlatformProcIOInfo(out, proc->pid, proc);
}

_Use_decl_annotations_
bool procIOInfoByID(ProcIOInfo* out, ProcessID pid)
{
    memset(out, 0, sizeof(ProcIOInfo));

    if (pid == PROCESS_InvalidID) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    return _sysPlatformProcIOInfo(out, pid, NULL);
}

_Use_decl_annotations_
float64 procCPUUsage(const ProcCPUTimes* first, const ProcCPUTimes* second)
{
    // A process's own counters say nothing about how much time was available to use, so the
    // capacity comes from elapsed wall time multiplied by the number of processors. That is what
    // makes this figure directly comparable with sysCPUUsage().
    int64 elapsed = second->sampled - first->sampled;
    return sysUsagePct(second->total - first->total, elapsed * sysCPUCount());
}

_Use_decl_annotations_
bool procCPUSample(ProcCPUSampler* s, Process* proc, float64* pct)
{
    ProcCPUTimes now;

    *pct = 0.0;
    if (!procCPUTimes(&now, proc))
        return false;

    bool ready = s->primed;
    if (ready)
        *pct = procCPUUsage(&s->last, &now);

    s->last   = now;
    s->primed = true;
    return ready;
}

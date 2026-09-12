#include "cx/sys/sysinfo_private.h"
#include "cx/debug/error.h"
#include "cx/platform/win.h"
#include "cx/platform/win/win_sys_prochandle.h"
#include "cx/platform/win/win_sys_processobj.h"
#include "cx/platform/win/win_time.h"
#include "cx/time/time.h"
#include "cx/utils/lazyinit.h"

#include <psapi.h>

// Everything newer than Windows 2000 is reached through GetProcAddress, so this file adds no
// load-time import and the binary starts on every OS cx supports. psapi is loaded rather than
// linked for the same reason -- naming its library would put it in the import table whether or
// not the machine has it.
typedef BOOL(WINAPI* LPFN_GST)(LPFILETIME, LPFILETIME, LPFILETIME);
typedef ULONGLONG(WINAPI* LPFN_GTC64)(void);
typedef BOOL(WINAPI* LPFN_GPMI)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
typedef LONG(WINAPI* LPFN_NTQSI)(ULONG, PVOID, ULONG, PULONG);

static LPFN_GST fnGetSystemTimes;
static LPFN_GTC64 fnGetTickCount64;
static LPFN_GPMI fnGetProcessMemoryInfo;
static LPFN_NTQSI fnNtQuerySystemInformation;

static LazyInitState sysApiInitState;

static void sysApiInit(void* unused)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE ntdll    = GetModuleHandleW(L"ntdll.dll");
    HMODULE psapi    = LoadLibraryW(L"psapi.dll");

    if (kernel32) {
        fnGetSystemTimes = (LPFN_GST)GetProcAddress(kernel32, "GetSystemTimes");
        fnGetTickCount64 = (LPFN_GTC64)GetProcAddress(kernel32, "GetTickCount64");
    }

    if (ntdll)
        fnNtQuerySystemInformation =
            (LPFN_NTQSI)GetProcAddress(ntdll, "NtQuerySystemInformation");

    if (psapi)
        fnGetProcessMemoryInfo = (LPFN_GPMI)GetProcAddress(psapi, "GetProcessMemoryInfo");
}

// The two NtQuerySystemInformation classes used as fallbacks, with cx's own names for the
// classes and structures. They are not in the SDK headers, and spelling them here rather than
// importing them keeps this file buildable against any SDK version.
#define CX_SystemTimeOfDayInformation           3
#define CX_SystemProcessorPerformanceInformation 8

typedef struct CXSystemTimeOfDay {
    LARGE_INTEGER BootTime;
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER TimeZoneBias;
    ULONG CurrentTimeZoneId;
} CXSystemTimeOfDay;

typedef struct CXProcessorPerf {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;   // includes IdleTime
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} CXProcessorPerf;

// Windows counts time in hundreds of nanoseconds everywhere; cx counts in microseconds.
static int64 hundredNsToUsec(int64 v)
{
    return v / 10;
}

static int64 fileTimeSpan(const FILETIME* ft)
{
    // A FILETIME used as a duration rather than an instant, so no epoch shift applies.
    return hundredNsToUsec(((int64)ft->dwHighDateTime << 32) | ft->dwLowDateTime);
}

// The handle to ask about a process. A caller that already has a Process uses the handle it
// opened, which also pins the process id against reuse; otherwise one is opened for the
// duration of the call. *temp says whether the result has to be closed afterwards.
static HANDLE sysProcHandle(ProcessID pid, Process* proc, bool* temp)
{
    *temp = false;

    if (proc) {
        WinProcess* wproc = objDynCast(WinProcess, proc);
        if (wproc && wproc->h)
            return wproc->h;
    }

    HANDLE h = _procWinOpenHandle(pid);
    if (!h) {
        winMapLastError();
        return NULL;
    }

    *temp = true;
    return h;
}

static void sysProcHandleDone(HANDLE h, bool temp)
{
    if (temp)
        CloseHandle(h);
}

_Use_decl_annotations_
bool _sysPlatformMemInfo(SysMemInfo* out)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);

    if (!GlobalMemoryStatusEx(&ms))
        return winMapLastError();

    out->physTotal = ms.ullTotalPhys;
    out->physAvail = ms.ullAvailPhys;
    out->valid |= SYSMEM_Phys;

    // Despite the names, these two are the commit limit and the commit still available, not the
    // size of the paging file. Windows keeps no separate figure for the paging file alone, which
    // is why the swap fields stay unreported here.
    out->commitTotal = ms.ullTotalPageFile;
    out->commitAvail = ms.ullAvailPageFile;
    out->valid |= SYSMEM_Commit;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    out->pageSize = si.dwPageSize;
    out->valid |= SYSMEM_PageSize;

    return true;
}

// Adds up the per-processor counters the kernel keeps. Used where GetSystemTimes is missing,
// which is anything older than Windows XP Service Pack 1.
static bool sysCPUTimesNt(SysCPUTimes* out)
{
    if (!fnNtQuerySystemInformation)
        return false;

    int n = out->cpus > 0 ? out->cpus : 1;
    CXProcessorPerf* perf = xaAlloc(sizeof(CXProcessorPerf) * n, XA_Zero);
    ULONG got             = 0;

    bool ok = fnNtQuerySystemInformation(CX_SystemProcessorPerformanceInformation, perf,
                                         (ULONG)(sizeof(CXProcessorPerf) * n), &got) >= 0 &&
              got >= sizeof(CXProcessorPerf);

    if (ok) {
        int64 idle = 0, kernel = 0, user = 0;

        for (ULONG i = 0; i < got / sizeof(CXProcessorPerf); i++) {
            idle += perf[i].IdleTime.QuadPart;
            kernel += perf[i].KernelTime.QuadPart;
            user += perf[i].UserTime.QuadPart;
        }

        // Kernel time includes idle time, so subtracting it gives the time actually spent in the
        // operating system.
        out->idle   = hundredNsToUsec(idle);
        out->system = hundredNsToUsec(kernel - idle);
        out->user   = hundredNsToUsec(user);
        out->valid  = SYSCPU_User | SYSCPU_System | SYSCPU_Idle;
    }

    xaFree(perf);
    return ok;
}

_Use_decl_annotations_
bool _sysPlatformCPUTimes(SysCPUTimes* out)
{
    lazyInit(&sysApiInitState, sysApiInit, NULL);

    FILETIME idle, kernel, user;

    if (fnGetSystemTimes && fnGetSystemTimes(&idle, &kernel, &user)) {
        int64 idleus = fileTimeSpan(&idle);

        out->idle   = idleus;
        out->system = fileTimeSpan(&kernel) - idleus;   // kernel time includes idle time
        out->user   = fileTimeSpan(&user);
        out->valid  = SYSCPU_User | SYSCPU_System | SYSCPU_Idle;
        return true;
    }

    if (sysCPUTimesNt(out))
        return true;

    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformUptime(SysUptime* out)
{
    lazyInit(&sysApiInitState, sysApiInit, NULL);

    if (fnGetTickCount64) {
        out->uptime = timeFromMsec((int64)fnGetTickCount64());
        out->valid |= SYSUP_Uptime;
        return true;
    }

    // The 32-bit GetTickCount is not a fallback: it wraps after 49 days, so it would report a
    // confidently wrong number on exactly the machines this matters for. The kernel's own record
    // of when it started has no such limit and goes back to NT 4.
    CXSystemTimeOfDay tod;
    ULONG got = 0;

    if (fnNtQuerySystemInformation &&
        fnNtQuerySystemInformation(CX_SystemTimeOfDayInformation, &tod, sizeof(tod), &got) >= 0 &&
        got >= sizeof(tod)) {
        out->uptime   = hundredNsToUsec(tod.CurrentTime.QuadPart - tod.BootTime.QuadPart);
        out->boottime = timeFromFileTime((FILETIME*)&tod.BootTime);
        out->valid |= SYSUP_Uptime | SYSUP_BootTime;
        return true;
    }

    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformLoadAvg(SysLoadAvg* out)
{
    // Windows keeps no run queue average, and there is nothing to approximate one from that
    // would not be a guess dressed up as a measurement.
    cxerr = CX_NotSupported;
    return false;
}

_Use_decl_annotations_
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc)
{
    lazyInit(&sysApiInitState, sysApiInit, NULL);

    if (!fnGetProcessMemoryInfo) {
        cxerr = CX_NotSupported;
        return false;
    }

    bool temp;
    HANDLE h = sysProcHandle(pid, proc, &temp);
    if (!h)
        return false;

    // The extended structure adds PrivateUsage on the end. An OS that does not know about it
    // fails the call rather than filling in what it can, so the plain one is tried next.
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);

    bool ext = fnGetProcessMemoryInfo(h, (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc)) != 0;

    if (!ext) {
        memset(&pmc, 0, sizeof(pmc));
        pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS);

        if (!fnGetProcessMemoryInfo(h, (PPROCESS_MEMORY_COUNTERS)&pmc,
                                    sizeof(PROCESS_MEMORY_COUNTERS))) {
            winMapLastError();
            sysProcHandleDone(h, temp);
            return false;
        }
    }

    sysProcHandleDone(h, temp);

    out->resident     = pmc.WorkingSetSize;
    out->peakResident = pmc.PeakWorkingSetSize;
    out->valid |= PROCMEM_Resident | PROCMEM_PeakResident;

    // PrivateUsage is what the process has charged against the commit limit; PagefileUsage is
    // the same number under the older name. Windows does not report the size of the address
    // space a process has reserved, so the virtual fields stay unreported.
    out->privateSize = ext ? pmc.PrivateUsage : pmc.PagefileUsage;
    out->valid |= PROCMEM_Private;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc)
{
    bool temp;
    HANDLE h = sysProcHandle(pid, proc, &temp);
    if (!h)
        return false;

    FILETIME created, exited, kernel, user;
    bool ok = GetProcessTimes(h, &created, &exited, &kernel, &user) != 0;

    if (!ok)
        winMapLastError();

    sysProcHandleDone(h, temp);
    if (!ok)
        return false;

    out->user   = fileTimeSpan(&user);
    out->system = fileTimeSpan(&kernel);
    out->valid  = PROCCPU_User | PROCCPU_System;

    // Unlike the two above, this one really is an instant.
    out->started = timeFromFileTime(&created);
    out->valid |= PROCCPU_Started;

    return true;
}

_Use_decl_annotations_
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc)
{
    bool temp;
    HANDLE h = sysProcHandle(pid, proc, &temp);
    if (!h)
        return false;

    IO_COUNTERS io;
    bool ok = GetProcessIoCounters(h, &io) != 0;

    if (!ok)
        winMapLastError();

    sysProcHandleDone(h, temp);
    if (!ok)
        return false;

    out->readBytes  = io.ReadTransferCount;
    out->writeBytes = io.WriteTransferCount;
    out->readOps    = io.ReadOperationCount;
    out->writeOps   = io.WriteOperationCount;
    out->otherBytes = io.OtherTransferCount;
    out->otherOps   = io.OtherOperationCount;
    out->valid      = PROCIO_Bytes | PROCIO_Ops | PROCIO_Other;

    return true;
}

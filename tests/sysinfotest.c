#include <cx/container.h>
#include <cx/cx.h>
#include <cx/debug/error.h>
#include <cx/fs.h>
#include <cx/platform/os.h>
#include <cx/string.h>
#include <cx/sys.h>
#include <cx/thread.h>
#include <cx/time.h>

#define TEST_FILE sysinfotest
#define TEST_FUNCS sysinfotest_funcs
#include "common.h"

// How long every test that needs measurable processor time spends burning it. Processor
// accounting is quantized -- 10ms per tick on Linux, similar elsewhere -- so a short burst can
// land entirely inside one tick and register as nothing at all.
#define kSpinTime timeMS(250)

// Burns processor time in the calling thread for the given duration. Deliberately not a sleep:
// the point is to make the counters move.
static void spin(int64 usec)
{
    int64 end          = clockTimer() + usec;
    volatile uint64 acc = 0;

    while (clockTimer() < end) {
        for (int i = 0; i < 4096; i++) acc += (uint64)i;
    }
}

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_WASM)
// Confirms a call that this platform cannot answer refuses in the documented way, rather than
// returning an empty structure that would read as real zeroes.
static int checkNotSupported(bool ret, strref what)
{
    if (ret)
        TEST_FAIL(1, _SL("${string} succeeded on a platform that has no such figure"),
                  stvar(strref, what));

    if (cxerr != CX_NotSupported)
        TEST_FAIL(1, _SL("${string} failed with cxerr ${int}, wanted CX_NotSupported (${int})"),
                  stvar(strref, what), stvar(int32, cxerr),
                  stvar(int32, (int32)CX_NotSupported));

    return 0;
}
#endif

// True where the operating system is being emulated closely enough to answer these calls, but
// not closely enough to answer them truthfully. Wine returns success with zeroes from the
// counters that describe a process other than the caller, so the checks that require a counter
// to have moved have nothing to observe there. Nothing about cx is being exercised either way.
static bool procStatsEmulated(void)
{
#if defined(_PLATFORM_WIN)
    return osIsWine();
#else
    return false;
#endif
}

// ---- system-wide ------------------------------------------------------------------------------

static int test_sysinfo_mem(void)
{
    SysMemInfo mem;
    int ret = 0;

    if (!sysMemInfo(&mem))
        TEST_FAIL(1, _SL("sysMemInfo failed, cxerr ${int}"), stvar(int32, cxerr));

    // Every platform can say how much memory there is; the rest varies.
    if (!(mem.valid & SYSMEM_Phys))
        TEST_FAIL(1, _SL("sysMemInfo did not report physical memory, valid ${uint}"),
                  stvar(uint32, mem.valid));

    if (mem.physTotal == 0)
        TEST_FAILV(ret, 1, _SL("physTotal is zero, valid ${uint}"),
                   stvar(uint32, mem.valid));

    if (mem.physAvail > mem.physTotal)
        TEST_FAILV(ret, 1, _SL("physAvail ${uint} exceeds physTotal ${uint}"),
                   stvar(uint64, mem.physAvail), stvar(uint64, mem.physTotal));

    if ((mem.valid & SYSMEM_Commit) && mem.commitAvail > mem.commitTotal)
        TEST_FAILV(ret, 1, _SL("commitAvail ${uint} exceeds commitTotal ${uint}"),
                   stvar(uint64, mem.commitAvail), stvar(uint64, mem.commitTotal));

    if ((mem.valid & SYSMEM_Swap) && mem.swapAvail > mem.swapTotal)
        TEST_FAILV(ret, 1, _SL("swapAvail ${uint} exceeds swapTotal ${uint}"),
                   stvar(uint64, mem.swapAvail), stvar(uint64, mem.swapTotal));

    // A page size that is not a power of two means the field was filled from the wrong place.
    if ((mem.valid & SYSMEM_PageSize) &&
        (mem.pageSize < 512 || (mem.pageSize & (mem.pageSize - 1)) != 0))
        TEST_FAILV(ret, 1, _SL("pageSize ${uint} is not a sensible page size"),
                   stvar(uint64, mem.pageSize));

    return ret;
}

static int test_sysinfo_cpu(void)
{
    SysCPUTimes before, after;
    int ret = 0;

    if (!sysCPUTimes(&before)) {
#if defined(_PLATFORM_WASM)
        return checkNotSupported(false, _SL("sysCPUTimes"));
#else
        TEST_FAIL(1, _SL("sysCPUTimes failed, cxerr ${int}"), stvar(int32, cxerr));
#endif
    }

    if (before.cpus < 1)
        TEST_FAILV(ret, 1, _SL("cpus is ${int}, wanted at least 1"), stvar(int32, before.cpus));

    if (before.total != before.busy + before.idle)
        TEST_FAILV(ret, 1, _SL("total ${int} != busy ${int} + idle ${int}"),
                   stvar(int64, before.total), stvar(int64, before.busy),
                   stvar(int64, before.idle));

    spin(kSpinTime);

    if (!sysCPUTimes(&after))
        TEST_FAIL(1, _SL("second sysCPUTimes failed, cxerr ${int}"), stvar(int32, cxerr));

    if (after.total <= before.total)
        TEST_FAILV(ret, 1, _SL("total did not advance: ${int} then ${int}"),
                   stvar(int64, before.total), stvar(int64, after.total));

    // This process just spent a quarter second at full tilt, so some of the machine's time has
    // to have been accounted as busy.
    if (after.busy <= before.busy)
        TEST_FAILV(ret, 1, _SL("busy did not advance across a spin: ${int} then ${int}"),
                   stvar(int64, before.busy), stvar(int64, after.busy));

    float64 pct = sysCPUUsage(&before, &after);
    if (pct <= 0.0 || pct > 100.0)
        TEST_FAILV(ret, 1, _SL("sysCPUUsage returned ${float}, wanted above 0 and up to 100"),
                   stvar(float64, pct));

    return ret;
}

static int test_sysinfo_cpusampler(void)
{
    SysCPUSampler sampler;
    float64 pct = -1.0;
    int ret     = 0;

    sysCPUSamplerInit(&sampler);

    // The first call has no earlier reading to compare against, so it establishes one and says
    // it has no answer.
    if (sysCPUSample(&sampler, &pct))
        TEST_FAIL(1, _SL("sysCPUSample reported a figure (${float}) on its first call"),
                  stvar(float64, pct));

    if (pct != 0.0)
        TEST_FAILV(ret, 1, _SL("first sysCPUSample left pct at ${float}, wanted 0"),
                   stvar(float64, pct));

    spin(kSpinTime);

    if (!sysCPUSample(&sampler, &pct)) {
#if defined(_PLATFORM_WASM)
        return ret;   // nothing to sample here; the first call already proved that
#else
        TEST_FAIL(1, _SL("second sysCPUSample reported nothing, cxerr ${int}"),
                  stvar(int32, cxerr));
#endif
    }

    if (pct <= 0.0 || pct > 100.0)
        TEST_FAILV(ret, 1, _SL("sysCPUSample returned ${float}, wanted above 0 and up to 100"),
                   stvar(float64, pct));

    return ret;
}

static int test_sysinfo_uptime(void)
{
    SysUptime up;
    int ret = 0;

    if (!sysUptime(&up)) {
#if defined(_PLATFORM_WASM)
        return checkNotSupported(false, _SL("sysUptime"));
#else
        TEST_FAIL(1, _SL("sysUptime failed, cxerr ${int}"), stvar(int32, cxerr));
#endif
    }

    // Both are filled in everywhere: whichever one the platform knows natively, the other is
    // worked out from it.
    if ((up.valid & (SYSUP_Uptime | SYSUP_BootTime)) != (SYSUP_Uptime | SYSUP_BootTime))
        TEST_FAIL(1, _SL("sysUptime reported valid ${uint}, wanted both fields"),
                  stvar(uint32, up.valid));

    if (up.uptime <= 0)
        TEST_FAILV(ret, 1, _SL("uptime is ${int}, wanted a positive duration"),
                   stvar(int64, up.uptime));

    int64 now = clockWall();
    if (up.boottime >= now)
        TEST_FAILV(ret, 1, _SL("boottime ${int} is not in the past (now ${int})"),
                   stvar(int64, up.boottime), stvar(int64, now));

    // The two describe the same thing, so they have to agree. A minute of slack absorbs the
    // clock drift between a boot time the kernel recorded and one derived from the current time.
    int64 diff = (now - up.boottime) - up.uptime;
    if (diff < 0)
        diff = -diff;

    if (diff > timeS(60))
        TEST_FAILV(ret, 1, _SL("uptime and boottime disagree by ${int} microseconds"),
                   stvar(int64, diff));

    return ret;
}

static int test_sysinfo_loadavg(void)
{
    SysLoadAvg load;

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_WASM)
    return checkNotSupported(sysLoadAvg(&load), _SL("sysLoadAvg"));
#else
    int ret = 0;

    if (!sysLoadAvg(&load))
        TEST_FAIL(1, _SL("sysLoadAvg failed, cxerr ${int}"), stvar(int32, cxerr));

    if (!(load.valid & SYSLOAD_Load1))
        TEST_FAIL(1, _SL("sysLoadAvg reported valid ${uint}, wanted at least the one minute "
                         "average"),
                  stvar(uint32, load.valid));

    if (load.load1 < 0.0)
        TEST_FAILV(ret, 1, _SL("load1 is ${float}, which cannot be negative"),
                   stvar(float64, load.load1));

    if ((load.valid & SYSLOAD_Load5) && load.load5 < 0.0)
        TEST_FAILV(ret, 1, _SL("load5 is ${float}"), stvar(float64, load.load5));

    if ((load.valid & SYSLOAD_Load15) && load.load15 < 0.0)
        TEST_FAILV(ret, 1, _SL("load15 is ${float}"), stvar(float64, load.load15));

    return ret;
#endif
}

// ---- child processes ---------------------------------------------------------------------------
//
// Run as `test_runner sysinfotest child_spin`, launched by the tests below. Never given an
// add_test line or an alltests entry.

// Burns processor time for long enough that the parent can watch it do so.
static int test_sysinfo_child_spin(void)
{
    // Long enough to outlive the parent's sampling loop by a wide margin; the parent kills it
    // as soon as it has measured what it needs.
    spin(timeS(30));
    return 0;
}

static int test_sysinfo_child_exit0(void)
{
    return 0;
}

// Runs this same test_runner as a child, on the named child_* subtest.
static Process* launchSelf(strref subtest)
{
    string exe = 0;
    sa_string argv;

    fsExe(&exe);
    saInit(&argv, string, 2);
    saPush(&argv, strref, _SL("sysinfotest"));
    saPush(&argv, strref, subtest);

    Process* proc = procLaunch(exe, argv, NULL);

    saDestroy(&argv);
    strDestroy(&exe);
    return proc;
}

// ---- per process -------------------------------------------------------------------------------

static int test_sysinfo_procmem_self(void)
{
    ProcMemInfo mem, byid;
    int ret = 0;

    Process* self = procOpen(procCurrentID());
    if (!self)
        TEST_FAIL(1, _SL("procOpen of our own pid ${int} failed, cxerr ${int}"),
                  stvar(int64, procCurrentID()), stvar(int32, cxerr));

    if (!procMemInfo(&mem, self)) {
        TEST_FAILV(ret, 1, _SL("procMemInfo failed, cxerr ${int}"), stvar(int32, cxerr));
    } else if (!(mem.valid & PROCMEM_Resident)) {
        TEST_FAILV(ret, 1, _SL("procMemInfo did not report resident memory, valid ${uint}"),
                   stvar(uint32, mem.valid));
    } else if (mem.resident == 0) {
        TEST_FAILV(ret, 1, _SL("resident is zero for a running process, valid ${uint}"),
                   stvar(uint32, mem.valid));
    } else if ((mem.valid & PROCMEM_PeakResident) && mem.peakResident < mem.resident) {
        TEST_FAILV(ret, 1, _SL("peakResident ${uint} is below resident ${uint}"),
                   stvar(uint64, mem.peakResident), stvar(uint64, mem.resident));
    } else if ((mem.valid & PROCMEM_Virtual) && mem.virtualSize < mem.resident) {
        TEST_FAILV(ret, 1, _SL("virtualSize ${uint} is below resident ${uint}"),
                   stvar(uint64, mem.virtualSize), stvar(uint64, mem.resident));
    }

    // Reaching the same process through its id has to produce the same answer. The two readings
    // are taken moments apart, so they are compared loosely -- a factor of four apart means one
    // of them is measuring something else entirely, not that memory moved.
    if (!procMemInfoByID(&byid, procCurrentID())) {
        TEST_FAILV(ret, 1, _SL("procMemInfoByID failed, cxerr ${int}"), stvar(int32, cxerr));
    } else if (byid.valid != mem.valid) {
        TEST_FAILV(ret, 1, _SL("procMemInfoByID reported valid ${uint}, handle reported ${uint}"),
                   stvar(uint32, byid.valid), stvar(uint32, mem.valid));
    } else if (byid.resident > mem.resident * 4 || mem.resident > byid.resident * 4) {
        TEST_FAILV(ret, 1, _SL("resident by handle ${uint} and by id ${uint} are far apart"),
                   stvar(uint64, mem.resident), stvar(uint64, byid.resident));
    }

    procRelease(&self);
    return ret;
}

static int test_sysinfo_procmem_badid(void)
{
    ProcMemInfo mem;

    if (procMemInfoByID(&mem, PROCESS_InvalidID))
        TEST_FAIL(1,
                  _SL("procMemInfoByID succeeded for an invalid process id, reporting valid "
                      "${uint}"),
                  stvar(uint32, mem.valid));

    if (cxerr != CX_InvalidArgument)
        TEST_FAIL(1, _SL("procMemInfoByID set cxerr ${int}, wanted CX_InvalidArgument (${int})"),
                  stvar(int32, cxerr), stvar(int32, (int32)CX_InvalidArgument));

    return 0;
}

static int test_sysinfo_proccpu_self(void)
{
    ProcCPUTimes before, after;
    int ret = 0;

    Process* self = procOpen(procCurrentID());
    if (!self)
        TEST_FAIL(1, _SL("procOpen of our own pid failed, cxerr ${int}"), stvar(int32, cxerr));

    if (!procCPUTimes(&before, self)) {
        procRelease(&self);
        TEST_FAIL(1, _SL("procCPUTimes failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    if (!(before.valid & PROCCPU_User))
        TEST_FAILV(ret, 1, _SL("procCPUTimes did not report user time, valid ${uint}"),
                   stvar(uint32, before.valid));

    if (before.total != before.user + before.system)
        TEST_FAILV(ret, 1, _SL("total ${int} != user ${int} + system ${int}"),
                   stvar(int64, before.total), stvar(int64, before.user),
                   stvar(int64, before.system));

    if ((before.valid & PROCCPU_Started) && before.started > clockWall())
        TEST_FAILV(ret, 1, _SL("started ${int} is in the future (now ${int})"),
                   stvar(int64, before.started), stvar(int64, clockWall()));

    spin(kSpinTime);

    if (!procCPUTimes(&after, self)) {
        procRelease(&self);
        TEST_FAIL(1, _SL("second procCPUTimes failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    if (after.total <= before.total)
        TEST_FAILV(ret, 1, _SL("processor time did not advance across a spin: ${int} then ${int}"),
                   stvar(int64, before.total), stvar(int64, after.total));

    float64 pct = procCPUUsage(&before, &after);
    if (pct <= 0.0 || pct > 100.0)
        TEST_FAILV(ret, 1, _SL("procCPUUsage returned ${float}, wanted above 0 and up to 100"),
                   stvar(float64, pct));

    procRelease(&self);
    return ret;
}

static int test_sysinfo_proccpu_child(void)
{
    ProcCPUSampler sampler;
    float64 pct = 0.0;
    int ret     = 0;

    Process* proc = launchSelf(_SL("child_spin"));
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    procCPUSamplerInit(&sampler);

    if (procCPUSample(&sampler, proc, &pct))
        TEST_FAILV(ret, 1, _SL("procCPUSample reported ${float} on its first call"),
                   stvar(float64, pct));

    // A process that has only just been launched may not have been scheduled yet, and how long
    // that takes varies enormously with how loaded the machine is. Keep sampling until it has
    // used some processor time rather than assuming one interval is always enough. Sleeping
    // rather than spinning here keeps this process out of the measurement.
    int64 deadline = clockTimer() + timeS(10);
    bool measured  = false;

    while (!measured && clockTimer() < deadline) {
        osSleep(timeMS(250));

        if (!procCPUSample(&sampler, proc, &pct)) {
            TEST_FAILV(ret, 1,
                       _SL("procCPUSample reported nothing for child ${int}, cxerr ${int}"),
                       stvar(int64, procID(proc)), stvar(int32, cxerr));
            break;
        }

        measured = pct > 0.0;
    }

    if (ret == 0 && !measured && procStatsEmulated()) {
        TEST_WARN(_SL("child ${int} measured no processor time; this platform does not report "
                      "it for other processes"),
                  stvar(int64, procID(proc)));
    } else if (ret == 0 && !measured) {
        TEST_FAILV(ret, 1,
                   _SL("spinning child ${int} never measured above 0 percent in 10 seconds"),
                   stvar(int64, procID(proc)));
    } else if (pct > 100.0) {
        TEST_FAILV(ret, 1, _SL("a single spinning child measured ${float} percent"),
                   stvar(float64, pct));
    }

    procTerminate(proc, true);
    procWait(proc, timeS(30));
    procRelease(&proc);
    return ret;
}

static int test_sysinfo_procio_self(void)
{
    ProcIOInfo before, after;
    int ret = 0;

    if (!procIOInfoByID(&before, procCurrentID())) {
        // Linux can be built without per-process I/O accounting, and a wasm sandbox has none at
        // all. Neither is a defect in this code.
        TEST_WARN(_SL("procIOInfo is unavailable here, cxerr ${int}"), stvar(int32, cxerr));
        return 0;
    }

    STR_CONST(kIOFile, "cx_sysinfotest_io.txt");
    string blob = 0;
    for (int i = 0; i < 512; i++) strAppend(&blob, _SL("the quick brown fox jumps over it. "));

    uint32 want = strLen(blob);
    FSFile* f   = fsOpen(kIOFile, FS_Overwrite);
    if (!f) {
        strDestroy(&blob);
        TEST_FAIL(1, _SL("could not open '${string}' to generate I/O with, cxerr ${int}"),
                  stvar(strref, kIOFile), stvar(int32, cxerr));
    }

    size_t wrote = 0;
    bool wrok    = fileWriteString(f, blob, &wrote);
    fileClose(&f);
    fsDelete(kIOFile);
    strDestroy(&blob);

    if (!wrok)
        TEST_FAIL(1, _SL("writing the test file failed after ${uint} of ${uint} bytes"),
                  stvar(uint64, (uint64)wrote), stvar(uint32, want));

    if (!procIOInfoByID(&after, procCurrentID()))
        TEST_FAIL(1, _SL("second procIOInfo failed, cxerr ${int}"), stvar(int32, cxerr));

    if (after.valid != before.valid)
        TEST_FAILV(ret, 1, _SL("valid changed between readings: ${uint} then ${uint}"),
                   stvar(uint32, before.valid), stvar(uint32, after.valid));

    // Only the byte counters are guaranteed to move for a write this small. FreeBSD counts
    // block-device operations, which a write this size may never reach a disk to perform.
    if ((after.valid & PROCIO_Bytes) && after.writeBytes <= before.writeBytes &&
        !procStatsEmulated())
        TEST_FAILV(ret, 1, _SL("writeBytes did not advance after writing ${uint} bytes: "
                               "${uint} then ${uint}"),
                   stvar(uint64, (uint64)wrote), stvar(uint64, before.writeBytes),
                   stvar(uint64, after.writeBytes));

    return ret;
}

static int test_sysinfo_procio_foreign(void)
{
    sa_ProcessInfo procs;
    ProcessID self = procCurrentID();
    int ret        = 0;

    if (!procEnum(&procs, 0))
        TEST_FAIL(1, _SL("procEnum failed, cxerr ${int}"), stvar(int32, cxerr));

    // Reading another user's counters is allowed to be refused; what it must never do is crash,
    // hang, or come back claiming success with nothing filled in.
    int checked = 0;
    for (int32 i = 0; i < saSize(procs) && checked < 8; i++) {
        ProcessID pid = procs.a[i].pid;
        if (pid == self || pid <= 0)
            continue;

        checked++;

        ProcIOInfo io;
        cxerr = CX_Success;

        if (procIOInfoByID(&io, pid)) {
            if (io.valid == 0)
                TEST_FAILV(ret, 1, _SL("procIOInfoByID(${int}) succeeded reporting nothing"),
                           stvar(int64, pid));
        } else if (cxerr == CX_Success) {
            TEST_FAILV(ret, 1, _SL("procIOInfoByID(${int}) failed without setting cxerr"),
                       stvar(int64, pid));
        }
    }

    saDestroy(&procs);
    return ret;
}

static int test_sysinfo_proc_exited(void)
{
    ProcMemInfo mem;
    ProcCPUTimes cpu;
    ProcIOInfo io;
    int ret = 0;

    Process* proc = launchSelf(_SL("child_exit0"));
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    ProcessID pid = procID(proc);

    if (!procWait(proc, timeS(30))) {
        procRelease(&proc);
        TEST_FAIL(1, _SL("child ${int} did not finish within 30s"), stvar(int64, pid));
    }

    // The process id may already belong to something else. Every one of these has to refuse
    // rather than report a stranger's numbers.
    if (procMemInfo(&mem, proc))
        TEST_FAILV(ret, 1,
                   _SL("procMemInfo answered for finished process ${int}, valid ${uint}"),
                   stvar(int64, pid), stvar(uint32, mem.valid));
    else if (cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("procMemInfo set cxerr ${int}, wanted CX_FileNotFound (${int})"),
                   stvar(int32, cxerr), stvar(int32, (int32)CX_FileNotFound));

    if (procCPUTimes(&cpu, proc))
        TEST_FAILV(ret, 1,
                   _SL("procCPUTimes answered for finished process ${int}, total ${int}"),
                   stvar(int64, pid), stvar(int64, cpu.total));

    if (procIOInfo(&io, proc))
        TEST_FAILV(ret, 1,
                   _SL("procIOInfo answered for finished process ${int}, valid ${uint}"),
                   stvar(int64, pid), stvar(uint32, io.valid));

    procRelease(&proc);
    return ret;
}

// Pins down which counters each platform is supposed to produce, so a backend quietly losing one
// shows up as a failure rather than as a zero nobody notices.
static int test_sysinfo_validmask(void)
{
    int ret = 0;

#if defined(_PLATFORM_WASM)
    return ret;   // nothing here reports enough to pin down
#else
    SysMemInfo mem;
    SysCPUTimes cpu;
    ProcMemInfo pmem;
    ProcCPUTimes pcpu;

    flags_t wantMem = SYSMEM_Phys | SYSMEM_PageSize;
    flags_t wantCPU = SYSCPU_User | SYSCPU_System | SYSCPU_Idle;
    flags_t wantPMem = PROCMEM_Resident;
    flags_t wantPCPU = PROCCPU_User | PROCCPU_System | PROCCPU_Started;

#if defined(_PLATFORM_LINUX)
    wantMem |= SYSMEM_Commit | SYSMEM_Swap;
    wantCPU |= SYSCPU_IOWait | SYSCPU_IRQ | SYSCPU_Steal;
    wantPMem |= PROCMEM_PeakResident | PROCMEM_Virtual | PROCMEM_PeakVirtual;
#elif defined(_PLATFORM_FBSD)
    wantCPU |= SYSCPU_IRQ;
    wantPMem |= PROCMEM_Virtual;
#elif defined(_PLATFORM_WIN)
    wantMem |= SYSMEM_Commit;
    wantPMem |= PROCMEM_PeakResident | PROCMEM_Private;
#endif

    if (!sysMemInfo(&mem) || (mem.valid & wantMem) != wantMem)
        TEST_FAILV(ret, 1, _SL("sysMemInfo reported valid ${uint}, wanted at least ${uint}"),
                   stvar(uint32, mem.valid), stvar(uint32, wantMem));

    if (!sysCPUTimes(&cpu) || (cpu.valid & wantCPU) != wantCPU)
        TEST_FAILV(ret, 1, _SL("sysCPUTimes reported valid ${uint}, wanted at least ${uint}"),
                   stvar(uint32, cpu.valid), stvar(uint32, wantCPU));

    if (!procMemInfoByID(&pmem, procCurrentID()) || (pmem.valid & wantPMem) != wantPMem)
        TEST_FAILV(ret, 1, _SL("procMemInfo reported valid ${uint}, wanted at least ${uint}"),
                   stvar(uint32, pmem.valid), stvar(uint32, wantPMem));

    if (!procCPUTimesByID(&pcpu, procCurrentID()) || (pcpu.valid & wantPCPU) != wantPCPU)
        TEST_FAILV(ret, 1, _SL("procCPUTimes reported valid ${uint}, wanted at least ${uint}"),
                   stvar(uint32, pcpu.valid), stvar(uint32, wantPCPU));

    return ret;
#endif
}

// ---- groups ------------------------------------------------------------------------------------
//
// One process launch per group rather than per subtest. Every subtest below is still registered
// on its own, so `test_runner sysinfotest <name>` can run one in isolation.

static int test_sysinfo_grp_sys(void)
{
    TEST_CHAIN(test_sysinfo_mem, test_sysinfo_cpu, test_sysinfo_cpusampler, test_sysinfo_uptime,
               test_sysinfo_loadavg);
}

static int test_sysinfo_grp_proc(void)
{
    TEST_CHAIN(test_sysinfo_procmem_self, test_sysinfo_procmem_badid, test_sysinfo_proccpu_self,
               test_sysinfo_proccpu_child, test_sysinfo_procio_self, test_sysinfo_procio_foreign,
               test_sysinfo_proc_exited, test_sysinfo_validmask);
}

testfunc sysinfotest_funcs[] = {
    { "mem",            test_sysinfo_mem            },
    { "cpu",            test_sysinfo_cpu            },
    { "cpusampler",     test_sysinfo_cpusampler     },
    { "uptime",         test_sysinfo_uptime         },
    { "loadavg",        test_sysinfo_loadavg        },
    { "procmem_self",   test_sysinfo_procmem_self   },
    { "procmem_badid",  test_sysinfo_procmem_badid  },
    { "proccpu_self",   test_sysinfo_proccpu_self   },
    { "proccpu_child",  test_sysinfo_proccpu_child  },
    { "procio_self",    test_sysinfo_procio_self    },
    { "procio_foreign", test_sysinfo_procio_foreign },
    { "proc_exited",    test_sysinfo_proc_exited    },
    { "validmask",      test_sysinfo_validmask      },
    { "child_spin",     test_sysinfo_child_spin     },
    { "child_exit0",    test_sysinfo_child_exit0    },
    { "grp_sys",        test_sysinfo_grp_sys        },
    { "grp_proc",       test_sysinfo_grp_proc       },
    { 0,                0                           }
};

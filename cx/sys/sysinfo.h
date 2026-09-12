#pragma once

/// @file sysinfo.h
/// @brief Processor, memory and I/O statistics for the machine and for individual processes

/// @defgroup sys_sysinfo System Statistics
/// @ingroup sys
/// @{

#include <cx/cx.h>
#include <cx/sys/process.h>

CX_C_BEGIN

/// @defgroup sys_sysinfo_overview Overview
/// @ingroup sys_sysinfo
/// @{
///
/// Two sets of readings live here: how busy the whole machine is, and how busy one process is.
/// Both are plain structures filled in by a single call, so nothing needs to be initialized or
/// destroyed -- declare one, pass its address, read the fields.
///
/// @code
///   SysMemInfo mem;
///   if (sysMemInfo(&mem)) {
///       // mem.physTotal bytes of memory are installed
///   }
/// @endcode
///
/// @section sys_sysinfo_valid What the system will and will not tell you
///
/// Operating systems do not all count the same things. FreeBSD keeps no record of committed
/// memory, Windows does not report how much of a process has been paged out, and only Linux
/// separates time spent waiting for a disk from time spent idle.
///
/// Every structure therefore carries a `valid` field: a set of flags naming the fields the
/// operating system actually reported. A field it did not report is also left at zero, so code
/// that only wants a rough number can ignore `valid` entirely and print the zero. Code that
/// needs to tell "nothing is being used" apart from "nobody is counting" checks the flag.
///
/// @code
///   if (mem.valid & SYSMEM_Commit) {
///       // mem.commitTotal and mem.commitAvail are real numbers
///   }
/// @endcode
///
/// Each field's own documentation says where it is unavailable.
///
/// A call that could report nothing at all on this platform -- sysLoadAvg() on Windows is the
/// only one -- returns false and sets cxerr to CX_NotSupported instead of returning an empty
/// structure. A call that can report something always returns true, with `valid` saying how
/// much.
///
/// @section sys_sysinfo_rates Processor time is a running total
///
/// sysCPUTimes() and procCPUTimes() report how much processor time has been used since the
/// machine, or the process, started. A single reading cannot tell you how busy anything is
/// right now; two readings and the time between them can.
///
/// sysCPUUsage() and procCPUUsage() do that arithmetic. Both return a percentage of the whole
/// machine's capacity, from 0 to 100, so the two are directly comparable: a process reading of
/// 25 means the process is using a quarter of everything the machine can do. Multiply by
/// osLogicalCPUs() for the per-core figure that tools like `top` print, where a process using
/// four cores fully reads 400.
///
/// @code
///   SysCPUTimes before, after;
///   sysCPUTimes(&before);
///   osSleep(timeS(1));
///   sysCPUTimes(&after);
///
///   float64 busy = sysCPUUsage(&before, &after);
/// @endcode
///
/// For code that polls in a loop, a sampler keeps the previous reading so only one call is
/// needed each time round. It has no answer to give until it has been called twice, and says so
/// by returning false.
///
/// @code
///   SysCPUSampler sampler;
///   sysCPUSamplerInit(&sampler);
///
///   for (;;) {
///       float64 busy;
///       if (sysCPUSample(&sampler, &busy)) {
///           // the machine was 'busy' percent loaded since the last time round
///       }
///       osSleep(timeS(1));
///   }
/// @endcode
///
/// Samplers hold nothing but a previous reading, so they need no cleanup, and a process
/// sampler does not hold the process either -- it is passed in on each call.
///
/// @section sys_sysinfo_priv Reading other processes
///
/// The per-process calls come in two forms: one taking a Process handle, and one taking a
/// process id directly. Both ask the operating system for the least access that will answer the
/// question, and neither ever asks for more rights than it was started with. A reading that
/// needs privileges the caller does not have comes back as a cleared flag, or as false with
/// cxerr set to CX_AccessDenied, never as an attempt to acquire those privileges.
///
/// In practice processor and memory figures can be read for any process on the machine, while
/// I/O counters on Linux can only be read for processes belonging to the same user.
///
/// @section sys_sysinfo_snapshot Every reading is a snapshot
///
/// The numbers describe the moment the call was made. Memory in particular moves constantly, so
/// two fields read by two separate calls will not agree exactly, and a process can exit between
/// one call and the next.
///
/// A handle whose process has already finished reports no statistics: the calls return false
/// with cxerr set to CX_FileNotFound rather than reporting whatever now holds that process id.
///
/// @}

/// Which fields of a SysMemInfo the operating system reported
enum SysMemInfoValid {
    SYSMEM_Phys     = 0x0001,   ///< physTotal and physAvail
    SYSMEM_Commit   = 0x0002,   ///< commitTotal and commitAvail
    SYSMEM_Swap     = 0x0004,   ///< swapTotal and swapAvail
    SYSMEM_PageSize = 0x0008,   ///< pageSize
};

/// How much memory the machine has and how much of it is spoken for
typedef struct SysMemInfo {
    /// @brief SysMemInfoValid flags naming the fields that were reported
    flags_t valid;

    uint64 physTotal;   ///< Bytes of memory installed in the machine
    uint64 physAvail;   ///< Bytes that can be used without paging something out first

    /// @brief Bytes of memory the system is willing to promise to programs in total
    ///
    /// Physical memory plus swap on most systems. Not reported on FreeBSD, which makes no such
    /// promise and hands out memory until it runs out.
    uint64 commitTotal;

    uint64 commitAvail;   ///< Bytes of that promise not yet handed out

    /// @brief Bytes of swap space configured
    ///
    /// Not reported on Windows, where swap cannot be separated from the total above.
    uint64 swapTotal;

    uint64 swapAvail;   ///< Bytes of swap space still free
    uint64 pageSize;    ///< Size of a memory page in bytes
} SysMemInfo;

/// Which breakdown fields of a SysCPUTimes the operating system reported
enum SysCPUTimesValid {
    SYSCPU_User   = 0x0001,   ///< user
    SYSCPU_System = 0x0002,   ///< system
    SYSCPU_Idle   = 0x0004,   ///< idle
    SYSCPU_IOWait = 0x0008,   ///< iowait
    SYSCPU_IRQ    = 0x0010,   ///< irq
    SYSCPU_Steal  = 0x0020,   ///< steal
};

/// Processor time the machine has used since it started
///
/// Every time is in microseconds, added up across all processors, so a machine with eight
/// processors accumulates eight seconds of time per second.
typedef struct SysCPUTimes {
    /// @brief SysCPUTimesValid flags naming the breakdown fields that were reported
    ///
    /// busy and total are always filled in; only the breakdown below varies by platform.
    flags_t valid;

    int64 sampled;   ///< When the reading was taken, from clockTimer()
    int64 busy;      ///< Time accounted for as anything other than idle
    int64 total;     ///< Time accounted for altogether: busy plus idle

    int64 user;      ///< Time spent running program code, including at low priority
    int64 system;    ///< Time spent inside the operating system
    int64 idle;      ///< Time spent with nothing to do
    int64 iowait;    ///< Time spent waiting for a disk. Linux only
    int64 irq;       ///< Time spent servicing hardware. Not reported on Windows
    int64 steal;     ///< Time a hypervisor gave to someone else. Linux only

    int32 cpus;      ///< How many logical processors these figures cover
} SysCPUTimes;

/// Which fields of a SysUptime the operating system reported
enum SysUptimeValid {
    SYSUP_Uptime   = 0x0001,   ///< uptime
    SYSUP_BootTime = 0x0002,   ///< boottime
};

/// How long the machine has been running
typedef struct SysUptime {
    flags_t valid;    ///< SysUptimeValid flags naming the fields that were reported
    int64 uptime;     ///< Microseconds since the machine booted
    int64 boottime;   ///< When the machine booted, as a cx time value
} SysUptime;

/// Which fields of a SysLoadAvg the operating system reported
enum SysLoadAvgValid {
    SYSLOAD_Load1  = 0x0001,   ///< load1
    SYSLOAD_Load5  = 0x0002,   ///< load5
    SYSLOAD_Load15 = 0x0004,   ///< load15
};

/// How many processes have been waiting to run, averaged over time
///
/// A figure of 1.0 means one process was runnable on average. Compare it against the number of
/// processors: on a four-processor machine a load of 4.0 is fully busy, and more than that
/// means work is queueing up.
typedef struct SysLoadAvg {
    flags_t valid;     ///< SysLoadAvgValid flags naming the fields that were reported
    float64 load1;     ///< Average over the last minute
    float64 load5;     ///< Average over the last five minutes
    float64 load15;    ///< Average over the last fifteen minutes
} SysLoadAvg;

/// Which fields of a ProcMemInfo the operating system reported
enum ProcMemInfoValid {
    PROCMEM_Resident     = 0x0001,   ///< resident
    PROCMEM_PeakResident = 0x0002,   ///< peakResident
    PROCMEM_Virtual      = 0x0004,   ///< virtualSize
    PROCMEM_PeakVirtual  = 0x0008,   ///< peakVirtualSize
    PROCMEM_Private      = 0x0010,   ///< privateSize
    PROCMEM_Swapped      = 0x0020,   ///< swapped
};

/// How much memory one process is using
typedef struct ProcMemInfo {
    flags_t valid;   ///< ProcMemInfoValid flags naming the fields that were reported

    uint64 resident;   ///< Bytes of the process currently held in memory

    /// @brief The largest that has ever been
    ///
    /// FreeBSD samples this periodically rather than tracking it exactly, so there it is a lower
    /// bound rather than an exact figure.
    uint64 peakResident;

    /// @brief Bytes of address space the process has reserved
    ///
    /// Usually much larger than what is in use, and not reported on Windows.
    uint64 virtualSize;

    uint64 peakVirtualSize;   ///< The largest that has ever been. Linux only

    /// @brief Bytes of memory the process has asked for and nobody else can share
    ///
    /// Windows only.
    uint64 privateSize;

    uint64 swapped;           ///< Bytes of the process paged out to swap. Linux only
} ProcMemInfo;

/// Which fields of a ProcCPUTimes the operating system reported
enum ProcCPUTimesValid {
    PROCCPU_User    = 0x0001,   ///< user
    PROCCPU_System  = 0x0002,   ///< system
    PROCCPU_Started = 0x0004,   ///< started
};

/// Processor time one process has used since it started
typedef struct ProcCPUTimes {
    /// @brief ProcCPUTimesValid flags naming the fields that were reported
    ///
    /// total is always filled in.
    flags_t valid;

    int64 sampled;   ///< When the reading was taken, from clockTimer()
    int64 total;     ///< Microseconds of processor time used altogether: user plus system
    int64 user;      ///< Microseconds spent running the program's own code
    int64 system;    ///< Microseconds spent inside the operating system on its behalf
    int64 started;   ///< When the process started, as a cx time value
} ProcCPUTimes;

/// Which fields of a ProcIOInfo the operating system reported
enum ProcIOInfoValid {
    PROCIO_Bytes = 0x0001,   ///< readBytes and writeBytes
    PROCIO_Ops   = 0x0002,   ///< readOps and writeOps
    PROCIO_Other = 0x0004,   ///< otherBytes and otherOps
};

/// How much reading and writing one process has done
///
/// The counts cover every kind of input and output, not just files -- a network socket and a
/// pipe are counted the same way a disk is.
typedef struct ProcIOInfo {
    flags_t valid;   ///< ProcIOInfoValid flags naming the fields that were reported

    uint64 readBytes;    ///< Bytes read. Not reported on FreeBSD
    uint64 writeBytes;   ///< Bytes written. Not reported on FreeBSD
    uint64 readOps;      ///< How many separate reads were made
    uint64 writeOps;     ///< How many separate writes were made
    uint64 otherBytes;   ///< Bytes transferred by anything that is neither. Windows only
    uint64 otherOps;     ///< How many such operations were made. Windows only
} ProcIOInfo;

/// Reads how much memory the machine has and how much is free.
///
/// @param out Receives the reading
/// @return true if anything could be read
///
/// Example:
/// @code
///   SysMemInfo mem;
///   if (sysMemInfo(&mem)) {
///       // mem.physAvail bytes are free of mem.physTotal
///   }
/// @endcode
bool sysMemInfo(_Out_ SysMemInfo* out);

/// Reads how much processor time the machine has used since it started.
///
/// Take two readings and pass them to sysCPUUsage() to find out how busy the machine is.
///
/// @param out Receives the reading
/// @return true if anything could be read
///
/// Example:
/// @code
///   SysCPUTimes cpu;
///   if (sysCPUTimes(&cpu)) {
///       // cpu.busy of cpu.total microseconds have been spent working
///   }
/// @endcode
bool sysCPUTimes(_Out_ SysCPUTimes* out);

/// Reads how long the machine has been running.
///
/// @param out Receives the reading
/// @return true if anything could be read
///
/// Example:
/// @code
///   SysUptime up;
///   if (sysUptime(&up)) {
///       // the machine has been up for timeToSeconds(up.uptime) seconds
///   }
/// @endcode
bool sysUptime(_Out_ SysUptime* out);

/// Reads how many processes have been waiting to run.
///
/// @param out Receives the reading
/// @return true if anything could be read; false with cxerr set to CX_NotSupported on Windows,
///         which keeps no such figure
///
/// Example:
/// @code
///   SysLoadAvg load;
///   if (sysLoadAvg(&load)) {
///       // load.load1 processes were runnable on average over the last minute
///   }
/// @endcode
bool sysLoadAvg(_Out_ SysLoadAvg* out);

/// Works out how busy the machine was between two readings.
///
/// @param first The earlier reading
/// @param second The later reading
/// @return Percentage of the machine's capacity used, from 0 to 100; 0 if the two readings are
///         the same
///
/// Example:
/// @code
///   float64 busy = sysCPUUsage(&before, &after);
/// @endcode
float64 sysCPUUsage(_In_ const SysCPUTimes* first, _In_ const SysCPUTimes* second);

/// Remembers the previous reading so repeated polling needs only one call each time
typedef struct SysCPUSampler {
    SysCPUTimes last;   ///< The previous reading
    bool primed;        ///< Whether a previous reading has been taken yet
} SysCPUSampler;

/// void sysCPUSamplerInit(SysCPUSampler *s);
///
/// Prepares a sampler for use.
///
/// @param s Sampler to prepare
///
/// Example:
/// @code
///   SysCPUSampler sampler;
///   sysCPUSamplerInit(&sampler);
/// @endcode
#define sysCPUSamplerInit(s) memset((s), 0, sizeof(SysCPUSampler))

/// Takes a reading and reports how busy the machine has been since the last one.
///
/// @param s Sampler holding the previous reading
/// @param pct Receives the percentage of the machine's capacity used, from 0 to 100
/// @return true if a percentage could be worked out; false on the first call, which only
///         establishes the starting point, and if the reading could not be taken
///
/// Example:
/// @code
///   float64 busy;
///   if (sysCPUSample(&sampler, &busy)) {
///       // the machine was 'busy' percent loaded since the last call
///   }
/// @endcode
bool sysCPUSample(_Inout_ SysCPUSampler* s, _Out_ float64* pct);

/// Reads how much memory a process is using.
///
/// @param out Receives the reading
/// @param proc Process to look at
/// @return true if anything could be read
///
/// Example:
/// @code
///   ProcMemInfo mem;
///   if (procMemInfo(&mem, proc)) {
///       // the process is holding mem.resident bytes in memory
///   }
/// @endcode
bool procMemInfo(_Out_ ProcMemInfo* out, _In_ Process* proc);

/// Reads how much memory a process is using, by process id.
///
/// @param out Receives the reading
/// @param pid Process to look at
/// @return true if anything could be read
///
/// Example:
/// @code
///   ProcMemInfo mem;
///   procMemInfoByID(&mem, procCurrentID());
/// @endcode
bool procMemInfoByID(_Out_ ProcMemInfo* out, ProcessID pid);

/// Reads how much processor time a process has used.
///
/// Take two readings and pass them to procCPUUsage() to find out how busy the process is.
///
/// @param out Receives the reading
/// @param proc Process to look at
/// @return true if anything could be read
///
/// Example:
/// @code
///   ProcCPUTimes cpu;
///   if (procCPUTimes(&cpu, proc)) {
///       // the process has used cpu.total microseconds of processor time
///   }
/// @endcode
bool procCPUTimes(_Out_ ProcCPUTimes* out, _In_ Process* proc);

/// Reads how much processor time a process has used, by process id.
///
/// @param out Receives the reading
/// @param pid Process to look at
/// @return true if anything could be read
///
/// Example:
/// @code
///   ProcCPUTimes cpu;
///   procCPUTimesByID(&cpu, procCurrentID());
/// @endcode
bool procCPUTimesByID(_Out_ ProcCPUTimes* out, ProcessID pid);

/// Reads how much reading and writing a process has done.
///
/// @param out Receives the reading
/// @param proc Process to look at
/// @return true if anything could be read; false with cxerr set to CX_AccessDenied where the
///         counters belong to another user
///
/// Example:
/// @code
///   ProcIOInfo io;
///   if (procIOInfo(&io, proc)) {
///       // the process has written io.writeBytes bytes
///   }
/// @endcode
bool procIOInfo(_Out_ ProcIOInfo* out, _In_ Process* proc);

/// Reads how much reading and writing a process has done, by process id.
///
/// @param out Receives the reading
/// @param pid Process to look at
/// @return true if anything could be read; false with cxerr set to CX_AccessDenied where the
///         counters belong to another user
///
/// Example:
/// @code
///   ProcIOInfo io;
///   procIOInfoByID(&io, procCurrentID());
/// @endcode
bool procIOInfoByID(_Out_ ProcIOInfo* out, ProcessID pid);

/// Works out how busy a process was between two readings.
///
/// @param first The earlier reading
/// @param second The later reading
/// @return Percentage of the machine's capacity used, from 0 to 100; 0 if the two readings are
///         the same
///
/// Example:
/// @code
///   float64 busy = procCPUUsage(&before, &after);
/// @endcode
float64 procCPUUsage(_In_ const ProcCPUTimes* first, _In_ const ProcCPUTimes* second);

/// Remembers the previous reading so repeated polling needs only one call each time
///
/// Does not hold the process itself; pass it to procCPUSample() on every call.
typedef struct ProcCPUSampler {
    ProcCPUTimes last;   ///< The previous reading
    bool primed;         ///< Whether a previous reading has been taken yet
} ProcCPUSampler;

/// void procCPUSamplerInit(ProcCPUSampler *s);
///
/// Prepares a sampler for use.
///
/// @param s Sampler to prepare
///
/// Example:
/// @code
///   ProcCPUSampler sampler;
///   procCPUSamplerInit(&sampler);
/// @endcode
#define procCPUSamplerInit(s) memset((s), 0, sizeof(ProcCPUSampler))

/// Takes a reading and reports how busy a process has been since the last one.
///
/// @param s Sampler holding the previous reading
/// @param proc Process to look at
/// @param pct Receives the percentage of the machine's capacity used, from 0 to 100
/// @return true if a percentage could be worked out; false on the first call, which only
///         establishes the starting point, and if the reading could not be taken
///
/// Example:
/// @code
///   float64 busy;
///   if (procCPUSample(&sampler, proc, &busy)) {
///       // the process was 'busy' percent of the machine since the last call
///   }
/// @endcode
bool procCPUSample(_Inout_ ProcCPUSampler* s, _In_ Process* proc, _Out_ float64* pct);

CX_C_END

/// @}

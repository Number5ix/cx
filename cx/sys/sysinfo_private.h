#pragma once

#include <cx/platform/base.h>
#include <cx/sys/sysinfo.h>

// Contract every platform backend implements.
//
// Backends fill in only what the operating system natively reports, and set the matching valid
// flags. Everything else -- zeroing the structure, stamping the sample time, the processor
// count, and the derived totals -- is done once in sysinfo.c, so no backend repeats it.

// Fill in what the machine's memory looks like. Returns false only if nothing at all could be
// read.
bool _sysPlatformMemInfo(SysMemInfo* out);

// Fill in the per-state processor times, in microseconds summed over every processor. busy and
// total are computed by the caller from whatever is filled in here.
bool _sysPlatformCPUTimes(SysCPUTimes* out);

// Fill in either or both of uptime and boottime; the caller derives the other one.
bool _sysPlatformUptime(SysUptime* out);

// Fill in the run queue averages. Platforms that keep no such figure set CX_NotSupported.
bool _sysPlatformLoadAvg(SysLoadAvg* out);

// Per-process readings. proc is the handle the caller supplied, or NULL when the caller only
// had a process id -- backends that need a native handle use proc's when there is one and open
// a temporary one otherwise. pid is always valid, and always matches proc when proc is set.
bool _sysPlatformProcMemInfo(ProcMemInfo* out, ProcessID pid, Process* proc);
bool _sysPlatformProcCPUTimes(ProcCPUTimes* out, ProcessID pid, Process* proc);
bool _sysPlatformProcIOInfo(ProcIOInfo* out, ProcessID pid, Process* proc);

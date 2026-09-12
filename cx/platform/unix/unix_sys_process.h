#pragma once

// Unix-internal declarations shared between the common Unix process code and the per-OS
// backends (linux_sys_process.c, freebsd_sys_process.c). The platform contract itself lives in
// cx/sys/process_private.h, which tail-includes this file.

#include "cx/sys/process.h"

// True if a process with this id exists. Used where there is no handle to consult, which on
// Unix is every process cx did not fork.
bool _procUnixAlive(ProcessID pid);

// True if this handle's pid still refers to the process it was opened for, rather than to
// something that was given the number after the original exited. Always true for a process cx
// forked, and for one whose start time could not be read when the handle was opened.
bool _procUnixSameProcess(Process* proc);

// When the process with this id started, in whatever unit the OS reports. Only ever compared
// against another reading for the same pid, to notice that the id has been handed to a
// different process. False if it cannot be read. Implemented per OS.
bool _procUnixStartTime(ProcessID pid, int64* out);

// Publish a status collected by waitpid() or equivalent on the handle, decoding a death by
// signal into the shell's 128 + signal convention.
void _procUnixPublishStatus(Process* proc, int status);

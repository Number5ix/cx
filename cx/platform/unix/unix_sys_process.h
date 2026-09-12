#pragma once

// Unix-internal declarations shared between the common Unix process code and the per-OS
// backends (linux_sys_process.c, freebsd_sys_process.c). The platform contract itself lives in
// cx/sys/process_private.h, which tail-includes this file.

#include "cx/sys/process.h"

// True if a process with this id exists. Used where there is no handle to consult, which on
// Unix is every process cx did not fork.
bool _procUnixAlive(ProcessID pid);

// When the process with this id started, in whatever unit the OS reports. Only ever compared
// against another reading for the same pid, to notice that the id has been handed to a
// different process. False if it cannot be read. Implemented per OS.
bool _procUnixStartTime(ProcessID pid, int64* out);

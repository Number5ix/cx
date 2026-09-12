#pragma once

// Unix-internal declarations shared between the common Unix statistics code and the per-OS
// backends (linux_sys_sysinfo.c, freebsd_sys_sysinfo.c).

#include "cx/sys/sysinfo.h"

// True if it is safe to report statistics for this handle. Applies the same reused-pid check
// the rest of the Unix process backend uses, so a handle whose process has exited reports
// nothing rather than the numbers belonging to whatever was given its id afterwards. Sets
// cxerr and returns false when it is not. proc may be NULL, in which case the caller only had
// a process id and there is nothing to check against.
bool _sysUnixProcUsable(Process* proc);

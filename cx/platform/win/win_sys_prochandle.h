#pragma once

// Opening a process handle, shared between the Windows process and statistics backends.
//
// Kept apart from win_sys_process.h because that header is tail-included by
// cx/sys/process_private.h, which shared code includes and which must never drag in the Win32
// SDK. This one names a HANDLE, so it needs the SDK and can only be included from a Windows
// backend source file.

#include "cx/platform/win.h"
#include "cx/sys/process.h"

// Open a process with the least access that answers cx's questions, stepping down through the
// rights until one is granted. Returns NULL if none was, and does not raise any privilege the
// caller was not already started with. Close the result with CloseHandle().
HANDLE _procWinOpenHandle(ProcessID pid);

#pragma once

#include <cx/cx.h>

/// Capture current call stack
/// @param nskip Number of stack frames to skip (0 to include this function)
/// @param nframes Maximum number of frames to capture
/// @param addrs Output array to receive instruction pointer addresses
/// @return Number of frames actually captured (may be less than nframes)
///
/// Captures the current call stack as an array of instruction pointer addresses.
/// Use `nskip` to exclude frames from the capture - typically set to 1 or more
/// to exclude the capture function itself and any wrapper functions.
///
/// **Performance:** This function is marked `_no_inline` and uses fast Windows
/// APIs (`RtlCaptureStackBackTrace`). Safe to call frequently.
///
/// **Example:**
/// @code
///   uintptr_t stack[64];
///   int frames = dbgStackTrace(1, 64, stack);  // Skip this function
///   // stack[] now contains up to 64 instruction pointers
/// @endcode
///
/// The captured addresses can be symbolicated using debugging tools (WinDbg, etc.)
/// or programmatically with `SymFromAddr` and related Windows debugging APIs.
int dbgStackTrace(int nskip, int nframes, uintptr_t *addrs);

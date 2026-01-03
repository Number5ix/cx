#pragma once

/// @file stacktrace.h
/// @brief Stack trace capture
///
/// @defgroup debug_stacktrace Stack Traces
/// @ingroup debug
/// @{
///
/// Platform-specific stack trace capture for debugging and crash analysis.
///
/// Provides the ability to capture the current call stack as an array of
/// instruction pointer addresses. These addresses can be symbolicated offline
/// or using platform-specific debugging APIs to obtain human-readable function
/// names, file names, and line numbers.
///
/// **Usage example:**
/// @code
///   uintptr_t frames[32];
///   int count = dbgStackTrace(0, 32, frames);
///   
///   // frames[] now contains instruction pointers
///   // Use debugging tools or APIs to symbolicate these addresses
///   for (int i = 0; i < count; i++) {
///       printf("Frame %d: 0x%p\n", i, (void*)frames[i]);
///   }
/// @endcode
///
/// Platform implementations:
/// - **Windows**: Uses `RtlCaptureStackBackTrace` for fast capture
/// - **Unix**: Implementation pending (uses platform-specific backtrace APIs)
/// - **WASM**: Limited or no stack trace support
///
/// Stack traces are automatically captured by the @ref debug_crash system
/// and included in crash reports.

#include <cx/platform/base.h>

#if defined(_WIN32)
#include <cx/platform/win/win_stacktrace.h>
#else
//#include <cx/platform/unix/unix_stacktrace.h>
#endif

/// @}  // end of debug_stacktrace group

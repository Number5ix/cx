#pragma once

/// @file os.h
/// @brief Operating system services

/// @defgroup platform_os OS Services
/// @ingroup platform
/// @{
///
/// Cross-platform operating system services for threading, timing, and system information.
/// This module provides a unified interface across Windows, Unix-like systems, and WebAssembly.
///
/// The implementation is selected at compile time based on the target platform:
/// - **Windows**: `win_os.h`
/// - **Unix** (Linux/FreeBSD): `unix_os.h`
/// - **WebAssembly**: `wasm_os.h`
///
/// **Available Services:**
/// - Thread control (yield, sleep)
/// - CPU detection (physical and logical core count)
/// - Cryptographically secure random number generation
///
/// Example usage:
/// @code
///   // Query system
///   int physical = osPhysicalCPUs();
///   int logical = osLogicalCPUs();
///
///   // Thread control
///   osYield();                // Yield to other threads
///   osSleep(1000000);        // Sleep for 1ms (microseconds)
///
///   // Random data
///   uint8 key[32];
///   if (osGenRandom(key, sizeof(key))) {
///       // Use cryptographically secure random key
///   }
/// @endcode

#include <cx/cx.h>
#include <cx/platform/base.h>

#if defined(_PLATFORM_WIN)
#include <cx/platform/win/win_os.h>
#elif defined(_PLATFORM_UNIX)
#include <cx/platform/unix/unix_os.h>
#elif defined(_PLATFORM_WASM)
#include <cx/platform/wasm/wasm_os.h>
#endif

/// Yield the current thread's time slice.
///
/// Hints to the OS scheduler that the current thread should yield execution
/// to other threads. Useful in busy-wait loops to reduce CPU usage.
void osYield();

/// Sleep for a specified duration.
///
/// Suspends execution of the current thread for at least the specified time.
/// The actual sleep duration may be longer due to system scheduling.
///
/// @param time Sleep duration in microseconds
void osSleep(int64 time);

/// Get the number of physical CPU cores.
///
/// Returns the count of physical processor cores, not including hyperthreading
/// or other logical processors.
///
/// @return Number of physical CPU cores, or 0 if detection fails
int osPhysicalCPUs();

/// Get the number of logical CPU cores.
///
/// Returns the count of logical processors, including hyperthreading cores.
/// This is typically the number of concurrent threads the system can execute.
///
/// @return Number of logical CPU cores, or 0 if detection fails
int osLogicalCPUs();

/// Generate cryptographically secure random data.
///
/// Fills the provided buffer with random bytes from the operating system's
/// cryptographically secure random number generator:
/// - Windows: `BCryptGenRandom`
/// - Linux: `/dev/urandom`
/// - FreeBSD: `arc4random_buf`
///
/// @param buffer Buffer to fill with random data
/// @param size Number of bytes to generate
/// @return true if random generation succeeded, false on error
bool osGenRandom(uint8* buffer, uint32 size);

/// @}  // end of platform_os group
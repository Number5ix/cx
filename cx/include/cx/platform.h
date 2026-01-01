#pragma once

/// @file platform.h
/// @brief Platform abstraction layer

/// @defgroup platform Platform Abstraction
/// @{
///
/// The platform abstraction layer provides cross-platform APIs and compiler/OS detection.
/// It isolates platform-specific code behind a consistent interface, supporting:
/// - **Windows** (MSVC compiler)
/// - **Linux** (GCC/Clang compilers)
/// - **FreeBSD** (GCC/Clang compilers)
/// - **WebAssembly** (Emscripten)
///
/// The abstraction is organized into three main areas:
/// - **Base Definitions**: Compiler detection, type definitions, and basic macros
/// - **CPU Operations**: Atomic operations, memory barriers, and CPU intrinsics
/// - **OS Services**: Threading, timing, random number generation, and CPU info
///
/// Platform-specific code is selected at compile time using preprocessor conditionals.
/// Applications should include this header and use the portable APIs rather than
/// directly using platform-specific functions.
///
/// Example usage:
/// @code
///   // OS services
///   int cpus = osPhysicalCPUs();
///   osSleep(1000000);  // Sleep for 1ms (time in microseconds)
///
///   // Atomic operations
///   atomic(int32) counter = 0;
///   atomicFetchAdd(&counter, 1, AcqRel);
///
///   // Random data
///   uint8 buffer[32];
///   osGenRandom(buffer, sizeof(buffer));
/// @endcode

#include <cx/platform/cpu.h>

/// @}  // end of platform group

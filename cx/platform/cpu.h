#pragma once

/// @file cpu.h
/// @brief CPU-specific operations and atomic primitives

/// @defgroup platform_cpu CPU Operations
/// @ingroup platform
/// @{
///
/// Cross-platform atomic operations, memory barriers, and CPU intrinsics.
/// This module provides:
/// - Atomic load/store/exchange/compare-exchange operations
/// - Atomic arithmetic (add, sub, and, or, xor)
/// - Memory barriers and fences
/// - CPU intrinsics (pause, prefetch, etc.)
///
/// The implementation is selected at compile time based on the compiler:
/// - **MSVC**: Uses `<intrin.h>` and MSVC intrinsics
/// - **Clang/GCC**: Uses C11 atomics and compiler built-ins
///
/// **Atomic Types:**
/// Use the `atomic(type)` macro to declare atomic variables:
/// @code
///   atomic(int32) counter = 0;
///   atomic(ptr) pointer = NULL;
/// @endcode
///
/// **Memory Ordering:**
/// - `Relaxed` - No synchronization or ordering constraints
/// - `Acquire` - Prevents reordering of subsequent loads/stores before this operation
/// - `Release` - Prevents reordering of previous loads/stores after this operation
/// - `AcqRel` - Combination of Acquire and Release
/// - `SeqCst` - Sequentially consistent ordering (strongest guarantee)
///
/// **Common Operations:**
/// @code
///   // Load and store
///   int32 val = atomicLoad(int32, &counter, Acquire);
///   atomicStore(&counter, 42, Release);
///
///   // Fetch and add
///   int32 old = atomicFetchAdd(&counter, 1, AcqRel);
///
///   // Compare and exchange
///   int32 expected = 0;
///   bool success = atomicCompareExchange(&counter, &expected, 1, AcqRel, Acquire);
///
///   // Exchange (atomic swap)
///   int32 prev = atomicExchange(&counter, 100, AcqRel);
/// @endcode
///
/// See the compiler-specific headers for the complete API.

#include <cx/platform/base.h>

#if defined(_COMPILER_MSVC)
#include <cx/platform/msvc/msvc_cpu.h>
#elif defined(_COMPILER_CLANG) || defined(_COMPILER_GCC)
#include <cx/platform/clang/clang_cpu.h>
#endif

/// @}  // end of platform_cpu group

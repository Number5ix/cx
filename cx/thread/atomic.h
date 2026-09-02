/// @file atomic.h
/// @brief Atomic operations
/// @defgroup thread_atomic Atomics
/// @ingroup thread
/// @{
///
/// C11-style atomic types and operations, with one consistent spelling across
/// compilers and platforms.
///
/// @section thread_atomic_declaring Declaring an atomic
///
/// Wrap a type in `atomic(type)` to declare an atomic variable or struct field:
///
/// @code
///   atomic(int32) counter;
///   atomic(ptr) head;
///   atomic(bool) done;
/// @endcode
///
/// The available type tokens are: `ptr` (a `void*`), `bool`, `size` (`size_t`),
/// `intptr`, `uintptr`, `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`,
/// `uint32`, and `uint64`.
///
/// An atomic variable must not be read or written directly - always go through the
/// operations below, which take the same type token used to declare it.
///
/// @section thread_atomic_orders Memory orders
///
/// Every operation takes a memory order, written as a bare name (the operation macro
/// adds the required prefix internally): `Relaxed`, `Acquire`, `Release`, `AcqRel`,
/// or `SeqCst`. These match the C11 memory order semantics of the same names -
/// `Relaxed` only guarantees atomicity of the operation itself, `Acquire`/`Release`
/// establish a happens-before relationship between a store and a later load of the
/// same value, `AcqRel` combines both for read-modify-write operations, and `SeqCst`
/// adds a single total order across all `SeqCst` operations for cases that need the
/// strongest guarantee.
///
/// @section thread_atomic_ops Operations
///
/// @code
///   atomic(int32) counter;
///   atomicStore(int32, &counter, 0, Relaxed);
///
///   int32 prev = atomicFetchAdd(int32, &counter, 1, AcqRel);
///
///   atomic(ptr) head;
///   void *cur = atomicLoad(ptr, &head, Acquire);
///   void *newnode = ...;
///   if (atomicCompareExchange(ptr, strong, &head, &cur, newnode, AcqRel, Acquire)) {
///       // cur was still the current value of head, and head is now newnode
///   } else {
///       // cur has been updated to head's actual current value; try again
///   }
/// @endcode
///
/// - `atomicLoad(type, atomic_ptr, order)` reads the current value.
/// - `atomicStore(type, atomic_ptr, val, order)` writes a new value.
/// - `atomicExchange(type, atomic_ptr, val, order)` writes a new value and returns
///   the value that was replaced.
/// - `atomicCompareExchange(type, weak|strong, atomic_ptr, expected_ptr, desired,
///   success_order, fail_order)` writes desired only if the current value equals
///   `*expected_ptr`, returning true on success. On failure, `*expected_ptr` is
///   updated to the actual current value, so a failed call can typically be retried
///   directly with the same variables. Use `strong` unless the operation is already
///   inside a retry loop, where `weak` allows a cheaper implementation that may fail
///   even when the comparison would have succeeded.
/// - `atomicFetchAdd`, `atomicFetchSub`, `atomicFetchAnd`, `atomicFetchOr`, and
///   `atomicFetchXor` (all `(type, atomic_ptr, val, order)`) apply the operation and
///   return the value from before it was applied. These are only available for the
///   integer types, not `ptr` or `bool`.
/// - `atomicFence(order)` issues a standalone memory fence.
/// - `atomicInit(val)` produces a static initializer for an atomic variable, for use
///   where a function call isn't allowed:
///   @code
///     atomic(int32) counter = atomicInit(0);
///   @endcode
///
/// @note A true weak compare-exchange isn't available everywhere: some platforms
/// implement `weak` the same as `strong`, so don't rely on being able to observe a
/// spurious `weak` failure - only on the fact that a `strong` failure is never
/// spurious.
///
/// @section thread_atomic_64bit 64-bit atomics on 32-bit platforms
///
/// `atomic(int64)` and `atomic(uint64)` work on 32-bit platforms as well as 64-bit
/// ones. On 32-bit x86 specifically, the read-modify-write operations
/// (atomicExchange, atomicCompareExchange, and the atomicFetch* family) are
/// implemented with a compare-and-swap retry loop rather than a single instruction,
/// since the platform exposes no native 8-byte instruction for them. atomicLoad and
/// atomicStore use a fast path where available. Expect 64-bit atomics to be slower
/// than other sizes on 32-bit x86, and avoid them on hot paths that must also run
/// there.

#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

#if defined(_COMPILER_MSVC)
#include <cx/platform/msvc/msvc_atomic.h>
#elif defined(_COMPILER_CLANG) || defined(_COMPILER_GCC)
#include <cx/platform/clang/clang_atomic.h>
#endif

#ifdef _64BIT
CX_GENERATE_ATOMICS(void*, ptr, 3)
CX_GENERATE_ATOMICS(bool, bool, 0)
CX_GENERATE_INT_ATOMICS(size_t, size, 3)
CX_GENERATE_INT_ATOMICS(intptr, intptr, 3)
CX_GENERATE_INT_ATOMICS(int8, int8, 0)
CX_GENERATE_INT_ATOMICS(int16, int16, 1)
CX_GENERATE_INT_ATOMICS(int32, int32, 2)
CX_GENERATE_INT_ATOMICS(int64, int64, 3)
CX_GENERATE_INT_ATOMICS(uintptr, uintptr, 3)
CX_GENERATE_INT_ATOMICS(uint8, uint8, 0)
CX_GENERATE_INT_ATOMICS(uint16, uint16, 1)
CX_GENERATE_INT_ATOMICS(uint32, uint32, 2)
CX_GENERATE_INT_ATOMICS(uint64, uint64, 3)
#else
CX_GENERATE_ATOMICS(void*, ptr, 2)
CX_GENERATE_ATOMICS(bool, bool, 0)
CX_GENERATE_INT_ATOMICS(size_t, size, 2)
CX_GENERATE_INT_ATOMICS(intptr, intptr, 2)
CX_GENERATE_INT_ATOMICS(int8, int8, 0)
CX_GENERATE_INT_ATOMICS(int16, int16, 1)
CX_GENERATE_INT_ATOMICS(int32, int32, 2)
CX_GENERATE_INT_ATOMICS(uintptr, uintptr, 2)
CX_GENERATE_INT_ATOMICS(uint8, uint8, 0)
CX_GENERATE_INT_ATOMICS(uint16, uint16, 1)
CX_GENERATE_INT_ATOMICS(uint32, uint32, 2)
CX_GENERATE_INT_ATOMICS(int64, int64, 3)
CX_GENERATE_INT_ATOMICS(uint64, uint64, 3)
#endif

/// @}
// end of thread_atomic group

#pragma once

// Platform-specific atomics implementation (lifted from jemalloc 5.x)
// This implements C11-style atomics.
#include <cx/cx.h>
#include <cx/platform/base.h>

#if defined(_COMPILER_MSVC)
#include <cx/platform/msvc/msvc_atomic.h>
#elif defined(_COMPILER_CLANG)
#include <cx/platform/clang/clang_atomic.h>
#endif

/*
 * This header gives more or less a backport of C11 atomics. The user can write
 * CX_GENERATE_ATOMICS(type, short_type, lg_sizeof_type); to generate
 * counterparts of the C11 atomic functions for type, as so:
 *   CX_GENERATE_ATOMICS(int *, pi, 3);
 * and then write things like:
 *   int *some_ptr;
 *   atomic_pi_t atomic_ptr_to_int;
 *   atomic_store_pi(&atomic_ptr_to_int, some_ptr, ATOMIC_RELAXED);
 *   int *prev_value = atomic_exchange_pi(&ptr_to_int, NULL, ATOMIC_ACQ_REL);
 *   assert(some_ptr == prev_value);
 * and expect things to work in the obvious way.
 *
 * Also included (with naming differences to avoid conflicts with the standard
 * library):
 *   atomic_fence(atomic_memory_order_t) (mimics C11's atomic_thread_fence).
 *   ATOMIC_INIT (mimics C11's ATOMIC_VAR_INIT).
 */

/*
 * Pure convenience, so that we don't have to type "atomic_memory_order_"
 * quite so often.
 */
#define ATOMIC_RELAXED atomic_memory_order_relaxed
#define ATOMIC_ACQUIRE atomic_memory_order_acquire
#define ATOMIC_RELEASE atomic_memory_order_release
#define ATOMIC_ACQ_REL atomic_memory_order_acq_rel
#define ATOMIC_SEQ_CST atomic_memory_order_seq_cst

#ifdef _64BIT
CX_GENERATE_ATOMICS(void *, ptr, 3)
CX_GENERATE_ATOMICS(bool, bool, 0)
CX_GENERATE_INT_ATOMICS(size_t, size, 3)
CX_GENERATE_INT_ATOMICS(intptr, intptr, 3)
CX_GENERATE_INT_ATOMICS(uint8, uint8, 0)
CX_GENERATE_INT_ATOMICS(uint16, uint16, 1)
CX_GENERATE_INT_ATOMICS(uint32, uint32, 2)
CX_GENERATE_INT_ATOMICS(uint64, uint64, 3)
#else
CX_GENERATE_ATOMICS(void *, ptr, 2)
CX_GENERATE_ATOMICS(bool, bool, 0)
CX_GENERATE_INT_ATOMICS(size_t, size, 2)
CX_GENERATE_INT_ATOMICS(intptr, intptr, 2)
CX_GENERATE_INT_ATOMICS(uint8, uint8, 0)
CX_GENERATE_INT_ATOMICS(uint16, uint16, 1)
CX_GENERATE_INT_ATOMICS(uint32, uint32, 2)
#endif

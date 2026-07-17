#ifndef __cplusplus
#pragma once

#include <stdatomic.h>

#define atomicInit(...) (__VA_ARGS__)

#define AtomicMemoryOrder memory_order
#define ATOMIC_MO_Relaxed memory_order_relaxed
#define ATOMIC_MO_Acquire memory_order_acquire
#define ATOMIC_MO_Release memory_order_release
#define ATOMIC_MO_AcqRel  memory_order_acq_rel
#define ATOMIC_MO_SeqCst  memory_order_seq_cst

#define atomicFence(order) atomic_thread_fence(ATOMIC_MO_##order)

#define atomic(type)                        cx_atomic_##type
#define atomicLoad(type, atomic_ptr, order) _atomicLoad_##type(atomic_ptr, ATOMIC_MO_##order)
#define atomicStore(type, atomic_ptr, val, order) \
    _atomicStore_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicExchange(type, atomic_ptr, val, order) \
    _atomicExchange_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicCompareExchange(type,                                       \
                              semantics,                                  \
                              atomic_ptr,                                 \
                              expected_ptr,                               \
                              desired,                                    \
                              successorder,                               \
                              failorder)                                  \
    _atomicCompareExchange_##semantics##_##type(atomic_ptr,               \
                                                expected_ptr,             \
                                                desired,                  \
                                                ATOMIC_MO_##successorder, \
                                                ATOMIC_MO_##failorder)

#define atomicFetchAdd(type, atomic_ptr, val, order) \
    _atomicFetchAdd_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchSub(type, atomic_ptr, val, order) \
    _atomicFetchSub_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchAnd(type, atomic_ptr, val, order) \
    _atomicFetchAnd_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchOr(type, atomic_ptr, val, order) \
    _atomicFetchOr_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchXor(type, atomic_ptr, val, order) \
    _atomicFetchXor_##type(atomic_ptr, val, ATOMIC_MO_##order)

#define CX_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)                             \
    typedef _Atomic(type) cx_atomic_##short_type;                                               \
                                                                                                \
    _meta_inline type _atomicLoad_##short_type(const cx_atomic_##short_type* a,                 \
                                               AtomicMemoryOrder mo)                            \
    {                                                                                           \
        /*                                                                                      \
         * A strict interpretation of the C standard prevents                                   \
         * atomic_load from taking a const argument, but it's                                   \
         * convenient for our purposes. This cast is a workaround.                              \
         */                                                                                     \
        cx_atomic_##short_type* a_nonconst = (cx_atomic_##short_type*)a;                        \
        return atomic_load_explicit(a_nonconst, mo);                                            \
    }                                                                                           \
                                                                                                \
    _meta_inline void _atomicStore_##short_type(cx_atomic_##short_type* a,                      \
                                                type val,                                       \
                                                AtomicMemoryOrder mo)                           \
    {                                                                                           \
        atomic_store_explicit(a, val, mo);                                                      \
    }                                                                                           \
                                                                                                \
    _meta_inline type                                                                           \
        _atomicExchange_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                           \
        return atomic_exchange_explicit(a, val, mo);                                            \
    }                                                                                           \
                                                                                                \
    _meta_inline bool _atomicCompareExchange_weak_##short_type(cx_atomic_##short_type* a,       \
                                                               type* expected,                  \
                                                               type desired,                    \
                                                               AtomicMemoryOrder success_mo,    \
                                                               AtomicMemoryOrder failure_mo)    \
    {                                                                                           \
        return atomic_compare_exchange_weak_explicit(a,                                         \
                                                     expected,                                  \
                                                     desired,                                   \
                                                     success_mo,                                \
                                                     failure_mo);                               \
    }                                                                                           \
                                                                                                \
    _meta_inline bool _atomicCompareExchange_strong_##short_type(cx_atomic_##short_type* a,     \
                                                                 type* expected,                \
                                                                 type desired,                  \
                                                                 AtomicMemoryOrder success_mo,  \
                                                                 AtomicMemoryOrder failure_mo)  \
    {                                                                                           \
        return atomic_compare_exchange_strong_explicit(a,                                       \
                                                       expected,                                \
                                                       desired,                                 \
                                                       success_mo,                              \
                                                       failure_mo);                             \
    }

#define CX_EXTERN_ATOMICS(type, short_type)                                                     \
    extern inline type _atomicLoad_##short_type(const cx_atomic_##short_type* a,                \
                                                AtomicMemoryOrder mo);                          \
                                                                                                \
    extern inline void _atomicStore_##short_type(cx_atomic_##short_type* a,                     \
                                                 type val,                                      \
                                                 AtomicMemoryOrder mo);                         \
                                                                                                \
    extern inline type _atomicExchange_##short_type(cx_atomic_##short_type* a,                  \
                                                    type val,                                   \
                                                    AtomicMemoryOrder mo);                      \
                                                                                                \
    extern inline bool _atomicCompareExchange_weak_##short_type(cx_atomic_##short_type* a,      \
                                                                type* expected,                 \
                                                                type desired,                   \
                                                                AtomicMemoryOrder success_mo,   \
                                                                AtomicMemoryOrder failure_mo);  \
                                                                                                \
    extern inline bool _atomicCompareExchange_strong_##short_type(cx_atomic_##short_type* a,    \
                                                                  type* expected,               \
                                                                  type desired,                 \
                                                                  AtomicMemoryOrder success_mo, \
                                                                  AtomicMemoryOrder failure_mo);

/*
 * Integral types have some special operations available that non-integral ones
 * lack.
 */
#define CX_GENERATE_INT_ATOMICS(type, short_type, /* unused */ lg_size)                         \
    CX_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)                                 \
                                                                                                \
    _meta_inline type                                                                           \
        _atomicFetchAdd_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                           \
        return atomic_fetch_add_explicit(a, val, mo);                                           \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchSub_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                           \
        return atomic_fetch_sub_explicit(a, val, mo);                                           \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchAnd_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                           \
        return atomic_fetch_and_explicit(a, val, mo);                                           \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchOr_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo)  \
    {                                                                                           \
        return atomic_fetch_or_explicit(a, val, mo);                                            \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchXor_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                           \
        return atomic_fetch_xor_explicit(a, val, mo);                                           \
    }

#define CX_EXTERN_INT_ATOMICS(type, short_type)                                \
    CX_EXTERN_ATOMICS(type, short_type)                                        \
                                                                               \
    extern inline type _atomicFetchAdd_##short_type(cx_atomic_##short_type* a, \
                                                    type val,                  \
                                                    AtomicMemoryOrder mo);     \
    extern inline type _atomicFetchSub_##short_type(cx_atomic_##short_type* a, \
                                                    type val,                  \
                                                    AtomicMemoryOrder mo);     \
    extern inline type _atomicFetchAnd_##short_type(cx_atomic_##short_type* a, \
                                                    type val,                  \
                                                    AtomicMemoryOrder mo);     \
    extern inline type _atomicFetchOr_##short_type(cx_atomic_##short_type* a,  \
                                                   type val,                   \
                                                   AtomicMemoryOrder mo);      \
    extern inline type _atomicFetchXor_##short_type(cx_atomic_##short_type* a, \
                                                    type val,                  \
                                                    AtomicMemoryOrder mo);
#else   // __cplusplus

/*
 * g++ and pre-C++23 clang++ can't use C11 <stdatomic.h> and the _Atomic keyword.
 *
 * For C++ we need a std::atomic-based implementation that is ABI-compatible with the C branch
 * above. The C source files are still compiled as C and use the _Atomic types; C++ only references
 * the same struct layout and call these inline wrappers, so the two must agree on size and
 * alignment.
 *
 * This header is always included outside any extern "C" block, so <atomic> is safe to pull in here.
 * <atomic> additionally carries its own include guard.
 */

#pragma once

#include <atomic>

#define atomicInit(...) (__VA_ARGS__)

#define AtomicMemoryOrder std::memory_order
#define ATOMIC_MO_Relaxed std::memory_order_relaxed
#define ATOMIC_MO_Acquire std::memory_order_acquire
#define ATOMIC_MO_Release std::memory_order_release
#define ATOMIC_MO_AcqRel  std::memory_order_acq_rel
#define ATOMIC_MO_SeqCst  std::memory_order_seq_cst

#define atomicFence(order) std::atomic_thread_fence(ATOMIC_MO_##order)

#define atomic(type)                        cx_atomic_##type
#define atomicLoad(type, atomic_ptr, order) _atomicLoad_##type(atomic_ptr, ATOMIC_MO_##order)
#define atomicStore(type, atomic_ptr, val, order) \
    _atomicStore_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicExchange(type, atomic_ptr, val, order) \
    _atomicExchange_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicCompareExchange(type,                                       \
                              semantics,                                  \
                              atomic_ptr,                                 \
                              expected_ptr,                               \
                              desired,                                    \
                              successorder,                               \
                              failorder)                                  \
    _atomicCompareExchange_##semantics##_##type(atomic_ptr,               \
                                                expected_ptr,             \
                                                desired,                  \
                                                ATOMIC_MO_##successorder, \
                                                ATOMIC_MO_##failorder)

#define atomicFetchAdd(type, atomic_ptr, val, order) \
    _atomicFetchAdd_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchSub(type, atomic_ptr, val, order) \
    _atomicFetchSub_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchAnd(type, atomic_ptr, val, order) \
    _atomicFetchAnd_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchOr(type, atomic_ptr, val, order) \
    _atomicFetchOr_##type(atomic_ptr, val, ATOMIC_MO_##order)
#define atomicFetchXor(type, atomic_ptr, val, order) \
    _atomicFetchXor_##type(atomic_ptr, val, ATOMIC_MO_##order)

/*
 * The is_always_lock_free ABI check is only available with the C++17 library
 * feature. It is gated so this header stays clean under -std=c++14. A macro
 * body cannot contain preprocessor directives, so the conditional lives here.
 */
#if defined(__cpp_lib_atomic_is_always_lock_free)
#define CX_ATOMIC_LOCK_FREE_ASSERT(type, short_type)                    \
    static_assert(std::atomic<type>::is_always_lock_free,               \
                  "cx_atomic_" #short_type " must be always lock-free " \
                  "for C ABI compatibility");
#else
#define CX_ATOMIC_LOCK_FREE_ASSERT(type, short_type)
#endif

#define CX_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)                             \
    typedef std::atomic<type> cx_atomic_##short_type;                                           \
    static_assert(sizeof(cx_atomic_##short_type) == sizeof(type),                               \
                  "cx_atomic_" #short_type " size must match " #type " for C ABI compat");      \
    static_assert(alignof(cx_atomic_##short_type) == alignof(type),                             \
                  "cx_atomic_" #short_type " alignment must match " #type " for C ABI compat"); \
    CX_ATOMIC_LOCK_FREE_ASSERT(type, short_type)                                                \
                                                                                                \
    _meta_inline type _atomicLoad_##short_type(const cx_atomic_##short_type* a,                 \
                                               AtomicMemoryOrder mo)                            \
    {                                                                                           \
        /*                                                                                      \
         * A strict interpretation of the C standard prevents                                   \
         * atomic_load from taking a const argument, but it's                                   \
         * convenient for our purposes. This cast is a workaround.                              \
         */                                                                                     \
        cx_atomic_##short_type* a_nonconst = (cx_atomic_##short_type*)a;                        \
        return a_nonconst->load(mo);                                                            \
    }                                                                                           \
                                                                                                \
    _meta_inline void _atomicStore_##short_type(cx_atomic_##short_type* a,                      \
                                                type val,                                       \
                                                AtomicMemoryOrder mo)                           \
    {                                                                                           \
        a->store(val, mo);                                                                      \
    }                                                                                           \
                                                                                                \
    _meta_inline type                                                                           \
    _atomicExchange_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo)     \
    {                                                                                           \
        return a->exchange(val, mo);                                                            \
    }                                                                                           \
                                                                                                \
    _meta_inline bool _atomicCompareExchange_weak_##short_type(cx_atomic_##short_type* a,       \
                                                               type* expected,                  \
                                                               type desired,                    \
                                                               AtomicMemoryOrder success_mo,    \
                                                               AtomicMemoryOrder failure_mo)    \
    {                                                                                           \
        return a->compare_exchange_weak(*expected, desired, success_mo, failure_mo);            \
    }                                                                                           \
                                                                                                \
    _meta_inline bool _atomicCompareExchange_strong_##short_type(cx_atomic_##short_type* a,     \
                                                                 type* expected,                \
                                                                 type desired,                  \
                                                                 AtomicMemoryOrder success_mo,  \
                                                                 AtomicMemoryOrder failure_mo)  \
    {                                                                                           \
        return a->compare_exchange_strong(*expected, desired, success_mo, failure_mo);          \
    }

#define CX_EXTERN_ATOMICS(type, short_type)

/*
 * Integral types have some special operations available that non-integral ones
 * lack.
 */
#define CX_GENERATE_INT_ATOMICS(type, short_type, /* unused */ lg_size)                     \
    CX_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)                             \
                                                                                            \
    _meta_inline type                                                                       \
    _atomicFetchAdd_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                       \
        return a->fetch_add(val, mo);                                                       \
    }                                                                                       \
    _meta_inline type                                                                       \
    _atomicFetchSub_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                       \
        return a->fetch_sub(val, mo);                                                       \
    }                                                                                       \
    _meta_inline type                                                                       \
    _atomicFetchAnd_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                       \
        return a->fetch_and(val, mo);                                                       \
    }                                                                                       \
    _meta_inline type                                                                       \
    _atomicFetchOr_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo)  \
    {                                                                                       \
        return a->fetch_or(val, mo);                                                        \
    }                                                                                       \
    _meta_inline type                                                                       \
    _atomicFetchXor_##short_type(cx_atomic_##short_type* a, type val, AtomicMemoryOrder mo) \
    {                                                                                       \
        return a->fetch_xor(val, mo);                                                       \
    }

#define CX_EXTERN_INT_ATOMICS(type, short_type)

#endif   // __cplusplus

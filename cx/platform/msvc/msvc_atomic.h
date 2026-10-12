#pragma once

#if defined(_M_X64)
#define CX_MemoryBarrier    __faststorefence
#define CX_ReadWriteBarrier _ReadWriteBarrier
#elif defined(_M_IX86)
__forceinline void CX_MemoryBarrier(void)
{
    long Barrier;

    _InterlockedOr(&Barrier, 0);
    return;
}
#define CX_ReadWriteBarrier _ReadWriteBarrier
#elif defined(_M_ARM64)
// A full barrier (dmb ish), the one the C++ runtime's atomics use on ARM64.
#define CX_MemoryBarrier()  __dmb(_ARM64_BARRIER_ISH)
#define CX_ReadWriteBarrier _ReadWriteBarrier
#else
#error "Don't know how to create atomics for this platform for MSVC."
#endif

#include "cx/utils/macros.h"

#define atomicInit(...) \
    {                   \
        __VA_ARGS__     \
    }

typedef enum {
    ATOMIC_MO_Relaxed,
    ATOMIC_MO_Acquire,
    ATOMIC_MO_Release,
    ATOMIC_MO_AcqRel,
    ATOMIC_MO_SeqCst
} AtmoicMemoryOrder;

typedef char _cx_atomic_repr_0_t;
typedef short _cx_atomic_repr_1_t;
typedef long _cx_atomic_repr_2_t;
typedef __int64 _cx_atomic_repr_3_t;

// Trivial per-size raw load/store, which the ordered load and store below are built from on
// x86/x64. Sizes 0-2 (and size 3 on x64) are always atomic as plain accesses on x86/x64;
// size 3 on x86 needs the special handling below, since a plain 8-byte access there can
// tear. ARM64 doesn't use these: its loads and stores carry their ordering themselves.
_meta_inline _cx_atomic_repr_0_t _cx_atomic_rawLoad_0(const _cx_atomic_repr_0_t* p)
{
    return *p;
}
_meta_inline void _cx_atomic_rawStore_0(_cx_atomic_repr_0_t* p, _cx_atomic_repr_0_t val)
{
    *p = val;
}
_meta_inline _cx_atomic_repr_1_t _cx_atomic_rawLoad_1(const _cx_atomic_repr_1_t* p)
{
    return *p;
}
_meta_inline void _cx_atomic_rawStore_1(_cx_atomic_repr_1_t* p, _cx_atomic_repr_1_t val)
{
    *p = val;
}
_meta_inline _cx_atomic_repr_2_t _cx_atomic_rawLoad_2(const _cx_atomic_repr_2_t* p)
{
    return *p;
}
_meta_inline void _cx_atomic_rawStore_2(_cx_atomic_repr_2_t* p, _cx_atomic_repr_2_t val)
{
    *p = val;
}

#if defined(_M_IX86)

// MSVC/x86 has no 64-bit Interlocked intrinsics except _InterlockedCompareExchange64
// (cmpxchg8b). Route every "*64" name the RMW macros paste through a "_cx_x86"-suffixed
// stand-in implemented here as a compare-exchange retry loop.
#define CX_ATOMIC_IL_SUFFIX_3 64_cx_x86

#include <emmintrin.h>

#if _M_IX86_FP >= 2

// SSE2 movq is an unlocked but atomic 8-byte load/store - the default target since
// VS2012 (/arch:SSE2), so this is the path every shipped x86 build actually takes.
_meta_inline _cx_atomic_repr_3_t _cx_atomic_rawLoad_3(const _cx_atomic_repr_3_t* p)
{
    _cx_atomic_repr_3_t ret;
    _mm_storel_epi64((__m128i*)&ret, _mm_loadl_epi64((const __m128i*)p));
    return ret;
}
_meta_inline void _cx_atomic_rawStore_3(_cx_atomic_repr_3_t* p, _cx_atomic_repr_3_t val)
{
    _mm_storel_epi64((__m128i*)p, _mm_loadl_epi64((const __m128i*)&val));
}

#else

// /arch:IA32 has no unlocked 8-byte load/store, so fall back to
// _InterlockedCompareExchange64. The "load" writes back the value it read (a CAS against
// itself), which is why it needs writable memory - it cannot honor const.
_meta_inline _cx_atomic_repr_3_t _cx_atomic_rawLoad_3(const _cx_atomic_repr_3_t* p)
{
    _cx_atomic_repr_3_t* pw = (_cx_atomic_repr_3_t*)p;
    return _InterlockedCompareExchange64(pw, 0, 0);
}
_meta_inline void _cx_atomic_rawStore_3(_cx_atomic_repr_3_t* p, _cx_atomic_repr_3_t val)
{
    _cx_atomic_repr_3_t old = *p;
    for (;;) {
        _cx_atomic_repr_3_t prev = _InterlockedCompareExchange64(p, val, old);
        if (prev == old) {
            return;
        }
        old = prev;
    }
}

#endif   // _M_IX86_FP >= 2

_meta_inline _cx_atomic_repr_3_t _InterlockedCompareExchange64_cx_x86(_cx_atomic_repr_3_t* p,
                                                                      _cx_atomic_repr_3_t xchg,
                                                                      _cx_atomic_repr_3_t comp)
{
    return _InterlockedCompareExchange64(p, xchg, comp);
}

// One CAS retry loop shared by every 64-bit RMW op MSVC/x86 lacks a native intrinsic
// for. "expr" computes the new value from "old"; it may reference "old" and "val". The
// generated name matches what CX_ATOMIC_IL_NAME pastes via the "64_cx_x86" suffix.
#define _CX_X86_ATOMIC64_RMW(name, expr)                                                  \
    _meta_inline _cx_atomic_repr_3_t name##64_cx_x86(_cx_atomic_repr_3_t * p,             \
                                                     _cx_atomic_repr_3_t val)             \
    {                                                                                     \
        _cx_atomic_repr_3_t old = *p;                                                     \
        for (;;) {                                                                        \
            _cx_atomic_repr_3_t desired = (expr);                                         \
            _cx_atomic_repr_3_t prev    = _InterlockedCompareExchange64(p, desired, old); \
            if (prev == old) {                                                            \
                return old;                                                               \
            }                                                                             \
            old = prev;                                                                   \
        }                                                                                 \
    }

_CX_X86_ATOMIC64_RMW(_InterlockedExchange, val)
_CX_X86_ATOMIC64_RMW(_InterlockedExchangeAdd,
                     (_cx_atomic_repr_3_t)((unsigned __int64)old + (unsigned __int64)val))
_CX_X86_ATOMIC64_RMW(_InterlockedAnd, old & val)
_CX_X86_ATOMIC64_RMW(_InterlockedOr, old | val)
_CX_X86_ATOMIC64_RMW(_InterlockedXor, old ^ val)

#else   // _M_X64, _M_ARM64

#define CX_ATOMIC_IL_SUFFIX_3 64

_meta_inline _cx_atomic_repr_3_t _cx_atomic_rawLoad_3(const _cx_atomic_repr_3_t* p)
{
    return *p;
}
_meta_inline void _cx_atomic_rawStore_3(_cx_atomic_repr_3_t* p, _cx_atomic_repr_3_t val)
{
    *p = val;
}

#endif   // defined(_M_IX86)

_meta_inline void _atomicFence(AtmoicMemoryOrder mo)
{
    CX_ReadWriteBarrier();
#if defined(_M_ARM64)
    /* ARM needs a barrier for everything but relaxed. Acquire only has to keep later loads
     * behind it, which is a cheaper barrier than the full one. */
    if (mo == ATOMIC_MO_Acquire) {
        __dmb(_ARM64_BARRIER_ISHLD);
    } else if (mo != ATOMIC_MO_Relaxed) {
        CX_MemoryBarrier();
    }
#elif defined(_M_IX86) || defined(_M_X64)
    /* x86 needs a barrier only for seq_cst. */
    if (mo == ATOMIC_MO_SeqCst) {
        CX_MemoryBarrier();
    }
#else
#error "Don't know how to create atomics for this platform for MSVC."
#endif
    CX_ReadWriteBarrier();
}
#define atomicFence(order) _atomicFence(ATOMIC_MO_##order)

// Ordered per-size load and store, used uniformly by CX_GENERATE_ATOMICS.
#if defined(_M_ARM64)

// On ARM64 a plain access gives no ordering at all, and a full barrier beside every load and
// store would be correct but slow. The acquiring load (ldar) and releasing store (stlr) order
// just the one access, which is what acquire and release ask for. A sequentially consistent
// store also has to keep a later load from passing it, hence the full barrier after it. This
// is how the C++ runtime implements std::atomic for ARM64.
#if _MSC_VER >= 1940
#define _CX_ARM64_LOAD_ACQUIRE(bits, p) \
    __load_acquire##bits((const volatile unsigned __int##bits*)(p))
#define _CX_ARM64_STORE_RELEASE(bits, p, val) \
    __stlr##bits((volatile unsigned __int##bits*)(p), (unsigned __int##bits)(val))
#else
// Toolsets before Visual Studio 2022 17.10 are not sure to have the ldar/stlr intrinsics;
// they get the plain access and the full barrier.
#define _CX_ARM64_BARRIER_ACCESS(bits)                                                       \
    _meta_inline __int##bits _cx_arm64_loadAcquire##bits(const volatile __int##bits* p)      \
    {                                                                                        \
        __int##bits ret = __iso_volatile_load##bits(p);                                      \
        CX_MemoryBarrier();                                                                  \
        return ret;                                                                          \
    }                                                                                        \
    _meta_inline void _cx_arm64_storeRelease##bits(volatile __int##bits* p, __int##bits val) \
    {                                                                                        \
        CX_MemoryBarrier();                                                                  \
        __iso_volatile_store##bits(p, val);                                                  \
    }

_CX_ARM64_BARRIER_ACCESS(8)
_CX_ARM64_BARRIER_ACCESS(16)
_CX_ARM64_BARRIER_ACCESS(32)
_CX_ARM64_BARRIER_ACCESS(64)

#define _CX_ARM64_LOAD_ACQUIRE(bits, p) \
    _cx_arm64_loadAcquire##bits((const volatile __int##bits*)(p))
#define _CX_ARM64_STORE_RELEASE(bits, p, val) \
    _cx_arm64_storeRelease##bits((volatile __int##bits*)(p), (__int##bits)(val))
#endif

#define _CX_ATOMIC_ORDERED_ACCESS(lg_size, bits)                                              \
    _meta_inline _cx_atomic_repr_##lg_size##_t                                                \
        _cx_atomic_load_##lg_size(const _cx_atomic_repr_##lg_size##_t* p,                     \
                                  AtmoicMemoryOrder mo)                                       \
    {                                                                                         \
        _cx_atomic_repr_##lg_size##_t ret;                                                    \
        if (mo == ATOMIC_MO_Relaxed) {                                                        \
            return (_cx_atomic_repr_##lg_size##_t)                                            \
                __iso_volatile_load##bits((const volatile __int##bits*)p);                    \
        }                                                                                     \
        ret = (_cx_atomic_repr_##lg_size##_t)_CX_ARM64_LOAD_ACQUIRE(bits, p);                 \
        CX_ReadWriteBarrier();                                                                \
        return ret;                                                                           \
    }                                                                                         \
    _meta_inline void _cx_atomic_store_##lg_size(_cx_atomic_repr_##lg_size##_t* p,            \
                                                 _cx_atomic_repr_##lg_size##_t val,           \
                                                 AtmoicMemoryOrder mo)                        \
    {                                                                                         \
        if (mo == ATOMIC_MO_Relaxed) {                                                        \
            __iso_volatile_store##bits((volatile __int##bits*)p, (__int##bits)val);           \
            return;                                                                           \
        }                                                                                     \
        CX_ReadWriteBarrier();                                                                \
        _CX_ARM64_STORE_RELEASE(bits, p, val);                                                \
        if (mo == ATOMIC_MO_SeqCst) {                                                         \
            CX_MemoryBarrier();                                                               \
        }                                                                                     \
    }

#else   // _M_IX86, _M_X64

// On x86/x64 the raw access is already ordered against everything but a later load, so a
// fence around it is all the stronger orderings need.
#define _CX_ATOMIC_ORDERED_ACCESS(lg_size, bits)                                              \
    _meta_inline _cx_atomic_repr_##lg_size##_t                                                \
        _cx_atomic_load_##lg_size(const _cx_atomic_repr_##lg_size##_t* p,                     \
                                  AtmoicMemoryOrder mo)                                       \
    {                                                                                         \
        _cx_atomic_repr_##lg_size##_t ret = _cx_atomic_rawLoad_##lg_size(p);                  \
        if (mo != ATOMIC_MO_Relaxed) {                                                        \
            _atomicFence(ATOMIC_MO_Acquire);                                                  \
        }                                                                                     \
        return ret;                                                                           \
    }                                                                                         \
    _meta_inline void _cx_atomic_store_##lg_size(_cx_atomic_repr_##lg_size##_t* p,            \
                                                 _cx_atomic_repr_##lg_size##_t val,           \
                                                 AtmoicMemoryOrder mo)                        \
    {                                                                                         \
        if (mo != ATOMIC_MO_Relaxed) {                                                        \
            _atomicFence(ATOMIC_MO_Release);                                                  \
        }                                                                                     \
        _cx_atomic_rawStore_##lg_size(p, val);                                                \
        if (mo == ATOMIC_MO_SeqCst) {                                                         \
            _atomicFence(ATOMIC_MO_SeqCst);                                                   \
        }                                                                                     \
    }

#endif

_CX_ATOMIC_ORDERED_ACCESS(0, 8)
_CX_ATOMIC_ORDERED_ACCESS(1, 16)
_CX_ATOMIC_ORDERED_ACCESS(2, 32)
_CX_ATOMIC_ORDERED_ACCESS(3, 64)

#define CX_ATOMIC_IL_REPR(lg_size) _cx_atomic_repr_##lg_size##_t

#define CX_ATOMIC_IL_NAME(base_name, lg_size) tokconcat(base_name, CX_ATOMIC_IL_SUFFIX(lg_size))

#define CX_ATOMIC_IL_SUFFIX(lg_size) tokconcat(CX_ATOMIC_IL_SUFFIX_, lg_size)

#define CX_ATOMIC_IL_SUFFIX_0 8
#define CX_ATOMIC_IL_SUFFIX_1 16
#define CX_ATOMIC_IL_SUFFIX_2
// CX_ATOMIC_IL_SUFFIX_3 is defined above, alongside the rawLoad/rawStore split, since its
// value depends on the same _M_IX86 vs _M_X64 branch.

#define CX_ATOMIC_IL_LOAD(lg_size)  tokconcat(_cx_atomic_load_, lg_size)
#define CX_ATOMIC_IL_STORE(lg_size) tokconcat(_cx_atomic_store_, lg_size)

#define CX_ATOMIC_IL_ALIGN(lg_size) tokconcat(CX_ATOMIC_IL_ALIGN_, lg_size)
#define CX_ATOMIC_IL_ALIGN_0
#define CX_ATOMIC_IL_ALIGN_1
#define CX_ATOMIC_IL_ALIGN_2
#if defined(_M_IX86)
// The movq/cmpxchg8b paths both require 8-byte alignment; state it explicitly rather
// than rely on the default struct layout giving it to us.
#define CX_ATOMIC_IL_ALIGN_3 alignMem(8)
#else
#define CX_ATOMIC_IL_ALIGN_3
#endif

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

#define CX_GENERATE_ATOMICS(type, short_type, lg_size)                                         \
    typedef struct {                                                                           \
        CX_ATOMIC_IL_ALIGN(lg_size) CX_ATOMIC_IL_REPR(lg_size) repr;                           \
    } cx_atomic_##short_type;                                                                  \
                                                                                               \
    _meta_inline type _atomicLoad_##short_type(const cx_atomic_##short_type* a,                \
                                               AtmoicMemoryOrder mo)                           \
    {                                                                                          \
        return (type)CX_ATOMIC_IL_LOAD(lg_size)(&a->repr, mo);                                 \
    }                                                                                          \
                                                                                               \
    _meta_inline void _atomicStore_##short_type(cx_atomic_##short_type* a,                     \
                                                type val,                                      \
                                                AtmoicMemoryOrder mo)                          \
    {                                                                                          \
        CX_ATOMIC_IL_STORE(lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val, mo);            \
    }                                                                                          \
                                                                                               \
    _meta_inline type                                                                          \
    _atomicExchange_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo)    \
    {                                                                                          \
        return (type)CX_ATOMIC_IL_NAME(_InterlockedExchange,                                   \
                                       lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val);    \
    }                                                                                          \
                                                                                               \
    _meta_inline bool _atomicCompareExchange_weak_##short_type(cx_atomic_##short_type* a,      \
                                                               type* expected,                 \
                                                               type desired,                   \
                                                               AtmoicMemoryOrder success_mo,   \
                                                               AtmoicMemoryOrder failure_mo)   \
    {                                                                                          \
        CX_ATOMIC_IL_REPR(lg_size) e = (CX_ATOMIC_IL_REPR(lg_size)) * expected;                \
        CX_ATOMIC_IL_REPR(lg_size) d = (CX_ATOMIC_IL_REPR(lg_size))desired;                    \
        CX_ATOMIC_IL_REPR(lg_size)                                                             \
        old = CX_ATOMIC_IL_NAME(_InterlockedCompareExchange, lg_size)(&a->repr, d, e);         \
        if (old == e) {                                                                        \
            return true;                                                                       \
        } else {                                                                               \
            *expected = (type)old;                                                             \
            return false;                                                                      \
        }                                                                                      \
    }                                                                                          \
                                                                                               \
    _meta_inline bool _atomicCompareExchange_strong_##short_type(cx_atomic_##short_type* a,    \
                                                                 type* expected,               \
                                                                 type desired,                 \
                                                                 AtmoicMemoryOrder success_mo, \
                                                                 AtmoicMemoryOrder failure_mo) \
    {                                                                                          \
        /* We implement the weak version with strong semantics. */                             \
        return _atomicCompareExchange_weak_##short_type(a,                                     \
                                                        expected,                              \
                                                        desired,                               \
                                                        success_mo,                            \
                                                        failure_mo);                           \
    }

#define CX_EXTERN_ATOMICS(type, short_type)                                                     \
    extern inline type _atomicLoad_##short_type(const cx_atomic_##short_type* a,                \
                                                AtmoicMemoryOrder mo);                          \
                                                                                                \
    extern inline void _atomicStore_##short_type(cx_atomic_##short_type* a,                     \
                                                 type val,                                      \
                                                 AtmoicMemoryOrder mo);                         \
                                                                                                \
    extern inline type _atomicExchange_##short_type(cx_atomic_##short_type* a,                  \
                                                    type val,                                   \
                                                    AtmoicMemoryOrder mo);                      \
                                                                                                \
    extern inline bool _atomicCompareExchange_weak_##short_type(cx_atomic_##short_type* a,      \
                                                                type* expected,                 \
                                                                type desired,                   \
                                                                AtmoicMemoryOrder success_mo,   \
                                                                AtmoicMemoryOrder failure_mo);  \
                                                                                                \
    extern inline bool _atomicCompareExchange_strong_##short_type(cx_atomic_##short_type* a,    \
                                                                  type* expected,               \
                                                                  type desired,                 \
                                                                  AtmoicMemoryOrder success_mo, \
                                                                  AtmoicMemoryOrder failure_mo);

#define CX_GENERATE_INT_ATOMICS(type, short_type, lg_size)                                      \
    CX_GENERATE_ATOMICS(type, short_type, lg_size)                                              \
                                                                                                \
    _meta_inline type                                                                           \
        _atomicFetchAdd_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo) \
    {                                                                                           \
        return (type)CX_ATOMIC_IL_NAME(_InterlockedExchangeAdd,                                 \
                                       lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val);     \
    }                                                                                           \
                                                                                                \
    _meta_inline type                                                                           \
        _atomicFetchSub_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo) \
    {                                                                                           \
        /*                                                                                      \
         * MSVC warns on negation of unsigned operands, but for us it                           \
         * gives exactly the right semantics (MAX_TYPE + 1 - operand).                          \
         */                                                                                     \
        __pragma(warning(push))                                                                 \
            __pragma(warning(disable : 4146)) return _atomicFetchAdd_##short_type(a, -val, mo); \
        __pragma(warning(pop))                                                                  \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchAnd_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo) \
    {                                                                                           \
        return (type)CX_ATOMIC_IL_NAME(_InterlockedAnd,                                         \
                                       lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val);     \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchOr_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo)  \
    {                                                                                           \
        return (type)CX_ATOMIC_IL_NAME(_InterlockedOr,                                          \
                                       lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val);     \
    }                                                                                           \
    _meta_inline type                                                                           \
        _atomicFetchXor_##short_type(cx_atomic_##short_type* a, type val, AtmoicMemoryOrder mo) \
    {                                                                                           \
        return (type)CX_ATOMIC_IL_NAME(_InterlockedXor,                                         \
                                       lg_size)(&a->repr, (CX_ATOMIC_IL_REPR(lg_size))val);     \
    }

#define CX_EXTERN_INT_ATOMICS(type, short_type)                               \
    CX_EXTERN_ATOMICS(type, short_type)                                       \
                                                                              \
    extern inline type atomicFetchAdd_##short_type(cx_atomic_##short_type* a, \
                                                   type val,                  \
                                                   AtmoicMemoryOrder mo);     \
                                                                              \
    extern inline type atomicFetchSub_##short_type(cx_atomic_##short_type* a, \
                                                   type val,                  \
                                                   AtmoicMemoryOrder mo);     \
    extern inline type atomicFetchAnd_##short_type(cx_atomic_##short_type* a, \
                                                   type val,                  \
                                                   AtmoicMemoryOrder mo);     \
    extern inline type atomicFetchOr_##short_type(cx_atomic_##short_type* a,  \
                                                  type val,                   \
                                                  AtmoicMemoryOrder mo);      \
    extern inline type atomicFetchXor_##short_type(cx_atomic_##short_type* a, \
                                                   type val,                  \
                                                   AtmoicMemoryOrder mo);

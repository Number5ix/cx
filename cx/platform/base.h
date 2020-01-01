#pragma once

#if defined(_MSC_VER)

#define _COMPILER_MSVC 1

// C11 thread local
#define _Thread_local __declspec(thread)

// We use inline functions for metaprogramming and really, REALLY want them
// to be inlined
#define _meta_inline __forceinline
#define _no_inline __declspec(noinline)
#define stackAlloc(sz) _alloca(sz)

// Sometimes things need to be aligned precisely
#define alignMem(bytes) __declspec(align(bytes))

#if defined(_WIN32)
#define _PLATFORM_WIN 1
#else
// this should be impossible
#error Unsupported operating system ???
#endif

#if defined(_M_IX86)
#define _ARCH_X86 1
#define _32BIT 1
#elif defined (_M_X64)
#define _ARCH_X64 1
#define _64BIT 1
#else
#error Unsupported architecture
#endif

#elif defined (__clang__)

#define _COMPILER_CLANG 1

// C11 thread local is already supported

// We use inline functions for metaprogramming and really, REALLY want them
// to be inlined
#define _meta_inline __attribute__((always_inline)) inline
#define _no_inline __attribute__((noinline))
#define stackAlloc(sz) alloca(sz)

// Sometimes things need to be aligned precisely
#define alignMem(bytes) __attribute__((aligned(bytes)))

#if defined(__linux__)
#define _PLATFORM_UNIX 1
#define _PLATFORM_LINUX 1
#elif defined(__FreeBSD__)
#define _PLATFORM_UNIX 1
#define _PLATFORM_FREEBSD 1
#else
#error Unsupported operating system
#endif

#if defined(__x86_32__)
#define _ARCH_X86 1
#define _32BIT 1
#elif defined(__x86_64__)
#define _ARCH_X64 1
#define _64BIT 1
#else
#error Unsupported architectures
#endif

#else
#error Unsupported compiler
#endif

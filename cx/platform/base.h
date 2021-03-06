#pragma once

#if defined(_MSC_VER)

#define _COMPILER_MSVC 1

// C11 thread local
#define _Thread_local __declspec(thread)

// C11 static assert
#define _Static_assert static_assert

// We use inline functions for metaprogramming and really, REALLY want them
// to be inlined
#define _meta_inline __forceinline
#define _no_inline __declspec(noinline)
#define stackAlloc(sz) _alloca(sz)

// Sometimes things need to be aligned precisely
#define alignMem(bytes) __declspec(align(bytes))

#if defined(_WIN32)
#define _PLATFORM_WIN 1
#define _PLATFORM_STR "win"
#else
// this should be impossible
#error Unsupported operating system ???
#endif

#if defined(_M_IX86)
#define _ARCH_X86 1
#define _ARCH_STR "x86"
#define _32BIT 1
#elif defined (_M_X64)
#define _ARCH_X64 1
#define _ARCH_STR "x64"
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
#define _PLATFORM_STR "linux"
#elif defined(__FreeBSD__)
#define _PLATFORM_UNIX 1
#define _PLATFORM_FBSD 1
#define _PLATFORM_STR "fbsd"
#else
#error Unsupported operating system
#endif

#if defined(__x86_32__)
#define _ARCH_X86 1
#define _ARCH_STR "x86"
#define _32BIT 1
#elif defined(__x86_64__)
#define _ARCH_X64 1
#define _ARCH_STR "x64"
#define _64BIT 1
#elif defined (__aarch64__)
#define _ARCH_ARM64 1
#define _ARCH_STR "arm64"
#define _64BIT 1
#else
#error Unsupported architecture
#endif

#else
#error Unsupported compiler
#endif

#if defined(_32BIT)
#define _word_align alignMem(4)
#elif defined(_64BIT)
#define _word_align alignMem(8)
#else
#error Unsupported architecture
#endif

#define _PLATFORM_ARCH_STR (_PLATFORM_STR "-" _ARCH_STR)

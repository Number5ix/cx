#pragma once

/// @file base.h
/// @brief Compiler and platform detection macros

/// @defgroup platform_base Base Platform Definitions
/// @ingroup platform
/// @{
///
/// Core platform and compiler detection macros used throughout the CX framework.
/// This header defines:
/// - Compiler identification macros (`_COMPILER_MSVC`, `_COMPILER_CLANG`, `_COMPILER_GCC`)
/// - Platform identification macros (`_PLATFORM_WIN`, `_PLATFORM_UNIX`, `_PLATFORM_LINUX`,
/// `_PLATFORM_FBSD`, `_PLATFORM_WASM`)
/// - Architecture macros (`_ARCH_X86`, `_ARCH_X64`, `_ARCH_ARM64`, `_ARCH_WASM`)
/// - Bit width macros (`_32BIT`, `_64BIT`)
/// - Portability macros for inline, alignment, thread-local storage, etc.
///
/// **Compiler Detection:**
/// - `_COMPILER_MSVC` - Microsoft Visual C++
/// - `_COMPILER_CLANG` - Clang/LLVM
/// - `_COMPILER_GCC` - GNU GCC
///
/// **Platform Detection:**
/// - `_PLATFORM_WIN` - Windows
/// - `_PLATFORM_UNIX` - Unix-like systems (Linux, FreeBSD)
/// - `_PLATFORM_LINUX` - Linux specifically
/// - `_PLATFORM_FBSD` - FreeBSD specifically
/// - `_PLATFORM_WASM` - WebAssembly (Emscripten)
///
/// **Architecture Detection:**
/// - `_ARCH_X86` - 32-bit x86
/// - `_ARCH_X64` - 64-bit x86-64
/// - `_ARCH_ARM64` - 64-bit ARM
/// - `_ARCH_WASM` - WebAssembly
///
/// **Portability Macros:**
/// - `_meta_inline` - Force inline for metaprogramming functions
/// - `_no_inline` - Prevent function inlining
/// - `_no_return` - Function never returns
/// - `alignMem(bytes)` - Align variable/struct to byte boundary
/// - `stackAlloc(sz)` - Allocate on stack (alloca wrapper)
/// - `_Thread_local` - Thread-local storage (C11 compatibility)
/// - `_Pure` - Function has no side effects
/// - `_Analysis_noreturn` - Static analysis hint for noreturn
///
/// **String Constants:**
/// - `_PLATFORM_STR` - Platform name string ("win", "linux", "fbsd", "wasm")
/// - `_ARCH_STR` - Architecture string ("x86", "x64", "arm64", "wasm32")
/// - `_PLATFORM_ARCH_STR` - Combined platform-architecture (e.g., "win-x64")
///
/// @defgroup platform_macros CX Platform Macros
/// @ingroup platform_base
/// @{
///
/// Cross-platform macros for controlling code generation and memory layout.
/// These macros provide a consistent interface across compilers for common
/// optimizations and attributes.
///
/// @section cx_platform_stackalloc stackAlloc(size)
///
/// Allocate memory on the stack (equivalent to alloca).
///
/// Allocates the specified number of bytes on the current stack frame. The memory
/// is automatically freed when the function returns. This is faster than heap
/// allocation but should be used carefully as it can cause stack overflow if
/// used with large or unbounded sizes.
///
/// **Warning**: The allocated memory is only valid until the function returns.
/// Do not return pointers to stack-allocated memory.
///
/// @param sz Number of bytes to allocate
/// @return Pointer to allocated stack memory
///
/// Example:
/// @code
///   void processData(size_t count) {
///       int *temp = stackAlloc(count * sizeof(int));
///       // Use temp array...
///       // Automatically freed on return
///   }
/// @endcode
///
/// @section cx_platform_meta_inline _meta_inline
///
/// Force a function to be inlined.
///
/// Apply to functions that are critical to performance or used for metaprogramming
/// where inlining is essential for proper code generation. The compiler will
/// aggressively inline these functions even at low optimization levels.
///
/// **Note**: Despite the leading underscore, this macro is part of the public API.
/// It's prefixed with underscore to match C11 conventions and avoid conflicts
/// with user code.
///
/// Example:
/// @code
///   _meta_inline int add(int a, int b) {
///       return a + b;
///   }
/// @endcode
///
/// @section cx_platform_no_inline _no_inline
///
/// Prevent a function from being inlined.
///
/// Apply to functions where inlining would be detrimental (e.g., rarely called
/// error handlers, functions with large bodies, or when you need a stable
/// function address for debugging).
///
/// Example:
/// @code
///   _no_inline void reportError(const char *msg) {
///       // Error handling code...
///   }
/// @endcode
///
/// @section cx_platform_no_return _no_return
///
/// Indicate that a function never returns to its caller.
///
/// Apply to functions that always exit the program, throw exceptions, or
/// perform long jumps. This helps the compiler optimize code paths and
/// eliminates warnings about missing return statements in calling code.
///
/// Example:
/// @code
///   _no_return void fatal(const char *msg) {
///       fprintf(stderr, "Fatal: %s\n", msg);
///       exit(1);
///   }
/// @endcode
///
/// @section cx_platform_alignmem alignMem(bytes)
///
/// Align a variable or struct to a specific byte boundary.
///
/// Ensures that the declared variable or type is aligned to the specified
/// power-of-2 byte boundary. This is useful for SIMD operations, cache line
/// alignment, or interfacing with hardware that requires specific alignment.
///
/// @param bytes Alignment in bytes (must be power of 2)
///
/// Example:
/// @code
///   // Align to 16-byte boundary for SIMD
///   alignMem(16) float vec[4];
///
///   // Cache line aligned structure
///   typedef struct alignMem(64) {
///       atomic(int) counter;
///       char padding[60];
///   } CacheLinePadded;
/// @endcode
///
/// @}  // end of platform_macros group

#if defined(_MSC_VER)

#include <intrin.h>
#include <assert.h>

#define _COMPILER_MSVC 1

// C11 thread local
#define _Thread_local __declspec(thread)

// C11 static assert
#define _Static_assert static_assert

// Nullability qualifiers
// SAL is much more powerful than these
#define _Nonnull
#define _Nullable
#define _Null_unspecified

// Pure functions
#define _Pure

#define _Analysis_noreturn _Analysis_noreturn_

// We use inline functions for metaprogramming and really, REALLY want them
// to be inlined
#define _meta_inline __forceinline
#define _no_inline __declspec(noinline)
#pragma warning (disable:6255)
#define stackAlloc(sz) _alloca(sz)

// Sometimes things need to be aligned precisely
#define alignMem(bytes) __declspec(align(bytes))

// Hint that some functions never return
#define _no_return __declspec(noreturn)

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

#elif defined (__clang__) || (defined(__GNUC__) && __GNUC__ > 4)

#if defined(__clang__)
#define _COMPILER_CLANG 1

// Nullability qualifiers
#pragma clang diagnostic ignored "-Wnullability-completeness"

#elif defined(__GNUC__)
#define _COMPILER_GCC 1

// Nullability qualifiers
#define _Nonnull
#define _Nullable
#define _Null_unspecified

#endif

// Pure functions
#define _Pure __attribute__((pure))

#if defined(__clang_analyzer__)
#define _Analysis_noreturn __attribute__((noreturn))
#else
#define _Analysis_noreturn
#endif

// C11 thread local is already supported

// We use inline functions for metaprogramming and really, REALLY want them
// to be inlined
#define _meta_inline __attribute__((always_inline)) inline
#define _no_inline __attribute__((noinline))
#define stackAlloc(sz) alloca(sz)

// Sometimes things need to be aligned precisely
#define alignMem(bytes) __attribute__((aligned(bytes)))

// Hint that some functions never return
#define _no_return __attribute__((noreturn))

#if defined(__linux__)
#include <alloca.h>
#define _PLATFORM_UNIX 1
#define _PLATFORM_LINUX 1
#define _PLATFORM_STR "linux"
#elif defined(__FreeBSD__)
#define _PLATFORM_UNIX 1
#define _PLATFORM_FBSD 1
#define _PLATFORM_STR "fbsd"
#elif defined(__EMSCRIPTEN__)
#define _PLATFORM_WASM 1
#define _PLATFORM_STR "wasm"
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
#elif defined(__EMSCRIPTEN__)
#define _ARCH_WASM 1
#define _ARCH_STR "wasm32"
#define _32BIT 1
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

/// @}  // end of platform_base group

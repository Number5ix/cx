#pragma once

/// @file meta.h
/// @brief Metaprogramming and control flow macros

/// @defgroup meta Metaprogramming
/// @{
///
/// Advanced C metaprogramming macros for complex control flow patterns.
///
/// The meta module provides compile-time and runtime control flow facilities built on creative
/// uses of for loops and other C constructs. The primary tool is blkWrap(), which enables
/// automatic cleanup and resource management patterns similar to RAII in C++.
///
/// **Primary Use Case - blkWrap():**
///
/// The blkWrap() macro executes code before and after a block, guaranteeing cleanup even if
/// the block exits early with `break` or `continue`. This is lightweight and suitable for most situations:
/// @code
///   blkWrap(mutex_lock(&mtx), mutex_unlock(&mtx)) {
///       // critical section - mutex is always unlocked on exit
///       if (condition) break;  // early exit still runs cleanup
///   }
/// @endcode
///
/// **Advanced Features:**
///
/// For more complex scenarios involving nested unwinding or exception-like behavior, the module
/// also provides protected blocks (pblock) and try/catch constructs (ptTry/ptCatch/ptFinally).
/// However, these are heavyweight operations with significant performance implications:
/// - Add measurable execution overhead from setjmp/longjmp
/// - Inhibit compiler optimizations throughout the protected region
/// - Consume large amounts of stack space for each nested level
///
/// Use these advanced features sparingly, only when simpler alternatives are insufficient.
///
/// **Compile-Time Feature Inhibition:**
///
/// The module includes a facility for compile-time checks that prevent use of specific language
/// features (like `return`) within certain blocks, helping catch errors early.
///
/// See @ref meta_block for block wrapping and basic control flow, @ref meta_pblock for protected blocks,
/// and @ref meta_ptry for exception handling.

#include <cx/cx.h>

#include <cx/meta/block.h>
#include <cx/meta/pblock.h>
#include <cx/meta/ptry.h>

/// @}

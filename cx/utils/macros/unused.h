/// @file unused.h
/// @brief Macros for suppressing compiler warnings
///
/// @addtogroup utils_macros
/// @{

#pragma once

/// Mark a variable as intentionally unused without evaluating it
///
/// This macro silences "unused variable" warnings without actually evaluating the expression.
/// The expression is used in an unevaluated context (the false branch of a ternary operator)
/// so it has no runtime cost and doesn't trigger side effects.
///
/// The GCC implementation uses a helper function to work around compiler-specific issues.
///
/// Example:
/// @code
///   int result = someFunction();
///   unused_noeval(result);  // Silence warning without evaluating
/// @endcode
#if !defined(__clang__) && defined(__GNUC__)
#define unused_noeval(x) (_unused_helper(true ? (intptr_t)0 : (intptr_t)(x)))
#include <stdint.h>
__attribute__((always_inline)) inline int _unused_helper(intptr_t _dummy)
{
    (void)_dummy;
    return 0;
}
#else
#define unused_noeval(x) (true ? (void)0 : (void)(x))
#endif

/// No-operation statement
///
/// A statement that does nothing, useful for satisfying syntax requirements or as a
/// placeholder. Commonly used in macro definitions where a statement is required but no
/// action is needed.
///
/// Example:
/// @code
///   #define DEBUG_LOG(msg) nop_stmt  // Debug logging disabled
/// @endcode
#define nop_stmt ((void)0)

/// @}

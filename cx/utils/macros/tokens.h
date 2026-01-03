/// @file tokens.h
/// @brief Token pasting and stringification macros
///
/// @addtogroup utils_macros
/// @{

#pragma once

/// Concatenate two tokens after macro expansion
///
/// Uses an extra level of indirection to ensure both tokens are expanded before
/// concatenation. Without this indirection, the `##` operator would paste tokens
/// before they are expanded.
///
/// Example:
/// @code
///   #define PREFIX foo
///   tokconcat(PREFIX, _bar)  // Expands to: foo_bar
/// @endcode
#define tokconcat(x, y)         _tokconcat_actual(x, y)
#define _tokconcat_actual(x, y) x##y

/// Concatenate two tokens (alias for tokconcat)
#define tokcat2(x, y) _tokconcat_actual(x, y)

/// Concatenate three tokens
#define tokcat3(x1, x2, x3) tokcat2(tokcat2(x1, x2), x3)

/// Concatenate four tokens
#define tokcat4(x1, x2, x3, x4) tokcat2(tokcat3(x1, x2, x3), x4)

/// Concatenate five tokens
#define tokcat5(x1, x2, x3, x4, x5) tokcat2(tokcat4(x1, x2, x3, x4), x5)

/// Concatenate two tokens with underscore separator after macro expansion
///
/// Similar to tokconcat but inserts an underscore between the tokens.
///
/// Example:
/// @code
///   #define PREFIX foo
///   tokconcatU(PREFIX, bar)  // Expands to: foo_bar
/// @endcode
#define tokconcatU(x, y)         _tokconcatU_actual(x, y)
#define _tokconcatU_actual(x, y) x##_##y

/// Concatenate two tokens with underscore (alias for tokconcatU)
#define tokcatU2(x, y) _tokconcatU_actual(x, y)

/// Concatenate three tokens with underscores
#define tokcatU3(x1, x2, x3) tokcatU2(tokcatU2(x1, x2), x3)

/// Concatenate four tokens with underscores
#define tokcatU4(x1, x2, x3, x4) tokcatU2(tokcatU3(x1, x2, x3), x4)

/// Concatenate five tokens with underscores
#define tokcatU5(x1, x2, x3, x4, x5) tokcatU2(tokcatU4(x1, x2, x3, x4), x5)

/// Convert a token to a string literal after macro expansion
///
/// Uses an extra level of indirection to ensure the token is expanded before
/// stringification. The `#` operator would stringify the token name without
/// expansion otherwise.
///
/// Example:
/// @code
///   #define VALUE 123
///   tokstring(VALUE)  // Expands to: "123"
/// @endcode
#define tokstring(x)         _tokstring_actual(x)
#define _tokstring_actual(x) #x

/// Force macro expansion of arguments
///
/// This utility macro forces the preprocessor to fully expand its arguments.
/// It's primarily used to work around MSVC preprocessor quirks and to separate
/// tokens in macro processing.
///
/// Example:
/// @code
///   #define PAIR (a, b)
///   tokeval PAIR  // Expands to: a, b
/// @endcode
#define tokeval(...) __VA_ARGS__

/// @}

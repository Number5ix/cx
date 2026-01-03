/// @file compare.h
/// @brief Comparison and clamping macros
///
/// @defgroup utils_compare Comparison Macros
/// @ingroup utils
/// @{
///
/// Provides commonly-used comparison and value clamping macros for constraining
/// values to specific ranges or selecting minimum/maximum values.
///
/// Available macros:
/// - `clamplow(val, low)` - Clamps value to minimum bound
/// - `clamphigh(val, high)` - Clamps value to maximum bound  
/// - `clamp(val, low, high)` - Clamps value to range [low, high]
/// - `min(a, b)` - Returns minimum of two values
/// - `max(a, b)` - Returns maximum of two values
///
/// **Warning**: These are macros that evaluate arguments multiple times. Avoid
/// using expressions with side effects (e.g., `clamp(x++, 0, 10)`).

#pragma once

#define clamplow(val, low) ((val)>(low)?(val):(low))
#define clamphigh(val, high) ((val)<(high)?(val):(high))
#define clamp(val, low, high) ((val)>(low)?((val)<(high)?(val):(high)):(low))

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#endif

/// @}

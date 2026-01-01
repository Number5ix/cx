/// @file closure.h
/// @brief Closures and callback chains aggregated header

/// @defgroup closure Closures and Callbacks
/// @{
///
/// Function closures with captured environment and thread-safe callback chains.
///
/// Closures provide a way to capture variables (environment) with a function pointer for
/// later execution. This is more flexible than plain function pointers as it allows you
/// to pass context data without global variables or manual context structs.
///
/// Closure chains provide thread-safe lists of closures, ideal for implementing event
/// systems with multiple callbacks.
///
/// Key features:
/// - Capture arguments as variants (stvar) with the closure
/// - Thread-safe attachment/detachment for closure chains
/// - Optional tokens for selective detachment from chains
/// - One-shot execution mode for closure chains
///
/// @see @ref closure_basic "Basic Closures"
/// @see @ref closure_chain "Closure Chains"

#pragma once

#include <cx/closure/closure.h>
#include <cx/closure/cchain.h>

/// @}
// end of closure group

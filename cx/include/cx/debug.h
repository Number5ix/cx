#pragma once

/// @file debug.h
/// @brief Debug utilities and diagnostic tools

/// @defgroup debug Debug
/// @{
///
/// Debug utilities including assertions, crash handlers, stack traces, and diagnostic logging.
///
/// The debug module provides tools for development and troubleshooting:
/// - Runtime assertions with detailed failure reporting
/// - Crash handlers and signal management
/// - Stack trace generation and symbolication
/// - Debug logging with filtering capabilities
/// - Black box recording for post-mortem analysis

#include <cx/debug/assert.h>
#include <cx/debug/blackbox.h>
#include <cx/debug/crash.h>
#include <cx/debug/dbglog.h>
#include <cx/debug/stacktrace.h>

/// @}  // end of debug group

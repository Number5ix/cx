#pragma once

/// @file dbglog.h
/// @brief Memory-buffered debug log for crash dump analysis
///
/// @defgroup debug_log Debug Log Buffer
/// @ingroup debug
/// @{
///
/// Circular memory buffer that captures recent log messages for crash dump analysis.
///
/// The debug log is a fixed 64KB circular buffer registered as a log destination
/// that automatically includes itself in crash dumps. This allows crash analyzers
/// to see the last ~64KB of log output leading up to a crash, providing critical
/// context for debugging.
///
/// **Key features:**
/// - Fixed 64KB size (`DBGLOG_SIZE`)
/// - Circular buffer overwrites oldest messages when full
/// - Automatically registered with crash handler when enabled
/// - Global pointer (`dbgLog`) for debugger access
/// - Configurable log level filtering
///
/// **Typical workflow:**
/// @code
///   // Enable debug logging at startup
///   dbgLogEnable(LOG_Verbose);
///   
///   // Normal logging calls automatically go to debug buffer
///   logInfo(_S"Application started");
///   logDebug(_S"Loading configuration");
///   
///   // On crash, the buffer contents are included in crash dumps
///   // Disable when no longer needed (rare)
///   dbgLogDisable();
/// @endcode
///
/// The buffer uses the compact log format from @ref log_membuf and is
/// visible to both debuggers (via `dbgLog` pointer) and crash dump tools.

#include <cx/cx.h>

CX_C_BEGIN

/// Size of the debug log buffer in bytes (64KB)
#define DBGLOG_SIZE 65535

/// Pointer to debug log buffer (NULL when disabled)
///
/// Global pointer to the circular log buffer. Visible to debuggers and
/// automatically included in crash dumps when enabled. Points to NULL
/// when debug logging is not active.
extern char *dbgLog;

/// Enable debug log buffer and register with logging system
/// @param level Maximum log level to capture (e.g., LOG_Verbose, LOG_Debug, LOG_Info)
///
/// Allocates a 64KB circular buffer, registers it as a log destination, and
/// includes it in crash dumps. If debug logging is already enabled, disables
/// it first before re-enabling with the new level.
///
/// All log messages at or below the specified level (from all non-private
/// categories) will be written to the buffer in addition to other destinations.
void dbgLogEnable(int level);

/// Disable debug log buffer and unregister from logging system
///
/// Unregisters the debug log destination, removes the buffer from crash dumps,
/// and frees the buffer memory. Sets `dbgLog` to NULL. Safe to call when
/// already disabled (no-op).
void dbgLogDisable();

/// @}  // end of debug_log group

CX_C_END

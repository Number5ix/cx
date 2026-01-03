#pragma once
/// @file log/logdefer.h
/// @brief Deferred logging for capturing early startup logs

/// @defgroup log_defer Deferred Logging
/// @ingroup log
/// @{
///
/// Deferred logging captures log messages in a temporary buffer during application
/// startup, then transfers them to a real destination once it becomes available.
/// This ensures no logs are lost during initialization before file systems or other
/// resources are ready.
///
/// Messages are stored in memory with full timestamps and batch information, then
/// replayed to the final destination atomically when it's registered. The defer
/// buffer is destroyed during the transfer.
///
/// **Basic Usage:**
/// @code
///   // Very early in startup - before file system is ready
///   LogDeferData *deferdata = logDeferCreate();
///   LogDest *deferdest = logDeferRegister(LOG_Info, NULL, deferdata);
///   
///   // Application can log immediately
///   logStr(Info, _S"Starting initialization...");
///   logStr(Info, _S"Loading configuration...");
///   
///   // Later - file system is ready, transfer deferred logs
///   LogFileData *lfd = logfileCreate(vfs, _S"app.log", &cfg);
///   LogDest *dest = logfileRegisterWithDefer(LOG_Info, NULL, lfd, deferdest);
///   
///   // All early logs now written to file, logging continues normally
///   logStr(Info, _S"Initialization complete");
/// @endcode

#include <cx/log/log.h>

/// Opaque handle for deferred log buffer
typedef struct LogDeferData LogDeferData;

// ============================================================================
// High Level Interface
// ============================================================================

/// Create a deferred logging buffer
///
/// Allocates a temporary buffer for storing log messages that will be transferred
/// to a real destination later. The buffer grows dynamically to accommodate all
/// deferred messages.
///
/// @return Deferred logging handle, or NULL on allocation failure
/// @code
///   LogDeferData *deferdata = logDeferCreate();
/// @endcode
_Ret_valid_ LogDeferData* logDeferCreate(void);

/// Register a deferred logging destination
///
/// Registers a destination that buffers log messages in memory. Messages are held
/// until the defer destination is transferred to a real destination using
/// logfileRegisterWithDefer(), logmembufRegisterWithDefer(), or
/// logRegisterDestWithDefer().
///
/// @param level Maximum log level to capture
/// @param catfilter Category filter, or NULL for all non-private categories
/// @param deferdata Deferred buffer handle from logDeferCreate()
/// @return Destination handle for later transfer, or NULL on failure
/// @code
///   LogDeferData *deferdata = logDeferCreate();
///   LogDest *deferdest = logDeferRegister(LOG_Info, NULL, deferdata);
/// @endcode
LogDest* logDeferRegister(int level, _In_opt_ LogCategory* catfilter, _In_ LogDeferData* deferdata);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These functions are typically used through the high-level wrappers above
// or through destination-specific "WithDefer" registration functions.

/// Log message callback for deferred destinations
///
/// Stores a log message in the defer buffer for later replay. Messages are stored
/// with their original timestamps and batch IDs preserved.
///
/// @param level Log severity level
/// @param cat Category, or NULL for default
/// @param timestamp Wall clock timestamp
/// @param msg Log message text
/// @param batchid Batch identifier for grouping
/// @param userdata LogDeferData pointer from logDeferCreate()
void logDeferDest(int level, _In_opt_ LogCategory* cat, int64 timestamp, _In_opt_ strref msg,
                  uint32 batchid, _In_opt_ void* userdata);

/// Register a destination and transfer deferred logs atomically
///
/// Registers a new destination with custom callbacks while simultaneously flushing
/// all deferred logs to it. The defer destination is unregistered and its buffer
/// destroyed during this process. All deferred messages are replayed in order with
/// their original timestamps and batch information.
///
/// This is a low-level function; prefer using destination-specific wrappers like
/// logfileRegisterWithDefer() or logmembufRegisterWithDefer().
///
/// @param maxlevel Maximum log level for new destination
/// @param catfilter Category filter for new destination, or NULL for all non-private categories
/// @param msgfunc Message callback for new destination
/// @param batchfunc Optional batch completion callback for new destination
/// @param closefunc Optional cleanup callback for new destination
/// @param userdata User context for new destination callbacks
/// @param deferdest Deferred destination to transfer and destroy (becomes invalid)
/// @return New destination handle, or NULL on failure
/// @code
///   // Custom destination with deferred log transfer
///   LogDest *dest = logRegisterDestWithDefer(
///       LOG_Info, NULL,
///       myMsgFunc, myBatchFunc, myCloseFunc, &mydata,
///       deferdest
///   );
/// @endcode
_Ret_opt_valid_ LogDest*
logRegisterDestWithDefer(int maxlevel, _In_opt_ LogCategory* catfilter, _In_ LogDestMsg msgfunc,
                         _In_opt_ LogDestBatchDone batchfunc, _In_opt_ LogDestClose closefunc,
                         _In_opt_ void* userdata, _In_opt_ _Post_invalid_ LogDest* deferdest);

/// @}  // end of log_defer group


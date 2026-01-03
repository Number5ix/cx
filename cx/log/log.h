#pragma once
/// @file log/log.h
/// @brief Core logging system API

/// @defgroup log_core Core Logging API
/// @ingroup log
/// @{
///
/// The CX logging system provides asynchronous, multi-threaded logging with support for
/// multiple destinations, log levels, categories, and batching. Logging is performed on
/// a dedicated background thread to minimize impact on application performance.
///
/// **Basic Usage:**
/// @code
///   logStr(Info, _S"Application started");
///   logFmt(Warn, _S"Invalid value: ${int}", stvar(int32, value));
/// @endcode
///
/// **Categories:**
/// Categories allow filtering logs by subsystem. Create a category once and reuse it:
/// @code
///   LogCategory *netcat = logCreateCat(_S"Network", false);
///   logStrC(Info, netcat, _S"Connection established");
/// @endcode
///
/// **Destinations:**
/// Register destinations to control where logs are written. Multiple destinations can be
/// active simultaneously:
/// @code
///   // Register a file destination
///   LogFileData *lfd = logfileCreate(vfs, _S"app.log", &config);
///   LogDest *dest = logfileRegister(LOG_Info, NULL, lfd);
///
///   // Later, unregister when done
///   logUnregisterDest(dest);
/// @endcode
///
/// **Batching:**
/// Batch multiple log messages together to ensure they appear consecutively in output:
/// @code
///   logBatchBegin();
///   logStr(Info, _S"Starting operation");
///   logStr(Info, _S"Step 1 complete");
///   logStr(Info, _S"Step 2 complete");
///   logBatchEnd();
/// @endcode

#include <cx/cx.h>
#include <cx/string/strbase.h>
#include <cx/stype/stvar.h>

/// Log severity levels
///
/// Levels are ordered from most to least severe. When registering a destination with a
/// maximum level, all messages at that level and below (more severe) will be delivered.
enum LOG_LEVEL_ENUM {
    LOG_Fatal,     ///< Fatal errors, application cannot continue
    LOG_Error,     ///< Non-fatal errors requiring attention
    LOG_Warn,      ///< Warning conditions that may indicate problems
    LOG_Notice,    ///< Normal but significant conditions
    LOG_Info,      ///< Informational messages
    LOG_Verbose,   ///< Detailed informational messages
    LOG_Diag,      ///< Release build diagnostics not normally needed
    LOG_Debug,     ///< Debug messages (compiled out of non-development builds)
    LOG_Trace,     ///< Detailed trace messages (only available in debug builds)

    LOG_Count      // Not a real level, used for array sizing
};

/// Array of log level names as strings (e.g., "Fatal", "Error", etc.)
extern strref LogLevelNames[];

/// Array of single-character log level abbreviations (e.g., "F", "E", etc.)
extern strref LogLevelAbbrev[];

/// Log category for filtering and organizing log messages
///
/// Categories allow grouping related log messages together and filtering them at
/// destinations. Create categories with logCreateCat() and use them with logStrC()
/// and logFmtC() macros.
typedef struct LogCategory {
    string name;   ///< Category name
    bool priv;     ///< Private categories are filtered out unless explicitly requested
} LogCategory;

/// Opaque handle to a registered log destination
typedef struct LogDest LogDest;

/// Default log category used when no category is specified
extern LogCategory* LogDefault;

typedef struct LogDeferData LogDeferData;

/// Create a new log category
///
/// Categories are used to organize and filter log messages by subsystem or component.
/// The same category pointer can be passed to multiple log calls.
///
/// @param name Name of the category for display and identification
/// @param priv If true, this is a private category that will be filtered out by default
/// @return Category handle, or NULL if logging system is not initialized
/// @code
///   LogCategory *netcat = logCreateCat(_S"Network", false);
///   logStrC(Info, netcat, _S"Connection established");
/// @endcode
_Ret_opt_valid_ LogCategory* logCreateCat(_In_ strref name, bool priv);

/// Callback function type for log destinations
///
/// This function is called for each log message that passes the destination's level
/// and category filters. Messages with the same batchid should be kept together when
/// possible (e.g., not split across log file rotations).
///
/// @param level Log severity level (LOG_Fatal, LOG_Error, etc.)
/// @param cat Category of the message, or NULL for default
/// @param timestamp Wall clock timestamp when message was logged
/// @param msg The log message text
/// @param batchid Opaque batch identifier for grouping related messages
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestMsg)(int level, _In_opt_ LogCategory* cat, int64 timestamp,
                           _In_opt_ strref msg, uint32 batchid, _In_opt_ void* userdata);

/// Callback function type for batch completion notification
///
/// Called after all messages in a batch have been delivered to the destination.
/// Destinations can use this to flush buffers or perform cleanup after a batch.
///
/// @param batchid The batch that was completed
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestBatchDone)(uint32 batchid, _In_opt_ void* userdata);

/// Callback function type for destination cleanup
///
/// Called when a destination is unregistered. The destination should release
/// any resources it holds.
///
/// @param userdata User-provided context pointer from logRegisterDest()
typedef void (*LogDestClose)(_In_opt_ void* userdata);

/// Register a new log destination
///
/// Registers callbacks that will receive log messages matching the specified level
/// and category filters. Multiple destinations can be registered simultaneously.
///
/// @param maxlevel Maximum log level to receive (e.g., LOG_Info receives Fatal through Info)
/// @param catfilter Category filter, or NULL to receive all non-private categories
/// @param msgfunc Callback invoked for each log message
/// @param batchfunc Optional callback invoked when a batch completes
/// @param closefunc Optional callback invoked when destination is unregistered
/// @param userdata User context pointer passed to all callbacks
/// @return Destination handle for later unregistration, or NULL on failure
/// @code
///   LogDest *dest = logRegisterDest(LOG_Info, NULL, myMsgFunc, NULL, myCloseFunc, &mydata);
/// @endcode
_Ret_opt_valid_ LogDest*
logRegisterDest(int maxlevel, _In_opt_ LogCategory* catfilter, _In_ LogDestMsg msgfunc,
                _In_opt_ LogDestBatchDone batchfunc, _In_opt_ LogDestClose closefunc,
                _In_opt_ void* userdata);

/// Unregister a log destination
///
/// Removes the destination from the logging system and calls its close callback if provided.
/// The destination handle becomes invalid after this call.
///
/// @param dhandle Destination handle returned from logRegisterDest()
/// @return true if destination was found and removed, false otherwise
bool logUnregisterDest(_Pre_valid_ _Post_invalid_ LogDest* dhandle);

/// Flush all pending log messages
///
/// Blocks until all queued log messages have been processed by all destinations.
/// Useful before critical operations or shutdown to ensure logs are written.
void logFlush(void);

/// Shutdown the logging system
///
/// Flushes all pending logs, unregisters all destinations, and invalidates all categories.
/// After shutdown, logging calls will be ignored until logRestart() is called.
void logShutdown(void);

/// Restart the logging system after shutdown
///
/// Reinitializes the logging system after a previous logShutdown() call. This allows
/// logging to resume after being explicitly stopped.
void logRestart(void);

/// Begin a log batch
///
/// Groups subsequent log messages into a batch that will be delivered together.
/// Batches can be nested; only when the outermost batch ends will messages be sent.
///
/// @code
///   logBatchBegin();
///   logStr(Info, _S"Operation started");
///   logStr(Info, _S"Step 1 complete");
///   logStr(Info, _S"Step 2 complete");
///   logBatchEnd();  // All three messages delivered together
/// @endcode
void logBatchBegin(void);

/// End a log batch
///
/// Completes a log batch started with logBatchBegin(). When the outermost batch ends,
/// all batched messages are queued for delivery to destinations.
void logBatchEnd(void);

/// @} // end of log_core group

/// @defgroup log_macros Log Message Macros
/// @ingroup log
/// @{
///
/// These macros provide the primary interface for logging messages. They compile to
/// no-ops for levels that are disabled based on DEBUG_LEVEL, ensuring zero overhead
/// for disabled log levels.
///
/// **Debug Level Filtering:**
/// - `DEBUG_LEVEL >= 2`: All levels including Trace
/// - `DEBUG_LEVEL >= 1`: Debug and above (no Trace)
/// - `DEBUG_LEVEL == 0`: Diag and above (no Debug or Trace)
///
/// **Dev variants:**
/// The `Dev` prefix variants (e.g., `logStr(DevInfo, ...)`) are only compiled in
/// development builds and map to their corresponding regular levels in production.

/// void logStr(level, str)
///
/// Log a string message using the default category
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param str String or string reference to log
/// @code
///   logStr(Info, _S"Application started");
///   logStr(Error, errorMessage);
/// @endcode
#define logStr(level, str) _logStr_##level(LOG_##level, LogDefault, str)

/// void logStrC(level, cat, str)
///
/// Log a string message with a specific category
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param cat LogCategory pointer
/// @param str String or string reference to log
/// @code
///   LogCategory *netcat = logCreateCat(_S"Network", false);
///   logStrC(Info, netcat, _S"Connection established");
/// @endcode
#define logStrC(level, cat, str) _logStr_##level(LOG_##level, cat, str)

/// void logFmt(level, fmt, ...)
///
/// Log a formatted message using the default category
/// @param level Log level without LOG_ prefix (e.g., Info, Warn, Error)
/// @param fmt Format string (see @ref string_format for format syntax)
/// @param ... Format arguments wrapped in stvar()
/// @code
///   logFmt(Info, _S"Connection from ${string}:${int}",
///          stvar(string, hostname), stvar(int32, port));
///   logFmt(Warn, _S"Invalid value: ${int}", stvar(int32, value));
/// @endcode
#define logFmt(level, fmt, ...)                    \
    _logFmt_##level(LOG_##level,                   \
                    LogDefault,                    \
                    fmt,                           \
                    count_macro_args(__VA_ARGS__), \
                    ((stvar[]) { __VA_ARGS__ }))

/// void logFmtC(level, cat, fmt, ...)
///
/// Log a formatted message with a specific category
/// @param level Log level without LOG_ prefix
/// @param cat LogCategory pointer
/// @param fmt Format string
/// @param ... Format arguments wrapped in stvar()
/// @code
///   LogCategory *dbcat = logCreateCat(_S"Database", false);
///   logFmtC(Warn, dbcat, _S"Query took ${int}ms", stvar(int32, elapsed));
/// @endcode
#define logFmtC(level, cat, fmt, ...)              \
    _logFmt_##level(LOG_##level,                   \
                    cat,                           \
                    fmt,                           \
                    count_macro_args(__VA_ARGS__), \
                    ((stvar[]) { __VA_ARGS__ }))

/// @}  // end of log_macros group

// Internal implementation functions used by macros - do not call directly
void _logStr(int level, int64 timestamp, _In_ LogCategory* cat, _In_ strref str);
void _logFmt(int level, int64 timestamp, _In_ LogCategory* cat, _In_ strref fmtstr, int n,
             _In_ stvar* args);

// Implementation macros for conditional compilation based on DEBUG_LEVEL
#if DEBUG_LEVEL >= 2
#define _logStr_Trace(level, cat, str)              _logStr(level, -1, cat, str)
#define _logFmt_Trace(level, cat, fmt, nargs, args) _logFmt(level, -1, cat, fmt, nargs, args)
#else
#define _logStr_Trace(level, cat, str)              ((void)0)
#define _logFmt_Trace(level, cat, fmt, nargs, args) ((void)0)
#endif

#if DEBUG_LEVEL >= 1
#define _logStr_Debug(level, cat, str)                _logStr(level, -1, cat, str)
#define _logFmt_Debug(level, cat, fmt, nargs, args)   _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_DevDiag(level, cat, str)              _logStr(LOG_Diag, -1, cat, str)
#define _logFmt_DevDiag(level, cat, fmt, nargs, args) _logFmt(LOG_Diag, -1, cat, fmt, nargs, args)
#define _logStr_DevVerbose(level, cat, str)           _logStr(LOG_Verbose, -1, cat, str)
#define _logFmt_DevVerbose(level, cat, fmt, nargs, args) \
    _logFmt(LOG_Verbose, -1, cat, fmt, nargs, args)
#define _logStr_DevInfo(level, cat, str)              _logStr(LOG_Info, -1, cat, str)
#define _logFmt_DevInfo(level, cat, fmt, nargs, args) _logFmt(LOG_Info, -1, cat, fmt, nargs, args)
#define _logStr_DevNotice(level, cat, str)            _logStr(LOG_Notice, -1, cat, str)
#define _logFmt_DevNotice(level, cat, fmt, nargs, args) \
    _logFmt(LOG_Notice, -1, cat, fmt, nargs, args)
#define _logStr_DevWarn(level, cat, str)               _logStr(LOG_Warn, -1, cat, str)
#define _logFmt_DevWarn(level, cat, fmt, nargs, args)  _logFmt(LOG_Warn, -1, cat, fmt, nargs, args)
#define _logStr_DevError(level, cat, str)              _logStr(LOG_Error, -1, cat, str)
#define _logFmt_DevError(level, cat, fmt, nargs, args) _logFmt(LOG_Error, -1, cat, fmt, nargs, args)
#else
#define _logStr_Debug(level, cat, str)      ((void)0)
#define _logFmt_Debug(level, cat, str)      ((void)0)
#define _logStr_DevDiag(level, cat, str)    ((void)0)
#define _logFmt_DevDiag(level, cat, str)    ((void)0)
#define _logStr_DevVerbose(level, cat, str) ((void)0)
#define _logFmt_DevVerbose(level, cat, str) ((void)0)
#define _logStr_DevInfo(level, cat, str)    ((void)0)
#define _logFmt_DevInfo(level, cat, str)    ((void)0)
#define _logStr_DevNotice(level, cat, str)  ((void)0)
#define _logFmt_DevNotice(level, cat, str)  ((void)0)
#define _logStr_DevWarn(level, cat, str)    ((void)0)
#define _logFmt_DevWarn(level, cat, str)    ((void)0)
#define _logStr_DevError(level, cat, str)   ((void)0)
#define _logFmt_DevError(level, cat, str)   ((void)0)
#endif

#define _logStr_Diag(level, cat, str)                 _logStr(level, -1, cat, str)
#define _logFmt_Diag(level, cat, fmt, nargs, args)    _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Verbose(level, cat, str)              _logStr(level, -1, cat, str)
#define _logFmt_Verbose(level, cat, fmt, nargs, args) _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Info(level, cat, str)                 _logStr(level, -1, cat, str)
#define _logFmt_Info(level, cat, fmt, nargs, args)    _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Notice(level, cat, str)               _logStr(level, -1, cat, str)
#define _logFmt_Notice(level, cat, fmt, nargs, args)  _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Warn(level, cat, str)                 _logStr(level, -1, cat, str)
#define _logFmt_Warn(level, cat, fmt, nargs, args)    _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Error(level, cat, str)                _logStr(level, -1, cat, str)
#define _logFmt_Error(level, cat, fmt, nargs, args)   _logFmt(level, -1, cat, fmt, nargs, args)
#define _logStr_Fatal(level, cat, str)                _logStr(level, -1, cat, str)
#define _logFmt_Fatal(level, cat, fmt, nargs, args)   _logFmt(level, -1, cat, fmt, nargs, args)

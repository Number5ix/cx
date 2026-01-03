#pragma once
/// @file log/logfile.h
/// @brief File-based log destination with rotation support

/// @defgroup log_file File Logging
/// @ingroup log
/// @{
///
/// File-based logging destination with support for automatic log rotation, flexible
/// formatting, and customizable output options. Files can be rotated based on size
/// or time, with configurable retention policies.
///
/// **Basic Usage:**
/// @code
///   LogFileConfig cfg = {
///       .dateFormat = LOG_DateISO,
///       .rotateMode = LOG_RotateSize,
///       .rotateSize = 10 * 1024 * 1024,  // 10MB
///       .rotateKeepFiles = 5,
///       .spacing = 2,
///       .flags = LOG_IncludeCategory
///   };
///
///   LogFileData *lfd = logfileCreate(vfs, _S"app.log", &cfg);
///   LogDest *dest = logfileRegister(LOG_Info, NULL, lfd);
///
///   // Later, unregister to close
///   logUnregisterDest(dest);
/// @endcode

#include <cx/fs/vfs.h>
#include <cx/log/log.h>

/// Timestamp format options for log file output
enum LOG_DATE_FORMATS {
    LOG_DateISO,             ///< ISO 8601 format: "2026-01-02 15:04:05"
    LOG_DateISOCompact,      ///< Compact ISO: "20260102 150405"
    LOG_DateNCSA,            ///< NCSA Common Log format: "02/Jan/2026:15:04:05 +0000"
    LOG_DateSyslog,          ///< Syslog format: "Jan  2 15:04:05"
    LOG_DateISOCompactMsec   ///< Compact ISO with milliseconds: "20260102 150405.123"
};

/// Formatting flags for log file output
enum LOG_FLAGS {
    LOG_LocalTime       = 0x0001,   ///< Use local time instead of UTC
    LOG_OmitLevel       = 0x0002,   ///< Do not include severity level
    LOG_ShortLevel      = 0x0004,   ///< Use single-character level abbreviations
    LOG_BracketLevel    = 0x0008,   ///< Enclose log level in brackets [INFO]
    LOG_JustifyLevel    = 0x0010,   ///< Make level a fixed-width column
    LOG_IncludeCategory = 0x0020,   ///< Include category name in output
    LOG_BracketCategory = 0x0040,   ///< Enclose category in brackets [Network]
    LOG_AddColon        = 0x0080,   ///< Add colon after the prefix
    LOG_CategoryFirst   = 0x0100,   ///< Category between date and level instead of at end
};

/// Log rotation mode
enum LOG_ROTATE_MODE {
    LOG_RotateSize = 1,   ///< Rotate when file exceeds rotateSize bytes
    LOG_RotateTime,       ///< Rotate at specified time of day
};

/// Configuration for file-based logging
///
/// Controls output formatting, rotation behavior, and retention policies.
typedef struct LogFileConfig {
    int dateFormat;         ///< Date format from LOG_DATE_FORMATS enum
    int rotateMode;         ///< Rotation mode from LOG_ROTATE_MODE enum
    int spacing;            ///< Number of spaces between prefix and message (default: 2)

    uint32 flags;           ///< Bitwise OR of LOG_FLAGS values

    int64 rotateSize;       ///< Size threshold for LOG_RotateSize mode (bytes)
    uint8 rotateHour;       ///< Hour for LOG_RotateTime mode (0-23)
    uint8 rotateMinute;     ///< Minute for LOG_RotateTime mode (0-59)
    uint8 rotateSecond;     ///< Second for LOG_RotateTime mode (0-59)

    int rotateKeepFiles;    ///< Minimum number of rotated files to keep (0 = unlimited)
    int64 rotateKeepTime;   ///< Minimum time to keep rotated files (0 = no time limit)
} LogFileConfig;

/// Opaque handle for file logging state
typedef struct LogFileData LogFileData;

// ============================================================================
// High Level Interface
// ============================================================================

/// Create a file logging destination
///
/// Initializes a file-based log destination with the specified configuration.
/// The file is opened immediately and created if it doesn't exist. If rotation
/// is enabled, existing rotated log files are scanned to enforce retention policies.
///
/// @param vfs Virtual filesystem to use for file operations
/// @param filename Path to the log file
/// @param config Logging configuration (copied, caller retains ownership)
/// @return File logging handle, or NULL on failure
/// @code
///   LogFileConfig cfg = {
///       .dateFormat = LOG_DateISO,
///       .rotateMode = LOG_RotateSize,
///       .rotateSize = 10 * 1024 * 1024,
///       .spacing = 2,
///       .flags = LOG_IncludeCategory | LOG_BracketLevel
///   };
///   LogFileData *lfd = logfileCreate(vfs, _S"server.log", &cfg);
/// @endcode
LogFileData* logfileCreate(_Inout_ VFS* vfs, _In_ strref filename, _In_ LogFileConfig* config);

/// Register a file destination with the logging system
///
/// Registers the file as a log destination. Messages will be formatted according
/// to the configuration and written to the file. The file destination will be
/// automatically cleaned up when unregistered.
///
/// @param maxlevel Maximum log level to write to file
/// @param catfilter Category filter, or NULL for all non-private categories
/// @param logfile File logging handle from logfileCreate()
/// @return Destination handle for later unregistration, or NULL on failure
LogDest* logfileRegister(int maxlevel, _In_opt_ LogCategory* catfilter, _In_ LogFileData* logfile);

/// Register a file destination and flush deferred logs
///
/// Atomically registers a file destination while flushing previously deferred logs
/// to it. This ensures all logs from application startup are captured even if the
/// log file couldn't be opened immediately.
///
/// @param maxlevel Maximum log level to write to file
/// @param catfilter Category filter, or NULL for all non-private categories
/// @param logfile File logging handle from logfileCreate()
/// @param deferdest Deferred destination to flush (destroyed during this call)
/// @return Destination handle for later unregistration, or NULL on failure
/// @code
///   // Early in startup - buffer logs temporarily
///   LogDeferData *deferdata = logDeferCreate();
///   LogDest *deferdest = logDeferRegister(LOG_Info, NULL, deferdata);
///
///   // Later - open file and transfer buffered logs
///   LogFileData *lfd = logfileCreate(vfs, _S"app.log", &cfg);
///   LogDest *dest = logfileRegisterWithDefer(LOG_Info, NULL, lfd, deferdest);
/// @endcode
LogDest* logfileRegisterWithDefer(int maxlevel, _In_opt_ LogCategory* catfilter,
                                  _In_ LogFileData* logfile, _In_ LogDest* deferdest);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Log message callback for file destinations
///
/// Formats and writes a log message to the file. Checks for rotation after
/// each write.
///
/// @param level Log severity level
/// @param cat Category, or NULL for default
/// @param timestamp Wall clock timestamp
/// @param msg Log message text
/// @param batchid Batch identifier for grouping
/// @param userdata LogFileData pointer from logfileCreate()
void logfileMsgFunc(int level, _In_opt_ LogCategory* cat, int64 timestamp, _In_opt_ strref msg,
                    uint32 batchid, _In_opt_ void* userdata);

/// Batch completion callback for file destinations
///
/// Flushes the file buffer to ensure batch messages are written together.
///
/// @param batchid Completed batch identifier
/// @param userdata LogFileData pointer from logfileCreate()
void logfileBatchFunc(uint32 batchid, _In_opt_ void* userdata);

/// Cleanup callback for file destinations
///
/// Closes the log file and releases resources.
///
/// @param userdata LogFileData pointer from logfileCreate()
void logfileCloseFunc(_In_opt_ void* userdata);

/// @}  // end of log_file group

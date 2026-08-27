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
/// The file is a **transport**: it owns opening, rotation and retention, and takes a serializer
/// that decides what a record looks like on the way in (see @ref log_serializer). The same
/// rotating file therefore holds text or NDJSON depending only on what it was given.
///
/// **Basic Usage:**
/// @code
///   LogFileConfig cfg = {
///       .rotateMode = LOG_RotateSize,
///       .rotateSize = 10 * 1024 * 1024,  // 10MB
///       .rotateKeepFiles = 5,
///   };
///   LogTextConfig tcfg = { .dateFormat = LOG_DateISO, .flags = LOG_IncludeChannel };
///
///   LogDest *dest = logfileRegister(LOG_Info, NULL, vfs, _SL("app.log"), &cfg,
///                                   logTextSerializer(&tcfg));
///
///   // ...or the same rotation policy, written as NDJSON
///   LogDest *jdest = logfileRegister(LOG_Info, NULL, vfs, _SL("app.ndjson"), &cfg,
///                                    logNdjsonSerializer(NULL));
///
///   // Later, unregister to close
///   logUnregisterDest(dest);
/// @endcode

#include <cx/fs/vfs.h>
#include <cx/log/log.h>
#include <cx/log/logserializer.h>

CX_C_BEGIN

/// Log rotation mode
enum LOG_ROTATE_MODE {
    LOG_RotateSize = 1,   ///< Rotate when file exceeds rotateSize bytes
    LOG_RotateTime,       ///< Rotate at specified time of day
};

/// Configuration for file-based logging
///
/// Controls rotation behavior and retention policies. Output formatting belongs to the
/// serializer the file is created with, not here.
typedef struct LogFileConfig {
    int rotateMode;         ///< Rotation mode from LOG_ROTATE_MODE enum

    uint32 flags;           ///< Bitwise OR of LOG_FLAGS values; only LOG_LocalTime is consulted,
                            ///< to decide which day a time-based rotation belongs to

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

/// Register a file logging destination
///
/// Opens the log file and registers it with the logging system in one step. The file is created
/// if it doesn't exist, and if rotation is enabled, existing rotated log files are scanned to
/// enforce retention policies. Records are serialized and written to the file; it is closed when
/// logUnregisterDest() retires the returned handle.
///
/// @param maxlevel Maximum log level to write to the file
/// @param chanfilter Channel path pattern, or NULL for every unrestricted channel
/// @param vfs Virtual filesystem to use for file operations
/// @param filename Path to the log file
/// @param config Rotation configuration (copied, caller retains ownership)
/// @param ser Serializer to render records with; **ownership transfers**, including if this
///            call fails. NULL gets a default text serializer.
/// @return Destination handle for later unregistration, or NULL if the file could not be
///         opened or the destination could not be registered
/// @code
///   LogFileConfig cfg = {
///       .rotateMode = LOG_RotateSize,
///       .rotateSize = 10 * 1024 * 1024,
///   };
///   LogTextConfig tcfg = { .flags = LOG_IncludeChannel | LOG_BracketLevel };
///   LogDest *dest = logfileRegister(LOG_Info, NULL, vfs, _SL("server.log"), &cfg,
///                                   logTextSerializer(&tcfg));
/// @endcode
LogDest* logfileRegister(int maxlevel, _In_opt_ strref chanfilter, _Inout_ VFS* vfs,
                         _In_ strref filename, _In_ const LogFileConfig* config,
                         _In_opt_ LogSerializer* ser);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Create a file logging destination
///
/// Only needed to register the file by hand with logRegisterDest() -- logfileRegister() does
/// this and the registration together. The returned handle belongs to exactly one destination:
/// it is closed and freed by logfileCloseFunc() and cannot be registered twice.
///
/// The file is opened immediately and created if it doesn't exist. If rotation is enabled,
/// existing rotated log files are scanned to enforce retention policies.
///
/// @param vfs Virtual filesystem to use for file operations
/// @param filename Path to the log file
/// @param config Rotation configuration (copied, caller retains ownership)
/// @param ser Serializer to render records with; **ownership transfers**, including if this
///            call fails. NULL gets a default text serializer.
/// @return File logging handle, or NULL on failure
/// @code
///   LogFileConfig cfg = { .rotateMode = LOG_RotateSize, .rotateSize = 10 * 1024 * 1024 };
///   LogFileData *lfd = logfileCreate(vfs, _SL("server.log"), &cfg, NULL);
///   LogDest *dest = logRegisterDest(LOG_Info, NULL, logfileMsgFunc, logfileBatchFunc,
///                                   logfileCloseFunc, lfd);
/// @endcode
LogFileData* logfileCreate(_Inout_ VFS* vfs, _In_ strref filename, _In_ const LogFileConfig* config,
                           _In_opt_ LogSerializer* ser);

/// Log message callback for file destinations
///
/// Renders and writes a log record to the file. Checks for rotation after
/// each write.
///
/// @param rec Log record to write
/// @param userdata LogFileData pointer from logfileCreate()
void logfileMsgFunc(_In_ const LogRecord* rec, _In_opt_ void* userdata);

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

CX_C_END
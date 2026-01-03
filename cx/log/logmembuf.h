#pragma once
/// @file log/logmembuf.h
/// @brief Memory buffer log destination for testing and debugging

/// @defgroup log_membuf Memory Buffer Logging
/// @ingroup log
/// @{
///
/// Memory buffer logging destination that writes log messages to a fixed-size
/// circular buffer in memory. Useful for debugging, testing, and capturing logs
/// in memory-constrained environments. When the buffer fills, new messages wrap
/// around and overwrite the oldest entries.
///
/// **Basic Usage:**
/// @code
///   // Create 4KB buffer
///   LogMembufData *lmd = logmembufCreate(4096);
///   LogDest *dest = logmembufRegister(LOG_Debug, NULL, lmd);
///   
///   // Log some messages
///   logStr(Info, _S"Test message");
///   logFlush();
///   
///   // Access the buffer contents directly
///   printf("Buffered logs:\n%.*s\n", (int)lmd->cur, lmd->buf);
///   
///   // Cleanup
///   logUnregisterDest(dest);
/// @endcode

#include <cx/log/log.h>

/// Memory buffer log destination state
///
/// Contains the circular buffer and current write position. When `cur` reaches
/// `size`, new messages wrap to the beginning. The buffer is null-terminated
/// when possible.
typedef struct LogMembufData {
    uint32 size;    ///< Total buffer size in bytes
    uint32 cur;     ///< Current write position (number of bytes written)
    char *buf;      ///< Buffer storage
} LogMembufData;

// ============================================================================
// High Level Interface
// ============================================================================

/// Create a memory buffer log destination
///
/// Allocates a fixed-size circular buffer for log messages. The buffer uses a
/// compact format with timestamps and single-character log levels. Messages
/// longer than the buffer size are truncated.
///
/// @param size Buffer size in bytes
/// @return Memory buffer handle, or NULL on allocation failure
/// @code
///   LogMembufData *lmd = logmembufCreate(8192);  // 8KB buffer
/// @endcode
_Ret_valid_
LogMembufData *logmembufCreate(uint32 size);

/// Register a memory buffer destination with the logging system
///
/// Registers the memory buffer as a log destination. Messages will be formatted
/// in a compact style and written to the circular buffer. The buffer destination
/// will be automatically cleaned up when unregistered.
///
/// @param maxlevel Maximum log level to write to buffer
/// @param catfilter Category filter, or NULL for all non-private categories
/// @param membuf Memory buffer handle from logmembufCreate()
/// @return Destination handle for later unregistration, or NULL on failure
LogDest* logmembufRegister(int maxlevel, _In_opt_ LogCategory* catfilter,
                           _In_ LogMembufData* membuf);

/// Register a memory buffer destination and flush deferred logs
///
/// Atomically registers a memory buffer destination while flushing previously
/// deferred logs to it. Useful for capturing early startup logs in memory before
/// other destinations are available.
///
/// @param maxlevel Maximum log level to write to buffer
/// @param catfilter Category filter, or NULL for all non-private categories
/// @param membuf Memory buffer handle from logmembufCreate()
/// @param deferdest Deferred destination to flush (destroyed during this call)
/// @return Destination handle for later unregistration, or NULL on failure
/// @code
///   // Early startup - defer logs
///   LogDeferData *deferdata = logDeferCreate();
///   LogDest *deferdest = logDeferRegister(LOG_Info, NULL, deferdata);
///   
///   // Later - create memory buffer and transfer logs
///   LogMembufData *lmd = logmembufCreate(4096);
///   LogDest *dest = logmembufRegisterWithDefer(LOG_Info, NULL, lmd, deferdest);
/// @endcode
LogDest* logmembufRegisterWithDefer(int maxlevel, _In_opt_ LogCategory* catfilter,
                                    _In_ LogMembufData* membuf, _In_ LogDest* deferdest);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Log message callback for memory buffer destinations
///
/// Formats and writes a log message to the circular buffer. Uses a compact
/// format: "YYYYMMDD HHMMSS L [Category]: Message\n"
///
/// @param level Log severity level
/// @param cat Category, or NULL for default
/// @param timestamp Wall clock timestamp
/// @param msg Log message text
/// @param batchid Batch identifier (unused)
/// @param userdata LogMembufData pointer from logmembufCreate()
void logmembufMsgFunc(int level, _In_opt_ LogCategory* cat, int64 timestamp, _In_opt_ strref msg,
                      uint32 batchid, _In_opt_ void* userdata);

/// Cleanup callback for memory buffer destinations
///
/// Frees the buffer and releases resources.
///
/// @param userdata LogMembufData pointer from logmembufCreate()
void logmembufCloseFunc(_In_opt_ void* userdata);

/// @}  // end of log_membuf group

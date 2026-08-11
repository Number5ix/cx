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
///   // Create 4KB buffer with the default compact text serializer
///   LogMembufData *lmd = logmembufCreate(4096, NULL);
///   LogDest *dest = logmembufRegister(LOG_Debug, NULL, lmd);
///
///   // Log some messages
///   logStr(Info, _SL("Test message"));
///   logFlush();
///
///   // Access the buffer contents directly
///   printf("Buffered logs:\n%.*s\n", (int)lmd->cur, lmd->buf);
///
///   // Cleanup
///   logUnregisterDest(dest);
/// @endcode

#include <cx/log/log.h>
#include <cx/log/logserializer.h>

CX_C_BEGIN

/// Memory buffer log destination state
///
/// Contains the circular buffer and current write position. When `cur` reaches
/// `size`, new messages wrap to the beginning. The buffer is null-terminated
/// when possible.
typedef struct LogMembufData {
    uint32 size;         ///< Total buffer size in bytes
    uint32 cur;          ///< Current write position (number of bytes written)
    char* buf;           ///< Buffer storage
    LogSerializer* ser;  ///< Owned; how a record becomes the bytes this ring stores
} LogMembufData;

// ============================================================================
// High Level Interface
// ============================================================================

/// Create a memory buffer log destination
///
/// Allocates a fixed-size circular buffer for log messages. Messages longer than
/// the buffer size are truncated.
///
/// @param size Buffer size in bytes
/// @param ser Serializer to render records with; **ownership transfers**. NULL gets a compact
///            text serializer: short levels, bracketed channels, second-resolution timestamps.
/// @return Memory buffer handle, or NULL on allocation failure
/// @code
///   LogMembufData *lmd = logmembufCreate(8192, NULL);  // 8KB buffer, compact text
///
///   // or capture structured records for a test to parse
///   LogMembufData *jmd = logmembufCreate(8192, logNdjsonSerializer(NULL));
/// @endcode
_Ret_valid_ LogMembufData* logmembufCreate(uint32 size, _In_opt_ LogSerializer* ser);

/// Register a memory buffer destination with the logging system
///
/// Registers the memory buffer as a log destination. Records are serialized and
/// written to the circular buffer. The buffer destination
/// will be automatically cleaned up when unregistered.
///
/// @param maxlevel Maximum log level to write to buffer
/// @param chanfilter Channel path pattern, or NULL for every unrestricted channel
/// @param membuf Memory buffer handle from logmembufCreate()
/// @return Destination handle for later unregistration, or NULL on failure
LogDest* logmembufRegister(int maxlevel, _In_opt_ strref chanfilter,
                           _In_ LogMembufData* membuf);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Log message callback for memory buffer destinations
///
/// Serializes a log record and appends it to the circular buffer, newline-terminated.
///
/// @param rec Log record to write
/// @param userdata LogMembufData pointer from logmembufCreate()
void logmembufMsgFunc(_In_ const LogRecord* rec, _In_opt_ void* userdata);

/// Cleanup callback for memory buffer destinations
///
/// Frees the buffer and releases resources.
///
/// @param userdata LogMembufData pointer from logmembufCreate()
void logmembufCloseFunc(_In_opt_ void* userdata);

/// @}  // end of log_membuf group

CX_C_END
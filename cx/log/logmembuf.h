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
///   // Register a 4KB buffer with the default compact text serializer
///   LogDest *dest = logmembufRegister(LOG_Debug, NULL, 4096, NULL);
///
///   // Log some messages
///   logStr(Info, _SL("Test message"));
///   logFlush();
///
///   // Access the buffer contents directly
///   LogMembufData *lmd = logmembufData(dest);
///   printf("Buffered logs:\n%.*s\n", (int)lmd->cur, lmd->buf);
///
///   // Cleanup -- this frees the buffer, so lmd is invalid afterwards
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

/// Register a memory buffer log destination
///
/// Allocates a fixed-size circular buffer and registers it with the logging system in one step.
/// Records are serialized and written to the buffer; messages longer than the buffer size are
/// dropped, and when the buffer fills, writing wraps back to the beginning. The buffer is freed
/// when logUnregisterDest() retires the returned handle.
///
/// Use logmembufData() to reach the buffer contents.
///
/// @param maxlevel Maximum log level to write to the buffer
/// @param chanfilter Channel path pattern, or NULL for every unrestricted channel
/// @param size Buffer size in bytes
/// @param ser Serializer to render records with; **ownership transfers**, including if this
///            call fails. NULL gets a compact text serializer: short levels, bracketed channels,
///            second-resolution timestamps.
/// @return Destination handle for later unregistration, or NULL on failure
/// @code
///   LogDest *dest = logmembufRegister(LOG_Debug, NULL, 8192, NULL);   // 8KB, compact text
///
///   // or capture structured records for a test to parse
///   LogDest *jdest = logmembufRegister(LOG_Debug, NULL, 8192, logNdjsonSerializer(NULL));
/// @endcode
LogDest* logmembufRegister(int maxlevel, _In_opt_ strref chanfilter, uint32 size,
                           _In_opt_ LogSerializer* ser);

/// Get the buffer behind a memory buffer destination
///
/// The returned struct is owned by the destination and lives exactly as long as it does:
/// logUnregisterDest() frees the buffer, so nothing may read it afterwards.
///
/// Reading `cur` races with the drain thread, which may be appending while this runs. Read it
/// once into a local and use that for both the length and any copy -- reading it twice lets it
/// grow in between.
///
/// @param dest Destination handle from logmembufRegister()
/// @return Buffer state, or NULL if dest is not a memory buffer destination
/// @code
///   LogDest *dest = logmembufRegister(LOG_Debug, NULL, 4096, NULL);
///   logFlush();
///
///   LogMembufData *lmd = logmembufData(dest);
///   uint32 cur = lmd->cur;
///   printf("%.*s", (int)cur, lmd->buf);
/// @endcode
_Ret_maybenull_ LogMembufData* logmembufData(_In_opt_ LogDest* dest);

// ============================================================================
// Low Level Interface
// ============================================================================
//
// These callbacks can be used directly with logRegisterDest() for custom
// destination handling. Most users should use the high-level interface above.

/// Create a memory buffer log destination
///
/// Only needed to register the buffer by hand with logRegisterDest() -- logmembufRegister() does
/// this and the registration together. The returned handle belongs to exactly one destination:
/// it is freed by logmembufCloseFunc() and cannot be registered twice.
///
/// @param size Buffer size in bytes
/// @param ser Serializer to render records with; **ownership transfers**. NULL gets a compact
///            text serializer: short levels, bracketed channels, second-resolution timestamps.
/// @return Memory buffer handle
/// @code
///   LogMembufData *lmd = logmembufCreate(8192, NULL);
///   LogDest *dest = logRegisterDest(LOG_Debug, NULL, logmembufMsgFunc, NULL,
///                                   logmembufCloseFunc, lmd);
/// @endcode
_Ret_valid_ LogMembufData* logmembufCreate(uint32 size, _In_opt_ LogSerializer* ser);

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
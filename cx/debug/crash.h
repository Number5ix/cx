#pragma once

/// @file crash.h
/// @brief Crash handler and dump generation
///
/// @defgroup debug_crash Crash Handler
/// @ingroup debug
/// @{
///
/// Platform-agnostic crash handler with memory dump generation and reporting.
///
/// The crash handler system provides:
/// - Automatic crash detection and signal/exception handling
/// - Memory dump generation (mini or full dumps)
/// - Stack trace capture and symbolication
/// - Custom metadata injection into crash reports
/// - Memory region inclusion for debugging
/// - Crash report upload to reporting services
/// - User prompts and progress UI (configurable)
/// - Pre/post crash callbacks for cleanup or logging
///
/// **Typical initialization:**
/// @code
///   // Non-interactive service (auto-upload crashes)
///   dbgCrashSetMode(DBG_CrashNonInteractive);
///   
///   // Interactive application (prompt user)
///   dbgCrashSetMode(DBG_CrashInteractive);
///   dbgCrashSetPath(_S"C:/CrashDumps");
///   
///   // Add version info that appears in all reports
///   dbgCrashAddVersionStr("app_version", "1.2.3");
///   dbgCrashAddVersionInt("build_number", 12345);
/// @endcode
///
/// **Triggering a controlled crash:**
/// @code
///   // Add crash-specific metadata
///   dbgCrashAddMetaStr("operation", "file_write");
///   dbgCrashAddMetaInt("error_code", errno);
///   
///   // Trigger crash handler (does not return)
///   dbgCrashNow(0);
/// @endcode

#include <cx/cx.h>

CX_C_BEGIN

/// Flags controlling crash handler behavior
///
/// These flags can be combined with bitwise OR to configure how crashes are handled.
enum DEBUG_CRASH_FLAGS_ENUM {
    DBG_CrashExit        = 0x0001,          ///< Exit process gracefully after handling crash
    DBG_CrashDump        = 0x0002,          ///< Generate minidump (small memory dump)
    DBG_CrashFullDump    = 0x0004,          ///< Generate full memory dump (large, includes entire process)
    DBG_CrashUpload      = 0x0008,          ///< Submit crash report to reporting service
    DBG_CrashBreakpoint  = 0x0010,          ///< Trigger breakpoint for debugger attachment
    DBG_CrashDelete      = 0x0020,          ///< Delete dump file after successful upload
    DBG_CrashInternal    = 0x0040,          ///< Submit to internal endpoint rather than public reporting service
    DBG_CrashProgressUI  = 0x0080,          ///< Show progress UI while uploading crash report
    DBG_CrashDevMode     = 0x0100,          ///< Process is in development mode; allow debugging and special handling
    DBG_CrashNotify      = 0x0200,          ///< Show notification dialog but do not offer user options
    DBG_CrashPrompt      = 0x1000,          ///< Prompt user upon crash; user can choose crash handling options
    DBG_CrashPromptLocal = 0x3000,          ///< Prompt user but disallow upload (implies CrashPrompt)
};

/// Preset crash mode for interactive applications
///
/// Exits gracefully, prompts user for action, and shows progress UI.
/// Suitable for desktop applications where user interaction is expected.
#define DBG_CrashInteractive (DBG_CrashExit | DBG_CrashPrompt | DBG_CrashProgressUI)

/// Preset crash mode for non-interactive services
///
/// Exits gracefully, generates minidump, uploads automatically, and deletes dump after upload.
/// Suitable for background services, daemons, or server processes.
#define DBG_CrashNonInteractive (DBG_CrashExit | DBG_CrashDump | DBG_CrashUpload | DBG_CrashDelete)

/// Set crash handler behavior mode
/// @param mode Combination of DEBUG_CRASH_FLAGS_ENUM flags
///
/// Configures how the crash handler behaves when a crash occurs.
/// Thread-safe and can be changed at runtime.
/// @code
///   dbgCrashSetMode(DBG_CrashInteractive);
///   // or custom combination:
///   dbgCrashSetMode(DBG_CrashExit | DBG_CrashDump | DBG_CrashPrompt);
/// @endcode
void dbgCrashSetMode(uint32 mode);

/// Get current crash handler mode
/// @return Current mode flags
uint32 dbgCrashGetMode();

/// Set crash dump output directory
/// @param path Directory path where crash dumps should be written
/// @return true on success, false on failure
///
/// Specifies where crash dump files (.dmp) should be saved.
/// If not set, uses platform-specific default location.
void dbgCrashSetPath(_In_ strref path);

/// Callback function invoked when a crash occurs
///
/// @param after false during initial crash handling (right after exception);
///              true just before process exit
/// @return true to continue crash processing; false to abort and use OS default handler
///
/// **CRITICAL:** Callbacks must be signal-safe. Do NOT perform:
/// - Memory allocation (xaAlloc, malloc, etc.)
/// - Complex locking (may deadlock)
/// - Logging to files or network
/// - String operations that allocate
///
/// Safe operations:
/// - Writing to pre-allocated buffers
/// - Simple flag setting
/// - Stack-based operations
///
/// Callbacks are invoked twice: once during initial exception handling for
/// immediate response, and again just before exit for final cleanup.
typedef bool(*dbgCrashCallback)(bool after);

/// Register a crash callback function
/// @param cb Callback to invoke on crash
///
/// Callbacks are deduplicated - adding the same callback multiple times has no effect.
/// All registered callbacks are invoked in registration order.
void dbgCrashAddCallback(dbgCrashCallback cb);

/// Unregister a previously registered crash callback
/// @param cb Callback to remove
void dbgCrashRemoveCallback(dbgCrashCallback cb);

/// Mark a memory region for inclusion in crash dumps
/// @param ptr Pointer to start of memory region
/// @param sz Size of memory region in bytes
///
/// Ensures the specified memory region is included in minidumps.
/// Useful for capturing circular buffers, shared memory, or other
/// important data structures that might not be automatically included.
///
/// Automatically included regions:
/// - Stack of all threads
/// - Global/static data segments
/// - Memory around crash location
void dbgCrashIncludeMemory(void *ptr, size_t sz);

/// Remove a previously marked memory region from crash dumps
/// @param ptr Pointer to start of memory region
/// @param sz Size of memory region in bytes
///
/// Removes a region previously added with dbgCrashIncludeMemory().
/// Does NOT exclude automatically selected regions like stacks and data segments.
void dbgCrashExcludeMemory(void *ptr, size_t sz);

/// Add string metadata to crash report
/// @param name Metadata key name
/// @param val String value
///
/// Adds crash-specific metadata that appears in the crash report.
/// Should be used for information directly related to the crash condition,
/// called just before triggering a crash.
///
/// For runtime metadata that persists across the application lifecycle,
/// use the @ref debug_blackbox system instead.
///
/// Metadata is automatically JSON-escaped and included in the crash report payload.
void dbgCrashAddMetaStr(_In_z_ const char *name, _In_z_ const char *val);

/// Add integer metadata to crash report
/// @param name Metadata key name
/// @param val Integer value
///
/// Like dbgCrashAddMetaStr() but for integer values.
void dbgCrashAddMetaInt(_In_z_ const char *name, int val);

/// Add string version metadata to crash report root
/// @param name Version field name
/// @param val String value
///
/// Adds version information to the root level of crash reports.
/// Typically called during application initialization for version identifiers,
/// build numbers, or configuration that applies to all crashes.
///
/// Appears at crash report root level, separate from per-crash metadata.
void dbgCrashAddVersionStr(_In_z_ const char *name, _In_z_ const char *val);

/// Add integer version metadata to crash report root
/// @param name Version field name
/// @param val Integer value
///
/// Like dbgCrashAddVersionStr() but for integer values.
void dbgCrashAddVersionInt(_In_z_ const char *name, int val);

/// Trigger crash handler immediately
/// @param skipframes Number of stack frames to skip in stack trace
///
/// Invokes the crash handler as if a fatal exception occurred.
/// Does not return - process terminates according to configured mode.
///
/// Use skipframes to exclude internal error handling frames from the
/// captured stack trace. Pass 0 to include all frames.
///
/// Before calling, use dbgCrashAddMetaStr()/dbgCrashAddMetaInt() to
/// add crash-specific context.
void _no_return dbgCrashNow(int skipframes);

/// @}  // end of debug_crash group

CX_C_END

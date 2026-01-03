#pragma once

/// @file error.h
/// @brief Thread-local error code facility
///
/// @defgroup debug_error Error Codes
/// @ingroup debug
/// @{
///
/// Thread-local error code system for functions that cannot return error values.
///
/// The cxerr facility provides a thread-safe error reporting mechanism similar to
/// the standard `errno`, but integrated into the CX framework. Functions that need
/// to report errors but cannot return an error code (e.g., functions returning
/// pointers or having other return value semantics) can set `cxerr` to indicate
/// the reason for failure.
///
/// **Key characteristics:**
/// - Thread-local storage - each thread has independent error state
/// - Zero (`CX_Success`) indicates success
/// - Must be explicitly checked after operations that may fail
/// - Not automatically cleared - check immediately after the failing operation
///
/// **Typical usage pattern:**
/// @code
///   cxerr = CX_Success;  // Clear error before operation
///   float val = strToFloat32(_S"invalid");
///   if (cxerr != CX_Success) {
///       logError(_S"Conversion failed: %"), cxErrMsg(cxerr));
///   }
/// @endcode
///
/// Functions should document when they set `cxerr` on failure.

#include <cx/cx.h>

CX_C_BEGIN

/// Thread-local error code
///
/// Set by functions to indicate error conditions. Check this variable
/// after calling functions that document cxerr usage.
/// Always `CX_Success` (0) when no error occurred.
extern _Thread_local int cxerr;

/// Standard CX error codes
///
/// Error codes used throughout the CX framework. Functions set `cxerr`
/// to one of these values to indicate specific error conditions.
enum CX_ERROR {
    CX_Success = 0,             ///< Operation completed successfully (no error)
    CX_Unspecified,             ///< Unknown or unspecified error occurred
    CX_InvalidArgument,         ///< Function argument is invalid or malformed
    CX_AccessDenied,            ///< Permission denied or access not allowed
    CX_FileNotFound,            ///< Requested file or resource does not exist
    CX_AlreadyExists,           ///< File or resource already exists; refusing to overwrite
    CX_IsDirectory,             ///< Attempted to treat a directory as a file
    CX_ReadOnly,                ///< Attempted to write to read-only path or resource
    CX_Range,                   ///< Value is out of valid range
};

/// Get human-readable error message for an error code
/// @param err Error code from CX_ERROR enum
/// @return Constant string describing the error
///
/// Returns a descriptive message for the given error code.
/// Useful for logging or displaying error information to users.
/// @code
///   if (cxerr != CX_Success) {
///       printf("Error: %s\n", cxErrMsg(cxerr));
///   }
/// @endcode
_Ret_z_
const char *cxErrMsg(int err);

/// @}  // end of debug_error group

CX_C_END

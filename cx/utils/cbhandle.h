/// @file cbhandle.h
/// @brief Generic callback handle system (DEPRECATED)
/// @defgroup utils_cbhandle Callback Handles
/// @ingroup utils
/// @{
///
/// @deprecated This module is deprecated. Use @ref closure "Closures" or @ref closure_chain
/// "Closure Chains" instead.
///
/// Generic callback handle system for storing and retrieving function pointers with type safety.
///
/// This older system converts function pointers to integer handles that can be stored and
/// retrieved later. It provides basic type checking by storing the function signature as a string.
///
/// **Why this is deprecated:**
/// - Closures provide the ability to capture arguments/environment with the callback
/// - Closure chains provide thread-safe callback lists with attachment/detachment
/// - Both are more flexible and integrated with the rest of CX
///
/// For new code, use:
/// - @ref closure.h for single callbacks with captured environment
/// - @ref cchain.h for lists of callbacks (e.g., event handlers)
///
/// @see @ref closure "Closures"
/// @see @ref closure_chain "Closure Chains"

#pragma once

// Generic callback handles

#include <cx/cx.h>

/// Generic callback function pointer type
typedef void(*GenericCallback)();

int _callbackGetHandle(_In_z_ const char *cbtype, _In_ GenericCallback func);
_Ret_opt_valid_
GenericCallback _callbackGetFunc(_In_z_ const char *cbtype, int handle);

/// int callbackGetHandle(cbtype, func)
///
/// Convert a typed function pointer to an integer handle.
///
/// Stores the function pointer internally and returns a handle that can be used to retrieve
/// it later. The function signature type is stored for basic type checking.
///
/// @deprecated Use closureCreate() instead to capture a callback with environment.
///
/// @param cbtype Function pointer type (e.g., myCallbackType)
/// @param func Function pointer to store
/// @return Integer handle (0 if func is NULL)
#define callbackGetHandle(cbtype, func) _callbackGetHandle(#cbtype, (GenericCallback)((cbtype)func))

/// cbtype callbackGetFunc(cbtype, handle)
///
/// Retrieve a typed function pointer from a handle.
///
/// Looks up the function pointer associated with the handle and verifies the type matches.
/// Returns NULL if the handle is invalid or the type doesn't match.
///
/// @deprecated Use closures or closure chains instead.
///
/// @param cbtype Expected function pointer type
/// @param handle Handle returned by callbackGetHandle()
/// @return Function pointer cast to cbtype, or NULL if invalid/type mismatch
#define callbackGetFunc(cbtype, handle) ((cbtype)_callbackGetFunc(#cbtype, handle))

/// @}
// end of utils_cbhandle group

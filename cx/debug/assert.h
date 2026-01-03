#pragma once

/// @file assert.h
/// @brief Runtime assertion macros and failure handling
///
/// @defgroup debug_assert Assertions
/// @ingroup debug
/// @{
///
/// Runtime assertions with configurable failure behavior.
///
/// CX provides multiple levels of assertions that compile to different behavior based
/// on DEBUG_LEVEL:
/// - DEBUG_LEVEL >= 2: Debug assertions enabled (dbgAssert, dbgVerify)
/// - DEBUG_LEVEL >= 1: Development assertions enabled (devAssert, devVerify, relAssert)
/// - DEBUG_LEVEL == 0: Only release assertions enabled (relAssert)
///
/// Assertion variants:
/// - **Assert**: Evaluates expression and crashes if false (void return)
/// - **Verify**: Evaluates expression, crashes if false, but returns the boolean result
/// - **FatalError**: Immediately triggers assertion failure with a message
///
/// All assertion failures trigger registered callbacks before taking action.

#include <cx/platform/base.h>
#include <cx/platform/cpp.h>
#include <cx/utils/macros/salieri.h>
#include <cx/utils/macros/unused.h>
#include <stdbool.h>

/// Action to take when an assertion fails
enum ASSERT_ACTION_ENUM {
    ASSERT_Crash = 0,   ///< Intentionally crash to invoke crash handler or get memory dump
    ASSERT_Exit,        ///< Immediately terminate the process with exit(1)
    ASSERT_Ignore       ///< Ignore the failure and continue execution
};

/// Callback function invoked when an assertion fails
///
/// @param expr The failed expression as a string (may be NULL for FatalError)
/// @param msg Optional message provided with the assertion (may be NULL)
/// @param file Source file where assertion failed (NULL in release builds with DEBUG_LEVEL == 0)
/// @param ln Line number where assertion failed
/// @return One of the ASSERT_ACTION_ENUM values to control behavior
///
/// Callbacks are invoked before the default failure action. If multiple callbacks
/// are registered, they are called in order. If any callback returns ASSERT_Crash or
/// ASSERT_Exit, that action is taken immediately. Otherwise, the last callback's
/// return value determines the action.
typedef int (*dbgAssertCallback)(_In_opt_z_ const char* expr, _In_opt_z_ const char* msg,
                                 _In_opt_z_ const char* file, int ln);

/// Register a callback to be invoked on assertion failure
/// @param cb Callback function to add
///
/// Callbacks are deduplicated - adding the same callback multiple times has no effect.
void dbgAssertAddCallback(dbgAssertCallback cb);

/// Unregister a previously registered assertion callback
/// @param cb Callback function to remove
void dbgAssertRemoveCallback(dbgAssertCallback cb);

#if DEBUG_LEVEL >= 1
CX_C _no_inline bool _cxAssertFail(_In_opt_z_ const char* expr, _In_opt_z_ const char* msg,
                                   _In_z_ const char* file, int ln);
#else
CX_C _no_inline bool _cxAssertFail(_In_opt_z_ const char* expr, _In_opt_z_ const char* msg);
#endif

#if DEBUG_LEVEL >= 2
#define dbgAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define dbgAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define dbgVerify(expr)         (!!(expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define dbgVerifyMsg(expr, msg) (!!(expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#else
/// Debug assertion - only active when DEBUG_LEVEL >= 2
///
/// void dbgAssert(expr)
///
/// Evaluates expression and triggers assertion failure if false.
/// Compiled out completely in dev/release builds.
/// @param expr Expression to evaluate
#define dbgAssert(expr)         unused_noeval(expr)
/// Debug assertion with message - only active when DEBUG_LEVEL >= 2
///
/// void dbgAssertMsg(expr, msg)
///
/// Like dbgAssert but includes a custom message in the failure report.
/// @param expr Expression to evaluate
/// @param msg String literal message to include on failure
#define dbgAssertMsg(expr, msg) unused_noeval(expr)
/// Debug verify - only active when DEBUG_LEVEL >= 2
///
/// bool dbgVerify(expr)
///
/// Like dbgAssert but returns the boolean result of the expression.
/// Useful when the expression result is needed in the calling code.
/// @param expr Expression to evaluate
/// @return Boolean result of expression evaluation
#define dbgVerify(expr)         (!!(expr))
/// Debug verify with message - only active when DEBUG_LEVEL >= 2
///
/// bool dbgVerifyMsg(expr, msg)
///
/// Like dbgVerify but includes a custom message in the failure report.
/// @param expr Expression to evaluate
/// @param msg String literal message to include on failure
/// @return Boolean result of expression evaluation
#define dbgVerifyMsg(expr, msg) (!!(expr))
#endif

#if DEBUG_LEVEL >= 1
#define devAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define devAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define devVerify(expr)         (!!(expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define devVerifyMsg(expr, msg) (!!(expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define relAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define relAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define devFatalError(msg)      _cxAssertFail(NULL, msg, __FILE__, __LINE__)
#define relFatalError(msg)      _cxAssertFail(NULL, msg, __FILE__, __LINE__)
#elif defined(DIAGNOSTIC)
#define devAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL))
#define devAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg))
#define devVerify(expr)         (!!(expr) || _cxAssertFail(#expr, NULL))
#define devVerifyMsg(expr, msg) (!!(expr) || _cxAssertFail(#expr, msg))
#define relAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL))
#define relAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg))
#define devFatalError(msg)      _cxAssertFail(NULL, msg)
#define relFatalError(msg)      _cxAssertFail(NULL, msg)
#else
/// Development assertion - active when DEBUG_LEVEL >= 1
///
/// void devAssert(expr)
///
/// Evaluates expression and triggers assertion failure if false.
/// Active in debug and dev builds, compiled out in release builds.
/// @param expr Expression to evaluate
#define devAssert(expr)         unused_noeval(expr)
/// Development assertion with message - active when DEBUG_LEVEL >= 1
///
/// void devAssertMsg(expr, msg)
///
/// Like devAssert but includes a custom message in the failure report.
/// @param expr Expression to evaluate
/// @param msg String literal message to include on failure
#define devAssertMsg(expr, msg) unused_noeval(expr)
/// Development verify - active when DEBUG_LEVEL >= 1
///
/// bool devVerify(expr)
///
/// Like devAssert but returns the boolean result of the expression.
/// @param expr Expression to evaluate
/// @return Boolean result of expression evaluation
#define devVerify(expr)         (!!(expr))
/// Development verify with message - active when DEBUG_LEVEL >= 1
///
/// bool devVerifyMsg(expr, msg)
///
/// Like devVerify but includes a custom message in the failure report.
/// @param expr Expression to evaluate
/// @param msg String literal message to include on failure
/// @return Boolean result of expression evaluation
#define devVerifyMsg(expr, msg) (!!(expr))
/// Release assertion - always active
///
/// void relAssert(expr)
///
/// Evaluates expression and triggers assertion failure if false.
/// Active in all build types. Use sparingly for critical invariants.
/// @param expr Expression to evaluate
#define relAssert(expr)         (void)(!!(expr) || _cxAssertFail(#expr, NULL))
/// Release assertion with message - always active
///
/// void relAssertMsg(expr, msg)
///
/// Like relAssert but includes a custom message in the failure report.
/// @param expr Expression to evaluate
/// @param msg String literal message to include on failure
#define relAssertMsg(expr, msg) (void)(!!(expr) || _cxAssertFail(#expr, msg))
/// Development fatal error - active when DEBUG_LEVEL >= 1
///
/// void devFatalError(msg)
///
/// Immediately triggers assertion failure with a message.
/// Active in debug and dev builds, compiled out in release builds.
/// @param msg String literal error message
#define devFatalError(msg)      ((void)0)
/// Release fatal error - always active
///
/// void relFatalError(msg)
///
/// Immediately triggers assertion failure with a message.
/// Active in all build types.
/// @param msg String literal error message
#define relFatalError(msg)      _cxAssertFail(NULL, msg)
#endif

#if defined(DEBUG_LEGACY_ASSERT) && !defined(__INTELLISENSE__)
#define assert(expr) relAssert(expr)
#endif

/// @}  // end of debug_assert group

#pragma once

#include <stdbool.h>
#include <cx/core/cpp.h>

#if DEBUG_LEVEL >= 1
_EXTERN_C bool _cxAssertFail(const char *expr, const char *msg, const char *file, int ln);
#else
_EXTERN_C bool _cxAssertFail(const char *expr, const char *msg);
#endif

#if DEBUG_LEVEL >= 2
#define dbgAssert(expr) ((expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define dbgAssertMsg(expr, msg) ((expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#else
#define dbgAssert(expr) ((void)0)
#define dbgAssertMsg(expr, msg) ((void)0)
#endif

#if DEBUG_LEVEL >= 1
#define devAssert(expr) ((expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define devAssertMsg(expr, msg) ((expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define relAssert(expr) ((expr) || _cxAssertFail(#expr, NULL, __FILE__, __LINE__))
#define relAssertMsg(expr, msg) ((expr) || _cxAssertFail(#expr, msg, __FILE__, __LINE__))
#define devFatalError(msg) _cxAssertFail(NULL, msg, __FILE__, __LINE__)
#define relFatalError(msg) _cxAssertFail(NULL, msg, __FILE__, __LINE__)
#else
#define devAssert(expr) ((void)0)
#define devAssertMsg(expr, msg) ((void)0)
#define relAssert(expr) ((expr) || _cxAssertFail(#expr, NULL))
#define relAssertMsg(expr, msg) ((expr) || _cxAssertFail(#expr, msg))
#define devFatalError(msg) ((void)0)
#define relFatalError(msg) _cxAssertFail(NULL, msg)
#endif

#if defined(DEBUG_LEGACY_ASSERT) && !defined(__INTELLISENSE__)
#define assert(expr) relAssert(expr)
#endif

#include "cx/debug/assert.h"
#include "cx/debug/crash.h"
#include <stdlib.h>

// static assert to prove that cx/static_config.h was included somewhere
extern const int you_forgot_to_include_cx_static_config_h;
const void *_cx_invariant = &you_forgot_to_include_cx_static_config_h;

#if DEBUG_LEVEL >= 1
_no_inline bool _cxAssertFail(const char *expr, const char *msg, const char *file, int ln)
#else
_no_inline bool _cxAssertFail(const char *expr, const char *msg)
#endif
{
    if (expr)
        dbgCrashAddMetaStr("assertexpr", expr);
    if (msg)
        dbgCrashAddMetaStr("assertmsg", msg);
#if DEBUG_LEVEL >= 1
    dbgCrashAddMetaStr("assertfile", file);
    dbgCrashAddMetaInt("assertline", ln);
#endif
    dbgCrashNow(1);
    return true;
}

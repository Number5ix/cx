#include "cx/debug/assert.h"
#include "cx/debug/crash.h"
#include <stdlib.h>

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
    return false;
}

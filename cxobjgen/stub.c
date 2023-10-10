#include "cx/cx.h"
#include "cx/debug/assert.h"
#include <stdio.h>

#if DEBUG_LEVEL >= 1
CX_C bool _cxAssertFail(const char *expr, const char *msg, const char *file, int ln){
    printf("Assertion failure!\n");
    if (msg)
        printf("%s\n", msg);
    if (expr)
        printf("expr: %s\n", expr);
    if (file)
        printf("%s:%d\n", file, ln);
    exit(1);
}
#else
CX_C bool _cxAssertFail(const char *expr, const char *msg) { exit(1); }
#endif

_no_inline _no_return void dbgCrashNow(int skip)
{
    *(char *)(0) = 0;
}

intptr_t stCmp_suid(stype st, void *a, void *b) { return 0; }

bool stConvert_suid(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags) { return false; }

bool suidDecode(SUID *out, strref str) { return false; }

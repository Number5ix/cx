#include "cx/thread/tlscleanup_private.h"
#include "cx/platform/win.h"

// provide a symbol so that win_thread_thread.c can force this object file to be included,
// otherwise the #pragmas below won't work
int _cx_win_tls_cleanup = 0;

void _thrPlatformTLSCleanupEnable(void)
{
    // nothing to do on this platform
}

static void NTAPI cx_tls_cb(PVOID module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_THREAD_DETACH) {
        // check for registered thread cleanup functions
        _thrTLSCleanup();
    }
}

#if defined(_WIN64)
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:_cx_tls_callback")
#pragma const_seg(".CRT$XLC")
extern const PIMAGE_TLS_CALLBACK _cx_tls_callback[];
const PIMAGE_TLS_CALLBACK _cx_tls_callback[] = { &cx_tls_cb };
#pragma const_seg()
#else
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:__cx_tls_callback")
#pragma data_seg(".CRT$XLC")
PIMAGE_TLS_CALLBACK _cx_tls_callback[] = { &cx_tls_cb };
#pragma data_seg()
#endif

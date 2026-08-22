// Process-wide initialization for cxtls.
//
// Mbed TLS 4.x requires psa_crypto_init() before any TLS or X.509 call -- the PSA API is the only
// crypto API left, and the legacy entry points that used to work without it are gone. Rather than
// make that the application's problem, every cxtls factory calls _tlsInit() first and refuses to
// build anything if it failed, so no cxtls object can exist over an uninitialized crypto core.

#include "tls_private.h"

#include <cx/format.h>
#include <cx/utils/lazyinit.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL TlsLogChannel

LogChannel* TlsLogChannel;

static LazyInitState tlsInit_done;
static bool tlsInit_ok;

static void tlsInitOnce(void* unused)
{
    unused_noeval(unused);

    TlsLogChannel = logChan(_SL("cx/tls"));

    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        // Nothing in cxtls can work without this, and it fails for exactly one interesting reason
        // in practice: the five global mbedTLS mutexes not being statically initialized, which
        // comes back as PSA_ERROR_SERVICE_FAILURE. See 3rdparty/mbedtls-cx/README.md group C.
        tlsLogErr(Fatal, _S"psa_crypto_init", (int)st);
        return;
    }

    tlsInit_ok = true;
}

bool _tlsInit(void)
{
    lazyInit(&tlsInit_done, tlsInitOnce, NULL);
    return tlsInit_ok;
}

_Use_decl_annotations_
void _tlsErrDesc(strhandle out, int err)
{
    char buf[128];

    // mbedtls_strerror() covers MBEDTLS_ERR_* and leaves a generic string for anything else, which
    // is what a PSA_ERROR_* lands on. Reporting the number alongside it means an unrecognized code
    // is still actionable.
    mbedtls_strerror(err, buf, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;

    string desc = 0;
    strFromBytes(&desc, buf, (uint32)strlen(buf));
    strFormat(out, _SL("${string} (${int})"), stvar(string, desc), stvar(int32, err));
    strDestroy(&desc);
}

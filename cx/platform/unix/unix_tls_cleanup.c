#include "cx/thread/tlscleanup_private.h"

#include <pthread.h>

static pthread_key_t _cx_tls_key = (pthread_key_t)(-1);

static void cx_tls_cb(void* value)
{
    // use the non-platform specific code to run the registered cleanup functions for this thread
    _thrTLSCleanup();
}

void _thrPlatformTLSCleanupInit(void)
{
    pthread_key_create(&_cx_tls_key, &cx_tls_cb);
}

void _thrPlatformTLSCleanupEnable(void)
{
    // set a non-NULL value to ensure the callback will be called at thread exit
    pthread_setspecific(_cx_tls_key, (void*)1);
}
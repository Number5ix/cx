#include "cx/thread/tlscleanup_private.h"

#include <cx/utils/lazyinit.h>
#include <pthread.h>

static pthread_key_t cx_tls_key = (pthread_key_t)(-1);
static LazyInitState cx_tls_key_init_state;

static void cx_tls_cb(void* value)
{
    // use the non-platform specific code to run the registered cleanup functions for this thread
    _thrTLSCleanup();
}

static void _cx_tls_key_init(void* dummy)
{
    pthread_key_create(&cx_tls_key, &cx_tls_cb);
}

void _thrPlatformTLSCleanupEnable(void)
{
    lazyInit(&cx_tls_key_init_state, &_cx_tls_key_init, NULL);

    // set a non-NULL value to ensure the callback will be called at thread exit
    pthread_setspecific(cx_tls_key, (void*)1);
}
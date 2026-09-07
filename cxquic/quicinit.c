// Process-wide initialization for cxquic.
//
// QUIC's packet protection is PSA crypto from the very first packet, so the same rule cxtls has
// applies here: psa_crypto_init() must have run before anything else does. _quicInit() defers to
// cxtls for that and adds the log channel.

#include "quic_private.h"

#include <cx/utils/lazyinit.h>
#include <cxtls/tls_private.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

LogChannel* QuicLogChannel;

static LazyInitState quicInit_done;
static bool quicInit_ok;

static void quicInitOnce(void* unused)
{
    unused_noeval(unused);

    QuicLogChannel = logChan(_SL("cx/quic"));
    quicInit_ok = tlsInit();
}

bool _quicInit(void)
{
    lazyInit(&quicInit_done, quicInitOnce, NULL);
    return quicInit_ok;
}

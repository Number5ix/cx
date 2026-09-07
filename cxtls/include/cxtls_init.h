#pragma once

#include <cx/cx.h>

/// @file cxtls_init.h
/// @brief Process-wide crypto core initialization
///
/// Consumers that only use cxtls itself never need this: every cxtls factory brings the crypto
/// core up on its first call. It exists for the case cxtls.h's overview describes -- reaching past
/// cxtls to call mbedTLS or PSA directly, which since 4.x requires psa_crypto_init() to have run
/// first. Calling it here rather than calling psa_crypto_init() yourself keeps a single owner of
/// that process-wide state, so the two cannot race or double-initialize.
///
/// Deliberately does not pull in the rest of cxtls -- include this alone rather than cxtls.h if
/// the filter and certificate APIs are not wanted.

CX_C_BEGIN

/// @addtogroup tls
/// @{

/// Bring up the process-wide crypto core
///
/// Lazy and thread-safe: call it as many times and from as many threads as convenient. Sets up
/// everything needed for cxtls and crypto API usage.
///
/// @return true if the crypto core is usable
bool tlsInit(void);

/// @}

CX_C_END

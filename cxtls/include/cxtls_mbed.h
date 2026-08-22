#pragma once

#include <cxtls.h>

#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/// @file cxtls_mbed.h
/// @brief Typed access to the mbedTLS objects behind the cxtls classes
///
/// Everything here hands back a pointer cxtls still owns. It stays valid as long as the cx object
/// it came from does, and the thread-safety rules of that object still apply: a TlsConfig is
/// read-only once sealed, and an `mbedtls_ssl_context` may only be touched under the flow's filter
/// lock. Use nettlsFlowWithSsl() for context access.

CX_C_BEGIN

/// @addtogroup tls_mbed mbedTLS Interop
/// @ingroup tls
/// @{

/// The mbedTLS configuration behind a TlsConfig
///
/// Only available once the config has been sealed, since an unsealed one is still incomplete. Call
/// tlsconfigSeal() first if you need it before the first connection.
///
/// @param config Configuration to inspect
/// @return The sealed `mbedtls_ssl_config`, or NULL if the config is not sealed
_Ret_maybenull_ mbedtls_ssl_config* tlsconfigMbedConf(_In_ TlsConfig* config);

/// The certificate chain behind a TlsCAStore
///
/// @param ca Trust store to inspect
/// @return Its `mbedtls_x509_crt` chain, never NULL (an empty store is a single zeroed node)
_Ret_valid_ mbedtls_x509_crt* tlscastoreMbedCrt(_In_ TlsCAStore* ca);

/// The certificate chain and private key behind a TlsCreds
///
/// @param creds Credentials to inspect
/// @param crt Receives the certificate chain, or NULL if not wanted
/// @param key Receives the private key, or NULL if not wanted
void tlscredsMbed(_In_ TlsCreds* creds, _Outptr_opt_ mbedtls_x509_crt** crt,
                  _Outptr_opt_ mbedtls_pk_context** key);

/// @}

CX_C_END

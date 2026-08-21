// Wired in as MBEDTLS_USER_CONFIG_FILE (see 3rdparty/CMakeLists.txt). Mbed TLS
// includes this after mbedtls/mbedtls_config.h and before the config_adjust_*
// headers, so both #define and #undef behave as expected here.
//
// Intentionally empty. Crypto-domain options belong in cxtls_crypto_config.h --
// Mbed TLS actively rejects them here. Trimming the enabled TLS mechanisms is
// deferred until cxtls has real code to size against; see
// 3rdparty/mbedtls-cx/README.md.

#pragma once

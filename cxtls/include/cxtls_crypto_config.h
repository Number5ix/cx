// Wired in as TF_PSA_CRYPTO_USER_CONFIG_FILE (see 3rdparty/CMakeLists.txt).
// TF-PSA-Crypto includes this after psa/crypto_config.h and before the
// crypto_adjust_* headers, so both #define and #undef behave as expected here.
//
// Everything cx changes about the crypto configuration lives in this file, so
// that 3rdparty/mbedtls/tf-psa-crypto/include/psa/crypto_config.h stays at zero
// diff against upstream. Note that Mbed TLS refuses crypto-domain options set
// in the TLS config (library/mbedtls_config_check_user.h enforces it), which is
// why the threading and allocator settings belong here rather than in
// cxtls_mbedtls_config.h.

#pragma once

/* Route all mbedTLS allocation through cx's allocator, so TLS buffers are
 * covered by the same accounting, guard pages and leak checking as the rest of
 * the framework. */
#include <cx/xalloc/xalloc.h>
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO xa_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO   xa_free

/* Lock with cx's mutexes and condition variables rather than a second
 * threading implementation. Implemented in the vendored tree; see
 * MBEDTLS_THREADING_CX in mbedtls/threading.h. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_CX

/* Take entropy from cx's osGenRandom() instead of the built-in pollers. One RNG
 * code path on every platform, and cx's Windows backend already carries both
 * the BCrypt and the XP CryptGenRandom routes. The two options are mutually
 * exclusive upstream and the built-in one is on by default, so it has to go
 * first. Implemented in cxtls/cxtls_entropy.c. */
#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY

/* Mechanisms cx has no use for. Each one drops its builtin driver sources from
 * the build entirely -- see crypto_adjust_config_enable_builtins.h, which is
 * what turns a PSA_WANT_* into the MBEDTLS_*_C that guards the implementation.
 *
 * DES needs no entry: Mbed TLS 4.0 removed it outright, so there is no
 * PSA_WANT_KEY_TYPE_DES left to undefine and no des.c left to compile.
 *
 * RIPEMD-160 has no role in TLS or X.509; it exists for legacy interop that cx
 * does not have. */
#undef PSA_WANT_ALG_RIPEMD160

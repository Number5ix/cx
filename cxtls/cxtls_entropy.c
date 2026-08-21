// Entropy source for TF-PSA-Crypto.
//
// cxtls_crypto_config.h turns off MBEDTLS_PSA_BUILTIN_GET_ENTROPY and turns on
// MBEDTLS_PSA_DRIVER_GET_ENTROPY, which makes the library call this function
// instead of its own per-platform pollers. cx owns its entropy source end to
// end that way, on every platform rather than only on Windows XP, and
// osGenRandom() already picks the right OS primitive (getrandom, /dev/urandom,
// BCryptGenRandom, or CryptGenRandom under CX_XP_COMPAT).

#include <cx/platform/os.h>

#include <mbedtls/platform.h>
#include <mbedtls/private/entropy.h>
#include <psa/crypto.h>

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output, size_t output_size)
{
    // No flags are defined as of TF-PSA-Crypto 1.2.
    if (flags != 0) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    // osGenRandom() takes a uint32 length. Nothing in the library asks for
    // anywhere near this much in one call, but the truncation would be silent.
    if (output_size > UINT32_MAX) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    if (!osGenRandom(output, (uint32)output_size)) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    // The OS RNGs behind osGenRandom() are all full-entropy. Reporting less
    // than 8 bits per byte is treated as a failure by the caller in
    // tf-psa-crypto/drivers/builtin/src/entropy_poll.c.
    *estimate_bits = 8 * output_size;

    return 0;
}

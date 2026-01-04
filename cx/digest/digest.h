#pragma once

/// @file digest.h
/// @brief Message digest (hash) functions

/// @defgroup digest Digest Functions
/// @ingroup utils
/// @{
///
/// Cryptographic hash functions including MD5, SHA-1, and SHA-256.
///
/// The digest API provides incremental hashing through a simple three-step process:
/// 1. Initialize a context with digestInit()
/// 2. Feed data in chunks with digestUpdate() (can be called multiple times)
/// 3. Finalize and retrieve the hash with digestFinish()
///
/// All supported algorithms use a 64-byte internal block size and can process data
/// of any length. Contexts can be reused by calling digestInit() again.
///
/// Example:
/// @code
///   Digest ctx;
///   digestInit(&ctx, DIGEST_SHA256);
///   digestUpdate(&ctx, (uint8*)"Hello ", 6);
///   digestUpdate(&ctx, (uint8*)"World", 5);
///   uint8 hash[32];
///   digestFinish(&ctx, hash);
///   // hash now contains the SHA-256 of "Hello World"
/// @endcode

#include <cx/cx.h>

/// Internal block size in bytes used by all supported digest algorithms
#define DIGEST_BLOCKSIZE 64

/// Supported message digest algorithms
typedef enum {
    DIGEST_MD5,      ///< MD5 - 128-bit (16 bytes) - Not cryptographically secure
    DIGEST_SHA1,     ///< SHA-1 - 160-bit (20 bytes) - Deprecated for security
    DIGEST_SHA256,   ///< SHA-256 - 256-bit (32 bytes) - Secure and recommended
    DIGEST_COUNT
} DigestType;

/// Size in bytes of the resulting digest for each algorithm.
/// Index with DigestType: DigestSize[DIGEST_MD5] = 16, DigestSize[DIGEST_SHA1] = 20, DigestSize[DIGEST_SHA256] = 32
extern uint32 DigestSize[DIGEST_COUNT];

/// Digest context structure.
///
/// Contains the state for computing a message digest incrementally.
/// All fields are internal and should not be accessed directly.
typedef struct Digest {
    DigestType type; ///< Algorithm being used
    uint32 size;     ///< Total message size processed
    uint8 buffer[DIGEST_BLOCKSIZE]; ///< Internal data buffer
    uint32 state[8]; ///< Algorithm state (MD5: 4 words, SHA-1: 5 words, SHA-256: 8 words)
} Digest;

/// Initializes a digest context for computing a message digest.
///
/// Must be called before using digestUpdate() or digestFinish(). The context can be
/// reused after digestFinish() by calling digestInit() again.
///
/// @param digest Digest context to initialize
/// @param type Algorithm to use (DIGEST_MD5, DIGEST_SHA1, or DIGEST_SHA256)
///
/// Example:
/// @code
///   Digest ctx;
///   digestInit(&ctx, DIGEST_SHA256);
/// @endcode
void digestInit(_Out_ Digest* digest, DigestType type);

/// Incrementally processes data for the message digest.
///
/// Can be called multiple times to hash data in chunks. All data passed to
/// digestUpdate() is accumulated until digestFinish() is called to produce the
/// final hash.
///
/// @param digest Initialized digest context
/// @param data Data to process
/// @param size Number of bytes to process
///
/// Example:
/// @code
///   digestUpdate(&ctx, (uint8*)"Hello ", 6);
///   digestUpdate(&ctx, (uint8*)"World", 5);
/// @endcode
void digestUpdate(_Inout_ Digest* digest, _In_ uint8* data, uint32 size);

/// Finalizes the digest computation and outputs the hash.
///
/// Completes the digest computation by adding padding and length as required
/// by the algorithm specification, then outputs the final hash value. After
/// calling this function, digestInit() must be called again to reuse the context.
///
/// @param digest Digest context that has been updated with data
/// @param out Buffer to receive the hash output, must be at least DigestSize[type] bytes
///
/// Example:
/// @code
///   uint8 hash[32];  // SHA-256 produces 32 bytes
///   digestFinish(&ctx, hash);
/// @endcode
void digestFinish(_Inout_ Digest* digest, _Out_ uint8* out);

/// @}
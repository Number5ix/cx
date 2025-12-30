#pragma once

// Generic support for several message digest algorithms with 64-byte block size.

#include <cx/cx.h>

#define DIGEST_BLOCKSIZE 64

// Supported message digest algorithms
typedef enum {
    DIGEST_MD5,      // MD5 - 128-bit (16 bytes)
    DIGEST_SHA1,     // SHA-1 - 160-bit (20 bytes)
    DIGEST_SHA256,   // SHA-256 - 256-bit (32 bytes)
    DIGEST_COUNT
} DigestType;

// Size in bytes of the resulting digest for each algorithm
// MD5: 16 bytes, SHA1: 20 bytes, SHA256: 32 bytes
extern uint32 DigestSize[DIGEST_COUNT];

// Digest context structure
//
// Contains the state for computing a message digest incrementally.
// All fields are internal and should not be accessed directly.
typedef struct Digest {
    DigestType type;
    uint32 size;    // message size
    uint8 buffer[DIGEST_BLOCKSIZE]; // data buffer
    uint32 state[8]; // state (MD5: 4 words, SHA1: 5 words, SHA256: 8 words)
} Digest;

// Initializes a digest context for computing a message digest
//
// Must be called before using digestUpdate or digestFinish. The context can be
// reused by calling digestInit again.
//
// Parameters:
//   digest - Digest context to initialize
//   type - Algorithm to use (DIGEST_MD5, DIGEST_SHA1, or DIGEST_SHA256)
//
// Example:
//   Digest ctx;
//   digestInit(&ctx, DIGEST_SHA256);
void digestInit(_Out_ Digest* digest, DigestType type);

// Incrementally processes data for the message digest
//
// Can be called multiple times to hash data in chunks. All data passed to
// digestUpdate is accumulated until digestFinish is called to produce the
// final hash.
//
// Parameters:
//   digest - Initialized digest context
//   data - Data to process
//   size - Number of bytes to process
//
// Example:
//   digestUpdate(&ctx, (uint8*)"Hello ", 6);
//   digestUpdate(&ctx, (uint8*)"World", 5);
void digestUpdate(_Inout_ Digest* digest, _In_ uint8* data, uint32 size);

// Finalizes the digest computation and outputs the hash
//
// Completes the digest computation by adding padding and length as required
// by the algorithm specification, then outputs the final hash value. After
// calling this function, digestInit must be called again to reuse the context.
//
// Parameters:
//   digest - Digest context that has been updated with data
//   out - Buffer to receive the hash output, must be at least DigestSize[type] bytes
//
// Example:
//   uint8 hash[32];  // SHA256 produces 32 bytes
//   digestFinish(&ctx, hash);
void digestFinish(_Inout_ Digest* digest, _Out_ uint8* out);
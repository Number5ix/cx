// MurmurHash3 by Austin Appleby
// Code is in the public domain

#include "murmur.h"
#include "cx/utils/lazyinit.h"
#include "cx/string.h"
#include <mbedtls/entropy.h>

static LazyInitState msInit;
static uint32 murmur_seed;

static void initSeed(void *unused)
{
    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);
    mbedtls_entropy_func(&entropy, (unsigned char*)&murmur_seed, sizeof(murmur_seed));
    mbedtls_entropy_free(&entropy);
}

uint32 hashMurmur3(const uint8* key, size_t len)
{
    lazyInit(&msInit, initSeed, NULL);

    uint32_t h = murmur_seed;
    if (len > 3) {
        size_t i = len >> 2;
        do {
            uint32_t k;
            memcpy(&k, key, sizeof(uint32_t));
            key += sizeof(uint32_t);
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        do {
            k <<= 8;
            k |= key[i - 1];
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

// For use with case-insensitive hashes
// This uses the bit 6 trick to coerce everything to lowercase while hashing.
// Note that it will also cause some extra hash collisions if used on binary
// data, and of course does not work for UTF-8 strings that use a non-english
// alphabet
uint32 hashMurmur3i(const uint8* key, size_t len)
{
    lazyInit(&msInit, initSeed, NULL);

    uint32_t h = murmur_seed;
    if (len > 3) {
        size_t i = len >> 2;
        do {
            uint32_t k;
            memcpy(&k, key, sizeof(uint32_t));
            k |= 0x20202020;
            key += sizeof(uint32_t);
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        do {
            k <<= 8;
            k |= key[i - 1] | 0x20;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32 hashMurmur3Str(string s)
{
    lazyInit(&msInit, initSeed, NULL);

    size_t len = strLen(s);
    striter sit;
    striBorrow(&sit, s);

    uint32_t h = murmur_seed;
    if (len > 3) {
        size_t i = len >> 2;
        uint32 off = 0;
        do {
            uint32_t k;
            strCopyOut(s, off, (char*)&k, sizeof(k));
            off += sizeof(k);
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        do {
            k <<= 8;
            k |= strGetChar(s, (uint32)i - 1);
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32 hashMurmur3Stri(string s)
{
    lazyInit(&msInit, initSeed, NULL);

    size_t len = strLen(s);
    striter sit;
    striBorrow(&sit, s);

    uint32_t h = murmur_seed;
    if (len > 3) {
        size_t i = len >> 2;
        uint32 off = 0;
        do {
            uint32_t k;
            strCopyOut(s, off, (char*)&k, sizeof(k));
            k |= 0x20202020;
            off += sizeof(k);
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        do {
            k <<= 8;
            k |= strGetChar(s, (uint32)i - 1) | 0x20;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}
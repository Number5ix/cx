#include <cx/buffer/bufchain.h>
#include <cx/xalloc/xalloc.h>
#include <cx/string.h>

#define TEST_FILE buftest
#define TEST_FUNCS buftest_funcs
#include "common.h"

static const uint8 testdata1[] = "This is a test. This is a test. This is a test. This is a test. This is a test. This is a test. This is a test. This is a test.";
#define TESTDATA_LEN (sizeof(testdata1) - 1)

// Additional data for tests that need more content
static const uint8 testdata2[] = "More test data here. More test data here. More test data here. More test data here. More test data here. More test data here. More test data here.";
#define TESTDATA2_LEN (sizeof(testdata2) - 1)

static int test_bufchain_basic()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[512];
    
    // Test with 64-byte segment size (minimum enforced by implementation)
    bufchainInit(&chain, 64);

    // Write some data
    bufchainWrite(&chain, testdata1, 20);

    if (chain.total != 20)
        ret = 1;
    
    // Read it back
    size_t nread = bufchainRead(&chain, readbuf, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        ret = 1;
    
    if (chain.total != 0)
        ret = 1;
    
    // Write and read across segment boundary (64 bytes)
    bufchainWrite(&chain, testdata1, 80);
    nread = bufchainRead(&chain, readbuf, 80);
    if (nread != 80 || memcmp(readbuf, testdata1, 80))
        ret = 1;
    
    // Write more than one segment, read in parts
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, TESTDATA2_LEN);

    nread = bufchainRead(&chain, readbuf, 50);
    if (nread != 50 || memcmp(readbuf, testdata1, 50))
        ret = 1;

    nread = bufchainRead(&chain, readbuf + 50, 60);
    if (nread != 60 || memcmp(readbuf + 50, testdata1 + 50, 60))
        ret = 1;
    
    // Read across the boundary between testdata1 and testdata2
    size_t remaining1 = TESTDATA_LEN - 110;
    size_t remaining_total = remaining1 + TESTDATA2_LEN;
    nread                  = bufchainRead(&chain, readbuf + 110, remaining_total);
    if (nread != remaining_total)
        ret = 1;
    
    if (memcmp(readbuf + 110, testdata1 + 110, remaining1) ||
        memcmp(readbuf + 110 + remaining1, testdata2, TESTDATA2_LEN))
        ret = 1;
    
    if (chain.total != 0)
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

static int test_bufchain_peek()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[256];

    bufchainInit(&chain, 64);

    // Write test data spanning segments
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, 50);
    size_t total = chain.total;
    
    // Peek at beginning
    size_t nread = bufchainPeek(&chain, readbuf, 0, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        ret = 1;
    
    // Data should still be there
    if (chain.total != total)
        ret = 1;
    
    // Peek at offset
    nread = bufchainPeek(&chain, readbuf, 10, 30);
    if (nread != 30 || memcmp(readbuf, testdata1 + 10, 30))
        ret = 1;
    
    // Peek across segment boundary (64-byte segments)
    nread = bufchainPeek(&chain, readbuf, 50, 60);
    if (nread != 60 || memcmp(readbuf, testdata1 + 50, 60))
        ret = 1;
    
    // Peek near end
    nread = bufchainPeek(&chain, readbuf, total - 20, 20);
    if (nread != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20))
        ret = 1;
    
    // Peek past end
    nread = bufchainPeek(&chain, readbuf, total - 5, 20);
    if (nread != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5))
        ret = 1;
    
    // Now actually read some data
    nread = bufchainRead(&chain, readbuf, 30);
    if (nread != 30 || memcmp(readbuf, testdata1, 30))
        ret = 1;
    
    // Peek at what's left
    nread = bufchainPeek(&chain, readbuf, 0, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

static int test_bufchain_skip()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[256];

    bufchainInit(&chain, 64);

    // Write test data spanning segments
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, 50);

    // Skip some data
    size_t nskipped = bufchainSkip(&chain, 30);
    size_t total = TESTDATA_LEN + 50;
    if (nskipped != 30 || chain.total != (total - 30))
        ret = 1;
    
    // Read what's left from testdata1
    size_t nread = bufchainRead(&chain, readbuf, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        ret = 1;
    
    // Skip across segment boundary (64 bytes)
    nskipped = bufchainSkip(&chain, 70);
    if (nskipped != 70)
        ret = 1;
    
    // Read remainder (should be from testdata2)
    nread           = bufchainRead(&chain, readbuf, 100);
    size_t expected = total - 30 - 40 - 70;
    if (nread != expected || memcmp(readbuf, testdata2 + (50 - expected), expected))
        ret = 1;
    
    // Skip more than available
    bufchainWrite(&chain, testdata1, 20);
    nskipped = bufchainSkip(&chain, 100);
    if (nskipped != 20 || chain.total != 0)
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

typedef struct TestZCCtx {
    uint8 *out;
    size_t outp;
    size_t maxout;
    bool shouldConsume;
} TestZCCtx;

static bool zcReadCallback(const uint8 *buf, size_t bytes, void *ctx)
{
    TestZCCtx *tc = (TestZCCtx *)ctx;
    
    if (tc->outp + bytes > tc->maxout)
        return false;
    
    memcpy(tc->out + tc->outp, buf, bytes);
    tc->outp += bytes;
    
    return tc->shouldConsume;
}

static int test_bufchain_zerocopy_read()
{
    int ret = 0;
    BufChain chain;
    TestZCCtx ctx = { 0 };
    
    ctx.out = xaAlloc(256);
    ctx.maxout = 256;

    bufchainInit(&chain, 64);

    // Write test data spanning segments
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, 50);
    size_t total = chain.total;
    
    // Zero-copy read with consumption
    ctx.shouldConsume = true;
    size_t nread      = bufchainReadZC(&chain, 80, zcReadCallback, &ctx);
    if (nread != 80 || ctx.outp != 80 || memcmp(ctx.out, testdata1, 80))
        ret = 1;
    
    // Data should be consumed
    if (chain.total != (total - 80))
        ret = 1;
    
    // Zero-copy read without consumption
    ctx.outp = 0;
    ctx.shouldConsume = false;
    size_t remaining = chain.total;
    nread             = bufchainReadZC(&chain, 50, zcReadCallback, &ctx);
    if (nread != 50 || ctx.outp != 50)
        ret = 1;
    
    // Check data: 47 bytes from testdata1[80..126], then 3 bytes from testdata2
    if (memcmp(ctx.out, testdata1 + 80, TESTDATA_LEN - 80) ||
        memcmp(ctx.out + (TESTDATA_LEN - 80), testdata2, 50 - (TESTDATA_LEN - 80)))
        ret = 1;
    
    // Data should NOT be consumed
    if (chain.total != remaining)
        ret = 1;
    
    // Now consume it
    ctx.outp = 0;
    ctx.shouldConsume = true;
    nread             = bufchainReadZC(&chain, remaining, zcReadCallback, &ctx);
    if (nread != remaining || chain.total != 0)
        ret = 1;
    
    // Check the data spans from testdata1 to testdata2
    size_t from_td1 = TESTDATA_LEN - 80;
    if (memcmp(ctx.out, testdata1 + 80, from_td1) ||
        memcmp(ctx.out + from_td1, testdata2, 50))
        ret = 1;
    
    xaFree(ctx.out);
    bufchainDestroy(&chain);
    return ret;
}

static int test_bufchain_zerocopy_write()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[256];

    bufchainInit(&chain, 64);

    // Allocate buffers for zero-copy write
    uint8 *buf1 = xaAlloc(70);
    memcpy(buf1, testdata1, 70);
    
    uint8 *buf2 = xaAlloc(60);
    memcpy(buf2, testdata2, 60);
    
    // Zero-copy write
    bufchainWriteZC(&chain, buf1, 70, 70);
    bufchainWriteZC(&chain, buf2, 60, 60);

    if (chain.total != 130)
        ret = 1;
    
    // Read back and verify
    size_t nread = bufchainRead(&chain, readbuf, 130);
    if (nread != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60))
        ret = 1;
    
    if (chain.total != 0)
        ret = 1;
    
    // Test partial buffer (size > bytes)
    uint8 *buf3 = xaAlloc(100);
    memcpy(buf3, testdata2, 55);
    bufchainWriteZC(&chain, buf3, 100, 55);

    if (chain.total != 55)
        ret = 1;

    nread = bufchainRead(&chain, readbuf, 55);
    if (nread != 55 || memcmp(readbuf, testdata2, 55))
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

static int test_bufchain_wraparound()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[256];
    
    // Use 64-byte segment (minimum)
    bufchainInit(&chain, 64);

    // Write, read partial, write more to cause wraparound
    bufchainWrite(&chain, testdata1, 50);
    size_t nread = bufchainRead(&chain, readbuf, 35);
    if (nread != 35 || memcmp(readbuf, testdata1, 35))
        ret = 1;
    
    // This should wraparound in the ring buffer
    bufchainWrite(&chain, testdata1 + 50, 55);

    // Read back and verify
    nread = bufchainRead(&chain, readbuf, 70);
    if (nread != 70)
        ret = 1;
    
    // First 15 bytes from first write, then 55 from second write
    if (memcmp(readbuf, testdata1 + 35, 15) || memcmp(readbuf + 15, testdata1 + 50, 55))
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

static int test_bufchain_multisegment()
{
    int ret = 0;
    BufChain chain;
    uint8 readbuf[512];
    
    // Use 64-byte segments (minimum)
    bufchainInit(&chain, 64);

    // Write enough to span multiple segments
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, TESTDATA2_LEN);
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);

    size_t total_written = TESTDATA_LEN + TESTDATA2_LEN + TESTDATA_LEN;
    
    // Should have multiple nodes (total > 128 bytes with 64-byte segments)
    int nodecount = 0;
    for (BufChainNode *node = chain.head; node; node = node->next)
        nodecount++;
    
    if (nodecount < 3)
        ret = 1;
    
    // Read across multiple segments
    size_t nread = bufchainRead(&chain, readbuf, 100);
    if (nread != 100 || memcmp(readbuf, testdata1, 100))
        ret = 1;
    
    // Read more across segments (should span into testdata2)
    nread = bufchainRead(&chain, readbuf + 100, 80);
    if (nread != 80)
        ret = 1;
    
    size_t from_td1 = TESTDATA_LEN - 100;
    if (memcmp(readbuf + 100, testdata1 + 100, from_td1) ||
        memcmp(readbuf + 100 + from_td1, testdata2, 80 - from_td1))
        ret = 1;
    
    // Read rest
    nread = bufchainRead(&chain, readbuf + 180, 500);
    if (nread != (total_written - 180))
        ret = 1;
    
    if (chain.total != 0)
        ret = 1;

    bufchainDestroy(&chain);
    return ret;
}

testfunc buftest_funcs[] = {
    { "basic", test_bufchain_basic },
    { "peek", test_bufchain_peek },
    { "skip", test_bufchain_skip },
    { "zerocopy_read", test_bufchain_zerocopy_read },
    { "zerocopy_write", test_bufchain_zerocopy_write },
    { "wraparound", test_bufchain_wraparound },
    { "multisegment", test_bufchain_multisegment },
    { 0, 0 }
};

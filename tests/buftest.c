#include <cx/buffer/bufchain.h>
#include <cx/buffer/buffer.h>
#include <cx/buffer/bufring.h>
#include <cx/string.h>
#include <cx/xalloc/xalloc.h>

#define TEST_FILE buftest
#define TEST_FUNCS buftest_funcs
#include "common.h"

static const uint8 testdata1[] = "This is a test. This is a test. This is a test. This is a test. This is a test. This is a test. This is a test. This is a test.";
#define TESTDATA_LEN (sizeof(testdata1) - 1)

// Additional data for tests that need more content
static const uint8 testdata2[] = "More test data here. More test data here. More test data here. More test data here. More test data here. More test data here. More test data here.";
#define TESTDATA2_LEN (sizeof(testdata2) - 1)

// ============= Simple Buffer Tests =============

static int test_buffer_create()
{
    int ret = 0;

    // Test basic creation
    buffer buf = bufCreate(256);
    if (!buf || buf->sz != 256 || buf->len != 0)
        ret = 1;

    // Write some data
    memcpy(buf->data, testdata1, 50);
    buf->len = 50;

    if (buf->len != 50 || memcmp(buf->data, testdata1, 50))
        ret = 1;

    bufDestroy(&buf);
    if (buf != NULL)
        ret = 1;

    // Test try_create variant that should succeed
    buf = bufTryCreate(512);
    if (!buf || buf->sz != 512 || buf->len != 0)
        ret = 1;

    // Write and verify data
    memcpy(buf->data, testdata2, 100);
    buf->len = 100;

    if (memcmp(buf->data, testdata2, 100))
        ret = 1;

    bufDestroy(&buf);

    // Test with very large size (may or may not fail, but shouldn't crash)
    buffer largeBuf = bufTryCreate(1024 * 1024 * 1024);   // 1GB
    if (largeBuf) {
        // If it succeeded, clean it up
        bufDestroy(&largeBuf);
    }

    return ret;
}

static int test_buffer_resize()
{
    int ret = 0;

    // Test resize on NULL buffer (should create)
    buffer buf = NULL;
    bufResize(&buf, 128);
    if (!buf || buf->sz != 128 || buf->len != 0)
        ret = 1;

    // Add some data
    memcpy(buf->data, testdata1, 80);
    buf->len = 80;

    // Resize larger - data should be preserved
    bufResize(&buf, 256);
    if (!buf || buf->sz != 256 || buf->len != 80)
        ret = 1;

    if (memcmp(buf->data, testdata1, 80))
        ret = 1;

    // Resize smaller - length should be truncated
    bufResize(&buf, 50);
    if (!buf || buf->sz != 50 || buf->len != 50)
        ret = 1;

    if (memcmp(buf->data, testdata1, 50))
        ret = 1;

    // Resize to same size - should be no-op
    buffer oldBuf = buf;
    bufResize(&buf, 50);
    if (buf != oldBuf)
        ret = 1;

    bufDestroy(&buf);

    // Test try_resize variant on NULL buffer
    buf          = NULL;
    bool success = bufTryResize(&buf, 128);
    if (!success || !buf || buf->sz != 128)
        ret = 1;

    // Add data
    memcpy(buf->data, testdata1, 100);
    buf->len = 100;

    // Resize larger
    success = bufTryResize(&buf, 512);
    if (!success || !buf || buf->sz != 512 || buf->len != 100)
        ret = 1;

    if (memcmp(buf->data, testdata1, 100))
        ret = 1;

    // Resize smaller with truncation
    success = bufTryResize(&buf, 60);
    if (!success || !buf || buf->sz != 60 || buf->len != 60)
        ret = 1;

    // Resize to same size
    success = bufTryResize(&buf, 60);
    if (!success)
        ret = 1;

    bufDestroy(&buf);
    return ret;
}

// ============= BufChain Tests =============

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

    // Write across segment boundary (64 bytes)
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

typedef struct TestBufChainZCCtx {
    uint8* out;
    size_t outp;
    size_t maxout;
    int segcount;
} TestBufChainZCCtx;

static bool bufchainZCReadCallback(buffer buf, size_t off, void* ctx)
{
    TestBufChainZCCtx* tc = (TestBufChainZCCtx*)ctx;

    size_t bytes = buf->len - off;
    if (tc->outp + bytes > tc->maxout) {
        bufDestroy(&buf);
        return false;
    }

    memcpy(tc->out + tc->outp, buf->data + off, bytes);
    tc->outp += bytes;
    tc->segcount++;

    bufDestroy(&buf);
    return true;
}

static int test_bufchain_zerocopy_read()
{
    int ret = 0;
    BufChain chain;
    TestBufChainZCCtx ctx = { 0 };

    ctx.out    = xaAlloc(512);
    ctx.maxout = 512;

    bufchainInit(&chain, 64);

    // Write test data spanning segments
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, 50);
    size_t total = chain.total;

    // Zero-copy read - should get complete segments
    size_t nread = bufchainReadZC(&chain, total, bufchainZCReadCallback, &ctx);

    // Data should be consumed
    if (chain.total != 0)
        ret = 1;

    // Verify we got multiple segments
    if (ctx.segcount < 2)
        ret = 1;

    // Verify the data
    if (memcmp(ctx.out, testdata1, TESTDATA_LEN) || memcmp(ctx.out + TESTDATA_LEN, testdata2, 50))
        ret = 1;

    // Test with partial read (partial read from head segment)
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    nread = bufchainRead(&chain, ctx.out, 30);   // Partial read from first segment

    // Now do zero-copy read - should get offset in first callback
    ctx.outp     = 30;
    ctx.segcount = 0;
    nread        = bufchainReadZC(&chain, 200, bufchainZCReadCallback, &ctx);

    if (chain.total != 0 || ctx.segcount < 1)
        ret = 1;

    // Verify complete data
    if (memcmp(ctx.out, testdata1, TESTDATA_LEN))
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
    buffer buf1 = bufCreate(70);
    memcpy(buf1->data, testdata1, 70);
    buf1->len = 70;

    buffer buf2 = bufCreate(60);
    memcpy(buf2->data, testdata2, 60);
    buf2->len = 60;

    // Zero-copy write
    bufchainWriteZC(&chain, &buf1);
    bufchainWriteZC(&chain, &buf2);

    // Buffers should be NULL now (ownership transferred)
    if (buf1 != NULL || buf2 != NULL)
        ret = 1;

    if (chain.total != 130)
        ret = 1;

    // Read back and verify
    size_t nread = bufchainRead(&chain, readbuf, 130);
    if (nread != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60))
        ret = 1;

    if (chain.total != 0)
        ret = 1;

    // Test partial buffer (len < size)
    buffer buf3 = bufCreate(100);
    memcpy(buf3->data, testdata2, 55);
    buf3->len = 55;
    bufchainWriteZC(&chain, &buf3);

    if (chain.total != 55)
        ret = 1;

    nread = bufchainRead(&chain, readbuf, 55);
    if (nread != 55 || memcmp(readbuf, testdata2, 55))
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
    for (BufChainNode* node = chain.head; node; node = node->next) nodecount++;

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

// ============= BufRing Tests =============

static int test_bufring_basic()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[512];

    // Test with 64-byte segment size (minimum enforced by implementation)
    bufringInit(&ring, 64);

    // Write some data
    bufringWrite(&ring, testdata1, 20);

    if (ring.total != 20)
        ret = 1;

    // Read it back
    size_t nread = bufringRead(&ring, readbuf, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        ret = 1;

    if (ring.total != 0)
        ret = 1;

    // Write and read across segment boundary (64 bytes)
    bufringWrite(&ring, testdata1, 80);
    nread = bufringRead(&ring, readbuf, 80);
    if (nread != 80 || memcmp(readbuf, testdata1, 80))
        ret = 1;

    // Write more than one segment, read in parts
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, TESTDATA2_LEN);

    nread = bufringRead(&ring, readbuf, 50);
    if (nread != 50 || memcmp(readbuf, testdata1, 50))
        ret = 1;

    nread = bufringRead(&ring, readbuf + 50, 60);
    if (nread != 60 || memcmp(readbuf + 50, testdata1 + 50, 60))
        ret = 1;

    // Read across the boundary between testdata1 and testdata2
    size_t remaining1      = TESTDATA_LEN - 110;
    size_t remaining_total = remaining1 + TESTDATA2_LEN;
    nread                  = bufringRead(&ring, readbuf + 110, remaining_total);
    if (nread != remaining_total)
        ret = 1;

    if (memcmp(readbuf + 110, testdata1 + 110, remaining1) ||
        memcmp(readbuf + 110 + remaining1, testdata2, TESTDATA2_LEN))
        ret = 1;

    if (ring.total != 0)
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_peek()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[256];

    bufringInit(&ring, 64);

    // Write test data spanning segments
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, 50);
    size_t total = ring.total;

    // Peek at beginning
    size_t nread = bufringPeek(&ring, readbuf, 0, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        ret = 1;

    // Data should still be there
    if (ring.total != total)
        ret = 1;

    // Peek at offset
    nread = bufringPeek(&ring, readbuf, 10, 30);
    if (nread != 30 || memcmp(readbuf, testdata1 + 10, 30))
        ret = 1;

    // Peek across segment boundary (64-byte segments)
    nread = bufringPeek(&ring, readbuf, 50, 60);
    if (nread != 60 || memcmp(readbuf, testdata1 + 50, 60))
        ret = 1;

    // Peek near end
    nread = bufringPeek(&ring, readbuf, total - 20, 20);
    if (nread != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20))
        ret = 1;

    // Peek past end
    nread = bufringPeek(&ring, readbuf, total - 5, 20);
    if (nread != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5))
        ret = 1;

    // Now actually read some data
    nread = bufringRead(&ring, readbuf, 30);
    if (nread != 30 || memcmp(readbuf, testdata1, 30))
        ret = 1;

    // Peek at what's left
    nread = bufringPeek(&ring, readbuf, 0, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_skip()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[256];

    bufringInit(&ring, 64);

    // Write test data spanning segments
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, 50);

    // Skip some data
    size_t nskipped = bufringSkip(&ring, 30);
    size_t total    = TESTDATA_LEN + 50;
    if (nskipped != 30 || ring.total != (total - 30))
        ret = 1;

    // Read what's left from testdata1
    size_t nread = bufringRead(&ring, readbuf, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        ret = 1;

    // Skip across segment boundary (64 bytes)
    nskipped = bufringSkip(&ring, 70);
    if (nskipped != 70)
        ret = 1;

    // Read remainder (should be from testdata2)
    nread           = bufringRead(&ring, readbuf, 100);
    size_t expected = total - 30 - 40 - 70;
    if (nread != expected || memcmp(readbuf, testdata2 + (50 - expected), expected))
        ret = 1;

    // Skip more than available
    bufringWrite(&ring, testdata1, 20);
    nskipped = bufringSkip(&ring, 100);
    if (nskipped != 20 || ring.total != 0)
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

typedef struct TestBufRingZCCtx {
    uint8 *out;
    size_t outp;
    size_t maxout;
    bool shouldConsume;
} TestBufRingZCCtx;

static bool bufringZCReadCallback(const uint8* buf, size_t bytes, void* ctx)
{
    TestBufRingZCCtx* tc = (TestBufRingZCCtx*)ctx;

    if (tc->outp + bytes > tc->maxout)
        return false;
    
    memcpy(tc->out + tc->outp, buf, bytes);
    tc->outp += bytes;
    
    return tc->shouldConsume;
}

static int test_bufring_zerocopy_read()
{
    int ret = 0;
    BufRing ring;
    TestBufRingZCCtx ctx = { 0 };

    ctx.out = xaAlloc(256);
    ctx.maxout = 256;

    bufringInit(&ring, 64);

    // Write test data spanning segments
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, 50);
    size_t total = ring.total;

    // Zero-copy read with consumption
    ctx.shouldConsume = true;
    size_t nread      = bufringReadZC(&ring, 80, bufringZCReadCallback, &ctx);
    if (nread != 80 || ctx.outp != 80 || memcmp(ctx.out, testdata1, 80))
        ret = 1;
    
    // Data should be consumed
    if (ring.total != (total - 80))
        ret = 1;
    
    // Zero-copy read without consumption
    ctx.outp = 0;
    ctx.shouldConsume = false;
    size_t remaining  = ring.total;
    nread             = bufringReadZC(&ring, 50, bufringZCReadCallback, &ctx);
    if (nread != 50 || ctx.outp != 50)
        ret = 1;
    
    // Check data: 47 bytes from testdata1[80..126], then 3 bytes from testdata2
    if (memcmp(ctx.out, testdata1 + 80, TESTDATA_LEN - 80) ||
        memcmp(ctx.out + (TESTDATA_LEN - 80), testdata2, 50 - (TESTDATA_LEN - 80)))
        ret = 1;
    
    // Data should NOT be consumed
    if (ring.total != remaining)
        ret = 1;
    
    // Now consume it
    ctx.outp = 0;
    ctx.shouldConsume = true;
    nread             = bufringReadZC(&ring, remaining, bufringZCReadCallback, &ctx);
    if (nread != remaining || ring.total != 0)
        ret = 1;
    
    // Check the data spans from testdata1 to testdata2
    size_t from_td1 = TESTDATA_LEN - 80;
    if (memcmp(ctx.out, testdata1 + 80, from_td1) ||
        memcmp(ctx.out + from_td1, testdata2, 50))
        ret = 1;
    
    xaFree(ctx.out);
    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_zerocopy_write()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[256];

    bufringInit(&ring, 64);

    // Allocate buffers for zero-copy write
    buffer buf1 = bufCreate(70);
    memcpy(buf1->data, testdata1, 70);
    buf1->len = 70;

    buffer buf2 = bufCreate(60);
    memcpy(buf2->data, testdata2, 60);
    buf2->len = 60;

    // Zero-copy write
    bufringWriteZC(&ring, &buf1);
    bufringWriteZC(&ring, &buf2);

    // Buffers should be NULL now (ownership transferred)
    if (buf1 != NULL || buf2 != NULL)
        ret = 1;

    if (ring.total != 130)
        ret = 1;
    
    // Read back and verify
    size_t nread = bufringRead(&ring, readbuf, 130);
    if (nread != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60))
        ret = 1;

    if (ring.total != 0)
        ret = 1;

    // Test partial buffer (len < size)
    buffer buf3 = bufCreate(100);
    memcpy(buf3->data, testdata2, 55);
    buf3->len = 55;
    bufringWriteZC(&ring, &buf3);

    if (ring.total != 55)
        ret = 1;

    nread = bufringRead(&ring, readbuf, 55);
    if (nread != 55 || memcmp(readbuf, testdata2, 55))
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_wraparound()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[256];
    
    // Use 64-byte segment (minimum)
    bufringInit(&ring, 64);

    // Write, read partial, write more to cause wraparound
    bufringWrite(&ring, testdata1, 50);
    size_t nread = bufringRead(&ring, readbuf, 35);
    if (nread != 35 || memcmp(readbuf, testdata1, 35))
        ret = 1;
    
    // This should wraparound in the ring buffer
    bufringWrite(&ring, testdata1 + 50, 55);

    // Read back and verify
    nread = bufringRead(&ring, readbuf, 70);
    if (nread != 70)
        ret = 1;
    
    // First 15 bytes from first write, then 55 from second write
    if (memcmp(readbuf, testdata1 + 35, 15) || memcmp(readbuf + 15, testdata1 + 50, 55))
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_multisegment()
{
    int ret = 0;
    BufRing ring;
    uint8 readbuf[512];
    
    // Use 64-byte segments (minimum)
    bufringInit(&ring, 64);

    // Write enough to span multiple segments
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, TESTDATA2_LEN);
    bufringWrite(&ring, testdata1, TESTDATA_LEN);

    size_t total_written = TESTDATA_LEN + TESTDATA2_LEN + TESTDATA_LEN;
    
    // Should have multiple nodes (total > 128 bytes with 64-byte segments)
    int nodecount = 0;
    for (BufRingNode* node = ring.head; node; node = node->next) nodecount++;

    if (nodecount < 3)
        ret = 1;
    
    // Read across multiple segments
    size_t nread = bufringRead(&ring, readbuf, 100);
    if (nread != 100 || memcmp(readbuf, testdata1, 100))
        ret = 1;
    
    // Read more across segments (should span into testdata2)
    nread = bufringRead(&ring, readbuf + 100, 80);
    if (nread != 80)
        ret = 1;
    
    size_t from_td1 = TESTDATA_LEN - 100;
    if (memcmp(readbuf + 100, testdata1 + 100, from_td1) ||
        memcmp(readbuf + 100 + from_td1, testdata2, 80 - from_td1))
        ret = 1;
    
    // Read rest
    nread = bufringRead(&ring, readbuf + 180, 500);
    if (nread != (total_written - 180))
        ret = 1;

    if (ring.total != 0)
        ret = 1;

    bufringDestroy(&ring);
    return ret;
}

testfunc buftest_funcs[] = {
    { "create",               test_buffer_create           },
    { "resize",               test_buffer_resize           },
    { "chain_basic",          test_bufchain_basic          },
    { "chain_peek",           test_bufchain_peek           },
    { "chain_skip",           test_bufchain_skip           },
    { "chain_zerocopy_read",  test_bufchain_zerocopy_read  },
    { "chain_zerocopy_write", test_bufchain_zerocopy_write },
    { "chain_multisegment",   test_bufchain_multisegment   },
    { "ring_basic",           test_bufring_basic           },
    { "ring_peek",            test_bufring_peek            },
    { "ring_skip",            test_bufring_skip            },
    { "ring_zerocopy_read",   test_bufring_zerocopy_read   },
    { "ring_zerocopy_write",  test_bufring_zerocopy_write  },
    { "ring_wraparound",      test_bufring_wraparound      },
    { "ring_multisegment",    test_bufring_multisegment    },
    { 0,                      0                            }
};

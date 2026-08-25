#include <cx/buffer/bufchain.h>
#include <cx/buffer/bufpool.h>
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
    Buffer buf = bufCreate(256);
    if (!buf || buf->sz != 256 || buf->len != 0)
        TEST_FAILV(ret, 1, _SL("!buf || buf->sz=${int} != 256 || buf->len=${int} != 0"), stvar(size, buf->sz), stvar(size, buf->len));

    // Write some data
    memcpy(buf->data, testdata1, 50);
    buf->len = 50;

    if (buf->len != 50 || memcmp(buf->data, testdata1, 50))
        TEST_FAILV(ret, 1, _SL("buf->len=${int} != 50 || memcmp(buf->data, testdata1, 50)"), stvar(size, buf->len));

    bufDestroy(&buf);
    if (buf != NULL)
        TEST_FAILV(ret, 1, _SL("buf != NULL"), stvNone);

    // Test try_create variant that should succeed
    buf = bufTryCreate(512);
    if (!buf)
        TEST_FAIL(1, _SL("!buf"), stvNone);
    if (buf->sz != 512 || buf->len != 0)
        TEST_FAILV(ret, 1, _SL("buf->sz=${int} != 512 || buf->len=${int} != 0"), stvar(size, buf->sz), stvar(size, buf->len));

    // Write and verify data
    memcpy(buf->data, testdata2, 100);
    buf->len = 100;

    if (memcmp(buf->data, testdata2, 100))
        TEST_FAILV(ret, 1, _SL("memcmp(buf->data, testdata2, 100)"), stvNone);

    bufDestroy(&buf);

    // Test with very large size (may or may not fail, but shouldn't crash)
    Buffer largeBuf = bufTryCreate(1024 * 1024 * 1024);   // 1GB
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
    Buffer buf = NULL;
    bufResize(&buf, 128);
    if (!buf || buf->sz != 128 || buf->len != 0)
        TEST_FAILV(ret, 1, _SL("!buf || buf->sz=${int} != 128 || buf->len=${int} != 0"), stvar(size, buf->sz), stvar(size, buf->len));

    // Add some data
    memcpy(buf->data, testdata1, 80);
    buf->len = 80;

    // Resize larger - data should be preserved
    bufResize(&buf, 256);
    if (!buf || buf->sz != 256 || buf->len != 80)
        TEST_FAILV(ret, 1, _SL("!buf || buf->sz=${int} != 256 || buf->len=${int} != 80"), stvar(size, buf->sz), stvar(size, buf->len));

    if (memcmp(buf->data, testdata1, 80))
        TEST_FAILV(ret, 1, _SL("memcmp(buf->data, testdata1, 80)"), stvNone);

    // Resize smaller - length should be truncated
    bufResize(&buf, 50);
    if (!buf || buf->sz != 50 || buf->len != 50)
        TEST_FAILV(ret, 1, _SL("!buf || buf->sz=${int} != 50 || buf->len=${int} != 50"), stvar(size, buf->sz), stvar(size, buf->len));

    if (memcmp(buf->data, testdata1, 50))
        TEST_FAILV(ret, 1, _SL("memcmp(buf->data, testdata1, 50)"), stvNone);

    // Resize to same size - should be no-op
    Buffer oldBuf = buf;
    bufResize(&buf, 50);
    if (buf != oldBuf)
        TEST_FAILV(ret, 1, _SL("buf != oldBuf"), stvNone);

    bufDestroy(&buf);

    // Test try_resize variant on NULL buffer
    buf          = NULL;
    bool success = bufTryResize(&buf, 128);
    if (!buf)
        TEST_FAIL(1, _SL("!buf"), stvNone);
    if (!success || buf->sz != 128)
        TEST_FAILV(ret, 1, _SL("!success || buf->sz=${int} != 128"), stvar(size, buf->sz));

    // Add data
    memcpy(buf->data, testdata1, 100);
    buf->len = 100;

    // Resize larger
    success = bufTryResize(&buf, 512);
    if (!success || !buf || buf->sz != 512 || buf->len != 100)
        TEST_FAILV(ret, 1, _SL("!success || !buf || buf->sz=${int} != 512 || buf->len=${int} != 100"), stvar(size, buf->sz), stvar(size, buf->len));

    if (memcmp(buf->data, testdata1, 100))
        TEST_FAILV(ret, 1, _SL("memcmp(buf->data, testdata1, 100)"), stvNone);

    // Resize smaller with truncation
    success = bufTryResize(&buf, 60);
    if (!success || !buf || buf->sz != 60 || buf->len != 60)
        TEST_FAILV(ret, 1, _SL("!success || !buf || buf->sz=${int} != 60 || buf->len=${int} != 60"), stvar(size, buf->sz), stvar(size, buf->len));

    // Resize to same size
    success = bufTryResize(&buf, 60);
    if (!success)
        TEST_FAILV(ret, 1, _SL("!success"), stvNone);

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
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 20"), stvar(size, chain.total));
    
    // Read it back
    size_t nread = bufchainRead(&chain, readbuf, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata1, 20)"), stvar(size, nread));
    
    if (chain.total != 0)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0"), stvar(size, chain.total));

    // Write across segment boundary (64 bytes)
    bufchainWrite(&chain, testdata1, 80);
    nread = bufchainRead(&chain, readbuf, 80);
    if (nread != 80 || memcmp(readbuf, testdata1, 80))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 80 || memcmp(readbuf, testdata1, 80)"), stvar(size, nread));
    
    // Write more than one segment, read in parts
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainWrite(&chain, testdata2, TESTDATA2_LEN);

    nread = bufchainRead(&chain, readbuf, 50);
    if (nread != 50 || memcmp(readbuf, testdata1, 50))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 50 || memcmp(readbuf, testdata1, 50)"), stvar(size, nread));

    nread = bufchainRead(&chain, readbuf + 50, 60);
    if (nread != 60 || memcmp(readbuf + 50, testdata1 + 50, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 60 || memcmp(readbuf + 50, testdata1 + 50, 60)"), stvar(size, nread));
    
    // Read across the boundary between testdata1 and testdata2
    size_t remaining1 = TESTDATA_LEN - 110;
    size_t remaining_total = remaining1 + TESTDATA2_LEN;
    nread                  = bufchainRead(&chain, readbuf + 110, remaining_total);
    if (nread != remaining_total)
        TEST_FAILV(ret, 1, _SL("nread=${int} != remaining_total=${int}"), stvar(size, nread), stvar(size, remaining_total));
    
    if (memcmp(readbuf + 110, testdata1 + 110, remaining1) ||
        memcmp(readbuf + 110 + remaining1, testdata2, TESTDATA2_LEN))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: readbuf vs testdata1/testdata2 tail"), stvNone);
    
    if (chain.total != 0)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0"), stvar(size, chain.total));

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
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata1, 20)"), stvar(size, nread));
    
    // Data should still be there
    if (chain.total != total)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != total=${int}"), stvar(size, chain.total), stvar(size, total));
    
    // Peek at offset
    nread = bufchainPeek(&chain, readbuf, 10, 30);
    if (nread != 30 || memcmp(readbuf, testdata1 + 10, 30))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 30 || memcmp(readbuf, testdata1 + 10, 30)"), stvar(size, nread));
    
    // Peek across segment boundary (64-byte segments)
    nread = bufchainPeek(&chain, readbuf, 50, 60);
    if (nread != 60 || memcmp(readbuf, testdata1 + 50, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 60 || memcmp(readbuf, testdata1 + 50, 60)"), stvar(size, nread));
    
    // Peek near end
    nread = bufchainPeek(&chain, readbuf, total - 20, 20);
    if (nread != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20)"), stvar(size, nread));
    
    // Peek past end
    nread = bufchainPeek(&chain, readbuf, total - 5, 20);
    if (nread != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5)"), stvar(size, nread));
    
    // Now actually read some data
    nread = bufchainRead(&chain, readbuf, 30);
    if (nread != 30 || memcmp(readbuf, testdata1, 30))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 30 || memcmp(readbuf, testdata1, 30)"), stvar(size, nread));
    
    // Peek at what's left
    nread = bufchainPeek(&chain, readbuf, 0, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 40 || memcmp(readbuf, testdata1 + 30, 40)"), stvar(size, nread));

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
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 30 || chain.total != (total - 30)"), stvar(size, nskipped));
    
    // Read what's left from testdata1
    size_t nread = bufchainRead(&chain, readbuf, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 40 || memcmp(readbuf, testdata1 + 30, 40)"), stvar(size, nread));
    
    // Skip across segment boundary (64 bytes)
    nskipped = bufchainSkip(&chain, 70);
    if (nskipped != 70)
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 70"), stvar(size, nskipped));
    
    // Read remainder (should be from testdata2)
    nread           = bufchainRead(&chain, readbuf, 100);
    size_t expected = total - 30 - 40 - 70;
    if (nread != expected || memcmp(readbuf, testdata2 + (50 - expected), expected))
        TEST_FAILV(ret, 1, _SL("nread=${int} != expected=${int} || memcmp(readbuf, testdata2 + (50 - expected), expected)"), stvar(size, nread), stvar(size, expected));
    
    // Skip more than available
    bufchainWrite(&chain, testdata1, 20);
    nskipped = bufchainSkip(&chain, 100);
    if (nskipped != 20 || chain.total != 0)
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 20 || chain.total=${int} != 0"), stvar(size, nskipped), stvar(size, chain.total));

    bufchainDestroy(&chain);
    return ret;
}

typedef struct TestBufChainZCCtx {
    uint8* out;
    size_t outp;
    size_t maxout;
    int segcount;
} TestBufChainZCCtx;

static bool bufchainZCReadCallback(Buffer buf, size_t off, void* ctx)
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
    bufchainReadZC(&chain, total, bufchainZCReadCallback, &ctx);

    // Data should be consumed
    if (chain.total != 0)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0"), stvar(size, chain.total));

    // Verify we got multiple segments
    if (ctx.segcount < 2)
        TEST_FAILV(ret, 1, _SL("ctx.segcount < 2"), stvNone);

    // Verify the data
    if (memcmp(ctx.out, testdata1, TESTDATA_LEN) || memcmp(ctx.out + TESTDATA_LEN, testdata2, 50))
        TEST_FAILV(ret, 1, _SL("memcmp(ctx.out, testdata1, TESTDATA_LEN) || memcmp(ctx.out + TESTDATA_LEN, testdata2, 50)"), stvNone);

    // Test with partial read (partial read from head segment)
    bufchainWrite(&chain, testdata1, TESTDATA_LEN);
    bufchainRead(&chain, ctx.out, 30);   // Partial read from first segment

    // Now do zero-copy read - should get offset in first callback
    ctx.outp     = 30;
    ctx.segcount = 0;
    bufchainReadZC(&chain, 200, bufchainZCReadCallback, &ctx);

    if (chain.total != 0 || ctx.segcount < 1)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0 || ctx.segcount < 1"), stvar(size, chain.total));

    // Verify complete data
    if (memcmp(ctx.out, testdata1, TESTDATA_LEN))
        TEST_FAILV(ret, 1, _SL("memcmp(ctx.out, testdata1, TESTDATA_LEN)"), stvNone);

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
    Buffer buf1 = bufCreate(70);
    memcpy(buf1->data, testdata1, 70);
    buf1->len = 70;

    Buffer buf2 = bufCreate(60);
    memcpy(buf2->data, testdata2, 60);
    buf2->len = 60;

    // Zero-copy write
    bufchainWriteZC(&chain, &buf1);
    bufchainWriteZC(&chain, &buf2);

    // Buffers should be NULL now (ownership transferred)
    if (buf1 != NULL || buf2 != NULL)
        TEST_FAILV(ret, 1, _SL("buf1 != NULL || buf2 != NULL"), stvNone);

    if (chain.total != 130)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 130"), stvar(size, chain.total));

    // Read back and verify
    size_t nread = bufchainRead(&chain, readbuf, 130);
    if (nread != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60)"), stvar(size, nread));

    if (chain.total != 0)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0"), stvar(size, chain.total));

    // Test partial buffer (len < size)
    Buffer buf3 = bufCreate(100);
    memcpy(buf3->data, testdata2, 55);
    buf3->len = 55;
    bufchainWriteZC(&chain, &buf3);

    if (chain.total != 55)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 55"), stvar(size, chain.total));

    nread = bufchainRead(&chain, readbuf, 55);
    if (nread != 55 || memcmp(readbuf, testdata2, 55))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 55 || memcmp(readbuf, testdata2, 55)"), stvar(size, nread));

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
        TEST_FAILV(ret, 1, _SL("nodecount < 3"), stvNone);

    // Read across multiple segments
    size_t nread = bufchainRead(&chain, readbuf, 100);
    if (nread != 100 || memcmp(readbuf, testdata1, 100))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 100 || memcmp(readbuf, testdata1, 100)"), stvar(size, nread));

    // Read more across segments (should span into testdata2)
    nread = bufchainRead(&chain, readbuf + 100, 80);
    if (nread != 80)
        TEST_FAILV(ret, 1, _SL("nread=${int} != 80"), stvar(size, nread));

    size_t from_td1 = TESTDATA_LEN - 100;
    if (memcmp(readbuf + 100, testdata1 + 100, from_td1) ||
        memcmp(readbuf + 100 + from_td1, testdata2, 80 - from_td1))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: readbuf vs testdata1/testdata2 tail"), stvNone);

    // Read rest
    nread = bufchainRead(&chain, readbuf + 180, 500);
    if (nread != (total_written - 180))
        TEST_FAILV(ret, 1, _SL("nread != (total_written - 180)"), stvNone);

    if (chain.total != 0)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 0"), stvar(size, chain.total));

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
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 20"), stvar(size, ring.total));

    // Read it back
    size_t nread = bufringRead(&ring, readbuf, 20);
    if (nread != 20 || memcmp(readbuf, testdata1, 20))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata1, 20)"), stvar(size, nread));

    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    // Write and read across segment boundary (64 bytes)
    bufringWrite(&ring, testdata1, 80);
    nread = bufringRead(&ring, readbuf, 80);
    if (nread != 80 || memcmp(readbuf, testdata1, 80))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 80 || memcmp(readbuf, testdata1, 80)"), stvar(size, nread));

    // Write more than one segment, read in parts
    bufringWrite(&ring, testdata1, TESTDATA_LEN);
    bufringWrite(&ring, testdata2, TESTDATA2_LEN);

    nread = bufringRead(&ring, readbuf, 50);
    if (nread != 50 || memcmp(readbuf, testdata1, 50))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 50 || memcmp(readbuf, testdata1, 50)"), stvar(size, nread));

    nread = bufringRead(&ring, readbuf + 50, 60);
    if (nread != 60 || memcmp(readbuf + 50, testdata1 + 50, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 60 || memcmp(readbuf + 50, testdata1 + 50, 60)"), stvar(size, nread));

    // Read across the boundary between testdata1 and testdata2
    size_t remaining1      = TESTDATA_LEN - 110;
    size_t remaining_total = remaining1 + TESTDATA2_LEN;
    nread                  = bufringRead(&ring, readbuf + 110, remaining_total);
    if (nread != remaining_total)
        TEST_FAILV(ret, 1, _SL("nread=${int} != remaining_total=${int}"), stvar(size, nread), stvar(size, remaining_total));

    if (memcmp(readbuf + 110, testdata1 + 110, remaining1) ||
        memcmp(readbuf + 110 + remaining1, testdata2, TESTDATA2_LEN))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: readbuf vs testdata1/testdata2 tail"), stvNone);

    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

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
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata1, 20)"), stvar(size, nread));

    // Data should still be there
    if (ring.total != total)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != total=${int}"), stvar(size, ring.total), stvar(size, total));

    // Peek at offset
    nread = bufringPeek(&ring, readbuf, 10, 30);
    if (nread != 30 || memcmp(readbuf, testdata1 + 10, 30))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 30 || memcmp(readbuf, testdata1 + 10, 30)"), stvar(size, nread));

    // Peek across segment boundary (64-byte segments)
    nread = bufringPeek(&ring, readbuf, 50, 60);
    if (nread != 60 || memcmp(readbuf, testdata1 + 50, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 60 || memcmp(readbuf, testdata1 + 50, 60)"), stvar(size, nread));

    // Peek near end
    nread = bufringPeek(&ring, readbuf, total - 20, 20);
    if (nread != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 20 || memcmp(readbuf, testdata2 + 50 - 20, 20)"), stvar(size, nread));

    // Peek past end
    nread = bufringPeek(&ring, readbuf, total - 5, 20);
    if (nread != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 5 || memcmp(readbuf, testdata2 + 50 - 5, 5)"), stvar(size, nread));

    // Now actually read some data
    nread = bufringRead(&ring, readbuf, 30);
    if (nread != 30 || memcmp(readbuf, testdata1, 30))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 30 || memcmp(readbuf, testdata1, 30)"), stvar(size, nread));

    // Peek at what's left
    nread = bufringPeek(&ring, readbuf, 0, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 40 || memcmp(readbuf, testdata1 + 30, 40)"), stvar(size, nread));

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
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 30 || ring.total != (total - 30)"), stvar(size, nskipped));

    // Read what's left from testdata1
    size_t nread = bufringRead(&ring, readbuf, 40);
    if (nread != 40 || memcmp(readbuf, testdata1 + 30, 40))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 40 || memcmp(readbuf, testdata1 + 30, 40)"), stvar(size, nread));

    // Skip across segment boundary (64 bytes)
    nskipped = bufringSkip(&ring, 70);
    if (nskipped != 70)
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 70"), stvar(size, nskipped));

    // Read remainder (should be from testdata2)
    nread           = bufringRead(&ring, readbuf, 100);
    size_t expected = total - 30 - 40 - 70;
    if (nread != expected || memcmp(readbuf, testdata2 + (50 - expected), expected))
        TEST_FAILV(ret, 1, _SL("nread=${int} != expected=${int} || memcmp(readbuf, testdata2 + (50 - expected), expected)"), stvar(size, nread), stvar(size, expected));

    // Skip more than available
    bufringWrite(&ring, testdata1, 20);
    nskipped = bufringSkip(&ring, 100);
    if (nskipped != 20 || ring.total != 0)
        TEST_FAILV(ret, 1, _SL("nskipped=${int} != 20 || ring.total=${int} != 0"), stvar(size, nskipped), stvar(size, ring.total));

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
        TEST_FAILV(ret, 1, _SL("nread=${int} != 80 || ctx.outp=${int} != 80 || memcmp(ctx.out, testdata1, 80)"), stvar(size, nread), stvar(size, ctx.outp));
    
    // Data should be consumed
    if (ring.total != (total - 80))
        TEST_FAILV(ret, 1, _SL("ring.total != (total - 80)"), stvNone);
    
    // Zero-copy read without consumption
    ctx.outp = 0;
    ctx.shouldConsume = false;
    size_t remaining  = ring.total;
    nread             = bufringReadZC(&ring, 50, bufringZCReadCallback, &ctx);
    if (nread != 50 || ctx.outp != 50)
        TEST_FAILV(ret, 1, _SL("nread=${int} != 50 || ctx.outp=${int} != 50"), stvar(size, nread), stvar(size, ctx.outp));
    
    // Check data: 47 bytes from testdata1[80..126], then 3 bytes from testdata2
    if (memcmp(ctx.out, testdata1 + 80, TESTDATA_LEN - 80) ||
        memcmp(ctx.out + (TESTDATA_LEN - 80), testdata2, 50 - (TESTDATA_LEN - 80)))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: ctx.out vs testdata1/testdata2 tail"), stvNone);
    
    // Data should NOT be consumed
    if (ring.total != remaining)
        TEST_FAILV(ret, 1, _SL("ring.total != remaining"), stvNone);
    
    // Now consume it
    ctx.outp = 0;
    ctx.shouldConsume = true;
    nread             = bufringReadZC(&ring, remaining, bufringZCReadCallback, &ctx);
    if (nread != remaining || ring.total != 0)
        TEST_FAILV(ret, 1, _SL("nread != remaining || ring.total=${int} != 0"), stvar(size, ring.total));
    
    // Check the data spans from testdata1 to testdata2
    size_t from_td1 = TESTDATA_LEN - 80;
    if (memcmp(ctx.out, testdata1 + 80, from_td1) ||
        memcmp(ctx.out + from_td1, testdata2, 50))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: ctx.out vs testdata1/testdata2 tail"), stvNone);
    
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
    Buffer buf1 = bufCreate(70);
    memcpy(buf1->data, testdata1, 70);
    buf1->len = 70;

    Buffer buf2 = bufCreate(60);
    memcpy(buf2->data, testdata2, 60);
    buf2->len = 60;

    // Zero-copy write
    bufringWriteZC(&ring, &buf1);
    bufringWriteZC(&ring, &buf2);

    // Buffers should be NULL now (ownership transferred)
    if (buf1 != NULL || buf2 != NULL)
        TEST_FAILV(ret, 1, _SL("buf1 != NULL || buf2 != NULL"), stvNone);

    if (ring.total != 130)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 130"), stvar(size, ring.total));
    
    // Read back and verify
    size_t nread = bufringRead(&ring, readbuf, 130);
    if (nread != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 130 || memcmp(readbuf, testdata1, 70) || memcmp(readbuf + 70, testdata2, 60)"), stvar(size, nread));

    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    // Test partial buffer (len < size)
    Buffer buf3 = bufCreate(100);
    memcpy(buf3->data, testdata2, 55);
    buf3->len = 55;
    bufringWriteZC(&ring, &buf3);

    if (ring.total != 55)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 55"), stvar(size, ring.total));

    nread = bufringRead(&ring, readbuf, 55);
    if (nread != 55 || memcmp(readbuf, testdata2, 55))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 55 || memcmp(readbuf, testdata2, 55)"), stvar(size, nread));

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
        TEST_FAILV(ret, 1, _SL("nread=${int} != 35 || memcmp(readbuf, testdata1, 35)"), stvar(size, nread));
    
    // This should wraparound in the ring buffer
    bufringWrite(&ring, testdata1 + 50, 55);

    // Read back and verify
    nread = bufringRead(&ring, readbuf, 70);
    if (nread != 70)
        TEST_FAILV(ret, 1, _SL("nread=${int} != 70"), stvar(size, nread));
    
    // First 15 bytes from first write, then 55 from second write
    if (memcmp(readbuf, testdata1 + 35, 15) || memcmp(readbuf + 15, testdata1 + 50, 55))
        TEST_FAILV(ret, 1, _SL("memcmp(readbuf, testdata1 + 35, 15) || memcmp(readbuf + 15, testdata1 + 50, 55)"), stvNone);

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
        TEST_FAILV(ret, 1, _SL("nodecount < 3"), stvNone);
    
    // Read across multiple segments
    size_t nread = bufringRead(&ring, readbuf, 100);
    if (nread != 100 || memcmp(readbuf, testdata1, 100))
        TEST_FAILV(ret, 1, _SL("nread=${int} != 100 || memcmp(readbuf, testdata1, 100)"), stvar(size, nread));
    
    // Read more across segments (should span into testdata2)
    nread = bufringRead(&ring, readbuf + 100, 80);
    if (nread != 80)
        TEST_FAILV(ret, 1, _SL("nread=${int} != 80"), stvar(size, nread));
    
    size_t from_td1 = TESTDATA_LEN - 100;
    if (memcmp(readbuf + 100, testdata1 + 100, from_td1) ||
        memcmp(readbuf + 100 + from_td1, testdata2, 80 - from_td1))
        TEST_FAILV(ret, 1, _SL("memcmp mismatch: readbuf vs testdata1/testdata2 tail"), stvNone);
    
    // Read rest
    nread = bufringRead(&ring, readbuf + 180, 500);
    if (nread != (total_written - 180))
        TEST_FAILV(ret, 1, _SL("nread != (total_written - 180)"), stvNone);

    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    bufringDestroy(&ring);
    return ret;
}

// ============= Reserve / Commit Tests =============

static int test_bufring_reserve()
{
    int ret = 0;
    BufRing ring;
    uint8* ptr    = NULL;
    size_t len    = 0;
    uint8 readbuf[256];

    bufringInit(&ring, 128);

    // Basic reserve on an empty ring
    bufringReserve(&ring, 64, &ptr, &len);
    if (!ptr || len < 64)
        TEST_FAILV(ret, 1, _SL("!ptr || len < 64"), stvNone);

    // Nothing is visible until it's committed
    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    memcpy(ptr, testdata1, 64);
    bufringCommit(&ring, 64);

    if (ring.total != 64)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 64"), stvar(size, ring.total));

    if (bufringRead(&ring, readbuf, 64) != 64 || memcmp(readbuf, testdata1, 64))
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 64) != 64 || memcmp(readbuf, testdata1, 64)"), stvNone);

    // Short commit: reserve a lot, commit a little. The uncommitted remainder must go back to
    // being free space, not be lost or leak into the readable total.
    bufringReserve(&ring, 100, &ptr, &len);
    if (!ptr || len < 100)
        TEST_FAILV(ret, 1, _SL("!ptr || len < 100"), stvNone);
    memcpy(ptr, testdata2, 10);
    bufringCommit(&ring, 10);

    if (ring.total != 10)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 10"), stvar(size, ring.total));
    if (bufringRead(&ring, readbuf, 10) != 10 || memcmp(readbuf, testdata2, 10))
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 10) != 10 || memcmp(readbuf, testdata2, 10)"), stvNone);

    // Zero commit releases the reservation without adding data
    bufringReserve(&ring, 32, &ptr, &len);
    bufringCommit(&ring, 0);
    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    // A reservation larger than the segment size must still be contiguous
    bufringReserve(&ring, 500, &ptr, &len);
    if (!ptr || len < 500)
        TEST_FAILV(ret, 1, _SL("!ptr || len < 500"), stvNone);
    for (size_t i = 0; i < 500; ++i)
        ptr[i] = (uint8)(i & 0xff);
    bufringCommit(&ring, 500);
    if (ring.total != 500)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 500"), stvar(size, ring.total));

    uint8 bigbuf[512];
    if (bufringRead(&ring, bigbuf, 500) != 500)
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, bigbuf, 500) != 500"), stvNone);
    for (size_t i = 0; i < 500; ++i) {
        if (bigbuf[i] != (uint8)(i & 0xff))
            TEST_FAILV(ret, 1, _SL("bigbuf[i] != (uint8)(i & 0xff)"), stvNone);
    }

    bufringDestroy(&ring);
    return ret;
}

static int test_bufring_reserve_across_read()
{
    int ret = 0;
    BufRing ring;
    uint8* ptr = NULL;
    size_t len = 0;
    uint8 readbuf[256];

    bufringInit(&ring, 128);

    // This is the case that matters for completion-based I/O: a reservation is outstanding
    // (an async receive is in flight) while the consumer drains data that arrived earlier.
    // The reserved pointer must survive that, including the case where the drain empties the
    // ring completely -- which is exactly when the ring would normally rewind its cursors.
    bufringWrite(&ring, testdata1, 50);

    bufringReserve(&ring, 32, &ptr, &len);
    if (!ptr || len < 32)
        TEST_FAILV(ret, 1, _SL("!ptr || len < 32"), stvNone);

    uint8* saved = ptr;
    memcpy(ptr, testdata2, 32);

    // Drain everything that was already in the ring
    if (bufringRead(&ring, readbuf, 50) != 50 || memcmp(readbuf, testdata1, 50))
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 50) != 50 || memcmp(readbuf, testdata1, 50)"), stvNone);
    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    // The reservation must not have moved and its contents must be intact
    if (ptr != saved || memcmp(ptr, testdata2, 32))
        TEST_FAILV(ret, 1, _SL("ptr != saved || memcmp(ptr, testdata2, 32)"), stvNone);

    bufringCommit(&ring, 32);
    if (ring.total != 32)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 32"), stvar(size, ring.total));
    if (bufringRead(&ring, readbuf, 32) != 32 || memcmp(readbuf, testdata2, 32))
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 32) != 32 || memcmp(readbuf, testdata2, 32)"), stvNone);

    // Same thing again, but drain with skip rather than read
    bufringWrite(&ring, testdata1, 40);
    bufringReserve(&ring, 16, &ptr, &len);
    saved = ptr;
    memcpy(ptr, testdata2 + 5, 16);
    if (bufringSkip(&ring, 40) != 40)
        TEST_FAILV(ret, 1, _SL("bufringSkip(&ring, 40) != 40"), stvNone);
    if (ptr != saved || memcmp(ptr, testdata2 + 5, 16))
        TEST_FAILV(ret, 1, _SL("ptr != saved || memcmp(ptr, testdata2 + 5, 16)"), stvNone);
    bufringCommit(&ring, 16);
    if (bufringRead(&ring, readbuf, 16) != 16 || memcmp(readbuf, testdata2 + 5, 16))
        TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 16) != 16 || memcmp(readbuf, testdata2 + 5, 16)"), stvNone);

    // Interleave reserve/commit with ordinary writes to make sure the ring stays consistent
    for (int i = 0; i < 20; ++i) {
        bufringWrite(&ring, testdata1, 33);
        bufringReserve(&ring, 17, &ptr, &len);
        memcpy(ptr, testdata2, 17);
        bufringCommit(&ring, 17);

        if (bufringRead(&ring, readbuf, 50) != 50)
            TEST_FAILV(ret, 1, _SL("bufringRead(&ring, readbuf, 50) != 50"), stvNone);
        if (memcmp(readbuf, testdata1, 33) || memcmp(readbuf + 33, testdata2, 17))
            TEST_FAILV(ret, 1, _SL("memcmp(readbuf, testdata1, 33) || memcmp(readbuf + 33, testdata2, 17)"), stvNone);
    }

    if (ring.total != 0)
        TEST_FAILV(ret, 1, _SL("ring.total=${int} != 0"), stvar(size, ring.total));

    bufringDestroy(&ring);
    return ret;
}

// ============= Gather IOV Tests =============

static int test_bufchain_gather_iov()
{
    int ret = 0;
    BufChain chain;
    BufIov iov[8];
    size_t niov = 0;
    uint8 readbuf[512];

    bufchainInit(&chain, 64);

    // Empty chain gathers nothing
    if (bufchainGatherIov(&chain, iov, 8, &niov) != 0 || niov != 0)
        TEST_FAILV(ret, 1, _SL("bufchainGatherIov(&chain, iov, 8, &niov) != 0 || niov=${int} != 0"), stvar(size, niov));

    // Write enough to span several segments
    bufchainWrite(&chain, testdata1, 130);

    size_t total = bufchainGatherIov(&chain, iov, 8, &niov);
    if (total != 130 || niov < 2)
        TEST_FAILV(ret, 1, _SL("total=${int} != 130 || niov < 2"), stvar(size, total));

    // The iov entries must describe exactly the chain contents, in order
    size_t off = 0;
    for (size_t i = 0; i < niov; ++i) {
        if (iov[i].len == 0)
            ret = 1;   // zero-length entries are rejected by some platforms
        if (memcmp(iov[i].data, testdata1 + off, iov[i].len))
            TEST_FAILV(ret, 1, _SL("memcmp(iov[i].data, testdata1 + off, iov[i].len)"), stvNone);
        off += iov[i].len;
    }
    if (off != 130)
        TEST_FAILV(ret, 1, _SL("off=${int} != 130"), stvar(size, off));

    // Gathering must not consume
    if (chain.total != 130)
        TEST_FAILV(ret, 1, _SL("chain.total=${int} != 130"), stvar(size, chain.total));

    // maxiov must be respected, and the total must then cover only the entries returned
    size_t partial = bufchainGatherIov(&chain, iov, 1, &niov);
    if (niov != 1 || partial != iov[0].len || partial >= 130)
        TEST_FAILV(ret, 1, _SL("niov=${int} != 1 || partial != iov[0].len || partial >= 130"), stvar(size, niov));

    // count is optional
    if (bufchainGatherIov(&chain, iov, 8, NULL) != 130)
        TEST_FAILV(ret, 1, _SL("bufchainGatherIov(&chain, iov, 8, NULL) != 130"), stvNone);

    // A partial read moves the cursor; the first entry must start at the cursor, not at the
    // start of the head segment
    if (bufchainRead(&chain, readbuf, 20) != 20)
        TEST_FAILV(ret, 1, _SL("bufchainRead(&chain, readbuf, 20) != 20"), stvNone);

    total = bufchainGatherIov(&chain, iov, 8, &niov);
    if (total != 110)
        TEST_FAILV(ret, 1, _SL("total=${int} != 110"), stvar(size, total));
    if (memcmp(iov[0].data, testdata1 + 20, iov[0].len))
        TEST_FAILV(ret, 1, _SL("memcmp(iov[0].data, testdata1 + 20, iov[0].len)"), stvNone);

    // Consuming what a "send" accepted is done with skip; a short write leaves the rest
    bufchainSkip(&chain, 30);
    total = bufchainGatherIov(&chain, iov, 8, &niov);
    if (total != 80 || memcmp(iov[0].data, testdata1 + 50, iov[0].len))
        TEST_FAILV(ret, 1, _SL("total=${int} != 80 || memcmp(iov[0].data, testdata1 + 50, iov[0].len)"), stvar(size, total));

    bufchainDestroy(&chain);
    return ret;
}

// ============= Buffer Pool Tests =============

static int test_bufpool_basic()
{
    int ret = 0;
    BufPool pool;

    bufpoolInit(&pool, 256, 4, 8);

    if (bufpoolInUse(&pool) != 0)
        TEST_FAILV(ret, 1, _SL("bufpoolInUse(&pool) != 0"), stvNone);

    Buffer bufs[8];
    for (int i = 0; i < 8; ++i) {
        bufs[i] = bufpoolGet(&pool);
        if (!bufs[i] || bufs[i]->sz != 256 || bufs[i]->len != 0)
            TEST_FAILV(ret, 1, _SL("!bufs[i] || bufs[i]->sz != 256 || bufs[i]->len != 0"), stvNone);
    }

    if (bufpoolInUse(&pool) != 8)
        TEST_FAILV(ret, 1, _SL("bufpoolInUse(&pool) != 8"), stvNone);

    // At the cap with none free: must fail rather than allocate
    Buffer over = bufpoolGet(&pool);
    if (over != NULL)
        TEST_FAILV(ret, 1, _SL("over != NULL"), stvNone);

    // Returning one makes it available again
    bufpoolPut(&pool, &bufs[0]);
    if (bufs[0] != NULL)
        TEST_FAILV(ret, 1, _SL("bufs[0] != NULL"), stvNone);

    bufs[0] = bufpoolGet(&pool);
    if (!bufs[0]) {
        TEST_FAILV(ret, 1, _SL("bufpoolGet returned NULL after full put/get cycle"), stvNone);
    } else {
        // len must be reset on reuse
        bufs[0]->len = 100;
        bufpoolPut(&pool, &bufs[0]);
        bufs[0] = bufpoolGet(&pool);
        if (!bufs[0] || bufs[0]->len != 0)
            TEST_FAILV(ret, 1, _SL("!bufs[0] || bufs[0]->len != 0"), stvNone);
    }

    for (int i = 0; i < 8; ++i)
        bufpoolPut(&pool, &bufs[i]);

    if (bufpoolInUse(&pool) != 0)
        TEST_FAILV(ret, 1, _SL("bufpoolInUse(&pool) != 0"), stvNone);

    // Putting a NULL is a no-op, not a crash
    Buffer nil = NULL;
    bufpoolPut(&pool, &nil);

    bufpoolDestroy(&pool);

    // An uncapped pool never fails to hand out buffers
    bufpoolInit(&pool, 64, 0, 0);
    Buffer many[64];
    for (int i = 0; i < 64; ++i) {
        many[i] = bufpoolGet(&pool);
        if (!many[i])
            TEST_FAILV(ret, 1, _SL("!many[i]"), stvNone);
    }
    for (int i = 0; i < 64; ++i)
        bufpoolPut(&pool, &many[i]);
    bufpoolDestroy(&pool);

    return ret;
}

static int test_bufpool_reuse()
{
    int ret = 0;
    BufPool pool;

    // Preallocated buffers must be handed out rather than newly allocated, and cycling through
    // the pool must not grow it.
    bufpoolInit(&pool, 128, 16, 16);

    for (int round = 0; round < 100; ++round) {
        Buffer b = bufpoolGet(&pool);
        if (!b || b->sz != 128) {
            TEST_FAILV(ret, 1, _SL("bufpoolGet returned bad buffer: b=${ptr}, sz=${int} != 128"),
                       stvar(ptr, b), stvar(int32, b ? (int32)b->sz : -1));
            bufpoolPut(&pool, &b);   // no-op when the get failed outright
            break;
        }

        memcpy(b->data, testdata1, 100);
        b->len = 100;

        bufpoolPut(&pool, &b);
        if (b != NULL)
            TEST_FAILV(ret, 1, _SL("b != NULL"), stvNone);
    }

    if (bufpoolInUse(&pool) != 0)
        TEST_FAILV(ret, 1, _SL("bufpoolInUse(&pool) != 0"), stvNone);

    // Drain the pool completely; exactly the cap should be available
    Buffer held[16];
    int got = 0;
    for (int i = 0; i < 16; ++i) {
        held[i] = bufpoolGet(&pool);
        if (held[i])
            ++got;
    }
    if (got != 16)
        TEST_FAILV(ret, 1, _SL("got=${int} != 16"), stvar(int32, got));
    if (bufpoolGet(&pool) != NULL)
        TEST_FAILV(ret, 1, _SL("bufpoolGet(&pool) != NULL"), stvNone);

    for (int i = 0; i < 16; ++i)
        bufpoolPut(&pool, &held[i]);

    bufpoolDestroy(&pool);
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
    { "ring_reserve",         test_bufring_reserve         },
    { "ring_reserve_read",    test_bufring_reserve_across_read },
    { "chain_gather_iov",     test_bufchain_gather_iov     },
    { "bufpool_basic",        test_bufpool_basic           },
    { "bufpool_reuse",        test_bufpool_reuse           },
    { 0,                      0                            }
};

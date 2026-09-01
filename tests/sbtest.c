#include <cx/serialize/streambuf.h>
#include <cx/buffer/buffer.h>
#include <cx/serialize/jsonout.h>
#include <cx/serialize/sbbuffer.h>
#include <cx/serialize/sbcon.h>
#include <cx/serialize/sbstring.h>
#include <cx/platform/os.h>
#include <cx/ssdtree/ssdtree.h>
#include <cx/thread/event.h>
#include <cx/thread/thread.h>
#include <cx/xalloc/xalloc.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/utils/compare.h>

#define TEST_FILE sbtest
#define TEST_FUNCS sbtest_funcs
#include "common.h"

static const uint8 testdata1[] = "This is a test. This is a test. This is a test. This is a test. This is a test. This is a test.";
#define TESTBUF_SZ 128
_Static_assert(TESTBUF_SZ > sizeof(testdata1), "TESTBUF_SZ must be big enough to hold the test data");

typedef struct TestCtx1 {
    uint8 *out;
    size_t outp;
    size_t shouldread;
    bool didclean;
    bool usesend;
} TestCtx1;

static bool sbsend1(StreamBuffer *sb, const uint8 *buf, size_t off, size_t sz, void *ctx)
{
    TestCtx1 *tc = (TestCtx1 *)ctx;
    if (tc->outp + sz > TESTBUF_SZ)
        return false;

    memcpy(tc->out + tc->outp, buf, sz);
    tc->outp += sz;
    return true;
}

static void sbnotify1(StreamBuffer *sb, size_t sz, void *ctx)
{
    TestCtx1 * tc = (TestCtx1 *)ctx;
    if (tc->outp + sz > TESTBUF_SZ)
        return;

    // test both read and send
    if (!tc->usesend) {
        size_t didread;
        sbufCRead(sb, tc->out + tc->outp, min(sz, tc->shouldread), &didread);
        tc->outp += didread;
        devAssert(didread == min(sz, tc->shouldread));
    } else {
        sbufCSend(sb, sbsend1, min(sz, tc->shouldread), ctx);
    }
}

static void sbclean1(void *ctx)
{
    TestCtx1 *tc = (TestCtx1 *)ctx;
    tc->didclean = true;
}

static int test_streambuf_push()
{
    int ret = 0;

    StreamBuffer *ptest;
    TestCtx1 c1 = { 0 };
    c1.out = xaAlloc(TESTBUF_SZ);

    for (int usesend = 0; usesend < 2; usesend++) {
        ptest = sbufCreate(32);
        if (!sbufCRegisterPush(ptest, sbnotify1, sbclean1, &c1)) {
            TEST_FAILV(ret, 1, _SL("sbufCRegisterPush failed, usesend=${int}"), stvar(int32, usesend));
            goto out;
        }

        memset(c1.out, 0, TESTBUF_SZ);
        c1.outp = 0;

        c1.shouldread = 5;
        c1.usesend = !!usesend;
        sbufPWrite(ptest, testdata1, 7);

        // check if data made it
        if (memcmp(c1.out, testdata1, 5))
            TEST_FAILV(ret, 1, _SL("first 5 bytes mismatch, usesend=${int}"), stvar(int32, usesend));

        c1.shouldread = 7;
        sbufPWrite(ptest, testdata1 + 9, 5);

        if (memcmp(c1.out, "This is test", 12) ||
            c1.outp != 12)
            TEST_FAILV(ret, 1, _SL("mismatch or c1.outp=${uint} != 12, usesend=${int}"), stvar(uint64, (uint64)c1.outp), stvar(int32, usesend));

        // test overflow buffer
        c1.outp = 0;
        c1.shouldread = 5;
        sbufPWrite(ptest, testdata1, 7);
        c1.shouldread = 1;
        sbufPWrite(ptest, testdata1 + 7, 8);

        if (sbufCAvail(ptest) != 9)
            TEST_FAILV(ret, 1, _SL("sbufCAvail(ptest)=${uint} != 9, usesend=${int}"), stvar(uint64, (uint64)sbufCAvail(ptest)), stvar(int32, usesend));

        c1.shouldread = 5;
        sbufPWrite(ptest, testdata1 + 15, 5);

        if (memcmp(c1.out, testdata1, 11) ||
            c1.outp != 11)
            TEST_FAILV(ret, 1, _SL("mismatch or c1.outp=${uint} != 11, usesend=${int}"), stvar(uint64, (uint64)c1.outp), stvar(int32, usesend));

        // push some more data
        sbufPWrite(ptest, testdata1 + 20, 40);
        if (c1.outp != 16)
            TEST_FAILV(ret, 1, _SL("c1.outp=${uint} != 16, usesend=${int}"), stvar(uint64, (uint64)c1.outp), stvar(int32, usesend));

        // flush everything that's left
        c1.shouldread = 10000;
        sbufClose(ptest);
        // this should cause sbclean1 to be called
        sbufCUnregister(ptest);
        sbufRelease(&ptest);

        if (memcmp(c1.out, testdata1, 60) ||
            c1.outp != 60)
            TEST_FAILV(ret, 1, _SL("mismatch or c1.outp=${uint} != 60, usesend=${int}"), stvar(uint64, (uint64)c1.outp), stvar(int32, usesend));

        if (!c1.didclean)
            TEST_FAILV(ret, 1, _SL("c1.didclean not set, usesend=${int}"), stvar(int32, usesend));
    }

out:
    xaFree(c1.out);
    return ret;
}

typedef struct TestCtx2 {
    size_t inp;
    bool didclean;
} TestCtx2;

static size_t sbpull2(StreamBuffer *sb, uint8 *buf, size_t sz, void *ctx)
{
    TestCtx2 *tc = (TestCtx2 *)ctx;
    size_t bytes = min(sz, sizeof(testdata1) - tc->inp);

    if (sz % 2 == 1) {
        memcpy(buf, testdata1 + tc->inp, bytes);
        tc->inp += bytes;
    } else {
        sbufPWrite(sb, testdata1 + tc->inp, bytes);
        tc->inp += bytes;
        bytes = 0;
    }

    if (tc->inp == sizeof(testdata1))
        sbufPUnregister(sb);

    return bytes;
}

static void sbclean2(void *ctx)
{
    TestCtx2 *tc = (TestCtx2 *)ctx;
    tc->didclean = true;
}

static int test_streambuf_pull()
{
    int ret = 0;

    StreamBuffer *ptest = sbufCreate(32);
    TestCtx2 ctx = { 0 };
    uint8 out[TESTBUF_SZ];
    size_t p = 0;

    if (!sbufPRegisterPull(ptest, sbpull2, sbclean2, &ctx))
        return false;

    size_t didread;

    sbufCRead(ptest, out + p, 5, &didread);
    if (didread != 5 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 5"), stvar(uint64, (uint64)didread));
    p += didread;

    sbufCRead(ptest, out + p, 15, &didread);
    if (didread != 15 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 15"), stvar(uint64, (uint64)didread));
    p += didread;

    sbufCRead(ptest, out + p, 3, &didread);
    if (didread != 3 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 3"), stvar(uint64, (uint64)didread));
    p += didread;

    sbufCRead(ptest, out + p, 40, &didread);
    if (didread != 40 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 40"), stvar(uint64, (uint64)didread));
    p += didread;

    sbufCRead(ptest, out + p, 13, &didread);
    if (didread != 13 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 13"), stvar(uint64, (uint64)didread));
    p += didread;

    // this should hit the end of the input
    sbufCRead(ptest, out + p, 25, &didread);
    if (didread != 20 ||
        memcmp(out + p, testdata1 + p, didread))
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 20"), stvar(uint64, (uint64)didread));
    p += didread;

    if (p != sizeof(testdata1) ||
        memcmp(out, testdata1, sizeof(testdata1)))
        TEST_FAILV(ret, 1, _SL("p=${uint} != sizeof(testdata1)=${uint}"), stvar(uint64, (uint64)p), stvar(uint64, (uint64)sizeof(testdata1)));

    // The producer unregistered itself when it ran dry, which is what runs its cleanup; the
    // stream itself is still open until the consumer says otherwise.
    if (!ctx.didclean)
        TEST_FAILV(ret, 1, _SL("ctx.didclean not set"), stvNone);
    if (sbufPAttached(ptest))
        TEST_FAILV(ret, 1, _SL("producer still attached after it ran dry"), stvNone);
    if (sbufIsClosed(ptest))
        TEST_FAILV(ret, 1, _SL("stream ended when the producer only unregistered"), stvNone);

    sbufClose(ptest);
    sbufRelease(&ptest);

    return ret;
}

static int test_streambuf_peek()
{
    int ret = 0;

    StreamBuffer *ptest = sbufCreate(32);
    TestCtx2 ctx = { 0 };
    uint8 out[TESTBUF_SZ];
    size_t p = 0, didread;

    if (!sbufPRegisterPull(ptest, sbpull2, sbclean2, &ctx))
        return false;

    if (!sbufCFeed(ptest, 5))
        TEST_FAILV(ret, 1, _SL("sbufCFeed(5) failed"), stvNone);

    if (!sbufCPeek(ptest, out + p, 0, 5))
        TEST_FAILV(ret, 1, _SL("sbufCPeek(0, 5) failed"), stvNone);
    if (memcmp(out + p, testdata1 + p, 5))
        TEST_FAILV(ret, 1, _SL("peeked bytes mismatch at p=${uint}"), stvar(uint64, (uint64)p));
    if (!sbufCSkip(ptest, 5))
        TEST_FAILV(ret, 1, _SL("sbufCSkip(5) failed"), stvNone);
    p += 5;

    // try peeking at future data
    if (!sbufCFeed(ptest, 15))
        TEST_FAILV(ret, 1, _SL("sbufCFeed(15) failed"), stvNone);

    if (!sbufCPeek(ptest, out + p + 10, 10, 5))
        TEST_FAILV(ret, 1, _SL("sbufCPeek(10, 5) failed"), stvNone);
    if (memcmp(out + p + 10, testdata1 + p + 10, 5))
        TEST_FAILV(ret, 1, _SL("future-peek bytes mismatch at p=${uint}"), stvar(uint64, (uint64)p));

    // now fill in the gap

    if (!sbufCRead(ptest, out + p, 10, &didread))
        TEST_FAILV(ret, 1, _SL("sbufCRead(10) failed"), stvNone);
    if (memcmp(out + p, testdata1 + p, 10))
        TEST_FAILV(ret, 1, _SL("read-back bytes mismatch at p=${uint}"), stvar(uint64, (uint64)p));
    p += 10;

    // and skip over what we already read
    if (!sbufCSkip(ptest, 5))
        TEST_FAILV(ret, 1, _SL("sbufCSkip(5) failed"), stvNone);
    p += 5;

    // populate the whole rest of the buffer
    if (!sbufCFeed(ptest, sizeof(testdata1) - p))
        TEST_FAILV(ret, 1, _SL("sbufCFeed(rest) failed, p=${uint}"), stvar(uint64, (uint64)p));

    // read the rest out of order
    if (!sbufCPeek(ptest, out + p + 41, 41, 35))
        TEST_FAILV(ret, 1, _SL("sbufCPeek(41, 35) failed"), stvNone);
    if (memcmp(out + p + 41, testdata1 + p + 41, 35))
        TEST_FAILV(ret, 1, _SL("out-of-order peek mismatch at p=${uint}"), stvar(uint64, (uint64)p));

    if (!sbufCPeek(ptest, out + p, 0, 41))
        TEST_FAILV(ret, 1, _SL("sbufCPeek(0, 41) failed"), stvNone);
    if (memcmp(out + p, testdata1 + p, 41))
        TEST_FAILV(ret, 1, _SL("full peek mismatch at p=${uint}"), stvar(uint64, (uint64)p));

    // skip to the end
    if (!sbufCSkip(ptest, 76))
        TEST_FAILV(ret, 1, _SL("sbufCSkip(76) failed"), stvNone);
    p += 76;

    // check entire buffer
    if (p != sizeof(testdata1) ||
        memcmp(out, testdata1, sizeof(testdata1)))
        TEST_FAILV(ret, 1, _SL("p=${uint} != sizeof(testdata1)=${uint} or final mismatch"), stvar(uint64, (uint64)p), stvar(uint64, (uint64)sizeof(testdata1)));

    sbufClose(ptest);
    sbufRelease(&ptest);

    if (!ctx.didclean)
        TEST_FAILV(ret, 1, _SL("ctx.didclean not set"), stvNone);

    return ret;
}

typedef struct TestCtx3 {
    uint8 *out;
    size_t outp;
    bool didclean;
} TestCtx3;

static void sbpush3(StreamBuffer *sb, const uint8 *buf, size_t sz, void *ctx)
{
    TestCtx3 *tc = (TestCtx3 *)ctx;

    if (tc->outp + sz > TESTBUF_SZ)
        return;

    memcpy(tc->out + tc->outp, buf, sz);
    tc->outp += sz;
}

static void sbclean3(void *ctx)
{
    TestCtx3 *tc = (TestCtx3 *)ctx;
    tc->didclean = true;
}

static int test_streambuf_direct()
{
    int ret = 0;
    StreamBuffer *ptest;
    TestCtx3 c3 = { 0 };
    c3.out = xaAlloc(TESTBUF_SZ);
    ptest = sbufCreate(0);
    if (!sbufCRegisterPushDirect(ptest, sbpush3, sbclean3, &c3)) {
        TEST_FAILV(ret, 1, _SL("sbufCRegisterPushDirect failed"), stvNone);
        goto out;
    }

    sbufPWrite(ptest, testdata1, 7);

    // check if data made it
    if (memcmp(c3.out, testdata1, 7))
        TEST_FAILV(ret, 1, _SL("first 7 bytes mismatch"), stvNone);

    sbufPWrite(ptest, testdata1 + 7, 5);

    if (memcmp(c3.out, testdata1, 12) ||
        c3.outp != 12)
        TEST_FAILV(ret, 1, _SL("mismatch or c3.outp=${uint} != 12"), stvar(uint64, (uint64)c3.outp));

    sbufPWrite(ptest, testdata1 + 12, sizeof(testdata1) - 12);
    if (memcmp(c3.out, testdata1, sizeof(testdata1)) ||
        c3.outp != sizeof(testdata1))
        TEST_FAILV(ret, 1, _SL("mismatch or c3.outp=${uint} != ${uint}"), stvar(uint64, (uint64)c3.outp), stvar(uint64, (uint64)sizeof(testdata1)));

    sbufClose(ptest);
    // this should cause sbclean3 to be called
    sbufCUnregister(ptest);
    sbufRelease(&ptest);

    if (!c3.didclean)
        TEST_FAILV(ret, 1, _SL("c3.didclean not set"), stvNone);

out:
    xaFree(c3.out);
    return ret;
}

static int test_streambuf_string()
{
    int ret = 0;
    string s1 = 0, s2 = 0;
    uint8 buf[128];
    size_t didread;
    strCopy(&s1, _S"This is a string test... This is a string test... This is a string test...");

    StreamBuffer *ptest;
    ptest = sbufCreate(16);
    if (!sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufStrPRegisterPull failed"), stvNone);

    if (strTestRefCount(s1) != 2)
        TEST_FAILV(ret, 1, _SL("strTestRefCount(s1)=${int} != 2"), stvar(int32, strTestRefCount(s1)));

    sbufCRead(ptest, buf, 12, &didread);
    if (memcmp(buf, strC(s1), 12))
        TEST_FAILV(ret, 1, _SL("first 12 bytes mismatch"), stvNone);

    sbufCRead(ptest, buf, 40, &didread);
    if (memcmp(buf, strC(s1) + 12, 40))
        TEST_FAILV(ret, 1, _SL("next 40 bytes mismatch"), stvNone);

    sbufCRead(ptest, buf, 22, &didread);
    if (didread != 22)
        TEST_FAILV(ret, 1, _SL("didread=${uint} != 22"), stvar(uint64, (uint64)didread));
    if (memcmp(buf, strC(s1) + 52, 22))
        TEST_FAILV(ret, 1, _SL("last 22 bytes mismatch"), stvNone);

    sbufClose(ptest);
    if (!sbufIsClosed(ptest))
        TEST_FAILV(ret, 1, _SL("sbufIsClosed false after sbufClose"), stvNone);

    sbufRelease(&ptest);

    // hook up a string consumer and use the string producer to push to it

    ptest = sbufCreate(16);
    if (!sbufStrCRegisterPush(ptest, &s2))
        TEST_FAIL(1, _SL("sbufStrCRegisterPush failed"), stvNone);
    sbufStrIn(ptest, s1);
    sbufClose(ptest);

    if (!strEq(s1, s2))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != s2='${string}'"), stvar(strref, s1), stvar(strref, s2));

    if (!sbufIsClosed(ptest) || sbufCAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or consumer still attached"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&s2);

    // then the reverse

    ptest = sbufCreate(16);
    if (!sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufStrPRegisterPull failed"), stvNone);
    sbufStrOut(ptest, &s2);
    sbufClose(ptest);
    if (!strEq(s1, s2))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != s2='${string}'"), stvar(strref, s1), stvar(strref, s2));

    if (!sbufIsClosed(ptest) || sbufPAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or producer still attached"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&s2);

    strDestroy(&s1);
    return ret;
}

static int test_streambuf_console()
{
    int ret = 0;
    string s1 = 0, out = 0;
    strCopy(&s1, _S"This is a console test... This is a console test... This is a console test...");

    ConCaps caps = { 0 };

    // push: string producer -> console consumer
    ConStream *con = conCreateMem(&caps);
    StreamBuffer *ptest = sbufCreate(16);
    if (!sbufConCRegisterPush(ptest, con))
        TEST_FAIL(1, _SL("sbufConCRegisterPush failed"), stvNone);
    sbufStrIn(ptest, s1);
    sbufClose(ptest);

    conMemGet(con, &out);
    if (!strEq(s1, out))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != out='${string}'"), stvar(strref, s1), stvar(strref, out));
    if (!sbufIsClosed(ptest) || sbufCAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or consumer still attached"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&out);
    conDestroy(&con);

    // pull: string producer -> console consumer via sbufConOut
    con = conCreateMem(&caps);
    ptest = sbufCreate(16);
    if (!sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufStrPRegisterPull failed"), stvNone);
    sbufConOut(ptest, con);
    sbufClose(ptest);

    conMemGet(con, &out);
    if (!strEq(s1, out))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != out='${string}'"), stvar(strref, s1), stvar(strref, out));
    if (!sbufIsClosed(ptest) || sbufPAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or producer still attached"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&out);
    conDestroy(&con);
    strDestroy(&s1);
    return ret;
}

static int test_streambuf_buffer()
{
    int ret = 0;
    size_t len = sizeof(testdata1) - 1;

    Buffer src = bufCreate(len);
    memcpy(src->data, testdata1, len);
    src->len = len;

    // push: Buffer producer -> Buffer consumer
    Buffer out          = 0;
    StreamBuffer* ptest = sbufCreate(16);
    if (!sbufBufCRegisterPush(ptest, &out))
        TEST_FAIL(1, _SL("sbufBufCRegisterPush failed"), stvNone);
    sbufBufIn(ptest, src, false);
    sbufClose(ptest);

    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("push produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    if (!sbufIsClosed(ptest) || sbufCAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or consumer still attached"), stvNone);
    sbufRelease(&ptest);

    // The consumer appends, so a second run through a fresh stream lands after the first.
    ptest = sbufCreate(16);
    if (!sbufBufCRegisterPush(ptest, &out))
        TEST_FAIL(1, _SL("sbufBufCRegisterPush failed on reuse"), stvNone);
    sbufBufIn(ptest, src, false);
    sbufClose(ptest);
    if (bufLen(out) != len * 2 || memcmp(out->data + len, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("a second push gave ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len * 2));
    sbufRelease(&ptest);
    bufDestroy(&out);

    // pull: Buffer producer -> Buffer consumer, with the consumer setting the pace
    ptest = sbufCreate(16);
    if (!sbufBufPRegisterPull(ptest, src, false))
        TEST_FAIL(1, _SL("sbufBufPRegisterPull failed"), stvNone);
    if (!sbufBufOut(ptest, &out))
        TEST_FAILV(ret, 1, _SL("sbufBufOut failed"), stvNone);
    sbufClose(ptest);
    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("pull produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    if (!sbufIsClosed(ptest) || sbufPAttached(ptest))
        TEST_FAILV(ret, 1, _SL("stream not ended or producer still attached"), stvNone);
    sbufRelease(&ptest);
    bufDestroy(&out);

    // The convenience constructor puts the caller on the producing end, writing straight into a
    // buffer that starts out NULL.
    ptest = sbufBufCreatePush(&out);
    if (!ptest)
        TEST_FAIL(1, _SL("sbufBufCreatePush failed"), stvNone);
    if (!sbufPWrite(ptest, testdata1, 10) || !sbufPWrite(ptest, testdata1 + 10, len - 10))
        TEST_FAILV(ret, 1, _SL("sbufPWrite failed"), stvNone);
    sbufClose(ptest);
    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("two direct writes produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    sbufRelease(&ptest);
    bufDestroy(&out);

    // sbufBufIn owning its source destroys it, which is the whole reason it takes ownership.
    Buffer owned = bufCreate(len);
    memcpy(owned->data, testdata1, len);
    owned->len = len;
    ptest      = sbufCreate(16);
    if (!sbufBufCRegisterPush(ptest, &out))
        TEST_FAIL(1, _SL("sbufBufCRegisterPush failed for the owned source"), stvNone);
    sbufBufIn(ptest, owned, true);
    sbufClose(ptest);
    if (bufLen(out) != len)
        TEST_FAILV(ret,
                   1,
                   _SL("an owned source produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    sbufRelease(&ptest);
    bufDestroy(&out);

    bufDestroy(&src);
    return ret;
}

// ------------------------------------------------------------------------------------------
// Roles that come and go
// ------------------------------------------------------------------------------------------

static void appendBytes(string *out, const uint8 *buf, size_t sz)
{
    string tmp  = 0;
    uint8 *dest = strBuffer(&tmp, (uint32)sz);
    memcpy(dest, buf, sz);
    strSetLen(&tmp, (uint32)sz);
    strAppend(out, tmp);
    strDestroy(&tmp);
}

// A pull producer over a fixed byte range that hands its slot back when it runs out, rather than
// ending the stream. Whoever is driving can then attach a replacement.
typedef struct RangeProducer {
    const uint8 *data;
    size_t len;
    size_t pos;
    bool didclean;
} RangeProducer;

static size_t rangePull(StreamBuffer *sb, uint8 *buf, size_t sz, void *ctx)
{
    RangeProducer *rp = (RangeProducer *)ctx;

    if (sz == 0) {
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    size_t n = min(sz, rp->len - rp->pos);
    memcpy(buf, rp->data + rp->pos, n);
    rp->pos += n;

    if (rp->pos == rp->len)
        sbufPUnregister(sb);

    return n;
}

static void rangeClean(void *ctx)
{
    ((RangeProducer *)ctx)->didclean = true;
}

// A push consumer that collects into a string. take caps how much it will accept per notify, so a
// test can leave a backlog in the ring on purpose.
typedef struct CollectCtx {
    string out;
    size_t take;   // 0 takes everything offered
    bool didclean;
} CollectCtx;

static void collectNotify(StreamBuffer *sb, size_t sz, void *ctx)
{
    CollectCtx *cc = (CollectCtx *)ctx;

    size_t want = cc->take > 0 ? min(sz, cc->take) : sz;
    if (want > 0) {
        uint8 tmp[TESTBUF_SZ];
        size_t didread;
        want = min(want, sizeof(tmp));
        if (sbufCRead(sb, tmp, want, &didread))
            appendBytes(&cc->out, tmp, didread);
    }

    if (sbufIsClosed(sb))
        sbufCUnregister(sb);
}

static void collectClean(void *ctx)
{
    ((CollectCtx *)ctx)->didclean = true;
}

// A producer that runs out early and calls it a failure rather than an ending.
typedef struct FailProducer {
    const uint8 *data;
    size_t len;
    size_t pos;
} FailProducer;

static size_t failPull(StreamBuffer *sb, uint8 *buf, size_t sz, void *ctx)
{
    FailProducer *fp = (FailProducer *)ctx;

    if (sz == 0) {
        if (sbufIsClosed(sb))
            sbufPUnregister(sb);
        return 0;
    }

    if (fp->pos == fp->len) {
        sbufError(sb);
        return 0;
    }

    size_t n = min(sz, fp->len - fp->pos);
    memcpy(buf, fp->data + fp->pos, n);
    fp->pos += n;
    return n;
}

// A pull producer runs dry, hands its slot back without ending the stream, and a replacement picks
// up where it left off. The consumer only ever sees a short read.
static int test_streambuf_phandoff()
{
    int ret = 0;
    uint8 out[TESTBUF_SZ];
    size_t got1 = 0, got2 = 0;

    RangeProducer p1 = { .data = testdata1, .len = 10 };
    RangeProducer p2 = { .data = testdata1 + 10, .len = 10 };

    StreamBuffer *sb = sbufCreate(32);
    if (!sbufPRegisterPull(sb, rangePull, rangeClean, &p1))
        TEST_FAIL(1, _SL("first sbufPRegisterPull failed"), stvNone);

    // asking for more than the first producer has short-reads rather than ending the stream
    sbufCRead(sb, out, 25, &got1);
    if (got1 != 10)
        TEST_FAILV(ret, 1, _SL("first drain read ${uint} bytes, want 10"), stvar(size, got1));
    if (sbufPAttached(sb))
        TEST_FAILV(ret, 1, _SL("producer still attached after it ran dry"), stvNone);
    if (sbufIsClosed(sb) || sbufIsError(sb))
        TEST_FAILV(ret, 1, _SL("stream ended or failed when the producer only unregistered"), stvNone);
    if (!p1.didclean)
        TEST_FAILV(ret, 1, _SL("the first producer's cleanup did not run at unregister"), stvNone);
    if (sbufCMore(sb))
        TEST_FAILV(ret, 1, _SL("sbufCMore true with no producer attached"), stvNone);

    // a replacement takes over and the consumer is none the wiser
    if (!sbufPRegisterPull(sb, rangePull, rangeClean, &p2))
        TEST_FAIL(1, _SL("second sbufPRegisterPull failed"), stvNone);
    if (!sbufCMore(sb))
        TEST_FAILV(ret, 1, _SL("sbufCMore false right after a producer attached"), stvNone);

    sbufCRead(sb, out + got1, 25, &got2);
    if (got2 != 10)
        TEST_FAILV(ret, 1, _SL("second drain read ${uint} bytes, want 10"), stvar(size, got2));
    if (memcmp(out, testdata1, 20))
        TEST_FAILV(ret, 1, _SL("the two producers did not join up"), stvNone);
    if (!p2.didclean)
        TEST_FAILV(ret, 1, _SL("the second producer's cleanup did not run at unregister"), stvNone);

    sbufClose(sb);
    sbufRelease(&sb);

    return ret;
}

// Bytes written with nobody attached pile up in the ring, and the consumer that registers next is
// handed all of them. A consumer may then leave and another take over mid-stream.
static int test_streambuf_chandoff()
{
    int ret = 0;
    CollectCtx c1 = { 0 }, c2 = { 0 };

    StreamBuffer *sb = sbufCreate(32);

    // start producing before there is anywhere for it to go
    if (!sbufPWrite(sb, testdata1, 10))
        TEST_FAILV(ret, 1, _SL("sbufPWrite with no consumer attached failed"), stvNone);
    if (sbufCAvail(sb) != 10)
        TEST_FAILV(ret, 1, _SL("sbufCAvail=${uint}, want 10 waiting in the ring"), stvar(size, sbufCAvail(sb)));
    if (sbufCAttached(sb))
        TEST_FAILV(ret, 1, _SL("sbufCAttached true with nothing registered"), stvNone);

    // registering hands over the backlog on the spot
    if (!sbufCRegisterPush(sb, collectNotify, collectClean, &c1))
        TEST_FAIL(1, _SL("first sbufCRegisterPush failed"), stvNone);
    if (strLen(c1.out) != 10)
        TEST_FAILV(ret, 1, _SL("the arriving consumer got ${int} bytes, want 10"), stvar(int32, (int32)strLen(c1.out)));

    sbufPWrite(sb, testdata1 + 10, 5);

    // the consumer steps aside; the producer neither knows nor cares
    sbufCUnregister(sb);
    if (!c1.didclean)
        TEST_FAILV(ret, 1, _SL("the first consumer's cleanup did not run at unregister"), stvNone);
    if (sbufIsClosed(sb))
        TEST_FAILV(ret, 1, _SL("stream ended when the consumer only unregistered"), stvNone);

    if (!sbufPWrite(sb, testdata1 + 15, 7))
        TEST_FAILV(ret, 1, _SL("sbufPWrite after the consumer left failed"), stvNone);

    // everything written in between reaches whoever attaches next
    if (!sbufCRegisterPush(sb, collectNotify, collectClean, &c2))
        TEST_FAIL(1, _SL("second sbufCRegisterPush failed"), stvNone);

    if (strLen(c1.out) != 15 || memcmp(strC(c1.out), testdata1, 15))
        TEST_FAILV(ret, 1, _SL("the first consumer ended up with '${string}'"), stvar(strref, c1.out));
    if (strLen(c2.out) != 7 || memcmp(strC(c2.out), testdata1 + 15, 7))
        TEST_FAILV(ret, 1, _SL("the second consumer ended up with '${string}'"), stvar(strref, c2.out));

    sbufClose(sb);
    sbufRelease(&sb);

    if (!c2.didclean)
        TEST_FAILV(ret, 1, _SL("the second consumer's cleanup did not run"), stvNone);

    strDestroy(&c1.out);
    strDestroy(&c2.out);
    return ret;
}

// The log-rotation case: a consumer is swapped out from under a stream it never asked to leave.
// Everything written before the swap has to reach the outgoing sink, and none of it the new one.
static int test_streambuf_cswap()
{
    int ret = 0;
    CollectCtx c1 = { .take = 4 }, c2 = { 0 };

    StreamBuffer *sb = sbufCreate(64);
    if (!sbufCRegisterPush(sb, collectNotify, collectClean, &c1))
        TEST_FAIL(1, _SL("first sbufCRegisterPush failed"), stvNone);

    // deliberately under-read, so a backlog is left behind that belongs to this consumer
    sbufPWrite(sb, testdata1, 12);
    if (strLen(c1.out) != 4)
        TEST_FAILV(ret, 1, _SL("the under-reading consumer took ${int} bytes, want 4"), stvar(int32, (int32)strLen(c1.out)));
    if (sbufCAvail(sb) != 8)
        TEST_FAILV(ret, 1, _SL("sbufCAvail=${uint}, want 8 left over"), stvar(size, sbufCAvail(sb)));

    // an honest answer on an unlocked buffer: it offered the backlog again and it was not all taken
    if (sbufPFlush(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush claimed success with a backlog still buffered"), stvNone);

    c1.take = 0;   // this time it will take everything
    if (!sbufPFlush(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush failed with a consumer willing to take it all"), stvNone);
    if (sbufCAvail(sb) != 0)
        TEST_FAILV(ret, 1, _SL("sbufCAvail=${uint} after a successful flush, want 0"), stvar(size, sbufCAvail(sb)));

    sbufCUnregister(sb);
    if (!c1.didclean)
        TEST_FAILV(ret, 1, _SL("the outgoing sink's cleanup did not run at the unregister"), stvNone);

    if (!sbufCRegisterPush(sb, collectNotify, collectClean, &c2))
        TEST_FAIL(1, _SL("second sbufCRegisterPush failed"), stvNone);
    sbufPWrite(sb, testdata1 + 12, 6);

    if (strLen(c1.out) != 12 || memcmp(strC(c1.out), testdata1, 12))
        TEST_FAILV(ret, 1, _SL("the outgoing sink ended up with '${string}'"), stvar(strref, c1.out));
    if (strLen(c2.out) != 6 || memcmp(strC(c2.out), testdata1 + 12, 6))
        TEST_FAILV(ret, 1, _SL("the incoming sink ended up with '${string}'"), stvar(strref, c2.out));

    sbufClose(sb);
    sbufRelease(&sb);

    strDestroy(&c1.out);
    strDestroy(&c2.out);
    return ret;
}

// A failure reported by the producer stops the stream dead but does not end it, so the driving side
// can unregister whoever failed, clear the error and carry on with a replacement.
static int test_streambuf_error()
{
    int ret = 0;
    uint8 out[TESTBUF_SZ];
    size_t got = 0;

    FailProducer fp = { .data = testdata1, .len = 8 };
    RangeProducer rp = { .data = testdata1 + 8, .len = 8 };

    StreamBuffer *sb = sbufCreate(32);
    if (!sbufPRegisterPull(sb, failPull, NULL, &fp))
        TEST_FAIL(1, _SL("sbufPRegisterPull failed"), stvNone);

    sbufCRead(sb, out, 8, &got);
    if (got != 8)
        TEST_FAILV(ret, 1, _SL("read ${uint} bytes before the failure, want 8"), stvar(size, got));

    // the next read runs the producer into its failure
    if (sbufCRead(sb, out + 8, 8, &got) || got != 0)
        TEST_FAILV(ret, 1, _SL("a read across the failure returned ${uint} bytes"), stvar(size, got));
    if (!sbufIsError(sb))
        TEST_FAILV(ret, 1, _SL("the producer's failure was not recorded"), stvNone);
    if (sbufIsClosed(sb))
        TEST_FAILV(ret, 1, _SL("a reported failure ended the stream"), stvNone);
    if (sbufCMore(sb))
        TEST_FAILV(ret, 1, _SL("sbufCMore true on a failed stream, which is the loop that hangs"), stvNone);
    if (sbufPWrite(sb, testdata1, 4))
        TEST_FAILV(ret, 1, _SL("a write succeeded while the error stood"), stvNone);

    // recover: drop the party that failed, clear the flag, attach a replacement
    sbufPUnregister(sb);
    sbufClearError(sb);
    if (sbufIsError(sb))
        TEST_FAILV(ret, 1, _SL("sbufClearError did not clear the error"), stvNone);
    if (!sbufPRegisterPull(sb, rangePull, rangeClean, &rp))
        TEST_FAIL(1, _SL("could not attach a replacement producer"), stvNone);

    sbufCRead(sb, out + 8, 8, &got);
    if (got != 8 || memcmp(out, testdata1, 16))
        TEST_FAILV(ret, 1, _SL("the replacement delivered ${uint} bytes"), stvar(size, got));

    sbufClose(sb);
    sbufRelease(&sb);

    return ret;
}

// sbufClose() has to reach a registered pull producer, since that final sz == 0 callback is the only
// thing that tells it to let go of the slot -- and therefore of its reference.
static int test_streambuf_endpull()
{
    int ret = 0;
    uint8 out[TESTBUF_SZ];
    size_t got = 0;

    for (int locked = 0; locked < 2; locked++) {
        RangeProducer rp = { .data = testdata1, .len = sizeof(testdata1) };

        StreamBuffer *sb = sbufCreate(32, locked ? SBUF_Locked : 0);
        if (!sbufPRegisterPull(sb, rangePull, rangeClean, &rp))
            TEST_FAILV(ret, 1, _SL("sbufPRegisterPull failed, locked=${int}"), stvar(int32, locked));

        sbufCRead(sb, out, 8, &got);
        if (got != 8)
            TEST_FAILV(ret, 1, _SL("read ${uint} bytes, want 8, locked=${int}"), stvar(size, got), stvar(int32, locked));
        if (!sbufPAttached(sb))
            TEST_FAILV(ret, 1, _SL("producer detached early, locked=${int}"), stvar(int32, locked));

        // abandoning it mid-source still has to get the producer to let go
        sbufClose(sb);
        if (sbufPAttached(sb))
            TEST_FAILV(ret, 1, _SL("producer still attached after sbufClose, locked=${int}"), stvar(int32, locked));
        if (!rp.didclean)
            TEST_FAILV(ret, 1, _SL("producer cleanup did not run, locked=${int}"), stvar(int32, locked));

        // the last reference, so anything the producer was still holding would leak here
        sbufRelease(&sb);
    }

    return ret;
}

// sbufPFlush() is a mid-stream catch-up, not an end-of-stream signal, and it has nothing to say
// about a buffer that only fills on demand.
static int test_streambuf_flush()
{
    int ret = 0;
    CollectCtx cc = { 0 };
    RangeProducer rp = { .data = testdata1, .len = 16 };

    StreamBuffer *sb = sbufCreate(32);
    if (!sbufCRegisterPush(sb, collectNotify, collectClean, &cc))
        TEST_FAIL(1, _SL("sbufCRegisterPush failed"), stvNone);

    // nothing buffered, so there is nothing to catch up on
    if (!sbufPFlush(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush failed on an empty buffer"), stvNone);

    sbufPWrite(sb, testdata1, 6);
    if (!sbufPFlush(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush failed after a consumer took everything"), stvNone);
    if (sbufIsClosed(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush ended the stream"), stvNone);
    if (!sbufCAttached(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush detached the consumer"), stvNone);

    sbufClose(sb);
    sbufRelease(&sb);
    strDestroy(&cc.out);

    // pull mode fills on demand and any write is on the consumer's own stack, so this is refused
    sb = sbufCreate(32);
    if (!sbufPRegisterPull(sb, rangePull, rangeClean, &rp))
        TEST_FAIL(1, _SL("sbufPRegisterPull failed"), stvNone);
    if (sbufPFlush(sb))
        TEST_FAILV(ret, 1, _SL("sbufPFlush succeeded in pull mode"), stvNone);

    sbufClose(sb);
    sbufRelease(&sb);

    return ret;
}

// One stream buffer, two JSON documents, with the application's own framing bytes in between --
// the case a stream that ended with its first document could not do.
static int test_streambuf_reuse()
{
    int ret    = 0;
    string out = 0;

    StreamBuffer *sb = sbufCreate(256);
    if (!sbufStrCRegisterPush(sb, &out))
        TEST_FAIL(1, _SL("sbufStrCRegisterPush failed"), stvNone);

    SSDNode *a = ssdCreateHashtable();
    ssdSet(a, _S"first", true, stvar(int32, 1));
    SSDNode *b = ssdCreateHashtable();
    ssdSet(b, _S"second", true, stvar(int32, 2));

    if (!jsonOutTree(sb, a, JSON_Single_Line | JSON_Compact))
        TEST_FAILV(ret, 1, _SL("the first jsonOutTree failed"), stvNone);

    // framing the application chose, written straight into the same stream
    sbufPWriteStr(sb, _S"\n---\n");

    if (!jsonOutTree(sb, b, JSON_Single_Line | JSON_Compact))
        TEST_FAILV(ret, 1, _SL("the second jsonOutTree failed"), stvNone);

    objRelease(&a);
    objRelease(&b);

    sbufClose(sb);
    sbufRelease(&sb);

    if (!strEq(out, _S"{\"first\":1}\n---\n{\"second\":2}"))
        TEST_FAILV(ret, 1, _SL("two documents into one stream gave '${string}'"), stvar(strref, out));

    strDestroy(&out);
    return ret;
}

// ------------------------------------------------------------------------------------------
// Locked buffers, with a real second thread
// ------------------------------------------------------------------------------------------

typedef struct ThreadedCtx {
    StreamBuffer *sb;
    size_t towrite;
    bool wait;         // block at the watermark rather than being refused
    bool flush;        // call sbufPFlush() when done writing
    bool writeOk;
    bool flushOk;
    Event parked;      // signalled once the producer is about to block
} ThreadedCtx;

static int threadedProducer(Thread *self)
{
    ThreadedCtx *tc = NULL;
    if (!stvlNext(&self->args, ptr, &tc))
        return 0;

    eventSignalAll(&tc->parked);

    // In chunks, because the watermark is only checked on the way in: one write big enough to
    // blow past the high mark goes through and parks the write after it, not itself.
    tc->writeOk = true;
    for (size_t pos = 0; pos < tc->towrite; pos += 4) {
        if (!sbufPWrite(tc->sb, testdata1 + pos, 4, tc->wait ? SBUF_Wait : 0)) {
            tc->writeOk = false;
            break;
        }
    }

    if (tc->flush)
        tc->flushOk = sbufPFlush(tc->sb);

    return 0;
}

// A producer parked at the watermark has to survive the consumer going away, and be released by
// whoever attaches next. If nobody does, sbufClose() has to wake it so it fails instead of hanging.
static int test_streambuf_threaded()
{
    int ret = 0;
    CollectCtx c1 = { .take = 1 }, c2 = { 0 };

    // --- a replacement consumer releases the parked producer ---------------------------------
    ThreadedCtx tc = { .towrite = 64, .wait = true };
    tc.sb          = sbufCreate(16, SBUF_Locked);
    eventInit(&tc.parked);
    sbufSetWatermark(tc.sb, 8, 4);

    if (!sbufCRegisterPush(tc.sb, collectNotify, collectClean, &c1))
        TEST_FAIL(1, _SL("sbufCRegisterPush failed"), stvNone);

    Thread *t = thrCreate(threadedProducer, _S"sbuf producer", stvar(ptr, &tc));
    if (!t)
        TEST_FAIL(1, _SL("thrCreate failed"), stvNone);

    // Let it get going and fill the ring past the high mark. The consumer takes a byte at a time,
    // so it cannot drain fast enough to keep the producer from parking.
    eventWait(&tc.parked);
    for (int i = 0; i < 200 && sbufCAvail(tc.sb) < 8; i++)
        osSleep(timeMS(1));

    // swap the consumer out from under the parked producer, then hand the stream to a new one
    sbufCUnregister(tc.sb);
    if (!sbufCRegisterPush(tc.sb, collectNotify, collectClean, &c2))
        TEST_FAIL(1, _SL("the replacement sbufCRegisterPush failed"), stvNone);

    thrWait(t, timeS(10));
    if (!tc.writeOk)
        TEST_FAILV(ret, 1, _SL("the parked producer never got to finish its write"), stvNone);
    if (strLen(c1.out) + strLen(c2.out) != 64)
        TEST_FAILV(ret, 1, _SL("the two consumers took ${int} bytes between them, want 64"),
                   stvar(int32, (int32)(strLen(c1.out) + strLen(c2.out))));

    thrShutdown(t);
    thrRelease(&t);
    sbufClose(tc.sb);
    sbufRelease(&tc.sb);
    eventDestroy(&tc.parked);
    strDestroy(&c1.out);
    strDestroy(&c2.out);

    // --- ending the stream wakes a parked producer so it fails rather than hanging ------------
    ThreadedCtx tc2 = { .towrite = 64, .wait = true };
    tc2.sb          = sbufCreate(16, SBUF_Locked);
    eventInit(&tc2.parked);
    sbufSetWatermark(tc2.sb, 8, 4);

    t = thrCreate(threadedProducer, _S"sbuf parked producer", stvar(ptr, &tc2));
    if (!t)
        TEST_FAIL(1, _SL("thrCreate failed"), stvNone);

    eventWait(&tc2.parked);
    for (int i = 0; i < 200 && !sbufPIsHeld(tc2.sb); i++)
        osSleep(timeMS(1));

    sbufClose(tc2.sb);
    thrWait(t, timeS(10));
    if (tc2.writeOk)
        TEST_FAILV(ret, 1, _SL("a write into an ended stream reported success"), stvNone);

    thrShutdown(t);
    thrRelease(&t);
    sbufRelease(&tc2.sb);
    eventDestroy(&tc2.parked);

    // --- sbufPFlush() waits for the draining thread to catch up ------------------------------
    ThreadedCtx tc3 = { .towrite = 32, .flush = true };
    tc3.sb          = sbufCreate(64, SBUF_Locked);
    eventInit(&tc3.parked);

    t = thrCreate(threadedProducer, _S"sbuf flushing producer", stvar(ptr, &tc3));
    if (!t)
        TEST_FAIL(1, _SL("thrCreate failed"), stvNone);

    // Nothing is registered, so the flush has nobody to notify and simply waits for this thread to
    // take the bytes.
    eventWait(&tc3.parked);
    uint8 drained[TESTBUF_SZ];
    size_t total = 0;
    for (int i = 0; i < 2000 && total < tc3.towrite; i++) {
        size_t avail = sbufCAvail(tc3.sb), didread = 0;
        if (avail > 0 && sbufCRead(tc3.sb, drained + total, avail, &didread))
            total += didread;
        else
            osSleep(timeMS(1));
    }

    thrWait(t, timeS(10));
    if (!tc3.flushOk)
        TEST_FAILV(ret, 1, _SL("sbufPFlush did not report the buffer as drained"), stvNone);
    if (total != 32 || memcmp(drained, testdata1, 32))
        TEST_FAILV(ret, 1, _SL("drained ${uint} bytes past a successful flush, want 32"), stvar(size, total));

    thrShutdown(t);
    thrRelease(&t);
    sbufClose(tc3.sb);
    sbufRelease(&tc3.sb);
    eventDestroy(&tc3.parked);

    return ret;
}

testfunc sbtest_funcs[] = {
    { "push", test_streambuf_push },
    { "pull", test_streambuf_pull },
    { "direct", test_streambuf_direct },
    { "peek", test_streambuf_peek },
    { "string", test_streambuf_string },
    { "buffer", test_streambuf_buffer },
    { "console", test_streambuf_console },
    { "phandoff", test_streambuf_phandoff },
    { "chandoff", test_streambuf_chandoff },
    { "cswap", test_streambuf_cswap },
    { "error", test_streambuf_error },
    { "endpull", test_streambuf_endpull },
    { "flush", test_streambuf_flush },
    { "reuse", test_streambuf_reuse },
    { "threaded", test_streambuf_threaded },
    { 0, 0 }
};

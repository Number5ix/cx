#include <cx/serialize/streambuf.h>
#include <cx/buffer/buffer.h>
#include <cx/serialize/sbbuffer.h>
#include <cx/serialize/sbcon.h>
#include <cx/serialize/sbstring.h>
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
        sbufCSend(sb, sbsend1, min(sz, tc->shouldread));
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
        if (!sbufPRegisterPush(ptest, NULL, 0)) {
            TEST_FAILV(ret, 1, _SL("sbufPRegisterPush failed, usesend=${int}"), stvar(int32, usesend));
            goto out;
        }

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
        sbufPFinish(ptest);
        // this should cause sbclean1 to be called
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
        sbufPFinish(sb);

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

    if (!sbufCRegisterPull(ptest, NULL, NULL))
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

    // test releasing in opposite order
    StreamBuffer *temp = ptest;
    sbufRelease(&temp);
    sbufCFinish(ptest);

    if (!ctx.didclean)
        TEST_FAILV(ret, 1, _SL("ctx.didclean not set"), stvNone);

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

    if (!sbufCRegisterPull(ptest, NULL, NULL))
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

    sbufCFinish(ptest);
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
    if (!sbufPRegisterPush(ptest, NULL, 0)) {
        TEST_FAILV(ret, 1, _SL("sbufPRegisterPush failed"), stvNone);
        goto out;
    }

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

    sbufPFinish(ptest);
    // this should cause sbclean3 to be called
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
    if (!sbufCRegisterPull(ptest, NULL, NULL) ||
        !sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufCRegisterPull/sbufStrPRegisterPull failed"), stvNone);

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

    sbufCFinish(ptest);
    if (!sbufIsPFinished(ptest))
        TEST_FAILV(ret, 1, _SL("sbufIsPFinished false after sbufCFinish"), stvNone);

    sbufRelease(&ptest);

    // hook up a string consumer and use the string producer to push to it

    ptest = sbufCreate(16);
    if (!sbufStrCRegisterPush(ptest, &s2))
        TEST_FAIL(1, _SL("sbufStrCRegisterPush failed"), stvNone);
    sbufStrIn(ptest, s1);

    if (!strEq(s1, s2))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != s2='${string}'"), stvar(strref, s1), stvar(strref, s2));

    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&s2);

    // then the reverse

    ptest = sbufCreate(16);
    if (!sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufStrPRegisterPull failed"), stvNone);
    sbufStrOut(ptest, &s2);
    if (!strEq(s1, s2))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != s2='${string}'"), stvar(strref, s1), stvar(strref, s2));

    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);

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

    conMemGet(con, &out);
    if (!strEq(s1, out))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != out='${string}'"), stvar(strref, s1), stvar(strref, out));
    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);

    sbufRelease(&ptest);
    strDestroy(&out);
    conDestroy(&con);

    // pull: string producer -> console consumer via sbufConOut
    con = conCreateMem(&caps);
    ptest = sbufCreate(16);
    if (!sbufStrPRegisterPull(ptest, s1))
        TEST_FAIL(1, _SL("sbufStrPRegisterPull failed"), stvNone);
    sbufConOut(ptest, con);

    conMemGet(con, &out);
    if (!strEq(s1, out))
        TEST_FAILV(ret, 1, _SL("s1='${string}' != out='${string}'"), stvar(strref, s1), stvar(strref, out));
    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);

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

    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("push produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);
    sbufRelease(&ptest);

    // The consumer appends, so a second run through a fresh stream lands after the first.
    ptest = sbufCreate(16);
    if (!sbufBufCRegisterPush(ptest, &out))
        TEST_FAIL(1, _SL("sbufBufCRegisterPush failed on reuse"), stvNone);
    sbufBufIn(ptest, src, false);
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
    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("pull produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    if (!sbufIsPFinished(ptest) || !sbufIsCFinished(ptest))
        TEST_FAILV(ret, 1, _SL("producer or consumer not finished"), stvNone);
    sbufRelease(&ptest);
    bufDestroy(&out);

    // The convenience constructor puts the caller on the producing end, writing straight into a
    // buffer that starts out NULL.
    ptest = sbufBufCreatePush(&out);
    if (!ptest)
        TEST_FAIL(1, _SL("sbufBufCreatePush failed"), stvNone);
    if (!sbufPWrite(ptest, testdata1, 10) || !sbufPWrite(ptest, testdata1 + 10, len - 10))
        TEST_FAILV(ret, 1, _SL("sbufPWrite failed"), stvNone);
    sbufPFinish(ptest);
    if (bufLen(out) != len || memcmp(out->data, testdata1, len))
        TEST_FAILV(ret,
                   1,
                   _SL("two direct writes produced ${uint} bytes, want ${uint}"),
                   stvar(size, bufLen(out)),
                   stvar(size, len));
    sbufRelease(&ptest);
    bufDestroy(&out);

    // sbufBufIn owning its source destroys it, which is the whole reason it takes ownership.
    // It registers the producer itself, so it needs a stream buffer that has none yet -- not one
    // from sbufBufCreatePush(), which puts the caller on that end.
    Buffer owned = bufCreate(len);
    memcpy(owned->data, testdata1, len);
    owned->len = len;
    ptest      = sbufCreate(16);
    if (!sbufBufCRegisterPush(ptest, &out))
        TEST_FAIL(1, _SL("sbufBufCRegisterPush failed for the owned source"), stvNone);
    sbufBufIn(ptest, owned, true);
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

testfunc sbtest_funcs[] = {
    { "push", test_streambuf_push },
    { "pull", test_streambuf_pull },
    { "direct", test_streambuf_direct },
    { "peek", test_streambuf_peek },
    { "string", test_streambuf_string },
    { "buffer", test_streambuf_buffer },
    { "console", test_streambuf_console },
    { 0, 0 }
};

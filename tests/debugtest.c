#include <cx/debug.h>
#include <string.h>

#define TEST_FILE debugtest
#define TEST_FUNCS debugtest_funcs
#include "common.h"

static int assertcount = 0;
static int asserttest = 0;
static bool assertfail = false;

static int assertCb(const char *expr, const char *msg, const char *file, int line)
{
    assertcount++;

    switch (asserttest) {
    case 0:
        if (strcmp(expr, "0 == 1") != 0 || msg != NULL)
            TEST_FAILV(assertfail, true, _SL("case 0: expr='${string}' msg=${ptr} (want expr='0 == 1', msg=NULL)"), stvar(strref, (strref)expr), stvar(ptr, (void*)msg));
        break;
    case 1:
        if (strcmp(expr, "0 == 1") != 0 || strcmp(msg, "Test 1-2-3") != 0)
            TEST_FAILV(assertfail, true, _SL("case 1: expr='${string}' msg='${string}' (want expr='0 == 1', msg='Test 1-2-3')"), stvar(strref, (strref)expr), stvar(strref, (strref)msg));
        break;
    case 2:
        // we should never get 2 because it's a true expression
        TEST_FAILV(assertfail, true, _SL("case 2: assertCb() called for a true expression '${string}'"), stvar(strref, (strref)expr));
        break;
    case 3:
        if (strcmp(expr, "false") != 0 || strcmp(msg, "Dev Only") != 0)
            TEST_FAILV(assertfail, true, _SL("case 3: expr='${string}' msg='${string}' (want expr='false', msg='Dev Only')"), stvar(strref, (strref)expr), stvar(strref, (strref)msg));
        break;
    case 4:
        if (expr != NULL || strcmp(msg, "OH NO!") != 0)
            TEST_FAILV(assertfail, true, _SL("case 4: expr=${ptr} msg='${string}' (want expr=NULL, msg='OH NO!')"), stvar(ptr, (void*)expr), stvar(strref, (strref)msg));
        break;
    }

    return ASSERT_Ignore;
}

static int test_assert()
{
    int ret = 0;
    int lasttest = 0;

    // install assertion handler so these don't all crash
    dbgAssertAddCallback(assertCb);

    relAssert(0 == 1);
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relAssert(0==1): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    relAssertMsg(0 == 1, "Test 1-2-3");
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relAssertMsg(0==1): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    lasttest = asserttest;
    relAssert(5 == 5);
    asserttest++;
    if (assertfail || assertcount != lasttest)
        TEST_FAILV(ret, 1, _SL("after relAssert(5==5): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, lasttest));
    assertcount = asserttest;           // resync

    if (devVerifyMsg(false, "Dev Only"))
        TEST_FAILV(ret, 1, _SL("devVerifyMsg(false, 'Dev Only') returned true"), stvNone);

    asserttest++;
#if DEBUG_LEVEL > 0
    // Should have asserted in dev / debug builds
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after devVerifyMsg (dev build): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));
#else
    // Should NOT have asserted in release builds
    if (assertfail || assertcount != asserttest - 1)
        TEST_FAILV(ret, 1, _SL("after devVerifyMsg (release build): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest - 1));
    assertcount++;      // pretend it did
#endif

    relFatalError("OH NO!");
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relFatalError('OH NO!'): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    return ret;
}

// Finds an entry by walking the black box from its head, the way a crash dump parser does
static BlackBoxEnt *bboxFind(const char *name)
{
    uint16 idx = ((uint16*)dbgBlackBox)[0];
    while (idx) {
        BlackBoxEnt *ent = (BlackBoxEnt*)&dbgBlackBox[idx];
        if (strcmp(ent->name, name) == 0)
            return ent;
        idx = ent->next;
    }
    return NULL;
}

static int bboxCount(void)
{
    int count = 0;
    for (uint16 idx = ((uint16*)dbgBlackBox)[0]; idx; idx = ((BlackBoxEnt*)&dbgBlackBox[idx])->next)
        count++;
    return count;
}

// Checks that every entry starts on an even offset
static bool bboxAligned(void)
{
    for (uint16 idx = ((uint16*)dbgBlackBox)[0]; idx; idx = ((BlackBoxEnt*)&dbgBlackBox[idx])->next) {
        if (idx & 1) {
            TEST_WARN(_SL("entry '${string}' is at odd offset ${int}"), stvar(strref, (strref)((BlackBoxEnt*)&dbgBlackBox[idx])->name), stvar(int32, idx));
            return false;
        }
    }
    return true;
}

// Checks that an entry exists with the given value and flags
static bool bboxCheck(const char *name, const char *val, uint8 flags)
{
    BlackBoxEnt *ent = bboxFind(name);
    if (!ent) {
        TEST_WARN(_SL("entry '${string}' not found (want value '${string}')"), stvar(strref, (strref)name), stvar(strref, (strref)val));
        return false;
    }

    const char *got = bboxGetVal(ent);
    if (strcmp(got, val) != 0 || ent->flags != flags) {
        TEST_WARN(_SL("entry '${string}': value='${string}' flags=${int} (want '${string}', ${int})"),
                  stvar(strref, (strref)name), stvar(strref, (strref)got), stvar(int32, ent->flags),
                  stvar(strref, (strref)val), stvar(int32, flags));
        return false;
    }

    return true;
}

static int test_bbox()
{
    int startcount = bboxCount();
    BlackBoxEnt *ent, *ent2;

    // basic set
    if (!bboxSet(_SL("bbtest_a"), _SL("hello"), 0) || !bboxCheck("bbtest_a", "hello", 0))
        return 1;
    ent = bboxFind("bbtest_a");
    if (ent->namelen != 9 || ent->vallen != 6)
        TEST_FAIL(1, _SL("bbtest_a: namelen=${int} vallen=${int} (want 9, 6)"), stvar(int32, ent->namelen), stvar(int32, ent->vallen));

    // a shorter value is written in place
    if (!bboxSet(_SL("bbtest_a"), _SL("hi"), BBox_Private) || !bboxCheck("bbtest_a", "hi", BBox_Private))
        return 1;
    ent2 = bboxFind("bbtest_a");
    if (ent2 != ent || ent2->vallen != 3)
        TEST_FAIL(1, _SL("shorter value: entry at ${ptr} vallen=${int} (want ${ptr}, 3)"), stvar(ptr, ent2), stvar(int32, ent2->vallen), stvar(ptr, ent));

    // a longer one replaces the entry
    if (!bboxSet(_SL("bbtest_a"), _SL("a considerably longer value"), 0) || !bboxCheck("bbtest_a", "a considerably longer value", 0))
        return 1;
    if (bboxCount() != startcount + 1)
        TEST_FAIL(1, _SL("after replacing bbtest_a: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount + 1));

    bboxDelete(_SL("bbtest_a"));
    bboxDelete(_SL("bbtest_a"));        // deleting a missing entry does nothing
    if (bboxFind("bbtest_a") || bboxCount() != startcount)
        TEST_FAIL(1, _SL("after deleting bbtest_a: found=${ptr} entries=${int} (want NULL, ${int})"), stvar(ptr, bboxFind("bbtest_a")), stvar(int32, bboxCount()), stvar(int32, startcount));

    // Entries of odd sizes still each start on an even offset. o1 and o2 are 23 bytes; o2
    // then shrinks in place to 21, and the entry after it has to skip the odd byte left over.
    if (((uintptr)dbgBlackBox & 1) != 0)
        TEST_FAIL(1, _SL("dbgBlackBox is at odd address ${ptr}"), stvar(ptr, dbgBlackBox));
    bboxSet(_SL("bbtest_o1"), _SL("bbbb"), 0);
    bboxSet(_SL("bbtest_o2"), _SL("cccc"), 0);
    bool aligned = bboxAligned();
    bboxSet(_SL("bbtest_o2"), _SL("cc"), 0);
    bboxSet(_SL("bbtest_o3"), _SL("d"), 0);
    aligned = aligned && bboxAligned();
    ent     = bboxFind("bbtest_o2");
    ent2    = bboxFind("bbtest_o3");
    if (aligned && ent && ent2 && (char*)ent2 != (char*)ent + 22) {
        TEST_WARN(_SL("bbtest_o3 is ${int} bytes after bbtest_o2 (want 22)"), stvar(int32, (int32)((char*)ent2 - (char*)ent)));
        aligned = false;
    }
    aligned = aligned && bboxCheck("bbtest_o1", "bbbb", 0) && bboxCheck("bbtest_o2", "cc", 0) && bboxCheck("bbtest_o3", "d", 0);
    bboxDelete(_SL("bbtest_o1"));
    bboxDelete(_SL("bbtest_o2"));
    bboxDelete(_SL("bbtest_o3"));
    if (!aligned || bboxCount() != startcount)
        TEST_FAIL(1, _SL("odd-sized entries: aligned=${int} entries=${int} (want 1, ${int})"), stvar(int32, aligned), stvar(int32, bboxCount()), stvar(int32, startcount));

    // Freeing an entry that sits between two free blocks must merge all three. b, c and d are
    // 28 bytes each, and x is sized to fit only in the 84 bytes they leave behind together.
    bboxSet(_SL("bbtest_b"), _SL("0123456789"), 0);
    bboxSet(_SL("bbtest_c"), _SL("0123456789"), 0);
    bboxSet(_SL("bbtest_d"), _SL("0123456789"), 0);
    bboxSet(_SL("bbtest_e"), _SL("0123456789"), 0);
    ent = bboxFind("bbtest_b");
    if (!ent || !bboxFind("bbtest_c") || !bboxFind("bbtest_d") || !bboxFind("bbtest_e"))
        TEST_FAIL(1, _SL("bbtest_b..e not all present: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount + 4));
    if ((char*)bboxFind("bbtest_d") != (char*)ent + 56 || (char*)bboxFind("bbtest_e") != (char*)ent + 84)
        TEST_FAIL(1, _SL("bbtest_b/d/e at ${ptr}/${ptr}/${ptr}, not 28 bytes apart"), stvar(ptr, ent), stvar(ptr, bboxFind("bbtest_d")), stvar(ptr, bboxFind("bbtest_e")));

    bboxDelete(_SL("bbtest_b"));
    bboxDelete(_SL("bbtest_d"));
    bboxDelete(_SL("bbtest_c"));
    bboxSet(_SL("bbtest_x"), _SL("012345678901234567890123456789012345678901234567890123456789012345"), 0);
    ent2 = bboxFind("bbtest_x");
    if (ent2 != ent)
        TEST_FAIL(1, _SL("bbtest_x placed at ${ptr}; want ${ptr}, the merged space of three freed entries"), stvar(ptr, ent2), stvar(ptr, ent));
    bboxDelete(_SL("bbtest_x"));
    bboxDelete(_SL("bbtest_e"));

    // values over BLACKBOX_VALUE_MAX are refused; 65530 is the size at which the entry's
    // 16-bit length fields used to wrap around separately from the size of the allocation
    static const uint32 bigsizes[] = { BLACKBOX_VALUE_MAX, 65530, 65536, 70000 };
    string big                     = 0;
    for (int i = 0; i < 4; i++) {
        memset(strBuffer(&big, bigsizes[i]), 'v', bigsizes[i]);
        strSetLen(&big, bigsizes[i]);
        bool stored = bboxSet(_SL("bbtest_big"), big, 0);
        if (stored || bboxFind("bbtest_big") || bboxCount() != startcount) {
            strDestroy(&big);
            TEST_FAIL(1, _SL("${int} byte value: returned=${int} found=${ptr} entries=${int} (want 0, NULL, ${int})"), stvar(int32, bigsizes[i]), stvar(int32, stored), stvar(ptr, bboxFind("bbtest_big")), stvar(int32, bboxCount()), stvar(int32, startcount));
        }
    }

    // the largest value that fits is one byte short of that, leaving room for its terminator
    strSetLen(&big, BLACKBOX_VALUE_MAX - 1);
    bool ok = bboxSet(_SL("bbtest_big"), big, 0);
    ent     = bboxFind("bbtest_big");
    if (!ok || !ent || ent->vallen != BLACKBOX_VALUE_MAX || strlen(bboxGetVal(ent)) != BLACKBOX_VALUE_MAX - 1) {
        strDestroy(&big);
        TEST_FAIL(1, _SL("${int} byte value: returned=${int} vallen=${int} (want 1, ${int})"), stvar(int32, BLACKBOX_VALUE_MAX - 1), stvar(int32, ok), stvar(int32, ent ? ent->vallen : -1), stvar(int32, BLACKBOX_VALUE_MAX));
    }

    // a value that is refused leaves the entry it would have replaced alone
    bboxSet(_SL("bbtest_big"), _SL("small"), 0);
    strSetLen(&big, BLACKBOX_VALUE_MAX);
    ok = bboxSet(_SL("bbtest_big"), big, 0);
    if (ok || !bboxCheck("bbtest_big", "small", 0)) {
        strDestroy(&big);
        TEST_FAIL(1, _SL("oversized replacement of bbtest_big: returned=${int} (want 0, value kept)"), stvar(int32, ok));
    }
    bboxDelete(_SL("bbtest_big"));

    // so are names that don't fit the 8-bit name length
    strSetLen(&big, 255);
    ok = bboxSet(big, _SL("value"), 0);
    if (ok || bboxCount() != startcount) {
        strDestroy(&big);
        TEST_FAIL(1, _SL("255 byte name: returned=${int} entries=${int} (want 0, ${int})"), stvar(int32, ok), stvar(int32, bboxCount()), stvar(int32, startcount));
    }

    // 254 is the longest that does
    strSetLen(&big, 254);
    ok = bboxSet(big, _SL("value"), 0) && bboxCheck(strC(big), "value", 0);
    bboxDelete(big);
    strDestroy(&big);
    if (!ok)
        return 1;

    if (bboxCount() != startcount)
        TEST_FAIL(1, _SL("at end of test: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount));

    return 0;
}

#define BBTEST_FILL 200

static int test_bboxbuf()
{
    int ret        = 0;
    int startcount = bboxCount();
    BlackBoxEnt *ent, *reg;
    char name[32];
    char *buf, *buf2, *fill;
    int nfill = 0;

    if (bboxReserve(_SL("bbtest_buf"), 0, 0) || bboxReserve(_SL("bbtest_buf"), BLACKBOX_VALUE_MAX + 1, 0))
        TEST_FAIL(1, _SL("bboxReserve() accepted a size of 0 or ${int}"), stvar(int32, BLACKBOX_VALUE_MAX + 1));

    buf = bboxReserve(_SL("bbtest_buf"), 64, 0);
    if (!buf)
        TEST_FAIL(1, _SL("bboxReserve(bbtest_buf, 64) returned NULL"), stvNone);

    ent = bboxFind("bbtest_buf");
    if (!ent)
        TEST_FAILV(ret, 1, _SL("bbtest_buf not found by walking the black box"), stvNone);
    else if (bboxGetVal(ent) != buf || ent->flags != BBox_Buffer || ent->vallen != 65)
        TEST_FAILV(ret, 1, _SL("bbtest_buf: value at ${ptr} flags=${int} vallen=${int} (want ${ptr}, ${int}, 65)"),
                   stvar(ptr, bboxGetVal(ent)), stvar(int32, ent->flags), stvar(int32, ent->vallen), stvar(ptr, buf), stvar(int32, BBox_Buffer));
    if (ret)
        goto out;

    if (buf < dbgBlackBox || buf + 65 > dbgBlackBox + BLACKBOX_SIZE)
        TEST_FAILV(ret, 1, _SL("buffer ${ptr} is outside the black box at ${ptr}"), stvar(ptr, buf), stvar(ptr, dbgBlackBox));
    for (int i = 0; i < 65; i++) {
        if (buf[i] != 0) {
            TEST_FAILV(ret, 1, _SL("new buffer has byte ${int} at offset ${int} (want 0)"), stvar(int32, buf[i]), stvar(int32, i));
            break;
        }
    }
    if (ret)
        goto out;

    // filling every byte the caller owns still leaves a terminated value
    memset(buf, 'x', 64);
    if (strlen(bboxGetVal(ent)) != 64)
        TEST_FAILV(ret, 1, _SL("full buffer reads as ${int} bytes (want 64)"), stvar(int32, (int32)strlen(bboxGetVal(ent))));

    // direct writes show up as the value with no further calls
    memcpy(buf, "running", 8);
    if (!bboxCheck("bbtest_buf", "running", BBox_Buffer))
        ret = 1;

    // reserving again shares the buffer, contents and all
    if (bboxReserve(_SL("bbtest_buf"), 64, 0) != buf || bboxReserve(_SL("bbtest_buf"), 16, 0) != buf)
        TEST_FAILV(ret, 1, _SL("reserving bbtest_buf again did not return ${ptr}"), stvar(ptr, buf));
    if (strcmp(buf, "running") != 0)
        TEST_FAILV(ret, 1, _SL("reserving bbtest_buf again changed it to '${string}' (want 'running')"), stvar(strref, (strref)buf));
    // ...but can't make it bigger
    if (bboxReserve(_SL("bbtest_buf"), 65, 0))
        TEST_FAILV(ret, 1, _SL("bboxReserve(bbtest_buf, 65) succeeded on a 64 byte buffer"), stvNone);
    if (bboxCount() != startcount + 1)
        TEST_FAILV(ret, 1, _SL("after reserving bbtest_buf again: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount + 1));

    // bboxSet writes into the buffer rather than replacing it, cutting the value to fit
    if (!bboxSet(_SL("bbtest_buf"), _SL("0123456789012345678901234567890123456789012345678901234567890123456789"), BBox_Private))
        TEST_FAILV(ret, 1, _SL("bboxSet on a buffer returned false"), stvNone);
    if (!bboxCheck("bbtest_buf", "012345678901234567890123456789012345678901234567890123456789012", BBox_Buffer | BBox_Private))
        ret = 1;
    if (bboxFind("bbtest_buf") != ent || ent->vallen != 65)
        TEST_FAILV(ret, 1, _SL("bboxSet on a buffer: entry at ${ptr} vallen=${int} (want ${ptr}, 65)"), stvar(ptr, bboxFind("bbtest_buf")), stvar(int32, ent->vallen), stvar(ptr, ent));
    bboxSet(_SL("bbtest_buf"), _SL("idle"), 0);
    if (!bboxCheck("bbtest_buf", "idle", BBox_Buffer))
        ret = 1;
    if (ret)
        goto out;

    // regular entries coming and going leave the buffer alone
    bboxSet(_SL("bbtest_reg"), _SL("abc"), 0);
    bboxSet(_SL("bbtest_reg2"), _SL("def"), 0);
    bboxSet(_SL("bbtest_reg"), _SL("a longer value that has to move"), 0);
    bboxDelete(_SL("bbtest_reg2"));
    reg = bboxFind("bbtest_reg");
    if (!bboxCheck("bbtest_reg", "a longer value that has to move", 0) || !bboxCheck("bbtest_buf", "idle", BBox_Buffer))
        ret = 1;
    else if ((char*)reg > buf)
        TEST_FAILV(ret, 1, _SL("regular entry at ${ptr} is above the buffer at ${ptr}"), stvar(ptr, reg), stvar(ptr, buf));

    // a regular entry's name can't be reserved, and a buffer flag on bboxSet is ignored
    if (bboxReserve(_SL("bbtest_reg"), 8, 0))
        TEST_FAILV(ret, 1, _SL("bboxReserve() succeeded on the name of a regular entry"), stvNone);
    bboxSet(_SL("bbtest_reg"), _SL("abc"), BBox_Buffer);
    if (!bboxCheck("bbtest_reg", "abc", 0))
        ret = 1;
    bboxDelete(_SL("bbtest_reg"));

    buf2 = bboxReserve(_SL("bbtest_buf2"), 128, BBox_Private);
    if (!buf2 || (buf2 < buf + 65 && buf < buf2 + 129))
        TEST_FAILV(ret, 1, _SL("second buffer at ${ptr} is missing or overlaps the first at ${ptr}"), stvar(ptr, buf2), stvar(ptr, buf));
    else {
        memcpy(buf2, "second", 7);
        if (!bboxCheck("bbtest_buf2", "second", BBox_Buffer | BBox_Private) || !bboxCheck("bbtest_buf", "idle", BBox_Buffer))
            ret = 1;
    }
    if (ret)
        goto out;

    // buffers of the largest size and of odd sizes
    fill = bboxReserve(_SL("bbtest_max"), BLACKBOX_VALUE_MAX, 0);
    if (!fill)
        TEST_FAILV(ret, 1, _SL("bboxReserve(bbtest_max, ${int}) returned NULL"), stvar(int32, BLACKBOX_VALUE_MAX));
    else
        memset(fill, 'm', BLACKBOX_VALUE_MAX);
    if (!bboxReserve(_SL("bbtest_odd1"), 7, 0) || !bboxReserve(_SL("bbtest_odd2"), 8, 0) || !bboxReserve(_SL("bbtest_odd3"), 9, 0))
        TEST_FAILV(ret, 1, _SL("could not reserve the 7, 8 and 9 byte buffers"), stvNone);
    bboxSet(_SL("bbtest_reg"), _SL("ab"), 0);
    if (!bboxAligned() || !bboxCheck("bbtest_buf", "idle", BBox_Buffer) || !bboxCheck("bbtest_buf2", "second", BBox_Buffer | BBox_Private))
        ret = 1;
    bboxDelete(_SL("bbtest_reg"));
    bboxDelete(_SL("bbtest_max"));
    bboxDelete(_SL("bbtest_odd1"));
    bboxDelete(_SL("bbtest_odd2"));
    bboxDelete(_SL("bbtest_odd3"));
    if (ret)
        goto out;

    // fill the box up; reserving has to fail cleanly once there is no room
    for (nfill = 0; nfill < BBTEST_FILL; nfill++) {
        snprintf(name, sizeof(name), "bbtest_fill%d", nfill);
        fill = bboxReserve((strref)name, 1024, 0);
        if (!fill)
            break;
        memset(fill, 'f', 1024);
    }
    if (nfill < 32 || nfill == BBTEST_FILL)
        TEST_FAILV(ret, 1, _SL("reserved ${int} 1024 byte buffers in a ${int} byte box"), stvar(int32, nfill), stvar(int32, BLACKBOX_SIZE));
    if (bboxCount() != startcount + 2 + nfill)
        TEST_FAILV(ret, 1, _SL("after filling: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount + 2 + nfill));
    if (!bboxAligned() || !bboxCheck("bbtest_buf", "idle", BBox_Buffer) || !bboxCheck("bbtest_buf2", "second", BBox_Buffer | BBox_Private))
        ret = 1;

    // so does a regular entry that no longer fits
    string big = 0;
    memset(strBuffer(&big, 2048), 'v', 2048);
    strSetLen(&big, 2048);
    if (bboxSet(_SL("bbtest_reg"), big, 0) || bboxFind("bbtest_reg"))
        TEST_FAILV(ret, 1, _SL("bboxSet of 2048 bytes into a full box: found=${ptr} (want false, NULL)"), stvar(ptr, bboxFind("bbtest_reg")));
    strDestroy(&big);

    // deleting gives the space back
    for (int i = 0; i < nfill; i++) {
        snprintf(name, sizeof(name), "bbtest_fill%d", i);
        bboxDelete((strref)name);
    }
    int nrefill = 0;
    for (; nrefill < BBTEST_FILL; nrefill++) {
        snprintf(name, sizeof(name), "bbtest_fill%d", nrefill);
        if (!bboxReserve((strref)name, 1024, 0))
            break;
    }
    if (nrefill != nfill)
        TEST_FAILV(ret, 1, _SL("reserved ${int} buffers after deleting ${int} of the same size"), stvar(int32, nrefill), stvar(int32, nfill));
    nfill = max(nfill, nrefill);

out:
    for (int i = 0; i < nfill; i++) {
        snprintf(name, sizeof(name), "bbtest_fill%d", i);
        bboxDelete((strref)name);
    }
    bboxDelete(_SL("bbtest_reg"));
    bboxDelete(_SL("bbtest_reg2"));
    bboxDelete(_SL("bbtest_max"));
    bboxDelete(_SL("bbtest_odd1"));
    bboxDelete(_SL("bbtest_odd2"));
    bboxDelete(_SL("bbtest_odd3"));
    bboxDelete(_SL("bbtest_buf2"));
    bboxDelete(_SL("bbtest_buf"));

    if (!ret && bboxCount() != startcount)
        TEST_FAILV(ret, 1, _SL("at end of test: ${int} entries (want ${int})"), stvar(int32, bboxCount()), stvar(int32, startcount));

    return ret;
}

testfunc debugtest_funcs[] = {
    { "assert", test_assert },
    { "bbox", test_bbox },
    { "bboxbuf", test_bboxbuf },
    { 0, 0 }
};

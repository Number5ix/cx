#include <cx/string.h>
#include <cx/string/strtest.h>

#define TEST_FILE strtest
#define TEST_FUNCS strtest_funcs
#include "common.h"

static int test_join()
{
    string t1 = _S"Test 1";
    string t2 = _S"Test 2";
    string t3 = _S"Test 3";

    string o = NULL;

    strConcat(&o, t1, t2);
    if (!strEq(o, _S"Test 1Test 2"))
        return 1;

    strNConcat(&o, t1, t2, t3);
    if (!strEq(o, _S"Test 1Test 2Test 3"))
        return 1;

    string s1 = NULL, s2 = NULL, s3 = NULL;
    strDup(&s1, t1);
    strDup(&s2, t2);
    strDup(&s3, t3);
    strNConcatC(&o, &s3, &s2, &s1);
    if (!strEq(o, _S"Test 3Test 2Test 1"))
        return 1;
    if (s1 != NULL || s2 != NULL || s3 != NULL)
        return 1;

    strDestroy(&o);
    return 0;
}

static int test_append()
{
    string t1 = _S"Test 1";
    string pfx = _S"Pre-";
    string sfx = _S"-ish";

    strPrepend(pfx, &t1);
    if (!strEq(t1, _S"Pre-Test 1"))
        return 1;

    strAppend(&t1, sfx);
    if (!strEq(t1, _S"Pre-Test 1-ish"))
        return 1;

    strDestroy(&t1);
    return 0;
}

static int test_substr()
{
    string t1 = _S"Relatively long substring test data";
    string o = NULL;

    strSubStr(&o, t1, 0, 6);
    if (!strEq(o, _S"Relati"))
        return 1;

    strSubStr(&o, t1, 11, 15);
    if (!strEq(o, _S"long"))
        return 1;

    strSubStr(&o, t1, 16, 25);
    if (!strEq(o, _S"substring"))
        return 1;

    strSubStr(&o, t1, 16, -5);
    if (!strEq(o, _S"substring test"))
        return 1;

    strSubStr(&o, t1, -9, -5);
    if (!strEq(o, _S"test"))
        return 1;

    strSubStr(&o, t1, -4, strEnd);
    if (!strEq(o, _S"data"))
        return 1;

    string s1 = NULL;
    strDup(&s1, t1);
    strSubStrC(&o, &s1, 11, 15);
    if (!strEq(o, _S"long"))
        return 1;
    if (s1 != NULL)
        return 1;

    strSubStr(&o, t1, 0, -5);
    if (!strEq(o, _S"Relatively long substring test"))
        return 1;

    strSubStrI(&o, -11, strEnd);
    if (!strEq(o, _S"string test"))
        return 1;

    strSubStr(&o, t1, 0, -5);
    strSubStrI(&o, 0, 4);
    if (!strEq(o, _S"Rela"))
        return 1;

    // negative indices further back than the start of the string clamp to the start
    strSubStr(&o, t1, -100, 6);
    if (!strEq(o, _S"Relati"))
        return 1;

    strSubStr(&o, t1, -100, strEnd);
    if (!strEq(o, t1))
        return 1;

    strSubStr(&o, t1, 0, -100);
    if (!strEmpty(o))
        return 1;

    // strGetChar/strSetChar resolve indices the same way as each other: negative counts
    // back from the end and clamps at the start, strEnd is one past the last byte
    if (strGetChar(t1, 0) != 'R' || strGetChar(t1, -1) != 'a')
        return 2;
    if (strGetChar(t1, -100) != 'R' || strGetChar(t1, 100) != 0 || strGetChar(t1, strEnd) != 0)
        return 2;

    strDup(&o, t1);
    strSetChar(&o, -100, 'r');
    if (!strEq(o, _S"relatively long substring test data") || strLen(o) != 35)
        return 3;

    strSetChar(&o, strEnd, '!');
    if (!strEq(o, _S"relatively long substring test data!"))
        return 3;

    // only a positive index past the end grows the string, zero padded
    strSetChar(&o, 40, 'X');
    if (strLen(o) != 41 || strGetChar(o, 40) != 'X' || strGetChar(o, 37) != 0)
        return 3;

    strDestroy(&o);
    return 0;
}

static int test_long()
{
    string o1 = NULL, o2 = NULL;
    string t1 = _S"Relatively long substring test data";       // 35 characters
    string s1 = NULL;

    strReset(&o1, 100);           // sizehint here is intentionally a LIE ;)
    strReset(&o2, 1);

    for (int i = 0; i < 1000; ++i) {
        strAppend(&o1, t1);
    }

    if (strLen(o1) != 35000)
        return 1;

    for (int i = 0; i < 1000; ++i) {
        strAppend(&o2, o1);
    }

    if (strLen(o2) != 35000000)
        return 1;

    strSubStr(&s1, o2, 7392013, 7392026);
    if (!strEq(s1, _S"ng substring "))
        return 1;

    strDestroy(&o1);
    strDestroy(&o2);
    strDestroy(&s1);
    return 0;
}

static int test_compare()
{
    string t1 = _S"Test 1";
    string t2 = _S"Test 2";
    string t1l = _S"Test 1 Long";
    string t2l = _S"Test 2 Long";
    string lsfx = _S" Long";

    if (strCmp(t1, t2) >= 0)
        return 1;

    if (strCmp(t1, t1l) >= 0)
        return 1;

    if (strCmp(t2l, t2) <= 0)
        return 1;

    if (strCmp(t1, t2l) >= 0)
        return 1;

    if (strCmp(t2, t1l) <= 0)
        return 1;

    string t2l2 = NULL;
    strConcat(&t2l2, t2, lsfx);

    if (strCmp(t2l, t2l2) != 0)
        return 1;

    strDestroy(&t2l2);

    return 0;
}

// Runs every comparison in both argument orders. expect is the sign of cmp(a, b), so a
// swapped result must have the opposite sign -- this is what catches a sign error in the
// swapped-operand arm of strCmp. Returns 0 on success.
static int cmpBothWays(strref a, strref b, int expect)
{
    bool eq = (expect == 0);

    if (strEq(a, b) != eq || strEq(b, a) != eq)
        return 1;

    int fwd = strCmp(a, b), rev = strCmp(b, a);
    if (expect == 0 && (fwd != 0 || rev != 0))
        return 2;
    if (expect < 0 && !(fwd < 0 && rev > 0))
        return 3;
    if (expect > 0 && !(fwd > 0 && rev < 0))
        return 4;

    return 0;
}

// Case-insensitive counterpart of cmpBothWays
static int cmpiBothWays(strref a, strref b, int expect)
{
    bool eq = (expect == 0);

    if (strEqi(a, b) != eq || strEqi(b, a) != eq)
        return 1;

    int fwd = strCmpi(a, b), rev = strCmpi(b, a);
    if (expect == 0 && (fwd != 0 || rev != 0))
        return 2;
    if (expect < 0 && !(fwd < 0 && rev > 0))
        return 3;
    if (expect > 0 && !(fwd > 0 && rev < 0))
        return 4;

    return 0;
}

// Comparisons where at least one side has no embedded length field (STR_LEN0): plain C
// strings, _S"" literals, and everything _SL() produces on MSVC. These take a fast path
// that walks to the NUL instead of measuring the string first, so they need coverage
// against every other string class, and on both sides of STR_LEN0_SCAN_THRESH.
static int test_compare_len0()
{
    // --- LEN0 vs LEN0 ---
    if (cmpBothWays(_S"abc", _S"abc", 0))
        return 1;
    if (cmpBothWays(_S"abc", _S"abd", -1))
        return 2;
    if (cmpBothWays(_S"abc", _S"abcd", -1))   // strict prefix
        return 3;
    if (cmpiBothWays(_S"AbC", _S"aBc", 0))
        return 4;
    if (cmpiBothWays(_S"AbC", _S"aBcd", -1))
        return 5;

    // --- LEN0 vs plain C string (no cx header at all) ---
    if (cmpBothWays(_S"abc", (strref)"abc", 0))
        return 10;
    if (cmpBothWays((strref)"abc", _S"abd", -1))
        return 11;
    if (cmpBothWays((strref)"abc", (strref)"abcd", -1))
        return 12;
    if (cmpiBothWays((strref)"ABC", _S"abc", 0))
        return 13;

    // --- empty strings ---
    // _S"" is a valid LEN0 string; a bare "" fails STR_CHECK_VALID and normalizes to the
    // shared empty string, which has a length field, so both paths are covered
    if (cmpBothWays(_S"", _S"", 0))
        return 20;
    if (cmpBothWays(_S"", (strref)"", 0))
        return 21;
    if (cmpBothWays(_S"", NULL, 0))
        return 22;
    if (cmpBothWays(_S"", _S"abc", -1))
        return 23;
    if (cmpBothWays(NULL, _S"abc", -1))
        return 24;

    // --- cstrEq / cstrCmp, which back the both-LEN0 case ---
    if (!cstrEq(NULL, NULL) || cstrCmp(NULL, NULL) != 0)
        return 30;
    if (cstrEq(NULL, "a") || cstrEq("a", NULL))
        return 31;
    if (cstrCmp(NULL, "a") >= 0 || cstrCmp("a", NULL) <= 0)
        return 32;
    if (!cstrEq("abc", "abc") || cstrCmp("abc", "abc") != 0)
        return 33;
    if (cstrEq("abc", "abd") || cstrCmp("abc", "abd") >= 0)
        return 34;
    if (cstrEq("abc", "abcd") || cstrCmp("abcd", "abc") <= 0)
        return 35;

    int ret = 0;

    // --- LEN0 vs heap string; strCopy forces a real allocation with a length field ---
    string heap = 0;
    strCopy(&heap, _S"abc");

    if (cmpBothWays(_S"abc", heap, 0))
        ret = 40;
    else if (cmpBothWays(_S"abd", heap, 1))
        ret = 41;
    else if (cmpBothWays(_S"ab", heap, -1))
        ret = 42;
    else if (cmpBothWays(_S"abcd", heap, 1))
        ret = 43;
    else if (cmpiBothWays(_S"ABC", heap, 0))
        ret = 44;

    strDestroy(&heap);
    if (ret)
        return ret;

    // --- LEN0 vs rope: the multi-run case ---
    // A rope only forms above ROPE_JOIN_THRESH (128) and the walk is only taken below
    // STR_LEN0_SCAN_THRESH (1024), so this has to land between the two to exercise
    // _strEqLen0's seek-to-next-run branch at all.
    strref lit64  = _S"Thirty-two character test string"
                     "gnirts tset retcarahc owt-ytrihT";
    strref lit128 = _S"Thirty-two character test string"
                     "gnirts tset retcarahc owt-ytrihT"
                     "Thirty-two character test string"
                     "gnirts tset retcarahc owt-ytrihT";

    string rope = 0;
    strAppend(&rope, lit64);
    strAppend(&rope, lit64);

    if (strTestRopeDepth(rope) < 1)
        ret = 50;   // not actually a rope; the run-crossing path would go untested
    else if (cmpBothWays(lit128, rope, 0))
        ret = 51;
    else if (cmpBothWays(lit64, rope, -1))   // strict prefix: c runs out mid-rope
        ret = 52;
    else if (cmpBothWays(_S"Thirty-two character test string"
                          "gnirts tset retcarahc owt-ytrihT"
                          "Thirty-two character test string"
                          "gnirts tset retcarahc owt-ytrihTX",
                         rope, 1))   // extension: the rope runs out first
        ret = 53;
    else if (cmpiBothWays(lit128, rope, 0))
        ret = 54;

    strDestroy(&rope);
    if (ret)
        return ret;

    // --- both sides of STR_LEN0_SCAN_THRESH ---
    // Above the threshold the fast path is skipped in favour of measure-then-memcmp.
    // Build identical content just under and just over it and require the same answers;
    // vary where the difference falls, since a late difference is what the threshold
    // exists to protect.
    static const uint32 sizes[] = { 512, 2000 };
    for (int i = 0; i < 2 && !ret; i++) {
        uint32 n = sizes[i];

        char* same  = xaAlloc(n + 1, XA_Zero);
        char* early = xaAlloc(n + 1, XA_Zero);
        char* late  = xaAlloc(n + 1, XA_Zero);
        char* shrt  = xaAlloc(n, XA_Zero);
        memset(same, 'a', n);
        memset(early, 'a', n);
        memset(late, 'a', n);
        memset(shrt, 'a', n - 1);
        early[0]    = 'b';
        late[n - 1] = 'b';

        string cxs = 0;
        strCopy(&cxs, (strref)same);   // heap copy, so it carries a length field

        if (cmpBothWays((strref)same, cxs, 0))
            ret = 60;
        else if (cmpBothWays((strref)early, cxs, 1))
            ret = 61;
        else if (cmpBothWays((strref)late, cxs, 1))
            ret = 62;
        else if (cmpBothWays((strref)shrt, cxs, -1))
            ret = 63;

        strDestroy(&cxs);
        xaFree(same);
        xaFree(early);
        xaFree(late);
        xaFree(shrt);
    }

    return ret;
}

static int test_find()
{
    string s = _S"The Quick Brown Fox Jumps Over The Lazy Dog";

    // multi-character needle, forward
    if (strFind(s, 0, _S"Quick") != 4 || strFind(s, 0, _S"quick") != -1)
        return 1;
    if (strFindi(s, 0, _S"quick") != 4 || strFindi(s, 0, _S"QUICK") != 4)
        return 2;
    if (strFindi(s, 0, _S"THE") != 0)
        return 3;

    // multi-character needle, reverse. Note strEnd rather than 0 -- for the reverse
    // searches 0 is an ordinary offset that searches an empty range.
    if (strFindR(s, strEnd, _S"The") != 31 || strFindR(s, strEnd, _S"the") != -1)
        return 4;
    if (strFindRi(s, strEnd, _S"the") != 31)
        return 5;

    // start/end positions apply the same way as the case-sensitive versions
    if (strFindi(s, 1, _S"the") != 31 || strFindi(s, -12, _S"the") != 31)
        return 6;
    if (strFindRi(s, 31, _S"the") != 0 || strFindRi(s, 0, _S"the") != -1)
        return 7;

    // single-character needle takes the _strFindChar path
    if (strFind(s, 0, _S"q") != -1 || strFindi(s, 0, _S"q") != 4)
        return 8;
    if (strFindR(s, strEnd, _S"O") != 26 || strFindRi(s, strEnd, _S"O") != 41)
        return 9;

    // a start further back than the beginning of the string clamps to the beginning,
    // and an end that does the same leaves an empty range to search
    if (strFind(s, -100, _S"Quick") != 4 || strFindi(s, -100, _S"quick") != 4)
        return 14;
    if (strFindChar(s, -100, 'Q') != 4 || strFindAny(s, -100, _S"Qx") != 4)
        return 15;
    if (strFindR(s, -100, _S"The") != -1 || strFindCharR(s, -100, 'T') != -1)
        return 16;

    // degenerate inputs
    if (strFindi(s, 0, _S"zebra") != -1 || strFindRi(s, strEnd, _S"zebra") != -1)
        return 10;
    if (strFindi(s, 0, _S"") != -1 || strFindRi(s, strEnd, _S"") != -1)
        return 11;
    if (strFindi(NULL, 0, _S"a") != -1 || strFindRi(NULL, strEnd, _S"a") != -1)
        return 12;

    // C strings promote to strref, which is the main reason these exist
    if (strFindi((strref) "en_US.UTF-8", 0, _S"utf-8") != 6)
        return 13;

    // a match that straddles a rope segment boundary exercises the multi-run compare
    string flat = NULL, rope = NULL;
    int ret     = 0;
    strNConcat(&flat, _S"Thirty-two character test string", _S"gnirts tset retcarahc owt-ytrihT");
    strAppend(&rope, flat);
    strAppend(&rope, flat);

    if (strTestRopeDepth(rope) < 1)
        ret = 20;   // not actually a rope; the checks below would prove nothing
    else if (strLen(rope) != 128)
        ret = 21;
    // rope[62..65] is "hTTh", spanning the segment boundary at 64
    else if (strFind(rope, 0, _S"hTTh") != 62 || strFind(rope, 0, _S"HTTH") != -1)
        ret = 22;
    else if (strFindi(rope, 0, _S"HTTH") != 62)
        ret = 23;
    else if (strFindRi(rope, strEnd, _S"HTTH") != 62)
        ret = 24;
    // and one that does not straddle, to be sure the boundary is not doing the work
    else if (strFindi(rope, 0, _S"THIRTY-TWO") != 0 ||
             strFindRi(rope, strEnd, _S"THIRTY-TWO") != 64)
        ret = 25;

    strDestroy(&flat);
    strDestroy(&rope);
    return ret;
}

static int test_rope()
{
    string t1 = _S"Thirty-two character test string";
    string t2 = _S"gnirts tset retcarahc owt-ytrihT";
    string s1 = NULL, s2 = NULL;
    string o1 = NULL, o2 = NULL, o3 = NULL;
    int i;

    strNConcat(&s1, t1, t2);
    strAppend(&o1, s1);
    strAppend(&o1, s1);

    if (strTestRopeDepth(o1) != 1)
        return 1;

    if (!strTestRopeNode(&o2, o1, true) || strLen(o2) != 64)
        return 1;
    if (!strTestRopeNode(&o2, o1, false) || strLen(o2) != 64)
        return 1;

    strDestroy(&o1);
    for (i = 0; i < 32; i++) {
        strAppend(&o1, s1);
    }

    if (strTestRefCount(s1) < 32)
        return 1;

    if (strTestRopeDepth(o1) > 6)
        return 1;

    if (!strTestRopeNode(&o2, o1, true) || strLen(o2) != 1024)
        return 1;
    if (!strTestRopeNode(&o2, o1, false) || strLen(o2) != 1024)
        return 1;

    strDestroy(&o1);
    // pathalogically build up a huge rope
    for (i = 0; i < 10000; i++) {
        strAppend(&o1, _S"b");
        strPrepend(_S"a", &o1);
    }

    // make sure it's not horribly unbalanced even in worst case
    if (strTestRopeDepth(o1) > 9)
        return 1;

    if (!strTestRopeNode(&o2, o1, true) || strLen(o2) < 8000)
        return 1;
    if (!strTestRopeNode(&o2, o1, false) || strLen(o2) < 8000)
        return 1;

    strDestroy(&o1);
    for (i = 0; i < 1024; i++) {
        strAppend(&o1, s1);
    }

    strSubStr(&s2, o1, 10000, 15000);
    strSubStr(&o2, s2, 0, 10);
    if (!strEq(o2, _S"cter test "))
        return 1;

    strNConcat(&o2, s1, s2, s2, s1);
    if (strLen(o2) != 10128)
        return 1;

    strSubStr(&o3, o2, 60, 70);
    if (!strEq(o3, _S"rihTcter t"))
        return 1;
    strSubStr(&o3, o2, 5059, 5069);
    if (!strEq(o3, _S"r tescter "))
        return 1;

    // manually fill in a buffer to avoid creating a rope.
    // need a single string to avoid rope segment optimizations or it
    // gets a lot harder to check the refcount.
    strReset(&s2, 1000);
    uint8 *buf = strBuffer(&s2, 1000);
    for (i = 0; i < 1000; i += 25) {
        memcpy(&buf[i], "1234567890123456789012345", 25);
    }
    strSubStr(&o1, s2, 250, 750);

    if (strTestRefCount(s2) < 2)
        return 1;

    strNConcat(&o2, o1, _S"Test 123", o1);

    if (strTestRefCount(s2) < 4)
        return 1;
    if (strLen(o2) != 1008)
        return 1;

    strDestroy(&o1);
    strDestroy(&o2);
    strDestroy(&o3);

    if (strTestRefCount(s2) != 1)
        return 1;

    strDestroy(&s1);
    strDestroy(&s2);

    return 0;
}

static float32 _float32_inf(bool negative)
{
    union {
        float32 f;
        uint32 i;
    } val;
    val.i = 0x7F800000U;        // expmask_32
    if (negative)
        val.i |= 0x80000000U;   // signmask_32
    return val.f;
}

static float32 _float32_nan()
{
    union {
        float32 f;
        uint32 i;
    } val;
    val.i = 0x7F800000U | 1;   // expmask_32 with fraction bit
    return val.f;
}

// Builds a 128 byte rope out of two 64 byte halves, so the multi-run striter paths are
// exercised. The segment boundary lands at offset 64; bytes 62..65 are "hTTh".
static void makeRope(string* o)
{
    string flat = 0;
    strNConcat(&flat, _S"Thirty-two character test string", _S"gnirts tset retcarahc owt-ytrihT");
    strDestroy(o);
    strAppend(o, flat);
    strAppend(o, flat);
    strDestroy(&flat);
}

static int test_bytes()
{
    static const uint8 raw[6] = { 'a', 0, 'b', 0xff, 0, 'c' };
    string s = 0, o = 0, alias = 0;
    int ret  = 0;

    // --- strFromBytes: binary safe, embedded NULs survive ---
    if (!strFromBytes(&s, raw, sizeof(raw)) || strLen(s) != 6)
        return 1;
    if (strGetChar(s, 0) != 'a' || strGetChar(s, 1) != 0 || strGetChar(s, 3) != 0xff ||
        strGetChar(s, 4) != 0 || strGetChar(s, 5) != 'c')
        return 2;

    // a NULL buffer or a zero size both produce an empty string
    if (!strFromBytes(&s, NULL, 10) || !strEmpty(s))
        return 3;
    if (!strFromBytes(&s, raw, 0) || !strEmpty(s))
        return 4;

    // --- strAppendBytes ---
    strDup(&s, _S"len=");
    if (!strAppendBytes(&s, raw, sizeof(raw)) || strLen(s) != 10)
        return 10;
    if (strGetChar(s, 3) != '=' || strGetChar(s, 5) != 0 || strGetChar(s, 7) != 0xff)
        return 11;
    if (!strAppendBytes(&s, NULL, 4) || strLen(s) != 10)
        return 12;
    if (!strAppendBytes(&s, raw, 0) || strLen(s) != 10)
        return 13;

    // appending to a NULL handle creates the string
    strDestroy(&s);
    if (!strAppendBytes(&s, raw, 3) || strLen(s) != 3 || strGetChar(s, 1) != 0)
        return 15;

    // --- strAppendChar ---
    strDup(&s, _S"item");
    strAppendChar(&s, ':');
    strAppendChar(&s, ' ');
    if (!strEq(s, _S"item: "))
        return 20;

    strDestroy(&s);
    strAppendChar(&s, 'x');   // NULL handle creates the string
    if (!strEq(s, _S"x"))
        return 21;

    // a byte outside ASCII is still appended, it just can't be claimed as UTF-8
    strDup(&s, _S"a");
    strAppendChar(&s, 0xe9);
    if (strLen(s) != 2 || strGetChar(s, 1) != 0xe9 || strValidUTF8(s))
        return 22;

    // C string promotion
    strDup(&s, (strref) "cstr");
    strAppendChar(&s, '!');
    if (!strEq(s, _S"cstr!"))
        return 23;

    // --- strRepeat ---
    if (!strRepeat(&o, _S"-=", 5) || !strEq(o, _S"-=-=-=-=-="))
        return 30;
    if (!strRepeat(&o, _S"ab", 0) || !strEmpty(o))
        return 31;
    if (!strRepeat(&o, _S"ab", 1) || !strEq(o, _S"ab"))
        return 32;
    if (!strRepeat(&o, NULL, 5) || !strEmpty(o))
        return 33;
    if (!strRepeat(&o, (strref) "xy", 3) || !strEq(o, _S"xyxyxy"))
        return 34;

    // aliasing: o == s has to give the same answer as a distinct output
    strDup(&alias, _S"ab");
    strRepeat(&o, alias, 4);
    strRepeat(&alias, alias, 4);
    if (!strEq(alias, o) || !strEq(alias, _S"abababab"))
        return 36;

    // a shared input must not be clobbered
    strCopy(&s, _S"zz");   // heap copy, so it is actually refcounted
    strDup(&alias, s);
    if (strTestRefCount(s) != 2)
        return 37;
    strRepeat(&o, s, 3);
    if (!strEq(o, _S"zzzzzz") || !strEq(s, _S"zz") || strTestRefCount(s) != 2)
        return 38;

    // --- strFillChar ---
    if (!strFillChar(&o, ' ', 4) || !strEq(o, _S"    "))
        return 40;
    if (!strFillChar(&o, 'x', 0) || !strEmpty(o))
        return 41;
    // reusing a handle that already holds something destroys the old value
    strDup(&o, _S"previous contents");
    if (!strFillChar(&o, '.', 3) || !strEq(o, _S"..."))
        return 43;

    // --- rope paths ---
    string rope = 0;
    makeRope(&rope);
    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 128) {
        ret = 50;   // not actually a rope; the checks below would prove nothing
        goto out;
    }

    // repeating a rope reads across the segment boundary
    strRepeat(&o, rope, 2);
    if (strLen(o) != 256) {
        ret = 51;
        goto out;
    }
    strSubStr(&s, o, 126, 130);
    if (!strEq(s, _S"hTTh")) {
        ret = 52;
        goto out;
    }

    // appending bytes to a rope flattens it first
    strDup(&s, rope);
    if (!strAppendBytes(&s, (const uint8*) "XY", 2) || strLen(s) != 130) {
        ret = 53;
        goto out;
    }
    if (strGetChar(s, 127) != 'T' || strGetChar(s, 128) != 'X' || strGetChar(s, 129) != 'Y')
        ret = 54;

out:
    strDestroy(&rope);
    strDestroy(&alias);
    strDestroy(&o);
    strDestroy(&s);
    return ret;
}

static int test_findchar()
{
    // index:            0123456789.123456789
    string s   = _S"The Quick Brown Fox";
    string all = _S"aaa";
    int ret    = 0;

    // --- strFindChar / strFindCharR ---
    if (strFindChar(s, 0, 'Q') != 4 || strFindChar(s, 0, 'q') != -1)
        return 1;
    if (strFindChari(s, 0, 'q') != 4 || strFindChari(s, 0, 'Q') != 4)
        return 2;
    if (strFindChar(s, 5, 'Q') != -1 || strFindChar(s, -3, 'o') != 17)
        return 3;

    // note strEnd rather than 0 -- for the reverse searches 0 searches an empty range
    if (strFindCharR(s, strEnd, 'o') != 17 || strFindCharR(s, 17, 'o') != 12)
        return 4;
    if (strFindCharR(s, strEnd, 'O') != -1 || strFindCharRi(s, strEnd, 'O') != 17)
        return 5;
    if (strFindCharR(s, 0, 'T') != -1 || strFindCharR(s, -18, 'T') != 0)
        return 6;

    // NULL and C string promotion
    if (strFindChar(NULL, 0, 'a') != -1 || strFindCharR(NULL, strEnd, 'a') != -1)
        return 7;
    if (strFindChari(NULL, 0, 'a') != -1 || strFindCharRi(NULL, strEnd, 'a') != -1)
        return 8;
    if (strFindChar((strref) "hello", 0, 'l') != 2 ||
        strFindCharR((strref) "hello", strEnd, 'l') != 3)
        return 9;

    // --- strFindAny / strFindAnyR ---
    if (strFindAny(s, 0, _S"wx") != 13 || strFindAnyR(s, strEnd, _S"wx") != 18)
        return 20;
    if (strFindAny(s, 0, _S"q") != -1 || strFindAnyi(s, 0, _S"q") != 4)
        return 21;
    if (strFindAnyR(s, strEnd, _S"q") != -1 || strFindAnyRi(s, strEnd, _S"q") != 4)
        return 22;
    if (strFindAnyR(s, strEnd, _S"aeiou") != 17)
        return 23;

    // an empty or NULL set never matches
    if (strFindAny(s, 0, _S"") != -1 || strFindAny(s, 0, NULL) != -1)
        return 24;
    if (strFindAnyR(s, strEnd, _S"") != -1 || strFindAnyR(s, strEnd, NULL) != -1)
        return 25;
    if (strFindAnyi(s, 0, NULL) != -1 || strFindAnyRi(s, strEnd, NULL) != -1)
        return 26;

    // --- strFindNotAny / strFindNotAnyR ---
    if (strFindNotAny(s, 0, _S"The ") != 4 || strFindNotAnyR(s, strEnd, _S"xo") != 16)
        return 30;
    if (strFindNotAny(s, 0, _S"the ") != 0 || strFindNotAnyi(s, 0, _S"the ") != 4)
        return 31;
    if (strFindNotAnyR(s, strEnd, _S"XO") != 18 || strFindNotAnyRi(s, strEnd, _S"XO") != 16)
        return 32;

    // an empty or NULL set excludes nothing, so the search position itself matches
    if (strFindNotAny(s, 0, _S"") != 0 || strFindNotAny(s, 3, NULL) != 3)
        return 33;
    if (strFindNotAnyR(s, strEnd, NULL) != 18)
        return 34;

    // every byte in the set, so there is nothing to find
    if (strFindNotAny(all, 0, _S"a") != -1 || strFindNotAnyR(all, strEnd, _S"a") != -1)
        return 35;
    if (strFindNotAnyi(all, 0, _S"A") != -1 || strFindNotAnyRi(all, strEnd, _S"A") != -1)
        return 36;

    // NULL subject and C string promotion
    if (strFindAny(NULL, 0, _S"a") != -1 || strFindNotAny(NULL, 0, _S"a") != -1)
        return 37;
    if (strFindAnyR(NULL, strEnd, _S"a") != -1 || strFindNotAnyR(NULL, strEnd, _S"a") != -1)
        return 38;
    if (strFindAny((strref) "a,b;c", 0, _S",;") != 1 ||
        strFindAnyR((strref) "a,b;c", strEnd, _S",;") != 3)
        return 39;

    // --- rope paths: matches in the second run, and either side of the boundary ---
    string rope = 0;
    makeRope(&rope);
    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 128) {
        ret = 50;   // not actually a rope; the checks below would prove nothing
        goto out;
    }

    if (strFindChar(rope, 1, 'T') != 63 || strFindChar(rope, 65, 'T') != 127)
        ret = 51;
    else if (strFindCharR(rope, strEnd, 'T') != 127 || strFindCharR(rope, 127, 'T') != 64)
        ret = 52;
    else if (strFindChari(rope, 65, 't') != 68 || strFindCharRi(rope, strEnd, 't') != 127)
        ret = 53;
    else if (strFindAny(rope, 65, _S"T") != 127 || strFindAnyR(rope, strEnd, _S"T") != 127)
        ret = 54;
    // rope[126] is 'h' and rope[127] is 'T', straddling nothing but the second segment
    else if (strFindNotAnyR(rope, strEnd, _S"T") != 126)
        ret = 55;
    else if (strFindNotAny(rope, 64, _S"Thirty-") != 72)   // "Thirty-t" is all in the set
        ret = 56;

out:
    strDestroy(&rope);
    return ret;
}

static int test_trim()
{
    string s = 0, o = 0, alias = 0;
    int ret  = 0;

    // --- default whitespace set ---
    if (!strTrim(&o, _S"  hello  ", NULL) || !strEq(o, _S"hello"))
        return 1;
    if (!strLTrim(&o, _S"  hello  ", NULL) || !strEq(o, _S"hello  "))
        return 2;
    if (!strRTrim(&o, _S"  hello  ", NULL) || !strEq(o, _S"  hello"))
        return 3;
    if (!strTrim(&o, _S"\t\r\n hi \v\f", NULL) || !strEq(o, _S"hi"))
        return 4;

    // leading only, then trailing only
    if (!strTrim(&o, _S"  hello", NULL) || !strEq(o, _S"hello"))
        return 5;
    if (!strTrim(&o, _S"hello  ", NULL) || !strEq(o, _S"hello"))
        return 6;

    // --- explicit sets ---
    if (!strTrim(&o, _S"[hello]", _S"[]") || !strEq(o, _S"hello"))
        return 10;
    if (!strLTrim(&o, _S"xxhixx", _S"x") || !strEq(o, _S"hixx"))
        return 11;
    if (!strRTrim(&o, _S"xxhixx", _S"x") || !strEq(o, _S"xxhi"))
        return 12;
    // an empty set trims nothing
    if (!strTrim(&o, _S"  hi  ", _S"") || !strEq(o, _S"  hi  "))
        return 13;

    // --- degenerate inputs ---
    // every byte is in the set
    if (!strTrim(&o, _S"     ", NULL) || !strEmpty(o))
        return 20;
    if (!strLTrim(&o, _S"     ", NULL) || !strEmpty(o))
        return 21;
    if (!strRTrim(&o, _S"     ", NULL) || !strEmpty(o))
        return 22;
    // nothing to trim
    if (!strTrim(&o, _S"hello", NULL) || !strEq(o, _S"hello"))
        return 23;
    // NULL and empty input, and a NULL output handle
    if (!strTrim(&o, NULL, NULL) || !strEmpty(o))
        return 24;
    if (!strTrim(&o, _S"", NULL) || !strEmpty(o))
        return 25;
    // C string promotion
    if (!strTrim(&o, (strref) "  cstr  ", NULL) || !strEq(o, _S"cstr"))
        return 27;

    // --- aliasing ---
    strDup(&alias, _S"  hello  ");
    strTrim(&o, alias, NULL);
    strTrim(&alias, alias, NULL);
    if (!strEq(alias, o) || !strEq(alias, _S"hello"))
        return 30;

    // trimming a shared string in place must not disturb the other reference
    strCopy(&s, _S"  pad  ");
    strDup(&alias, s);
    if (strTestRefCount(s) != 2)
        return 31;
    strTrim(&o, s, NULL);
    if (!strEq(o, _S"pad") || !strEq(s, _S"  pad  "))
        return 32;
    strTrim(&alias, alias, NULL);
    if (!strEq(alias, _S"pad") || !strEq(s, _S"  pad  "))
        return 33;

    // --- rope path: a result over ROPE_SUBSTR_THRESH is a reference, not a copy ---
    string rope = 0, padded = 0;
    makeRope(&rope);
    if (strTestRopeDepth(rope) < 1) {
        ret = 40;
        goto out;
    }

    strNConcat(&padded, _S"   ", rope, _S"   ");
    if (strLen(padded) != 134) {
        ret = 41;
        goto out;
    }
    if (!strTrim(&o, padded, NULL) || strLen(o) != 128) {
        ret = 42;
        goto out;
    }
    // the trimmed result still has to read back correctly across the segment boundary
    strSubStr(&s, o, 62, 66);
    if (!strEq(s, _S"hTTh"))
        ret = 43;
    else if (strGetChar(o, 0) != 'T' || strGetChar(o, 127) != 'T')
        ret = 44;

out:
    strDestroy(&padded);
    strDestroy(&rope);
    strDestroy(&alias);
    strDestroy(&o);
    strDestroy(&s);
    return ret;
}

static int test_replace()
{
    string s = 0, o = 0, alias = 0;
    int ret  = 0;

    // --- strReplaceChar ---
    if (!strReplaceChar(&o, _S"a\\b\\c", '\\', '/') || !strEq(o, _S"a/b/c"))
        return 1;
    if (!strReplaceChar(&o, _S"abc", 'z', '-') || !strEq(o, _S"abc"))
        return 2;
    if (!strReplaceChar(&o, NULL, 'a', 'b') || !strEmpty(o))
        return 3;
    if (!strReplaceChar(&o, (strref) "a.b.c", '.', '_') || !strEq(o, _S"a_b_c"))
        return 5;

    // case insensitive matches either case, but writes exactly what it was given
    if (!strReplaceChari(&o, _S"aAbB", 'a', '-') || !strEq(o, _S"--bB"))
        return 6;
    if (!strReplaceChar(&o, _S"aAbB", 'a', '-') || !strEq(o, _S"-AbB"))
        return 7;

    // aliasing, and a shared input that must not be clobbered
    strDup(&alias, _S"a-b-c");
    strReplaceChar(&o, alias, '-', '+');
    strReplaceChar(&alias, alias, '-', '+');
    if (!strEq(alias, o) || !strEq(alias, _S"a+b+c"))
        return 8;

    strCopy(&s, _S"x-y");
    strDup(&alias, s);
    if (strTestRefCount(s) != 2)
        return 9;
    strReplaceChar(&o, s, '-', '=');
    if (!strEq(o, _S"x=y") || !strEq(s, _S"x-y"))
        return 10;
    // in place on a shared string has to copy rather than write through
    strReplaceChar(&alias, alias, '-', '=');
    if (!strEq(alias, _S"x=y") || !strEq(s, _S"x-y"))
        return 11;

    // --- strReplace: grow, shrink, and equal length ---
    if (!strReplace(&o, _S"a,b,c", _S",", _S" - ", 0) || !strEq(o, _S"a - b - c"))
        return 20;
    if (!strReplace(&o, _S"aXXb", _S"XX", _S"-", 0) || !strEq(o, _S"a-b"))
        return 21;
    if (!strReplace(&o, _S"abc", _S"b", _S"Z", 0) || !strEq(o, _S"aZc"))
        return 22;
    // an empty replacement deletes the match
    if (!strReplace(&o, _S"a,b,c", _S",", NULL, 0) || !strEq(o, _S"abc"))
        return 23;
    // matches at the very start and very end
    if (!strReplace(&o, _S",a,", _S",", _S"|", 0) || !strEq(o, _S"|a|"))
        return 24;

    // --- max limit ---
    if (!strReplace(&o, _S"a,b,c", _S",", _S" - ", 1) || !strEq(o, _S"a - b,c"))
        return 30;
    if (!strReplace(&o, _S"a,b,c", _S",", _S" - ", 2) || !strEq(o, _S"a - b - c"))
        return 31;
    if (!strReplace(&o, _S"a,b,c", _S",", _S"|", 99) || !strEq(o, _S"a|b|c"))
        return 32;
    if (!strReplace(&o, _S"a,b,c", _S",", _S"|", -1) || !strEq(o, _S"a|b|c"))
        return 33;

    // --- degenerate inputs ---
    if (!strReplace(&o, _S"abc", NULL, _S"x", 0) || !strEq(o, _S"abc"))
        return 40;
    if (!strReplace(&o, _S"abc", _S"", _S"x", 0) || !strEq(o, _S"abc"))
        return 41;
    if (!strReplace(&o, _S"abc", _S"zz", _S"x", 0) || !strEq(o, _S"abc"))
        return 42;
    if (!strReplace(&o, NULL, _S"a", _S"x", 0) || !strEmpty(o))
        return 43;
    // the whole string is the match
    if (!strReplace(&o, _S"abc", _S"abc", NULL, 0) || !strEmpty(o))
        return 45;

    // --- case insensitive, and aliasing ---
    if (!strReplacei(&o, _S"Foo foo FOO", _S"foo", _S"bar", 0) || !strEq(o, _S"bar bar bar"))
        return 50;
    if (!strReplace(&o, _S"Foo foo FOO", _S"foo", _S"bar", 0) || !strEq(o, _S"Foo bar FOO"))
        return 51;
    if (!strReplacei(&o, (strref) "HTTP://x", _S"http://", _S"https://", 0) ||
        !strEq(o, _S"https://x"))
        return 52;

    strDup(&alias, _S"one two one");
    strReplace(&o, alias, _S"one", _S"1", 0);
    strReplace(&alias, alias, _S"one", _S"1", 0);
    if (!strEq(alias, o) || !strEq(alias, _S"1 two 1"))
        return 53;

    strCopy(&s, _S"keep me");
    strDup(&alias, s);
    strReplace(&o, s, _S"keep", _S"drop", 0);
    if (!strEq(o, _S"drop me") || !strEq(s, _S"keep me") || strTestRefCount(s) != 2)
        return 54;

    // --- strInsert ---
    if (!strInsert(&o, _S"hello world", 5, _S",") || !strEq(o, _S"hello, world"))
        return 60;
    if (!strInsert(&o, _S"world", 0, _S"hello ") || !strEq(o, _S"hello world"))
        return 61;
    if (!strInsert(&o, _S"hello", strEnd, _S"!") || !strEq(o, _S"hello!"))
        return 62;
    if (!strInsert(&o, _S"hello", -1, _S"-") || !strEq(o, _S"hell-o"))
        return 63;
    // offsets past the end clamp to the end
    if (!strInsert(&o, _S"ab", 99, _S"!") || !strEq(o, _S"ab!"))
        return 64;
    if (!strInsert(&o, _S"ab", -99, _S"!") || !strEq(o, _S"!ab"))
        return 65;
    // nothing to insert, and nothing to insert into
    if (!strInsert(&o, _S"ab", 1, NULL) || !strEq(o, _S"ab"))
        return 66;
    if (!strInsert(&o, NULL, 0, _S"x") || !strEq(o, _S"x"))
        return 67;
    if (!strInsert(&o, (strref) "ac", 1, (strref) "b") || !strEq(o, _S"abc"))
        return 69;

    strDup(&alias, _S"ac");
    strInsert(&o, alias, 1, _S"b");
    strInsert(&alias, alias, 1, _S"b");
    if (!strEq(alias, o) || !strEq(alias, _S"abc"))
        return 70;

    // --- strErase ---
    if (!strErase(&o, _S"hello, world", 5, 7) || !strEq(o, _S"helloworld"))
        return 80;
    if (!strErase(&o, _S"hello, world", -6, strEnd) || !strEq(o, _S"hello,"))
        return 81;
    if (!strErase(&o, _S"hello", 0, strEnd) || !strEmpty(o))
        return 82;
    // an empty or inverted range leaves the string alone
    if (!strErase(&o, _S"hello", 2, 2) || !strEq(o, _S"hello"))
        return 83;
    if (!strErase(&o, _S"hello", 4, 1) || !strEq(o, _S"hello"))
        return 84;
    if (!strErase(&o, NULL, 0, 2) || !strEmpty(o))
        return 85;
    if (!strErase(&o, (strref) "abXYc", 2, 4) || !strEq(o, _S"abc"))
        return 87;

    strDup(&alias, _S"abXYc");
    strErase(&o, alias, 2, 4);
    strErase(&alias, alias, 2, 4);
    if (!strEq(alias, o) || !strEq(alias, _S"abc"))
        return 88;

    // --- rope paths ---
    string rope = 0;
    makeRope(&rope);
    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 128) {
        ret = 90;   // not actually a rope; the checks below would prove nothing
        goto out;
    }

    // a match straddling the segment boundary at 64
    if (!strReplace(&o, rope, _S"hTTh", _S"[]", 0) || strLen(o) != 126) {
        ret = 91;
        goto out;
    }
    strSubStr(&s, o, 60, 66);
    if (!strEq(s, _S"ri[]ir")) {
        ret = 92;
        goto out;
    }

    // erasing across the boundary should leave the surviving spans intact
    if (!strErase(&o, rope, 32, 96) || strLen(o) != 64) {
        ret = 93;
        goto out;
    }
    if (strGetChar(o, 31) != 'g' || strGetChar(o, 32) != 'g' || strGetChar(o, 63) != 'T') {
        ret = 94;
        goto out;
    }

    // replacing a byte in a rope has to flatten it first
    if (!strReplaceChar(&o, rope, 'T', '_') || strLen(o) != 128)
        ret = 95;
    else if (strGetChar(o, 0) != '_' || strGetChar(o, 63) != '_' || strGetChar(o, 127) != '_')
        ret = 96;
    else if (!strInsert(&o, rope, 64, _S"|") || strLen(o) != 129)
        ret = 97;
    else if (strGetChar(o, 63) != 'T' || strGetChar(o, 64) != '|' || strGetChar(o, 65) != 'T')
        ret = 98;

out:
    strDestroy(&rope);
    strDestroy(&alias);
    strDestroy(&o);
    strDestroy(&s);
    return ret;
}

static int test_splitvar()
{
    sa_string parts = { 0 };
    string seg      = 0;
    int32 pos       = 0;
    int ret         = 0;

    // --- strSplitAny ---
    if (strSplitAny(&parts, _S"a,b;c", _S",;", false) != 3)
        return 1;
    if (!strEq(parts.a[0], _S"a") || !strEq(parts.a[1], _S"b") || !strEq(parts.a[2], _S"c"))
        return 2;
    // adjacent delimiters produce empty segments only when asked for
    if (strSplitAny(&parts, _S"a,;b", _S",;", false) != 2)
        return 3;
    if (strSplitAny(&parts, _S"a,;b", _S",;", true) != 3 || !strEmpty(parts.a[1]))
        return 4;
    // an empty or NULL set never matches, so nothing is split
    if (strSplitAny(&parts, _S"a,b", _S"", true) != 1 || !strEq(parts.a[0], _S"a,b"))
        return 5;
    if (strSplitAny(&parts, _S"a,b", NULL, true) != 1 || !strEq(parts.a[0], _S"a,b"))
        return 6;
    if (strSplitAny(&parts, NULL, _S",", false) != 0)
        return 7;
    if (strSplitAny(&parts, (strref) "a,b;c", _S",;", false) != 3)
        return 8;

    // --- strSplitMax: the last element holds the unsplit remainder ---
    if (strSplitMax(&parts, _S"key=a=b", _S"=", true, 2) != 2)
        return 10;
    if (!strEq(parts.a[0], _S"key") || !strEq(parts.a[1], _S"a=b"))
        return 11;
    if (strSplitMax(&parts, _S"a,b,c", _S",", true, 1) != 1 || !strEq(parts.a[0], _S"a,b,c"))
        return 12;
    // a limit larger than the number of pieces changes nothing
    if (strSplitMax(&parts, _S"a,b,c", _S",", true, 99) != 3 || !strEq(parts.a[2], _S"c"))
        return 13;
    // 0 and negative both mean unlimited, matching strSplit
    if (strSplitMax(&parts, _S"a,b,c", _S",", true, 0) != 3)
        return 14;
    if (strSplitMax(&parts, _S"a,b,c", _S",", true, -1) != 3)
        return 15;
    // skipped empty segments do not count against the limit
    if (strSplitMax(&parts, _S",a,b,c", _S",", false, 2) != 2)
        return 16;
    if (!strEq(parts.a[0], _S"a") || !strEq(parts.a[1], _S"b,c"))
        return 17;

    // --- strSplitAnyMax ---
    if (strSplitAnyMax(&parts, _S"cmd arg1 arg2 arg3", _S" ", false, 2) != 2)
        return 20;
    if (!strEq(parts.a[0], _S"cmd") || !strEq(parts.a[1], _S"arg1 arg2 arg3"))
        return 21;
    if (strSplitAnyMax(&parts, _S"a,b;c,d", _S",;", true, 3) != 3 || !strEq(parts.a[2], _S"c,d"))
        return 22;

    // strSplit itself must be unchanged now that it shares the body
    if (strSplit(&parts, _S"a,b,c", _S",", false) != 3 || !strEq(parts.a[1], _S"b"))
        return 25;
    if (strSplit(&parts, _S"a,,b", _S",", true) != 3 || !strEmpty(parts.a[1]))
        return 26;
    if (strSplit(&parts, _S"a,,b", _S",", false) != 2)
        return 27;

    // --- strSplitNext ---
    pos = 0;
    if (!strSplitNext(_S"a,b,c", &pos, _S",", &seg) || !strEq(seg, _S"a"))
        return 30;
    if (!strSplitNext(_S"a,b,c", &pos, _S",", &seg) || !strEq(seg, _S"b"))
        return 31;
    if (!strSplitNext(_S"a,b,c", &pos, _S",", &seg) || !strEq(seg, _S"c"))
        return 32;
    if (strSplitNext(_S"a,b,c", &pos, _S",", &seg))
        return 33;

    // a trailing separator yields one final empty segment, then stops
    pos = 0;
    if (!strSplitNext(_S"a,", &pos, _S",", &seg) || !strEq(seg, _S"a"))
        return 34;
    if (!strSplitNext(_S"a,", &pos, _S",", &seg) || !strEmpty(seg))
        return 35;
    if (strSplitNext(_S"a,", &pos, _S",", &seg))
        return 36;

    // no separator at all gives the whole string once
    pos = 0;
    if (!strSplitNext(_S"abc", &pos, _S",", &seg) || !strEq(seg, _S"abc"))
        return 37;
    if (strSplitNext(_S"abc", &pos, _S",", &seg))
        return 38;

    // NULL and empty inputs
    pos = 0;
    if (!strSplitNext(NULL, &pos, _S",", &seg) || !strEmpty(seg))
        return 39;
    if (strSplitNext(NULL, &pos, _S",", &seg))
        return 40;

    // --- strSplitNextAny ---
    pos = 0;
    if (!strSplitNextAny((strref) "a,b;c", &pos, _S",;", &seg) || !strEq(seg, _S"a"))
        return 50;
    if (!strSplitNextAny((strref) "a,b;c", &pos, _S",;", &seg) || !strEq(seg, _S"b"))
        return 51;
    if (!strSplitNextAny((strref) "a,b;c", &pos, _S",;", &seg) || !strEq(seg, _S"c"))
        return 52;
    if (strSplitNextAny((strref) "a,b;c", &pos, _S",;", &seg))
        return 53;

    // an empty set never matches, so the whole string comes back once
    pos = 0;
    if (!strSplitNextAny(_S"a,b", &pos, NULL, &seg) || !strEq(seg, _S"a,b"))
        return 54;
    if (strSplitNextAny(_S"a,b", &pos, NULL, &seg))
        return 55;

    // --- rope paths ---
    string rope = 0;
    makeRope(&rope);
    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 128) {
        ret = 60;   // not actually a rope; the checks below would prove nothing
        goto out;
    }

    // "hTTh" straddles the segment boundary at 64, so splitting on it crosses runs
    if (strSplit(&parts, rope, _S"hTTh", true) != 2) {
        ret = 61;
        goto out;
    }
    if (strLen(parts.a[0]) != 62 || strLen(parts.a[1]) != 62) {
        ret = 62;
        goto out;
    }

    // and the cursor form over the same rope
    pos = 0;
    if (!strSplitNextAny(rope, &pos, _S"-", &seg) || strLen(seg) != 6) {
        ret = 63;   // "Thirty" -- '-' sits at offset 6
        goto out;
    }
    if (!strSplitNextAny(rope, &pos, _S"-", &seg) || strLen(seg) != 50) {
        ret = 64;   // up to the '-' at offset 57
        goto out;
    }
    // and one that reaches past the segment boundary at 64
    if (!strSplitNextAny(rope, &pos, _S"-", &seg) || strLen(seg) != 12)
        ret = 65;

out:
    strDestroy(&rope);
    strDestroy(&seg);
    saDestroy(&parts);
    return ret;
}

static int test_hex()
{
    static const uint8 raw[5] = { 0x00, 0x12, 0xab, 0xFF, 0x7f };
    uint8 buf[16];
    string s = 0;
    int ret  = 0;

    // --- encode ---
    if (!strHexEncode(&s, raw, sizeof(raw), false) || !strEq(s, _S"0012abff7f"))
        return 1;
    if (!strHexEncode(&s, raw, sizeof(raw), true) || !strEq(s, _S"0012ABFF7F"))
        return 2;
    if (!strHexEncode(&s, raw, 0, false) || !strEmpty(s))
        return 3;

    // --- size query protocol ---
    if (strHexDecode(_S"0012abff7f", NULL, 0) != 5)
        return 10;
    if (strHexDecode(_S"", NULL, 0) != 0)
        return 11;

    // --- decode, both cases and mixed ---
    memset(buf, 0xcc, sizeof(buf));
    if (strHexDecode(_S"0012abff7f", buf, sizeof(buf)) != 5)
        return 20;
    if (memcmp(buf, raw, sizeof(raw)) != 0)
        return 21;
    if (buf[5] != 0)   // a NUL is appended when there is room
        return 22;

    memset(buf, 0xcc, sizeof(buf));
    if (strHexDecode(_S"0012ABFF7F", buf, sizeof(buf)) != 5 || memcmp(buf, raw, sizeof(raw)) != 0)
        return 23;
    memset(buf, 0xcc, sizeof(buf));
    if (strHexDecode(_S"0012aBFf7F", buf, sizeof(buf)) != 5 || memcmp(buf, raw, sizeof(raw)) != 0)
        return 24;

    // exactly sized buffer: decodes, but has no room for the extra NUL
    memset(buf, 0xcc, sizeof(buf));
    if (strHexDecode(_S"0012abff7f", buf, 5) != 5 || memcmp(buf, raw, sizeof(raw)) != 0)
        return 25;
    if (buf[5] != 0xcc)
        return 26;

    // --- rejection ---
    if (strHexDecode(_S"abc", buf, sizeof(buf)) != 0)   // odd length
        return 30;
    if (strHexDecode(_S"abc", NULL, 0) != 0)
        return 31;
    if (strHexDecode(_S"00gg", buf, sizeof(buf)) != 0)   // not hex
        return 32;
    if (strHexDecode(_S"00 1", buf, sizeof(buf)) != 0)
        return 33;
    if (strHexDecode(NULL, buf, sizeof(buf)) != 0 || strHexDecode(_S"", buf, sizeof(buf)) != 0)
        return 34;
    if (strHexDecode(_S"0012abff7f", buf, 4) != 0)   // buffer too small
        return 35;
    if (strHexDecode((strref) "12ab", buf, sizeof(buf)) != 2 || buf[0] != 0x12 || buf[1] != 0xab)
        return 36;

    // --- round trip through a rope, so the decode iterator crosses runs ---
    string rope = 0, hex = 0;
    uint8* big  = xaAlloc(64, XA_Zero);
    for (int i = 0; i < 64; i++) big[i] = (uint8)(i * 3);

    strHexEncode(&hex, big, 64, false);   // 128 hex digits
    strAppend(&rope, hex);
    strDestroy(&hex);
    strHexEncode(&hex, big, 64, false);
    strAppend(&rope, hex);   // 256 digits total, built as a rope

    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 256) {
        ret = 40;   // not actually a rope; the check below would prove nothing
    } else {
        uint8* out = xaAlloc(129, XA_Zero);
        if (strHexDecode(rope, out, 128) != 128)
            ret = 41;
        else if (memcmp(out, big, 64) != 0 || memcmp(out + 64, big, 64) != 0)
            ret = 42;
        xaFree(out);
    }

    xaFree(big);
    strDestroy(&hex);
    strDestroy(&rope);
    strDestroy(&s);
    return ret;
}

static int test_utf8()
{
    // 'a', U+00E9 (2 bytes), U+4E2D (3 bytes), U+1F600 (4 bytes), 'z' -- 11 bytes, 5 points
    string u8 = _SU"a\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80z";
    string s = 0, o = 0, alias = 0;
    int ret  = 0;

    // --- strU8Len ---
    if (strLen(u8) != 11 || strU8Len(u8) != 5)
        return 1;
    if (strU8Len(_S"plain ascii") != 11)
        return 2;
    if (strU8Len(NULL) != 0 || strU8Len(_S"") != 0)
        return 3;
    if (strU8Len((strref) "cstr") != 4)
        return 4;

    // invalid input reports 0
    static const uint8 bad[4] = { 'a', 0xE4, 0xB8, 'b' };
    strFromBytes(&s, bad, sizeof(bad));
    if (strU8Len(s) != 0)
        return 5;

    // --- strU8Offset ---
    if (strU8Offset(u8, 0) != 0 || strU8Offset(u8, 1) != 1 || strU8Offset(u8, 2) != 3)
        return 10;
    if (strU8Offset(u8, 3) != 6 || strU8Offset(u8, 4) != 10)
        return 11;
    // one past the last code point is the byte length, which is what a range endpoint needs
    if (strU8Offset(u8, 5) != 11 || strU8Offset(u8, strEnd) != 11)
        return 12;
    if (strU8Offset(u8, 6) != -1)
        return 13;
    // negative indices count back from the end
    if (strU8Offset(u8, -1) != 10 || strU8Offset(u8, -2) != 6 || strU8Offset(u8, -5) != 0)
        return 14;
    if (strU8Offset(u8, -6) != -1)
        return 15;
    // NULL and invalid input
    if (strU8Offset(NULL, 0) != 0 || strU8Offset(NULL, strEnd) != 0 || strU8Offset(NULL, 2) != -1)
        return 16;
    if (strU8Offset(s, 2) != -1)
        return 17;
    if (strU8Offset((strref) "abc", 2) != 2)
        return 18;

    // --- strSubStrU8 ---
    if (!strSubStrU8(&o, u8, 1, 3) || strLen(o) != 5)
        return 20;
    if (!strEq(o, _SU"\xC3\xA9\xE4\xB8\xAD"))
        return 21;
    if (!strSubStrU8(&o, u8, 0, 1) || !strEq(o, _S"a"))
        return 22;
    if (!strSubStrU8(&o, u8, -1, strEnd) || !strEq(o, _S"z"))
        return 23;
    if (!strSubStrU8(&o, u8, 3, 4) || strLen(o) != 4)
        return 24;
    // out of range indices clamp, exactly as strSubStr does
    if (!strSubStrU8(&o, u8, 0, 99) || strLen(o) != 11)
        return 25;
    if (!strSubStrU8(&o, u8, 99, strEnd) || !strEmpty(o))
        return 26;
    if (!strSubStrU8(&o, u8, 3, 1) || !strEmpty(o))
        return 27;
    // a NULL source is reported as failure, exactly as strSubStr does
    if (strSubStrU8(&o, NULL, 0, 2) || !strEmpty(o))
        return 28;
    // invalid UTF-8 is refused rather than sliced
    if (strSubStrU8(&o, s, 0, 1))
        return 29;

    // aliasing
    strDup(&alias, u8);
    strSubStrU8(&o, alias, 1, 3);
    strSubStrU8(&alias, alias, 1, 3);
    if (!strEq(alias, o) || strLen(alias) != 5)
        return 30;

    // --- strSanitizeUTF8 ---
    // valid input passes through untouched, and does not copy
    strCopy(&s, u8);
    strDup(&alias, s);
    if (!strSanitizeUTF8(&o, s) || !strEq(o, u8))
        return 40;
    if (!strEq(s, u8) || strTestRefCount(s) < 2)
        return 41;

    // a truncated sequence: 0xE4 starts three bytes but 'b' is not a continuation
    strFromBytes(&s, bad, sizeof(bad));
    if (!strSanitizeUTF8(&o, s) || strLen(o) != 8)
        return 42;   // 'a' + 2 replacement chars + 'b'
    if (!strValidUTF8(o) || strU8Len(o) != 4)
        return 43;
    if (strGetChar(o, 0) != 'a' || strGetChar(o, 7) != 'b')
        return 44;
    if (strGetChar(o, 1) != 0xef || strGetChar(o, 2) != 0xbf || strGetChar(o, 3) != 0xbd)
        return 45;

    // an overlong encoding of '/' is rejected even though its bytes look well formed
    static const uint8 overlong[5] = { 'x', 0xE0, 0x80, 0xAF, 'y' };
    strFromBytes(&s, overlong, sizeof(overlong));
    if (strU8Len(s) != 0)
        return 46;
    if (!strSanitizeUTF8(&o, s) || strLen(o) != 11)
        return 47;   // 'x' + 3 replacement chars + 'y'
    if (!strValidUTF8(o) || strU8Len(o) != 5)
        return 48;

    // a lone trailing lead byte, and a bare continuation byte
    static const uint8 trunc[2] = { 'q', 0xF0 };
    strFromBytes(&s, trunc, sizeof(trunc));
    if (!strSanitizeUTF8(&o, s) || strLen(o) != 4 || !strValidUTF8(o))
        return 49;

    // degenerate inputs
    if (!strSanitizeUTF8(&o, NULL) || !strEmpty(o))
        return 50;
    if (!strSanitizeUTF8(&o, _S"clean ascii") || !strEq(o, _S"clean ascii"))
        return 51;
    if (!strSanitizeUTF8(&o, (strref) "cstr") || !strEq(o, _S"cstr"))
        return 53;

    // aliasing: sanitizing in place has to give the same answer
    strFromBytes(&alias, bad, sizeof(bad));
    strFromBytes(&s, bad, sizeof(bad));
    strSanitizeUTF8(&o, s);
    strSanitizeUTF8(&alias, alias);
    if (!strEq(alias, o))
        return 54;

    // --- rope path ---
    string rope = 0, chunk = 0;
    strRepeat(&chunk, _SU"\xE4\xB8\xAD", 24);   // 72 bytes, 24 code points
    strAppend(&rope, chunk);
    strAppend(&rope, chunk);

    if (strTestRopeDepth(rope) < 1 || strLen(rope) != 144) {
        ret = 60;   // not actually a rope; the checks below would prove nothing
    } else if (strU8Len(rope) != 48) {
        ret = 61;
    } else if (strU8Offset(rope, 30) != 90 || strU8Offset(rope, -1) != 141) {
        ret = 62;   // both land past the segment boundary at 72
    } else if (!strSubStrU8(&o, rope, 23, 25) || strLen(o) != 6) {
        ret = 63;   // straddles the boundary
    } else if (!strSanitizeUTF8(&o, rope) || strLen(o) != 144) {
        ret = 64;
    }

    strDestroy(&chunk);
    strDestroy(&rope);
    strDestroy(&alias);
    strDestroy(&o);
    strDestroy(&s);
    return ret;
}

static int test_num()
{
    string s = 0;
    int32 i32;
    uint32 u32;
    int64 i64;
    uint64 u64;
    float32 f32;
    float64 f64;

    // ========== Integer To String Tests ==========

    // Int32 to string - positive
    strFromInt32(&s, 123, 10);
    if (!strEq(s, _S"123"))
        return 1;

    // Int32 to string - negative
    strFromInt32(&s, -456, 10);
    if (!strEq(s, _S"-456"))
        return 1;

    // Int32 to string - zero
    strFromInt32(&s, 0, 10);
    if (!strEq(s, _S"0"))
        return 1;

    // Int32 to string - minimum value
    strFromInt32(&s, MIN_INT32, 10);
    if (!strEq(s, _S"-2147483648"))
        return 1;

    // Int32 to string - maximum value
    strFromInt32(&s, MAX_INT32, 10);
    if (!strEq(s, _S"2147483647"))
        return 1;

    // Int32 to string - hexadecimal
    strFromInt32(&s, 255, 16);
    if (!strEq(s, _S"ff"))
        return 1;

    // UInt32 to string - positive
    strFromUInt32(&s, 4294967295U, 10);
    if (!strEq(s, _S"4294967295"))
        return 1;

    // UInt32 to string - zero
    strFromUInt32(&s, 0, 10);
    if (!strEq(s, _S"0"))
        return 1;

    // UInt32 to string - hexadecimal
    strFromUInt32(&s, 0xDEADBEEF, 16);
    if (!strEq(s, _S"deadbeef"))
        return 1;

    // Int64 to string - positive
    strFromInt64(&s, 9223372036854775807LL, 10);
    if (!strEq(s, _S"9223372036854775807"))
        return 1;

    // Int64 to string - negative
    strFromInt64(&s, -9223372036854775807LL - 1, 10);
    if (!strEq(s, _S"-9223372036854775808"))
        return 1;

    // UInt64 to string - maximum
    strFromUInt64(&s, 18446744073709551615ULL, 10);
    if (!strEq(s, _S"18446744073709551615"))
        return 1;

    // ========== String To Integer Tests ==========

    // String to Int32 - basic
    if (!strToInt32(&i32, _S"42", 10, true) || i32 != 42)
        return 1;

    // String to Int32 - negative
    if (!strToInt32(&i32, _S"-789", 10, true) || i32 != -789)
        return 1;

    // String to Int32 - with leading whitespace
    if (!strToInt32(&i32, _S"  123", 10, false) || i32 != 123)
        return 1;

    // String to Int32 - with trailing chars (non-strict)
    if (!strToInt32(&i32, _S"456abc", 10, false) || i32 != 456)
        return 1;

    // String to Int32 - with trailing chars (strict) should fail
    if (strToInt32(&i32, _S"456abc", 10, true))
        return 1;

    // String to Int32 - hex with 0x prefix
    if (!strToInt32(&i32, _S"0xFF", 0, true) || i32 != 255)
        return 1;

    // String to Int32 - hex without prefix
    if (!strToInt32(&i32, _S"FF", 16, true) || i32 != 255)
        return 1;

    // String to Int32 - leading + sign
    if (!strToInt32(&i32, _S"+123", 10, true) || i32 != 123)
        return 1;

    // String to UInt32 - basic
    if (!strToUInt32(&u32, _S"4000000000", 10, true) || u32 != 4000000000U)
        return 1;

    // String to UInt32 - hex
    if (!strToUInt32(&u32, _S"0xDEADBEEF", 0, true) || u32 != 0xDEADBEEF)
        return 1;

    // String to Int64 - large positive
    if (!strToInt64(&i64, _S"9223372036854775807", 10, true) || i64 != 9223372036854775807LL)
        return 1;

    // String to Int64 - large negative
    if (!strToInt64(&i64, _S"-9223372036854775808", 10, true) || i64 != (-9223372036854775807LL - 1))
        return 1;

    // String to UInt64 - maximum value
    if (!strToUInt64(&u64, _S"18446744073709551615", 10, true) || u64 != 18446744073709551615ULL)
        return 1;

    // String to Int32 - empty string should fail
    if (strToInt32(&i32, _S"", 10, true))
        return 1;

    // String to Int32 - non-numeric should fail
    if (strToInt32(&i32, _S"abc", 10, true))
        return 1;

    // ========== Float To String Tests ==========

    // Float32 to string - basic decimal
    strFromFloat32(&s, 3.14f);
    if (!strEq(s, _S"3.14"))
        return 1;

    // Float32 to string - negative
    strFromFloat32(&s, -2.5f);
    if (!strEq(s, _S"-2.5"))
        return 1;

    // Float32 to string - zero
    strFromFloat32(&s, 0.0f);
    if (!strEq(s, _S"0"))
        return 1;

    // Float32 to string - scientific notation (large)
    strFromFloat32(&s, 1.0e10f);
    // Just verify it's in scientific notation (contains 'e' and starts with '1')
    if (strFind(s, 0, _S"e") == -1 || strFind(s, 0, _S"1") != 0)
        return 1;

    // Float32 to string - scientific notation (small)
    strFromFloat32(&s, 1.23e-8f);
    // Just verify it contains 'e' for scientific notation
    if (strFind(s, 0, _S"e") == -1)
        return 1;

    // Float32 to string - infinity
    strFromFloat32(&s, _float32_inf(false));
    if (!strEq(s, _S"inf"))
        return 1;

    // Float32 to string - negative infinity
    strFromFloat32(&s, _float32_inf(true));
    if (!strEq(s, _S"-inf"))
        return 1;

    // Float32 to string - NaN
    strFromFloat32(&s, _float32_nan());
    if (!strEq(s, _S"nan"))
        return 1;

    // Float64 to string - basic decimal
    strFromFloat64(&s, 3.141592653589793);
    if (strFind(s, 0, _S"3.14159") != 0)
        return 1;

    // Float64 to string - negative
    strFromFloat64(&s, -42.875);
    if (!strEq(s, _S"-42.875"))
        return 1;

    // Float64 to string - zero
    strFromFloat64(&s, 0.0);
    if (!strEq(s, _S"0"))
        return 1;

    // Float64 to string - scientific notation
    strFromFloat64(&s, 2.5e-10);
    if (!strEq(s, _S"2.5e-10"))
        return 1;

    // ========== String To Float Tests ==========

    // String to Float32 - basic decimal
    if (!strToFloat32(&f32, _S"3.14", true) || (f32 < 3.13f || f32 > 3.15f))
        return 1;

    // String to Float32 - negative
    if (!strToFloat32(&f32, _S"-2.5", true) || f32 != -2.5f)
        return 1;

    // String to Float32 - zero
    if (!strToFloat32(&f32, _S"0.0", true) || f32 != 0.0f)
        return 1;

    // String to Float32 - scientific notation (positive exponent)
    if (!strToFloat32(&f32, _S"1.5e2", true) || (f32 < 149.9f || f32 > 150.1f))
        return 1;

    // String to Float32 - scientific notation (negative exponent)
    if (!strToFloat32(&f32, _S"2.5e-3", true) || (f32 < 0.0024f || f32 > 0.0026f))
        return 1;

    // String to Float32 - no leading zero
    if (!strToFloat32(&f32, _S".5", true) || (f32 < 0.49f || f32 > 0.51f))
        return 1;

    // String to Float32 - leading + sign
    if (!strToFloat32(&f32, _S"+1.5", true) || (f32 < 1.49f || f32 > 1.51f))
        return 1;

    // String to Float32 - with leading whitespace (non-strict)
    if (!strToFloat32(&f32, _S"  3.14", false) || (f32 < 3.13f || f32 > 3.15f))
        return 1;

    // String to Float32 - with trailing chars (non-strict)
    if (!strToFloat32(&f32, _S"2.5abc", false) || f32 != 2.5f)
        return 1;

    // String to Float32 - with trailing chars (strict) should fail
    if (strToFloat32(&f32, _S"2.5abc", true))
        return 1;

    // String to Float32 - infinity (lowercase)
    if (!strToFloat32(&f32, _S"inf", true) || f32 != _float32_inf(false))
        return 1;

    // String to Float32 - infinity (uppercase)
    if (!strToFloat32(&f32, _S"INF", true) || f32 != _float32_inf(false))
        return 1;

    // String to Float32 - negative infinity
    if (!strToFloat32(&f32, _S"-inf", true) || f32 != _float32_inf(true))
        return 1;

    // String to Float32 - NaN (lowercase)
    if (!strToFloat32(&f32, _S"nan", true) || f32 == f32)   // NaN != NaN
        return 1;

    // String to Float32 - NaN (uppercase)
    if (!strToFloat32(&f32, _S"NaN", true) || f32 == f32)   // NaN != NaN
        return 1;

    // String to Float64 - basic decimal
    if (!strToFloat64(&f64, _S"3.141592653589793", true) || (f64 < 3.14159265 || f64 > 3.14159266))
        return 1;

    // String to Float64 - negative
    if (!strToFloat64(&f64, _S"-42.875", true) || f64 != -42.875)
        return 1;

    // String to Float64 - zero
    if (!strToFloat64(&f64, _S"0", true) || f64 != 0.0)
        return 1;

    // String to Float64 - scientific notation
    if (!strToFloat64(&f64, _S"2.5e-10", true) || (f64 < 2.49e-10 || f64 > 2.51e-10))
        return 1;

    // String to Float64 - large exponent
    if (!strToFloat64(&f64, _S"1.5e100", true) || f64 < 1.0e100)
        return 1;

    // String to Float64 - capital E in exponent
    if (!strToFloat64(&f64, _S"2.5E+5", true) || (f64 < 249999.0 || f64 > 250001.0))
        return 1;

    // String to Float64 - empty string should fail
    if (strToFloat64(&f64, _S"", true))
        return 1;

    // String to Float64 - non-numeric should fail
    if (strToFloat64(&f64, _S"abc", true))
        return 1;

    // String to Float64 - just a decimal point should fail
    if (strToFloat64(&f64, _S".", true))
        return 1;

    // String to Float64 - exponent without digits should fail
    if (strToFloat64(&f64, _S"1e", true))
        return 1;

    // ========== Round-trip Tests ==========

    // Int32 round-trip
    strFromInt32(&s, -12345, 10);
    if (!strToInt32(&i32, s, 10, true) || i32 != -12345)
        return 1;

    // UInt32 round-trip
    strFromUInt32(&s, 987654321U, 10);
    if (!strToUInt32(&u32, s, 10, true) || u32 != 987654321U)
        return 1;

    // Int64 round-trip
    strFromInt64(&s, -1234567890123456LL, 10);
    if (!strToInt64(&i64, s, 10, true) || i64 != -1234567890123456LL)
        return 1;

    // UInt64 round-trip
    strFromUInt64(&s, 9876543210987654321ULL, 10);
    if (!strToUInt64(&u64, s, 10, true) || u64 != 9876543210987654321ULL)
        return 1;

    // Float32 round-trip (with tolerance)
    float32 orig_f32 = 123.456f;
    strFromFloat32(&s, orig_f32);
    if (!strToFloat32(&f32, s, true) || (f32 < orig_f32 - 0.001f || f32 > orig_f32 + 0.001f))
        return 1;

    // Float64 round-trip (with tolerance)
    float64 orig_f64 = 123.456789012345;
    strFromFloat64(&s, orig_f64);
    if (!strToFloat64(&f64, s, true) || (f64 < orig_f64 - 0.000001 || f64 > orig_f64 + 0.000001))
        return 1;

    strDestroy(&s);
    return 0;
}

// File-scope STR_CONST declarations (all platforms, compile-time length)
STR_CONST(kLitAscii, "hello");
STR_CONSTU(kLitUtf8, "caf\xC3\xA9");   // "café" in UTF-8
STR_CONSTO(kLitOther, "raw\x01\x02");
STR_CONSTL(kLitLong, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!!");
STR_CONSTUL(kLitLongU, "\xC3\xA9\xC3\xA0\xC3\xAA\xC3\xAB");   // "éàêë" in UTF-8
STR_CONSTOL(kLitLongO, "bin\x01\x02\x03\x04");

static int test_literal()
{
    // --- _S / _SU / _SO: STR_LEN0, runtime strlen ---
    if (strLen(_S"hello") != 5)
        return 1;
    if (strLen(_SU "caf\xC3\xA9") != 5)
        return 2;
    if (strLen(_SO "raw") != 3)
        return 3;
    if (!strEq(_S"hello", _S"hello"))
        return 4;

    // --- STR_CONST family (file-scope declarations, compile-time length) ---
    if (strLen(kLitAscii) != 5)
        return 10;
    if (!strEq(kLitAscii, _S"hello"))
        return 11;

    if (strLen(kLitUtf8) != 5)
        return 12;   // 4 ASCII + 2-byte sequence = 5 bytes
    if (!strEq(kLitUtf8, _SU "caf\xC3\xA9"))
        return 13;

    if (strLen(kLitOther) != 5)
        return 14;
    if (!strEq(kLitOther, _SO "raw\x01\x02"))
        return 15;

    if (strLen(kLitLong) != 65)
        return 16;
    if (!strEq(kLitLong, _S"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!!"))
        return 17;

    if (strLen(kLitLongU) != 8)
        return 18;   // 4 x 2-byte UTF-8 sequences
    if (!strEq(kLitLongU, _SU "\xC3\xA9\xC3\xA0\xC3\xAA\xC3\xAB"))
        return 19;

    if (strLen(kLitLongO) != 7)
        return 20;
    if (!strEq(kLitLongO, _SO "bin\x01\x02\x03\x04"))
        return 21;

    // --- _SL / _SLU / _SLO: STR_LEN8 on GCC/Clang, STR_LEN0 on MSVC ---
    // In all cases length and content must be correct regardless of platform.
    if (strLen(_SL("hello")) != 5)
        return 30;
    if (!strEq(_SL("hello"), _S"hello"))
        return 31;

    if (strLen(_SLU("caf\xC3\xA9")) != 5)
        return 32;
    if (!strEq(_SLU("caf\xC3\xA9"), kLitUtf8))
        return 33;

    if (strLen(_SLO("raw\x01\x02")) != 5)
        return 34;
    if (!strEq(_SLO("raw\x01\x02"), kLitOther))
        return 35;

    // --- _SLL / _SLUL / _SLOL: STR_LEN16 on GCC/Clang, STR_LEN0 on MSVC ---
    if (strLen(_SLL("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!!")) != 65)
        return 40;
    if (!strEq(_SLL("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!!!"), kLitLong))
        return 41;

    if (strLen(_SLUL("\xC3\xA9\xC3\xA0\xC3\xAA\xC3\xAB")) != 8)
        return 42;
    if (!strEq(_SLUL("\xC3\xA9\xC3\xA0\xC3\xAA\xC3\xAB"), kLitLongU))
        return 43;

    if (strLen(_SLOL("bin\x01\x02\x03\x04")) != 7)
        return 44;
    if (!strEq(_SLOL("bin\x01\x02\x03\x04"), kLitLongO))
        return 45;

    // --- STR_CONST as function-scope static ---
    STR_CONST(kLocal, "local constant");
    if (strLen(kLocal) != 14)
        return 50;
    if (!strEq(kLocal, _S"local constant"))
        return 51;

    return 0;
}

testfunc strtest_funcs[] = {
    { "join",        test_join         },
    { "append",      test_append       },
    { "substr",      test_substr       },
    { "compare",     test_compare      },
    { "comparelen0", test_compare_len0 },
    { "longstring",  test_long         },
    { "find",        test_find         },
    { "rope",        test_rope         },
    { "num",         test_num          },
    { "bytes",       test_bytes        },
    { "findchar",    test_findchar     },
    { "trim",        test_trim         },
    { "replace",     test_replace      },
    { "splitvar",    test_splitvar     },
    { "hex",         test_hex          },
    { "utf8",        test_utf8         },
    { "literal",     test_literal      },
    { 0,             0                 }
};

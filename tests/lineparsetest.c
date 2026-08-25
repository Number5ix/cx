#include <cx/serialize/streambuf.h>
#include <cx/serialize/sbstring.h>
#include <cx/serialize/lineparse.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/utils/compare.h>

#define TEST_FILE lineparsetest
#define TEST_FUNCS lineparsetest_funcs
#include "common.h"

static const char testdata_lf[] = "This is a test of the lineparser code. This is line 1.\n"
"This is line 2.\n"
"This is line 3.\n"
"This is line 4.\n"
"This is line 5.\n"
"This is line 6.\n"
"This is line 7.\n"
"This is line 8.\n"
"This is line 9.\n"
"This is line 10.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\n"
"(99) THIS IS THE NEXT TO LAST LINE!\n"
"(100) THIS IS THE LAST LINE!"
;

static const char testdata_crlf[] = "This is a test of the lineparser code. This is line 1.\r\n"
"This is line 2.\r\n"
"This is line 3.\r\n"
"This is line 4.\r\n"
"This is line 5.\r\n"
"This is line 6.\r\n"
"This is line 7.\r\n"
"This is line 8.\r\n"
"This is line 9.\r\n"
"This is line 10.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\r\n"
"(99) THIS IS THE NEXT TO LAST LINE!\r\n"
"(100) THIS IS THE LAST LINE!\r\n"
;

static const char testdata_mixed1[] = "This is a test of the lineparser code. This is line 1.\n"
"This is line 2.\r\n"
"This is line 3.\n"
"This is line 4.\r\n"
"This is line 5.\n"
"This is line 6.\r\n"
"This is line 7.\n"
"This is line 8.\r\n"
"This is line 9.\n"
"This is line 10.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"(99) THIS IS THE NEXT TO LAST LINE!\n"
"(100) THIS IS THE LAST LINE!"
;

static const char testdata_mixed2[] = "This is a test of the lineparser code. This is line 1.\r\n"
"This is line 2.\n"
"This is line 3.\r\n"
"This is line 4.\n"
"This is line 5.\r\n"
"This is line 6.\n"
"This is line 7.\r\n"
"This is line 8.\n"
"This is line 9.\r\n"
"This is line 10.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"Up to 90 more lines may follow.\r\n"
"Up to 90 more lines may follow.\n"
"(99) THIS IS THE NEXT TO LAST LINE!\r\n"
"(100) THIS IS THE LAST LINE!"
;

static string line1 = _S"This is a test of the lineparser code. This is line 1.";
static string line5 = _S"This is line 5.";
static string line8 = _S"This is line 8.";
static string line9 = _S"This is line 9.";
static string linerepeat = _S"Up to 90 more lines may follow.";
static string line99 = _S"(99) THIS IS THE NEXT TO LAST LINE!";
static string line100 = _S"(100) THIS IS THE LAST LINE!";

// Checks the line at line number `want` matches `expect`, logging both values on mismatch.
static void lpCheckEq(int *ret, int lines, int want, strref got, strref expect)
{
    if (lines == want && !strEq(got, expect))
        TEST_FAILV(*ret, 1, _SL("line ${int}: got '${string}', want '${string}'"), stvar(int32, lines), stvar(strref, got), stvar(strref, expect));
}

// Checks every line in (lo, hi) matches `expect`, logging both values on mismatch.
static void lpCheckRange(int *ret, int lines, int lo, int hi, strref got, strref expect)
{
    if (lines > lo && lines < hi && !strEq(got, expect))
        TEST_FAILV(*ret, 1, _SL("line ${int}: got '${string}', want '${string}'"), stvar(int32, lines), stvar(strref, got), stvar(strref, expect));
}

int test_lineparse_explicit()
{
    int ret = 0;
    string teststr_lf = 0;
    string teststr_crlf = 0;

    strCopy(&teststr_lf, (strref)testdata_lf);
    strCopy(&teststr_crlf, (strref)testdata_crlf);

    StreamBuffer *sb;
    int lines;
    string line = 0;

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_lf) ||
        !lparseRegisterPull(sb, LPARSE_LF))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // retest with NoIncomplete
    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_lf) ||
        !lparseRegisterPull(sb, LPARSE_LF | LPARSE_NoIncomplete))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
    }

    if (lines != 99)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 99)"), stvar(int32, lines));

    sbufRelease(&sb);

    // CRLF

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_crlf) ||
        !lparseRegisterPull(sb, LPARSE_CRLF))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // retest with NoIncomplete
    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_crlf) ||
        !lparseRegisterPull(sb, LPARSE_CRLF | LPARSE_NoIncomplete))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // finally a quick test with includeeol

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_crlf) ||
        !lparseRegisterPull(sb, LPARSE_CRLF | LPARSE_IncludeEOL))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);
    string temp = 0;

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        strConcat(&temp, line1, _S"\r\n");
        lpCheckEq(&ret, lines, 1, line, temp);
        strConcat(&temp, line5, _S"\r\n");
        lpCheckEq(&ret, lines, 5, line, temp);
        strConcat(&temp, linerepeat, _S"\r\n");
        lpCheckRange(&ret, lines, 10, 99, line, temp);
        strConcat(&temp, line99, _S"\r\n");
        lpCheckEq(&ret, lines, 99, line, temp);
        strConcat(&temp, line100, _S"\r\n");
        lpCheckEq(&ret, lines, 100, line, temp);
    }
    strDestroy(&temp);

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    strDestroy(&line);
    strDestroy(&teststr_lf);
    strDestroy(&teststr_crlf);

    return ret;
}

int test_lineparse_auto()
{
    int ret = 0;
    string teststr_lf = 0;
    string teststr_crlf = 0;
    string teststr_mixed1 = 0;
    string teststr_mixed2 = 0;

    strCopy(&teststr_lf, (strref)testdata_lf);
    strCopy(&teststr_crlf, (strref)testdata_crlf);
    strCopy(&teststr_mixed1, (strref)testdata_mixed1);
    strCopy(&teststr_mixed2, (strref)testdata_mixed2);

    StreamBuffer *sb;
    int lines;
    string line = 0;

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_lf) ||
        !lparseRegisterPull(sb, LPARSE_Auto))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // CRLF

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_crlf) ||
        !lparseRegisterPull(sb, LPARSE_Auto))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // Mixed 1 (should detect CR)
    string temp = 0;

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_mixed1) ||
        !lparseRegisterPull(sb, LPARSE_Auto))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        strConcat(&temp, line8, _S"\r");
        lpCheckEq(&ret, lines, 8, line, temp);
        if (lines > 10 && lines < 99 && (lines % 2) == 1 && !strEq(line, linerepeat))
            TEST_FAILV(ret, 1, _SL("line ${int} (odd): got '${string}', want '${string}'"), stvar(int32, lines), stvar(strref, line), stvar(strref, linerepeat));
        strConcat(&temp, linerepeat, _S"\r");
        if (lines > 10 && lines < 99 && (lines % 2) == 0 && !strEq(line, temp))
            TEST_FAILV(ret, 1, _SL("line ${int} (even): got '${string}', want '${string}'"), stvar(int32, lines), stvar(strref, line), stvar(strref, temp));
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // Mixed 1 (should detect CRLF)

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_mixed2) ||
        !lparseRegisterPull(sb, LPARSE_Auto))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        strNConcat(&temp, line8, _S"\n", line9);
        lpCheckEq(&ret, lines, 5, line, temp);
        strNConcat(&temp, linerepeat, _S"\n", linerepeat);
        lpCheckRange(&ret, lines, 6, 48, line, temp);
        lpCheckEq(&ret, lines, 51, line, line100);
    }

    if (lines != 51)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 51)"), stvar(int32, lines));

    sbufRelease(&sb);

    strDestroy(&temp);
    strDestroy(&line);
    strDestroy(&teststr_lf);
    strDestroy(&teststr_crlf);
    strDestroy(&teststr_mixed1);
    strDestroy(&teststr_mixed2);

    return ret;
}

int test_lineparse_mixed()
{
    int ret = 0;
    string teststr_mixed1 = 0;
    string teststr_mixed2 = 0;

    strCopy(&teststr_mixed1, (strref)testdata_mixed1);
    strCopy(&teststr_mixed2, (strref)testdata_mixed2);

    StreamBuffer *sb;
    int lines;
    string line = 0;

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_mixed1) ||
        !lparseRegisterPull(sb, LPARSE_Mixed))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    // Mixed2

    sb = sbufCreate(512);
    if (!sbufStrPRegisterPull(sb, teststr_mixed2) ||
        !lparseRegisterPull(sb, LPARSE_Mixed))
        TEST_FAIL(1, _SL("failed to register line-parser pull source"), stvNone);

    lines = 0;
    while (lparseLine(sb, &line)) {
        lines++;
        lpCheckEq(&ret, lines, 1, line, line1);
        lpCheckEq(&ret, lines, 5, line, line5);
        lpCheckRange(&ret, lines, 10, 99, line, linerepeat);
        lpCheckEq(&ret, lines, 99, line, line99);
        lpCheckEq(&ret, lines, 100, line, line100);
    }

    if (lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lines));

    sbufRelease(&sb);

    strDestroy(&line);
    strDestroy(&teststr_mixed1);
    strDestroy(&teststr_mixed2);

    return ret;
}

typedef struct LineParsePushTestCtx {
    int ret;
    int lines;
    bool didclean;
} LineParsePushTestCtx;

static void test_ctxcleanup(void *ctx)
{
    LineParsePushTestCtx *lppt = (LineParsePushTestCtx *)ctx;
    lppt->didclean = true;
}

static bool test_linecb(strref line, void *ctx)
{
    LineParsePushTestCtx *lppt = (LineParsePushTestCtx *)ctx;

    lppt->lines++;
    lpCheckEq(&lppt->ret, lppt->lines, 1, line, line1);
    lpCheckEq(&lppt->ret, lppt->lines, 5, line, line5);
    lpCheckRange(&lppt->ret, lppt->lines, 10, 99, line, linerepeat);
    lpCheckEq(&lppt->ret, lppt->lines, 99, line, line99);
    lpCheckEq(&lppt->ret, lppt->lines, 100, line, line100);

    return true;
}

int test_lineparse_push()
{
    StreamBuffer *sb;
    int ret = 0;
    LineParsePushTestCtx lppt = { 0 };
    string teststr_lf = 0;
    string teststr_crlf = 0;

    strCopy(&teststr_lf, (strref)testdata_lf);
    strCopy(&teststr_crlf, (strref)testdata_crlf);

    // test with a large buffer
    sb = sbufCreate(8192);
    if (!lparseRegisterPush(sb, test_linecb, test_ctxcleanup, &lppt, 0))
        TEST_FAIL(1, _SL("failed to register line-parser push sink"), stvNone);
    sbufStrIn(sb, teststr_lf);

    if (lppt.lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lppt.lines));

    sbufRelease(&sb);

    if (!lppt.didclean)
        TEST_FAILV(ret, 1, _SL("cleanup callback did not run"), stvNone);
    ret |= lppt.ret;

    // test with a very small buffer
    lppt = (LineParsePushTestCtx){ 0 };

    sb = sbufCreate(5);
    if (!lparseRegisterPush(sb, test_linecb, test_ctxcleanup, &lppt, 0))
        TEST_FAIL(1, _SL("failed to register line-parser push sink"), stvNone);
    sbufStrIn(sb, teststr_crlf);

    if (lppt.lines != 100)
        TEST_FAILV(ret, 1, _SL("total lines=${int} (want 100)"), stvar(int32, lppt.lines));
    ret |= lppt.ret;

    sbufRelease(&sb);

    if (!lppt.didclean)
        TEST_FAILV(ret, 1, _SL("cleanup callback did not run"), stvNone);

    strDestroy(&teststr_lf);
    strDestroy(&teststr_crlf);

    return ret;
}

testfunc lineparsetest_funcs[] = {
    { "explicit", test_lineparse_explicit },
    { "auto", test_lineparse_auto },
    { "mixed", test_lineparse_mixed },
    { "push", test_lineparse_push },
    { 0, 0 }
};

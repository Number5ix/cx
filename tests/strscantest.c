#include <cx/string.h>

#define TEST_FILE strscantest
#define TEST_FUNCS strscantest_funcs
#include "common.h"

static int test_literal()
{
    strscan sc;

    strscInit(&sc, _S"GET /index.html HTTP/1.1");

    if (!strscLit(&sc, _S"GET") || sc.pos != 3)
        TEST_FAIL(1, _SL("strscLit(GET): ok=${int} pos=${int} (want ok, pos=3)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (!strscWS1(&sc) || sc.pos != 4)
        TEST_FAIL(1, _SL("strscWS1: ok=${int} pos=${int} (want ok, pos=4)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (!strscTry(&sc, _S"/index") || sc.pos != 10)
        TEST_FAIL(1, _SL("strscTry(/index): ok=${int} pos=${int} (want ok, pos=10)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (strscTry(&sc, _S"nope") || sc.pos != 10 || !sc.ok)
        // a failed Try must not fail the scan
        TEST_FAIL(1, _SL("strscTry(nope): matched=${int} pos=${int} ok=${int} (want no match, pos=10, ok)"), stvar(int32, 0), stvar(int32, sc.pos), stvar(int32, (int32)sc.ok));
    if (!strscPeek(&sc, _S".html") || sc.pos != 10)
        // Peek must not consume
        TEST_FAIL(1, _SL("strscPeek(.html): ok=${int} pos=${int} (want ok, pos=10)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (strscPeekChar(&sc) != '.')
        TEST_FAIL(1, _SL("strscPeekChar()='${int}' (want '.')"), stvar(int32, strscPeekChar(&sc)));
    if (!strscChar(&sc, '.') || sc.pos != 11)
        TEST_FAIL(1, _SL("strscChar('.'): ok=${int} pos=${int} (want ok, pos=11)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (!strscLit(&sc, _S"html HTTP/1.1"))
        TEST_FAIL(1, _SL("strscLit('html HTTP/1.1') failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscDone(&sc))
        TEST_FAIL(1, _SL("strscDone() false at pos=${int}"), stvar(int32, sc.pos));
    if (!strscFinish(&sc))
        TEST_FAIL(1, _SL("strscFinish() failed at errpos=${int}"), stvar(int32, sc.errpos));

    // a mismatch is sticky: everything after it is a no-op, and the position where it
    // first went wrong is remembered rather than the position of the last attempt
    strscInit(&sc, _S"GET /");
    strscLit(&sc, _S"POST");
    if (sc.ok || sc.errpos != 0)
        TEST_FAIL(1, _SL("after failed strscLit(POST): ok=${int} errpos=${int} (want !ok, errpos=0)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.errpos));
    if (strscLit(&sc, _S"GET"))
        TEST_FAIL(1, _SL("strscLit(GET) matched after a sticky failure"), stvNone);
    if (sc.errpos != 0)
        TEST_FAIL(1, _SL("errpos=${int} after sticky failure (want 0)"), stvar(int32, sc.errpos));
    if (strscFinish(&sc))
        TEST_FAIL(1, _SL("strscFinish() succeeded after a sticky failure"), stvNone);

    // case insensitivity applies to literals and single characters
    strscInit(&sc, _S"Get /", STRSC_CaseInsensitive);
    if (!strscLit(&sc, _S"gEt") || !strscChar(&sc, ' ') || !strscChar(&sc, '/'))
        TEST_FAIL(1, _SL("case-insensitive literal/char match failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscFinish(&sc))
        TEST_FAIL(1, _SL("strscFinish() failed at errpos=${int}"), stvar(int32, sc.errpos));

    // whitespace: WS accepts none, WS1 requires some
    strscInit(&sc, _S"a  \t b");
    strscChar(&sc, 'a');
    if (!strscWS(&sc) || sc.pos != 5)
        TEST_FAIL(1, _SL("strscWS(): ok=${int} pos=${int} (want ok, pos=5)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (!strscWS(&sc) || sc.pos != 5)
        TEST_FAIL(1, _SL("strscWS() (accepting none): ok=${int} pos=${int} (want ok, pos=5)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    if (strscWS1(&sc) || sc.ok)
        TEST_FAIL(1, _SL("strscWS1() matched with nothing left: ok=${int}"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    return 0;
}

static int test_extract()
{
    strscan sc;
    string tok = 0;
    int ret    = 0;

    strscInit(&sc, _S"name=value; path=/; secure");

    if (!strscToken(&sc, &tok, _S"=") || !strEq(tok, _S"name"))
        TEST_FAILV(ret, 1, _SL("strscToken(=): ok=${int} tok='${string}' (want ok, tok=name)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscChar(&sc, '='))
        TEST_FAILV(ret, 1, _SL("strscChar('=') failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscUntil(&sc, &tok, _S"; ") || !strEq(tok, _S"value"))
        TEST_FAILV(ret, 1, _SL("strscUntil(; ): ok=${int} tok='${string}' (want ok, tok=value)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscLit(&sc, _S"; "))
        TEST_FAILV(ret, 1, _SL("strscLit('; ') failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscRest(&sc, &tok) || !strEq(tok, _S"path=/; secure"))
        TEST_FAILV(ret, 1, _SL("strscRest(): ok=${int} tok='${string}' (want ok, tok='path=/; secure')"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscDone(&sc) || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone/Finish failed at pos=${int} errpos=${int}"), stvar(int32, sc.pos), stvar(int32, sc.errpos));

    // an empty token fails; an empty Until result does not
    strscInit(&sc, _S"=x");
    if (strscToken(&sc, &tok, _S"=") || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscToken(=) on an empty token: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    strscInit(&sc, _S"=x");
    if (!strscUntil(&sc, &tok, _S"=") || !strEmpty(tok))
        TEST_FAILV(ret, 1, _SL("strscUntil(=) on an empty result: ok=${int} tok='${string}' (want ok, empty tok)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    strscFinish(&sc);

    // Until fails outright when the text never appears
    strscInit(&sc, _S"abc");
    if (strscUntil(&sc, &tok, _S"=") || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscUntil(=) with no match: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    // While is the mirror of Token
    strscInit(&sc, _S"12ab34");
    if (!strscWhile(&sc, &tok, _S"0123456789") || !strEq(tok, _S"12"))
        TEST_FAILV(ret, 1, _SL("strscWhile(digits): ok=${int} tok='${string}' (want ok, tok=12)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscWhile(&sc, &tok, _S"abcdef") || !strEq(tok, _S"ab"))
        TEST_FAILV(ret, 1, _SL("strscWhile(abcdef): ok=${int} tok='${string}' (want ok, tok=ab)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscWhile(&sc, &tok, _S"0123456789") || !strEq(tok, _S"34"))
        TEST_FAILV(ret, 1, _SL("strscWhile(digits): ok=${int} tok='${string}' (want ok, tok=34)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscDone(&sc) || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone/Finish failed at pos=${int} errpos=${int}"), stvar(int32, sc.pos), stvar(int32, sc.errpos));

    // a NULL output consumes without building a string, and the span reports what it ate
    strscInit(&sc, _S"GET /path");
    int32 off = -1, len = -1;
    if (!strscToken(&sc, NULL, _S" "))
        TEST_FAILV(ret, 1, _SL("strscToken(NULL, ' ') failed at pos=${int}"), stvar(int32, sc.pos));
    strscSpan(&sc, &off, &len);
    if (off != 0 || len != 3 || !strRangeEq(sc.s, _S"GET", off, (uint32)len))
        TEST_FAILV(ret, 1, _SL("strscSpan(): off=${int} len=${int} (want off=0, len=3, range='GET')"), stvar(int32, off), stvar(int32, len));
    strscFinish(&sc);

    // quoted strings, with and without escapes
    strscInit(&sc, _S"\"plain\" \"has \\\"quote\\\" in\" rest");
    if (!strscQuoted(&sc, &tok) || !strEq(tok, _S"plain"))
        TEST_FAILV(ret, 1, _SL("strscQuoted(): ok=${int} tok='${string}' (want ok, tok=plain)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscWS1() failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscQuoted(&sc, &tok) || !strEq(tok, _S"has \"quote\" in"))
        TEST_FAILV(ret, 1, _SL("strscQuoted() with escapes: ok=${int} tok='${string}' (want ok, tok='has \"quote\" in')"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscWS1(&sc) || !strscLit(&sc, _S"rest") || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("trailing WS1/Lit(rest)/Finish failed at pos=${int}"), stvar(int32, sc.pos));

    // an unterminated quote leaves the cursor alone rather than half-consuming
    strscInit(&sc, _S"\"never closed");
    if (strscQuoted(&sc, &tok) || sc.pos != 0 || sc.errpos != 0)
        TEST_FAILV(ret, 1, _SL("unterminated quote: ok=${int} pos=${int} errpos=${int} (want !ok, pos=0, errpos=0)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos), stvar(int32, sc.errpos));
    strscFinish(&sc);

    // lines: CRLF, bare LF, empty, and an unterminated last line
    strscInit(&sc, _S"one\r\ntwo\n\nfour");
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"one"))
        TEST_FAILV(ret, 1, _SL("strscLine(): ok=${int} tok='${string}' (want ok, tok=one)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"two"))
        TEST_FAILV(ret, 1, _SL("strscLine(): ok=${int} tok='${string}' (want ok, tok=two)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscLine(&sc, &tok) || !strEmpty(tok))
        TEST_FAILV(ret, 1, _SL("strscLine() on empty line: ok=${int} tok='${string}' (want ok, empty tok)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"four"))
        TEST_FAILV(ret, 1, _SL("strscLine(): ok=${int} tok='${string}' (want ok, tok=four)"), stvar(int32, (int32)sc.ok), stvar(strref, tok));
    if (!strscDone(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone() false at pos=${int}"), stvar(int32, sc.pos));
    if (strscLine(&sc, &tok) || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscLine() past end: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    strDestroy(&tok);
    return ret;
}

static int test_typed()
{
    strscan sc;
    int ret = 0;

    int32 i32;
    uint32 u32;
    int64 i64;
    uint64 u64;
    float64 f64;
    float32 f32;
    bool b;

    // a number stops at the first byte its own syntax does not allow
    strscInit(&sc, _S"12,-34");
    if (!strscInt32(&sc, &i32, 10) || i32 != 12 || sc.pos != 2)
        TEST_FAILV(ret, 1, _SL("strscInt32(): ok=${int} i32=${int} pos=${int} (want ok, i32=12, pos=2)"), stvar(int32, (int32)sc.ok), stvar(int32, i32), stvar(int32, sc.pos));
    if (!strscChar(&sc, ','))
        TEST_FAILV(ret, 1, _SL("strscChar(',') failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscInt32(&sc, &i32, 10) || i32 != -34)
        TEST_FAILV(ret, 1, _SL("strscInt32(): ok=${int} i32=${int} (want ok, i32=-34)"), stvar(int32, (int32)sc.ok), stvar(int32, i32));
    if (!strscDone(&sc) || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone/Finish failed at pos=${int} errpos=${int}"), stvar(int32, sc.pos), stvar(int32, sc.errpos));

    // unsigned readers do not take a sign
    strscInit(&sc, _S"-1");
    if (strscUInt32(&sc, &u32, 10) || sc.pos != 0)
        TEST_FAILV(ret, 1, _SL("strscUInt32() on '-1': ok=${int} pos=${int} (want !ok, pos=0)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.pos));
    strscFinish(&sc);

    // no leading whitespace and no "0x" prefix; base 16 works without one
    strscInit(&sc, _S" 5");
    if (strscUInt32(&sc, &u32, 10))
        TEST_FAILV(ret, 1, _SL("strscUInt32() matched leading whitespace, u32=${uint}"), stvar(uint32, u32));
    strscFinish(&sc);

    strscInit(&sc, _S"0x1f");
    if (!strscUInt64(&sc, &u64, 16) || u64 != 0 || sc.pos != 1)
        // reads the 0, stops at the x
        TEST_FAILV(ret, 1, _SL("strscUInt64(base16) on '0x1f': ok=${int} u64=${uint} pos=${int} (want ok, u64=0, pos=1)"), stvar(int32, (int32)sc.ok), stvar(uint64, u64), stvar(int32, sc.pos));
    strscFinish(&sc);

    strscInit(&sc, _S"1f");
    if (!strscUInt64(&sc, &u64, 16) || u64 != 31)
        TEST_FAILV(ret, 1, _SL("strscUInt64(base16) on '1f': ok=${int} u64=${uint} (want ok, u64=31)"), stvar(int32, (int32)sc.ok), stvar(uint64, u64));
    strscFinish(&sc);

    // range limits
    strscInit(&sc, _S"4294967295");
    if (!strscUInt32(&sc, &u32, 10) || u32 != 4294967295U)
        TEST_FAILV(ret, 1, _SL("strscUInt32() at UINT32_MAX: ok=${int} u32=${uint} (want ok, u32=4294967295)"), stvar(int32, (int32)sc.ok), stvar(uint32, u32));
    strscFinish(&sc);

    strscInit(&sc, _S"4294967296");
    if (strscUInt32(&sc, &u32, 10) || sc.errpos != 0)
        TEST_FAILV(ret, 1, _SL("strscUInt32() past UINT32_MAX: ok=${int} errpos=${int} (want !ok, errpos=0)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.errpos));
    strscFinish(&sc);

    strscInit(&sc, _S"-9223372036854775808");
    if (!strscInt64(&sc, &i64, 10) || i64 != (-9223372036854775807LL - 1))
        TEST_FAILV(ret, 1, _SL("strscInt64() at INT64_MIN: ok=${int} i64=${int} (want ok, i64=INT64_MIN)"), stvar(int32, (int32)sc.ok), stvar(int64, i64));
    strscFinish(&sc);

    // floats, including scientific notation and an exponent that is not one
    strscInit(&sc, _S"3.5e2 1e -0.25");
    if (!strscFloat64(&sc, &f64) || f64 != 350.0)
        TEST_FAILV(ret, 1, _SL("strscFloat64(3.5e2): ok=${int} f64=${float} (want ok, f64=350)"), stvar(int32, (int32)sc.ok), stvar(float64, f64));
    if (!strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscWS1() failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscFloat32(&sc, &f32) || f32 != 1.0f || strscPeekChar(&sc) != 'e')
        TEST_FAILV(ret, 1, _SL("strscFloat32(1e): ok=${int} f32=${float} peek='${int}' (want ok, f32=1, peek='e')"), stvar(int32, (int32)sc.ok), stvar(float64, (float64)f32), stvar(int32, strscPeekChar(&sc)));
    if (!strscChar(&sc, 'e') || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscChar('e')/WS1 failed at pos=${int}"), stvar(int32, sc.pos));
    if (!strscFloat64(&sc, &f64) || f64 != -0.25)
        TEST_FAILV(ret, 1, _SL("strscFloat64(-0.25): ok=${int} f64=${float} (want ok, f64=-0.25)"), stvar(int32, (int32)sc.ok), stvar(float64, f64));
    if (!strscDone(&sc) || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone/Finish failed at pos=${int} errpos=${int}"), stvar(int32, sc.pos), stvar(int32, sc.errpos));

    // booleans in all their spellings
    strscInit(&sc, _S"TRUE no 1 0 maybe");
    if (!strscBool(&sc, &b) || !b || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscBool(TRUE): ok=${int} b=${int} (want ok, b=true)"), stvar(int32, (int32)sc.ok), stvar(int32, (int32)b));
    if (!strscBool(&sc, &b) || b || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscBool(no): ok=${int} b=${int} (want ok, b=false)"), stvar(int32, (int32)sc.ok), stvar(int32, (int32)b));
    if (!strscBool(&sc, &b) || !b || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscBool(1): ok=${int} b=${int} (want ok, b=true)"), stvar(int32, (int32)sc.ok), stvar(int32, (int32)b));
    if (!strscBool(&sc, &b) || b || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscBool(0): ok=${int} b=${int} (want ok, b=false)"), stvar(int32, (int32)sc.ok), stvar(int32, (int32)b));
    if (strscBool(&sc, &b) || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscBool(maybe) matched: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    // strscVal reaches every type the conversion system knows
    strscInit(&sc, _S"42 hello 00000000000000000000000005 -7");
    uint8 u8   = 0;
    string s   = 0;
    SUID id    = { 0 };
    int16 i16  = 0;

    if (!strscVal(&sc, uint8, &u8) || u8 != 42 || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscVal(uint8): ok=${int} u8=${uint} (want ok, u8=42)"), stvar(int32, (int32)sc.ok), stvar(uint32, u8));
    if (!strscVal(&sc, string, &s) || !strEq(s, _S"hello") || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscVal(string): ok=${int} s='${string}' (want ok, s=hello)"), stvar(int32, (int32)sc.ok), stvar(strref, s));
    if (!strscVal(&sc, suid, &id) || id.high != 0 || id.low != 5 || !strscWS1(&sc))
        TEST_FAILV(ret, 1, _SL("strscVal(suid): ok=${int} high=${uint} low=${uint} (want ok, high=0, low=5)"), stvar(int32, (int32)sc.ok), stvar(uint64, id.high), stvar(uint64, id.low));
    if (!strscVal(&sc, int16, &i16) || i16 != -7)
        TEST_FAILV(ret, 1, _SL("strscVal(int16): ok=${int} i16=${int} (want ok, i16=-7)"), stvar(int32, (int32)sc.ok), stvar(int32, i16));
    if (!strscDone(&sc) || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscDone/Finish failed at pos=${int} errpos=${int}"), stvar(int32, sc.pos), stvar(int32, sc.errpos));
    strDestroy(&s);

    // a value that parses but does not fit fails the scan
    strscInit(&sc, _S"300");
    if (strscVal(&sc, uint8, &u8) || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscVal(uint8) accepted out-of-range '300': ok=${int}"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    return ret;
}

static int test_backtrack()
{
    strscan sc;
    int ret = 0;

    // "06 Nov 1994" and "Nov 6 1994" tried in turn, which is what rewinding is for
    string mon = 0;
    uint32 day = 0;

    strscInit(&sc, _S"Nov 6 1994");

    int32 mark = strscMark(&sc);
    strscUInt32(&sc, &day, 10);
    strscWS1(&sc);
    strscToken(&sc, &mon, _S" ");

    if (sc.ok)
        // the first form must not have matched
        TEST_FAILV(ret, 1, _SL("day-first form matched unexpectedly: ok=${int}"), stvar(int32, (int32)sc.ok));

    strscRewind(&sc, mark);
    if (!sc.ok || sc.errpos != -1 || sc.pos != 0)
        // rewinding un-does the failure as well as the position
        TEST_FAILV(ret, 1, _SL("after strscRewind(): ok=${int} errpos=${int} pos=${int} (want ok, errpos=-1, pos=0)"), stvar(int32, (int32)sc.ok), stvar(int32, sc.errpos), stvar(int32, sc.pos));

    strscToken(&sc, &mon, _S" ");
    strscWS1(&sc);
    strscUInt32(&sc, &day, 10);

    if (!sc.ok || !strEq(mon, _S"Nov") || day != 6)
        TEST_FAILV(ret, 1, _SL("month-first form: ok=${int} mon='${string}' day=${uint} (want ok, mon=Nov, day=6)"), stvar(int32, (int32)sc.ok), stvar(strref, mon), stvar(uint32, day));

    if (!strscWS1(&sc) || !strscLit(&sc, _S"1994") || !strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("trailing WS1/Lit(1994)/Finish failed at pos=${int}"), stvar(int32, sc.pos));

    // Seek does not clear the error flag, unlike Rewind
    strscInit(&sc, _S"abc");
    strscLit(&sc, _S"xyz");
    if (strscSeek(&sc, 0) || sc.ok)
        TEST_FAILV(ret, 1, _SL("strscSeek() cleared a sticky failure: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    // an out-of-range Seek fails rather than clamping
    strscInit(&sc, _S"abc");
    if (strscSeek(&sc, 4) || sc.ok)
        TEST_FAILV(ret, 1, _SL("out-of-range strscSeek(4) succeeded: ok=${int} (want !ok)"), stvar(int32, (int32)sc.ok));
    strscFinish(&sc);

    // strscFail lets a caller reject a value the scanner was happy with
    strscInit(&sc, _S"32");
    if (!strscUInt32(&sc, &day, 10))
        TEST_FAILV(ret, 1, _SL("strscUInt32() failed to parse '32', day=${uint}"), stvar(uint32, day));
    if (day > 31)
        strscFail(&sc);
    if (strscFinish(&sc))
        TEST_FAILV(ret, 1, _SL("strscFinish() succeeded after strscFail() rejected day=${uint}"), stvar(uint32, day));

    strDestroy(&mon);
    return ret;
}

testfunc strscantest_funcs[] = {
    { "literal", test_literal },
    { "extract", test_extract },
    { "typed", test_typed },
    { "backtrack", test_backtrack },
    { 0, 0 }
};

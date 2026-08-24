#include <cx/string.h>

#define TEST_FILE strscantest
#define TEST_FUNCS strscantest_funcs
#include "common.h"

static int test_literal()
{
    strscan sc;

    strscInit(&sc, _S"GET /index.html HTTP/1.1");

    if (!strscLit(&sc, _S"GET") || sc.pos != 3)
        return 1;
    if (!strscWS1(&sc) || sc.pos != 4)
        return 1;
    if (!strscTry(&sc, _S"/index") || sc.pos != 10)
        return 1;
    if (strscTry(&sc, _S"nope") || sc.pos != 10 || !sc.ok)
        return 1;   // a failed Try must not fail the scan
    if (!strscPeek(&sc, _S".html") || sc.pos != 10)
        return 1;   // Peek must not consume
    if (strscPeekChar(&sc) != '.')
        return 1;
    if (!strscChar(&sc, '.') || sc.pos != 11)
        return 1;
    if (!strscLit(&sc, _S"html HTTP/1.1"))
        return 1;
    if (!strscDone(&sc))
        return 1;
    if (!strscFinish(&sc))
        return 1;

    // a mismatch is sticky: everything after it is a no-op, and the position where it
    // first went wrong is remembered rather than the position of the last attempt
    strscInit(&sc, _S"GET /");
    strscLit(&sc, _S"POST");
    if (sc.ok || sc.errpos != 0)
        return 1;
    if (strscLit(&sc, _S"GET"))
        return 1;
    if (sc.errpos != 0)
        return 1;
    if (strscFinish(&sc))
        return 1;

    // case insensitivity applies to literals and single characters
    strscInit(&sc, _S"Get /", STRSC_CaseInsensitive);
    if (!strscLit(&sc, _S"gEt") || !strscChar(&sc, ' ') || !strscChar(&sc, '/'))
        return 1;
    if (!strscFinish(&sc))
        return 1;

    // whitespace: WS accepts none, WS1 requires some
    strscInit(&sc, _S"a  \t b");
    strscChar(&sc, 'a');
    if (!strscWS(&sc) || sc.pos != 5)
        return 1;
    if (!strscWS(&sc) || sc.pos != 5)
        return 1;
    if (strscWS1(&sc) || sc.ok)
        return 1;
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
        ret = 1;
    if (!strscChar(&sc, '='))
        ret = 1;
    if (!strscUntil(&sc, &tok, _S"; ") || !strEq(tok, _S"value"))
        ret = 1;
    if (!strscLit(&sc, _S"; "))
        ret = 1;
    if (!strscRest(&sc, &tok) || !strEq(tok, _S"path=/; secure"))
        ret = 1;
    if (!strscDone(&sc) || !strscFinish(&sc))
        ret = 1;

    // an empty token fails; an empty Until result does not
    strscInit(&sc, _S"=x");
    if (strscToken(&sc, &tok, _S"=") || sc.ok)
        ret = 1;
    strscFinish(&sc);

    strscInit(&sc, _S"=x");
    if (!strscUntil(&sc, &tok, _S"=") || !strEmpty(tok))
        ret = 1;
    strscFinish(&sc);

    // Until fails outright when the text never appears
    strscInit(&sc, _S"abc");
    if (strscUntil(&sc, &tok, _S"=") || sc.ok)
        ret = 1;
    strscFinish(&sc);

    // While is the mirror of Token
    strscInit(&sc, _S"12ab34");
    if (!strscWhile(&sc, &tok, _S"0123456789") || !strEq(tok, _S"12"))
        ret = 1;
    if (!strscWhile(&sc, &tok, _S"abcdef") || !strEq(tok, _S"ab"))
        ret = 1;
    if (!strscWhile(&sc, &tok, _S"0123456789") || !strEq(tok, _S"34"))
        ret = 1;
    if (!strscDone(&sc) || !strscFinish(&sc))
        ret = 1;

    // a NULL output consumes without building a string, and the span reports what it ate
    strscInit(&sc, _S"GET /path");
    int32 off = -1, len = -1;
    if (!strscToken(&sc, NULL, _S" "))
        ret = 1;
    strscSpan(&sc, &off, &len);
    if (off != 0 || len != 3 || !strRangeEq(sc.s, _S"GET", off, (uint32)len))
        ret = 1;
    strscFinish(&sc);

    // quoted strings, with and without escapes
    strscInit(&sc, _S"\"plain\" \"has \\\"quote\\\" in\" rest");
    if (!strscQuoted(&sc, &tok) || !strEq(tok, _S"plain"))
        ret = 1;
    if (!strscWS1(&sc))
        ret = 1;
    if (!strscQuoted(&sc, &tok) || !strEq(tok, _S"has \"quote\" in"))
        ret = 1;
    if (!strscWS1(&sc) || !strscLit(&sc, _S"rest") || !strscFinish(&sc))
        ret = 1;

    // an unterminated quote leaves the cursor alone rather than half-consuming
    strscInit(&sc, _S"\"never closed");
    if (strscQuoted(&sc, &tok) || sc.pos != 0 || sc.errpos != 0)
        ret = 1;
    strscFinish(&sc);

    // lines: CRLF, bare LF, empty, and an unterminated last line
    strscInit(&sc, _S"one\r\ntwo\n\nfour");
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"one"))
        ret = 1;
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"two"))
        ret = 1;
    if (!strscLine(&sc, &tok) || !strEmpty(tok))
        ret = 1;
    if (!strscLine(&sc, &tok) || !strEq(tok, _S"four"))
        ret = 1;
    if (!strscDone(&sc))
        ret = 1;
    if (strscLine(&sc, &tok) || sc.ok)
        ret = 1;
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
        ret = 1;
    if (!strscChar(&sc, ','))
        ret = 1;
    if (!strscInt32(&sc, &i32, 10) || i32 != -34)
        ret = 1;
    if (!strscDone(&sc) || !strscFinish(&sc))
        ret = 1;

    // unsigned readers do not take a sign
    strscInit(&sc, _S"-1");
    if (strscUInt32(&sc, &u32, 10) || sc.pos != 0)
        ret = 1;
    strscFinish(&sc);

    // no leading whitespace and no "0x" prefix; base 16 works without one
    strscInit(&sc, _S" 5");
    if (strscUInt32(&sc, &u32, 10))
        ret = 1;
    strscFinish(&sc);

    strscInit(&sc, _S"0x1f");
    if (!strscUInt64(&sc, &u64, 16) || u64 != 0 || sc.pos != 1)
        ret = 1;   // reads the 0, stops at the x
    strscFinish(&sc);

    strscInit(&sc, _S"1f");
    if (!strscUInt64(&sc, &u64, 16) || u64 != 31)
        ret = 1;
    strscFinish(&sc);

    // range limits
    strscInit(&sc, _S"4294967295");
    if (!strscUInt32(&sc, &u32, 10) || u32 != 4294967295U)
        ret = 1;
    strscFinish(&sc);

    strscInit(&sc, _S"4294967296");
    if (strscUInt32(&sc, &u32, 10) || sc.errpos != 0)
        ret = 1;
    strscFinish(&sc);

    strscInit(&sc, _S"-9223372036854775808");
    if (!strscInt64(&sc, &i64, 10) || i64 != (-9223372036854775807LL - 1))
        ret = 1;
    strscFinish(&sc);

    // floats, including scientific notation and an exponent that is not one
    strscInit(&sc, _S"3.5e2 1e -0.25");
    if (!strscFloat64(&sc, &f64) || f64 != 350.0)
        ret = 1;
    if (!strscWS1(&sc))
        ret = 1;
    if (!strscFloat32(&sc, &f32) || f32 != 1.0f || strscPeekChar(&sc) != 'e')
        ret = 1;
    if (!strscChar(&sc, 'e') || !strscWS1(&sc))
        ret = 1;
    if (!strscFloat64(&sc, &f64) || f64 != -0.25)
        ret = 1;
    if (!strscDone(&sc) || !strscFinish(&sc))
        ret = 1;

    // booleans in all their spellings
    strscInit(&sc, _S"TRUE no 1 0 maybe");
    if (!strscBool(&sc, &b) || !b || !strscWS1(&sc))
        ret = 1;
    if (!strscBool(&sc, &b) || b || !strscWS1(&sc))
        ret = 1;
    if (!strscBool(&sc, &b) || !b || !strscWS1(&sc))
        ret = 1;
    if (!strscBool(&sc, &b) || b || !strscWS1(&sc))
        ret = 1;
    if (strscBool(&sc, &b) || sc.ok)
        ret = 1;
    strscFinish(&sc);

    // strscVal reaches every type the conversion system knows
    strscInit(&sc, _S"42 hello 00000000000000000000000005 -7");
    uint8 u8   = 0;
    string s   = 0;
    SUID id    = { 0 };
    int16 i16  = 0;

    if (!strscVal(&sc, uint8, &u8) || u8 != 42 || !strscWS1(&sc))
        ret = 1;
    if (!strscVal(&sc, string, &s) || !strEq(s, _S"hello") || !strscWS1(&sc))
        ret = 1;
    if (!strscVal(&sc, suid, &id) || id.high != 0 || id.low != 5 || !strscWS1(&sc))
        ret = 1;
    if (!strscVal(&sc, int16, &i16) || i16 != -7)
        ret = 1;
    if (!strscDone(&sc) || !strscFinish(&sc))
        ret = 1;
    strDestroy(&s);

    // a value that parses but does not fit fails the scan
    strscInit(&sc, _S"300");
    if (strscVal(&sc, uint8, &u8) || sc.ok)
        ret = 1;
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
        ret = 1;   // the first form must not have matched

    strscRewind(&sc, mark);
    if (!sc.ok || sc.errpos != -1 || sc.pos != 0)
        ret = 1;   // rewinding un-does the failure as well as the position

    strscToken(&sc, &mon, _S" ");
    strscWS1(&sc);
    strscUInt32(&sc, &day, 10);

    if (!sc.ok || !strEq(mon, _S"Nov") || day != 6)
        ret = 1;

    if (!strscWS1(&sc) || !strscLit(&sc, _S"1994") || !strscFinish(&sc))
        ret = 1;

    // Seek does not clear the error flag, unlike Rewind
    strscInit(&sc, _S"abc");
    strscLit(&sc, _S"xyz");
    if (strscSeek(&sc, 0) || sc.ok)
        ret = 1;
    strscFinish(&sc);

    // an out-of-range Seek fails rather than clamping
    strscInit(&sc, _S"abc");
    if (strscSeek(&sc, 4) || sc.ok)
        ret = 1;
    strscFinish(&sc);

    // strscFail lets a caller reject a value the scanner was happy with
    strscInit(&sc, _S"32");
    if (!strscUInt32(&sc, &day, 10))
        ret = 1;
    if (day > 31)
        strscFail(&sc);
    if (strscFinish(&sc))
        ret = 1;

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

#include <cx/serialize/streambuf.h>
#include <cx/serialize/sbstring.h>
#include <cx/serialize/jsonparse.h>
#include <cx/serialize/jsonout.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include <cx/utils/compare.h>

#define TEST_FILE jsontest
#define TEST_FUNCS jsontest_funcs
#include "common.h"

static const char json_testdata1[] = "{\n"
"    \"num\" : 574,\n"
"    \"str\" : \"This is a test.\",\n"
"    \"str2\": \"Some so-called \\\"escape\\\" characters\\ncan be used.\" , \n"
"    \"str3\": \"Even \\ufd3Eunicode\\uFD3f and \\ud802\\udd08\",\n"
"    \"fl\": false,\n"
"    \"tr\": true,\n"
"    \"nl\": null,\n"
"    \"fltnum\": 109.3,\n"
"    \"scinum\": -3.4134e-34,\n"
"    \"obj\": {\n"
"        \"substr\": \"A string in a sub-object.\",\n"
"        \"subnum\": -3491,\n"
"        \"array\": [\n"
"            1,\n"
"            2,\n"
"            3,\n"
"            4,\n"
"            5\n"
"        ]\n"
"    },\n"
"    \"almostbig\": 9223372036854775807,\n"
"    \"almostsmall\": -9223372036854775808,\n"
"    \"toobig\": 9223372036854775808,\n"
"    \"toosmall\": -9223372036854775809,\n"
"    \"arr\": [\n"
"       \"array index 0\" , \n"
"       573,\n"
"       true,\n"
"       123.456"
"    ]\n"
"}\n";

// normalized for output test
static const char json_testdata1_exact[] = "{\n"
"    \"num\": 574,\n"
"    \"str\": \"This is a test.\",\n"
"    \"str2\": \"Some so-called \\\"escape\\\" characters\\ncan be used.\",\n"
"    \"str3\": \"Even \xEF\xB4\xBEunicode\xEF\xB4\xBF and \xF0\x90\xA4\x88\",\n"
"    \"fl\": false,\n"
"    \"tr\": true,\n"
"    \"nl\": null,\n"
"    \"fltnum\": 109.3,\n"
"    \"scinum\": -3.4134e-34,\n"
"    \"obj\": {\n"
"        \"substr\": \"A string in a sub-object.\",\n"
"        \"subnum\": -3491,\n"
"        \"array\": [\n"
"            1,\n"
"            2,\n"
"            3,\n"
"            4,\n"
"            5\n"
"        ]\n"
"    },\n"
"    \"almostbig\": 9223372036854775807,\n"
"    \"almostsmall\": -9223372036854775808,\n"
"    \"toobig\": 9223372036854776000,\n"
"    \"toosmall\": -9223372036854776000,\n"
"    \"arr\": [\n"
"        \"array index 0\",\n"
"        573,\n"
"        true,\n"
"        123.456\n"
"    ]\n"
"}";

static const char json_testdata1_err[] = "{\n"
"    \"num\" : 574,\n"
"    \"str\" : \"This is a test.\",\n"
"    \"str2\": \"Some so-called \\\"escape\\\" characters\\ncan be used.\" , \n"
"    \"str3\": \"Even \\ufd3Eunicode\\uFD3f and \\ud802\\udd08\",\n"
"    \"fl\": false,\n"
"    \"tr\": true,\n"
"    \"nl\"\n"
"    \"fltnum\": 109.3,\n"
"    \"scinum\": -3.4134e-34,\n"
"    \"obj\": {\n"
"        \"substr\": \"A string in a sub-object.\",\n"
"        \"subnum\": -3491,\n"
"        \"array\": [\n"
"            1,\n"
"            2,\n"
"            3,\n"
"            4,\n"
"            5\n"
"        ]\n"
"    },\n"
"    \"almostbig\": 9223372036854775807,\n"
"    \"almostsmall\": -9223372036854775808,\n"
"    \"toobig\": 9223372036854775808,\n"
"    \"toosmall\": -9223372036854775809,\n"
"    \"arr\": [\n"
"       \"array index 0\" , \n"
"       573,\n"
"       true,\n"
"       123.456"
"    ]\n"
"}\n";

static const char json_testdata1_err2[] = "{\n"
"    \"num\" : 574,\n"
"    \"str\" : \"This is a test.\",\n"
"    \"str2\": \"Some so-called \\\"escape\\\" characters\\ncan be used.\" , \n"
"    \"str3\": \"Even \\ufd3Eunicode\\uFD3f and \\ud802\\udd08\",\n"
"    \"fl\": false,\n"
"    \"tr\": true\n";

static const char json_testdata2[] = "[\n"
"    574,\n"
"    \"This is a test.\",\n"
"    \"Some so-called \\\"escape\\\" characters\\ncan be used.\" , \n"
"    \"Even \\ufd3Eunicode\\uFD3f and \\ud802\\udd08\",\n"
"    false,\n"
"    true,\n"
"    null,\n"
"    109.3,\n"
"    -3.4134e34,\n"
"    {\n"
"        \"substr\": \"A string in a sub-object.\",\n"
"        \"subnum\": -3491,\n"
"        \"array\": [\n"
"            1,\n"
"            2,\n"
"            3,\n"
"            4,\n"
"            5\n"
"        ]\n"
"    },\n"
"    9223372036854775807,\n"
"    -9223372036854775808,\n"
"    9223372036854775808,\n"
"    -9223372036854775809,\n"
"    [\n"
"       \"array index 0\" , \n"
"       573,\n"
"       true,\n"
"       123.456"
"    ],\n"
"    { },\n"
"    []\n"
"]\n";

static const char json_testdata3[] = "42";

static const char json_testdata4[] = "\"A String\"";

static const char json_testdata5[] = "false";

#define COUNTEVENTS(name) static const int name##_count = sizeof(name) / sizeof(name[0])
static JSONParseEvent expectedEvents1[] = {
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"num" } },
    {.etype = JSON_Int, .edata = {.intData = 574 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str" } },
    {.etype = JSON_String, .edata = {.strData = _S"This is a test."} },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str2" } },
    {.etype = JSON_String, .edata = {.strData = _S"Some so-called \"escape\" characters\ncan be used."}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str3" } },
    {.etype = JSON_String, .edata = {.strData = _S"Even \xEF\xB4\xBEunicode\xEF\xB4\xBF and \xF0\x90\xA4\x88"}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"fl" } },
    {.etype = JSON_False },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"tr" } },
    {.etype = JSON_True },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"nl" } },
    {.etype = JSON_Null },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"fltnum" } },
    {.etype = JSON_Float, .edata = {.floatData = 109.3 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"scinum" } },
    {.etype = JSON_Float, .edata = {.floatData = -3.4134e-34 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"obj" } },
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"substr" } },
    {.etype = JSON_String, .edata = {.strData = _S"A string in a sub-object."} },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"subnum" } },
    {.etype = JSON_Int, .edata = {.intData = -3491 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"array" } },
    {.etype = JSON_Array_Begin },
    {.etype = JSON_Int, .edata = {.intData = 1 } },
    {.etype = JSON_Int, .edata = {.intData = 2 } },
    {.etype = JSON_Int, .edata = {.intData = 3 } },
    {.etype = JSON_Int, .edata = {.intData = 4 } },
    {.etype = JSON_Int, .edata = {.intData = 5 } },
    {.etype = JSON_Array_End },
    {.etype = JSON_Object_End },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"almostbig" } },
    {.etype = JSON_Int, .edata = {.intData = 9223372036854775807LL } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"almostsmall" } },
    {.etype = JSON_Int, .edata = {.intData = -9223372036854775807LL - 1 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"toobig" } },
    {.etype = JSON_Float, .edata = {.floatData = 9223372036854775808.0 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"toosmall" } },
    {.etype = JSON_Float, .edata = {.floatData = -9223372036854775809.0 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"arr" } },
    {.etype = JSON_Array_Begin },
    {.etype = JSON_String, .edata = {.strData = _S"array index 0"} },
    {.etype = JSON_Int, .edata = {.intData = 573 } },
    {.etype = JSON_True },
    {.etype = JSON_Float, .edata = {.floatData = 123.456 } },
    {.etype = JSON_Array_End },
    {.etype = JSON_Object_End },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents1);

static JSONParseEvent expectedEvents1_err[] = {
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"num" } },
    {.etype = JSON_Int, .edata = {.intData = 574 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str" } },
    {.etype = JSON_String, .edata = {.strData = _S"This is a test."} },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str2" } },
    {.etype = JSON_String, .edata = {.strData = _S"Some so-called \"escape\" characters\ncan be used."}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str3" } },
    {.etype = JSON_String, .edata = {.strData = _S"Even \xEF\xB4\xBEunicode\xEF\xB4\xBF and \xF0\x90\xA4\x88"}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"fl" } },
    {.etype = JSON_False },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"tr" } },
    {.etype = JSON_True },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"nl" } },
    {.etype = JSON_Error },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents1_err);

static JSONParseEvent expectedEvents1_err2[] = {
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"num" } },
    {.etype = JSON_Int, .edata = {.intData = 574 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str" } },
    {.etype = JSON_String, .edata = {.strData = _S"This is a test."} },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str2" } },
    {.etype = JSON_String, .edata = {.strData = _S"Some so-called \"escape\" characters\ncan be used."}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"str3" } },
    {.etype = JSON_String, .edata = {.strData = _S"Even \xEF\xB4\xBEunicode\xEF\xB4\xBF and \xF0\x90\xA4\x88"}},
    {.etype = JSON_Object_Key, .edata = {.strData = _S"fl" } },
    {.etype = JSON_False },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"tr" } },
    {.etype = JSON_True },
    {.etype = JSON_Error },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents1_err2);

static JSONParseEvent expectedEvents2[] = {
    {.etype = JSON_Array_Begin },
    {.etype = JSON_Int, .edata = {.intData = 574 } },
    {.etype = JSON_String, .edata = {.strData = _S"This is a test."} },
    {.etype = JSON_String, .edata = {.strData = _S"Some so-called \"escape\" characters\ncan be used."}},
    {.etype = JSON_String, .edata = {.strData = _S"Even \xEF\xB4\xBEunicode\xEF\xB4\xBF and \xF0\x90\xA4\x88"}},
    {.etype = JSON_False },
    {.etype = JSON_True },
    {.etype = JSON_Null },
    {.etype = JSON_Float, .edata = {.floatData = 109.3 } },
    {.etype = JSON_Float, .edata = {.floatData = -3.4134e34 } },
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"substr" } },
    {.etype = JSON_String, .edata = {.strData = _S"A string in a sub-object."} },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"subnum" } },
    {.etype = JSON_Int, .edata = {.intData = -3491 } },
    {.etype = JSON_Object_Key, .edata = {.strData = _S"array" } },
    {.etype = JSON_Array_Begin },
    {.etype = JSON_Int, .edata = {.intData = 1 } },
    {.etype = JSON_Int, .edata = {.intData = 2 } },
    {.etype = JSON_Int, .edata = {.intData = 3 } },
    {.etype = JSON_Int, .edata = {.intData = 4 } },
    {.etype = JSON_Int, .edata = {.intData = 5 } },
    {.etype = JSON_Array_End },
    {.etype = JSON_Object_End },
    {.etype = JSON_Int, .edata = {.intData = 9223372036854775807LL } },
    {.etype = JSON_Int, .edata = {.intData = -9223372036854775807LL - 1 } },
    {.etype = JSON_Float, .edata = {.floatData = 9223372036854775808.0 } },
    {.etype = JSON_Float, .edata = {.floatData = -9223372036854775809.0 } },
    {.etype = JSON_Array_Begin },
    {.etype = JSON_String, .edata = {.strData = _S"array index 0"} },
    {.etype = JSON_Int, .edata = {.intData = 573 } },
    {.etype = JSON_True },
    {.etype = JSON_Float, .edata = {.floatData = 123.456 } },
    {.etype = JSON_Array_End },
    {.etype = JSON_Object_Begin },
    {.etype = JSON_Object_End },
    {.etype = JSON_Array_Begin },
    {.etype = JSON_Array_End },
    {.etype = JSON_Array_End },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents2);

static JSONParseEvent expectedEvents3[] = {
    {.etype = JSON_Int, .edata = {.intData = 42 } },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents3);

static JSONParseEvent expectedEvents4[] = {
    {.etype = JSON_String, .edata = {.strData = _S"A String"} },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents4);

static JSONParseEvent expectedEvents5[] = {
    {.etype = JSON_False },
    {.etype = JSON_End },
};
COUNTEVENTS(expectedEvents5);

typedef struct CBData1 {
    int count;
    bool failed;
    JSONParseEvent *expected;
    int expectedCount;
} CBData1;

static void parsecb1(JSONParseEvent *ev, void *userdata)
{
    CBData1 *cbd = (CBData1 *)userdata;
    if (cbd->count >= cbd->expectedCount) {
        cbd->failed = true;
        return;
    }

    JSONParseEvent *exp = &cbd->expected[cbd->count++];
    if (ev->etype != exp->etype) {
        cbd->failed = true;
        return;
    }

    if ((ev->etype == JSON_Object_Key || ev->etype == JSON_String)
        && !strEq(ev->edata.strData, exp->edata.strData)) {
        cbd->failed = true;
        return;
    }

    if (ev->etype == JSON_Int &&
        ev->edata.intData != exp->edata.intData) {
        cbd->failed = true;
        return;
    }

    if (ev->etype == JSON_Float &&
        ev->edata.floatData != exp->edata.floatData) {
        cbd->failed = true;
        return;
    }
}

#define RESETCBD(exname) cbd = (CBData1){ .expected = exname, .expectedCount = exname##_count }

static int test_json_parse()
{
    int ret = 0;
    string teststr = 0;
    CBData1 cbd;
    StreamBuffer *sb;

    strCopy(&teststr, (string)json_testdata1);
    RESETCBD(expectedEvents1);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // test data with an intentional parse error
    strCopy(&teststr, (string)json_testdata1_err);
    RESETCBD(expectedEvents1_err);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (jsonParse(sb, parsecb1, &cbd))              // should return false from failure!
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // slightly different parse error
    strCopy(&teststr, (string)json_testdata1_err2);
    RESETCBD(expectedEvents1_err2);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (jsonParse(sb, parsecb1, &cbd))              // should return false from failure!
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // array test
    strCopy(&teststr, (string)json_testdata2);
    RESETCBD(expectedEvents2);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // single-value tests
    // integer
    strCopy(&teststr, (string)json_testdata3);
    RESETCBD(expectedEvents3);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // string
    strCopy(&teststr, (string)json_testdata4);
    RESETCBD(expectedEvents4);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    // false
    strCopy(&teststr, (string)json_testdata5);
    RESETCBD(expectedEvents5);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, teststr);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);
    strDestroy(&teststr);

    return ret;
}

static bool outsub(StreamBuffer *sb, JSONParseEvent *ev, int numev, uint32 flags)
{
    bool ret = true;
    JSONOut *jo = jsonOutBegin(sb, flags);
    if (!jo)
        return false;

    for (int i = 0; i < numev; i++) {
        ret &= jsonOut(jo, &ev[i]);
        if (!ret)
            break;
    }

    jsonOutEnd(&jo);

    return ret;
}

static int test_json_out()
{
    int ret = 0;

    StreamBuffer *sb;
    string out = 0;

    // check expectedEvents1 against known-good normalized output
    sb = sbufCreate(256);
    if (!sbufStrCRegisterPush(sb, &out))
        return 1;

    if (!outsub(sb, expectedEvents1, expectedEvents1_count, JSON_Indent(4) | JSON_Unix_EOL))
        ret = 1;

    sbufRelease(&sb);

    if (!strEq(out, (string)json_testdata1_exact))
        ret = 1;

    strDestroy(&out);

    // check expectedEvents2 against itself by re-parsing the output
    sb = sbufCreate(256);
    if (!sbufStrCRegisterPush(sb, &out))
        return 1;

    if (!outsub(sb, expectedEvents2, expectedEvents2_count, 0)) // JSON_Minimal | JSON_ASCII_Only))
        ret = 1;

    sbufRelease(&sb);

    // feed the output string back into the parser
    CBData1 cbd;
    RESETCBD(expectedEvents2);

    sb = sbufCreate(256);
    sbufStrPRegisterPull(sb, out);
    if (!jsonParse(sb, parsecb1, &cbd))
        ret = 1;
    if (cbd.failed || cbd.count != cbd.expectedCount)
        ret = 1;
    sbufRelease(&sb);

    strDestroy(&out);

    return ret;
}

testfunc jsontest_funcs[] = {
    { "parse", test_json_parse },
    { "out", test_json_out },
    { 0, 0 }
};

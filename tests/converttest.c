#include <stdio.h>
#include <cx/string.h>
#include <cx/stype/stvar.h>
#include <cx/string/strtest.h>
#include "converttestobj.h"

#define TEST_FILE converttest
#define TEST_FUNCS converttest_functions
#include "common.h"

static int test_numeric()
{
    int ret = 0;

    int8 tiny;
    int16 med;
    int32 big;
    int64 huge;

    uint8 byte;
    uint16 word;
    uint32 dword;
    uint64 qword;

    float32 singy;
    float64 doublet;

    if (!stConvert(int32, &big, int16, 7) || big != 7)
        TEST_FAILV(ret, 1, _SL("stConvert(int32<-int16, 7): big=${int} (want 7)"), stvar(int32, big));

    if (!stConvert(int16, &med, uint64, 102) || med != 102)
        TEST_FAILV(ret, 1, _SL("stConvert(int16<-uint64, 102): med=${int} (want 102)"), stvar(int32, med));

    if (!stConvert(uint8, &byte, int32, 77) || byte != 77)
        TEST_FAILV(ret, 1, _SL("stConvert(uint8<-int32, 77): byte=${uint} (want 77)"), stvar(uint32, byte));

    if (!stConvert(float64, &doublet, uint16, 1023) || doublet != 1023)
        TEST_FAILV(ret, 1, _SL("stConvert(float64<-uint16, 1023): doublet=${float} (want 1023)"), stvar(float64, doublet));

    if (!stConvert(uint64, &qword, float32, 9) || qword != 9)
        TEST_FAILV(ret, 1, _SL("stConvert(uint64<-float32, 9): qword=${uint} (want 9)"), stvar(uint64, qword));

    if (!stConvert(int64, &huge, uint64, 0x7fffffffffffffffLL) || huge != 0x7fffffffffffffffLL)
        TEST_FAILV(ret, 1, _SL("stConvert(int64<-uint64, INT64_MAX): huge=${int} (want INT64_MAX)"), stvar(int64, huge));

    if (!stConvert(uint16, &word, int32, 65535) || word != 65535)
        TEST_FAILV(ret, 1, _SL("stConvert(uint16<-int32, 65535): word=${uint} (want 65535)"), stvar(uint32, word));

    // these should all fail
    if (stConvert(uint32, &dword, int16, -5))
        TEST_FAILV(ret, 1, _SL("stConvert(uint32<-int16, -5) unexpectedly succeeded: dword=${uint}"), stvar(uint32, dword));

    if (stConvert(int8, &tiny, uint32, 99999))
        TEST_FAILV(ret, 1, _SL("stConvert(int8<-uint32, 99999) unexpectedly succeeded: tiny=${int}"), stvar(int32, tiny));

    if (stConvert(int16, &med, uint16, 32769))
        TEST_FAILV(ret, 1, _SL("stConvert(int16<-uint16, 32769) unexpectedly succeeded: med=${int}"), stvar(int32, med));

    if (stConvert(float32, &singy, int64, 25000000, ST_Lossless))
        TEST_FAILV(ret, 1, _SL("stConvert(float32<-int64, 25000000, ST_Lossless) unexpectedly succeeded: singy=${float}"), stvar(float64, (float64)singy));

    // now try them again with overflow set
    if (!stConvert(uint32, &dword, int16, -5, ST_Overflow) || dword != 0xfffffffb)
        TEST_FAILV(ret, 1, _SL("stConvert(uint32<-int16, -5, ST_Overflow): dword=${uint} (want 0xfffffffb)"), stvar(uint32, dword));

    if (!stConvert(int8, &tiny, uint32, 99999, ST_Overflow) || tiny != -97)
        TEST_FAILV(ret, 1, _SL("stConvert(int8<-uint32, 99999, ST_Overflow): tiny=${int} (want -97)"), stvar(int32, tiny));

    if (!stConvert(int16, &med, uint16, 32769, ST_Overflow) || med != -32767)
        TEST_FAILV(ret, 1, _SL("stConvert(int16<-uint16, 32769, ST_Overflow): med=${int} (want -32767)"), stvar(int32, med));

    // without ST_Lossless should round to nearest float
    if (!stConvert(float32, &singy, int64, 25000001) || singy != 25000000)
        TEST_FAILV(ret, 1, _SL("stConvert(float32<-int64, 25000001): singy=${float} (want 25000000)"), stvar(float64, (float64)singy));

    // dumb conversion but it should still work
    stvar sv;
    if (!stConvert(stvar, &sv, uint32, 0xfffe1011) || stvarType(&sv)->id != stTypeId(uint32) ||
        sv.data.st_uint32 != 0xfffe1011)
        TEST_FAILV(ret, 1, _SL("stConvert(stvar<-uint32, 0xfffe1011): typeid=${int} val=${uint} (want uint32, 0xfffe1011)"), stvar(int32, (int32)stvarType(&sv)->id), stvar(uint32, sv.data.st_uint32));

    if (!stConvert(int64, &huge, stvar, sv) || huge != 0xfffe1011)
        TEST_FAILV(ret, 1, _SL("stConvert(int64<-stvar): huge=${int} (want 0xfffe1011)"), stvar(int64, huge));

    return ret;
}

static int test_string()
{
    int ret = 0;
    string test1 = 0;
    string test2 = 0;
    int64 i64;
    uint32 u32;
    uint16 u16;
    int8 i8;

    if (!stConvert(string, &test1, int32, 10754) || !strEq(test1, _S"10754"))
        TEST_FAILV(ret, 1, _SL("stConvert(string<-int32, 10754): got '${string}' (want '10754')"), stvar(strref, test1));
    strDestroy(&test1);

    if (!stConvert(string, &test1, float64, 22901.4434) || !strEq(test1, _S"22901.4434"))
        TEST_FAILV(ret, 1, _SL("stConvert(string<-float64, 22901.4434): got '${string}' (want '22901.4434')"), stvar(strref, test1));
    strDestroy(&test1);

    if (!stConvert(string, &test1, stvar, stvar(int16, -301)) || !strEq(test1, _S"-301"))
        TEST_FAILV(ret, 1, _SL("stConvert(string<-stvar(int16,-301)): got '${string}' (want '-301')"), stvar(strref, test1));
    strDestroy(&test1);

    if (!stConvert(int64, &i64, string, _S"203941") || i64 != 203941)
        TEST_FAILV(ret, 1, _SL("stConvert(int64<-string, '203941'): i64=${int} (want 203941)"), stvar(int64, i64));

    if (!stConvert(uint32, &u32, string, _S"0x90102034") || u32 != 0x90102034)
        TEST_FAILV(ret, 1, _SL("stConvert(uint32<-string, '0x90102034'): u32=${uint} (want 0x90102034)"), stvar(uint32, u32));

    if (!stConvert(uint16, &u16, string, _S"65535") || u16 != 65535)
        TEST_FAILV(ret, 1, _SL("stConvert(uint16<-string, '65535'): u16=${uint} (want 65535)"), stvar(uint32, u16));

    if (stConvert(int8, &i8, string, _S"128"))
        TEST_FAILV(ret, 1, _SL("stConvert(int8<-string, '128') unexpectedly succeeded: i8=${int}"), stvar(int32, i8));

    if (!stConvert(int8, &i8, string, _S"128", ST_Overflow) || i8 != -128)
        TEST_FAILV(ret, 1, _SL("stConvert(int8<-string, '128', ST_Overflow): i8=${int} (want -128)"), stvar(int32, i8));

    SUID tsuid;
    tsuid.high = 1;
    tsuid.low = 0;
    if (!stConvert(string, &test1, suid, tsuid) || !strEq(test1, _S"0000000000000g000000000000"))
        TEST_FAILV(ret, 1, _SL("stConvert(string<-suid): got '${string}' (want '0000000000000g000000000000')"), stvar(strref, test1));
    strDestroy(&test1);

    if (!stConvert(suid, &tsuid, string, _S"00000000000000000000000005") || tsuid.high != 0 || tsuid.low != 5)
        TEST_FAILV(ret, 1, _SL("stConvert(suid<-string): high=${uint} low=${uint} (want high=0, low=5)"), stvar(uint64, tsuid.high), stvar(uint64, tsuid.low));

    // converting a string to a string should just copy it
    strCopy(&test1, _S"Test String");
    if (!stConvert(string, &test2, string, test1) || !strEq(test1, test2) ||
        strTestRefCount(test1) != 2 || strTestRefCount(test2) != 2)
        TEST_FAILV(ret, 1, _SL("stConvert(string<-string): test1='${string}' test2='${string}' refs=${int}/${int} (want equal, refcount 2/2)"), stvar(strref, test1), stvar(strref, test2), stvar(int32, strTestRefCount(test1)), stvar(int32, strTestRefCount(test2)));

    strDestroy(&test1);
    strDestroy(&test2);

    // string -> bool accepts three spellings each way; True/False/Yes/No are case-insensitive
    bool b;
    static string truthy[] = { _S"True", _S"true", _S"TRUE", _S"1", _S"Yes", _S"yes" };
    for (size_t i = 0; i < sizeof(truthy) / sizeof(truthy[0]); i++) {
        b = false;
        if (!stConvert(bool, &b, string, truthy[i]) || !b)
            TEST_FAILV(ret, 1, _SL("stConvert(bool<-string, '${string}'): b=${int} (want true)"), stvar(strref, truthy[i]), stvar(int32, (int32)b));
    }

    static string falsy[] = { _S"False", _S"false", _S"FALSE", _S"0", _S"No", _S"no" };
    for (size_t i = 0; i < sizeof(falsy) / sizeof(falsy[0]); i++) {
        b = true;
        if (!stConvert(bool, &b, string, falsy[i]) || b)
            TEST_FAILV(ret, 1, _SL("stConvert(bool<-string, '${string}'): b=${int} (want false)"), stvar(strref, falsy[i]), stvar(int32, (int32)b));
    }

    // anything else is not a bool at all, rather than defaulting one way
    if (stConvert(bool, &b, string, _S"maybe") || stConvert(bool, &b, string, _S"") ||
        stConvert(bool, &b, string, _S"2"))
        TEST_FAILV(ret, 1, _SL("stConvert(bool<-string) unexpectedly succeeded for a non-bool spelling"), stvNone);

    return ret;
}

int test_object()
{
    int ret = 0;

    ConvertTestClass *ctc = converttestclassCreate(17, 220.43, _S"Object Test 1");

    int16 small;
    float32 shorty;
    string strtest = 0;

    if (!stConvert(int16, &small, object, ctc) || small != 17)
        TEST_FAILV(ret, 1, _SL("stConvert(int16<-object): small=${int} (want 17)"), stvar(int32, small));

    if (!stConvert(float32, &shorty, object, ctc) || shorty != 220.43f)
        TEST_FAILV(ret, 1, _SL("stConvert(float32<-object): shorty=${float} (want 220.43)"), stvar(float64, (float64)shorty));

    if (!stConvert(string, &strtest, object, ctc) || !strEq(strtest, _S"Object Test 1"))
        TEST_FAILV(ret, 1, _SL("stConvert(string<-object): got '${string}' (want 'Object Test 1')"), stvar(strref, strtest));

    objRelease(&ctc);
    strDestroy(&strtest);

    return ret;
}

testfunc converttest_functions[] = {
    { "numeric", test_numeric },
    { "string", test_string },
    { "object", test_object },
    { 0, 0 }
};

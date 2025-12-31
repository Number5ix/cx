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

testfunc strtest_funcs[] = {
    { "join",       test_join    },
    { "append",     test_append  },
    { "substr",     test_substr  },
    { "compare",    test_compare },
    { "longstring", test_long    },
    { "rope",       test_rope    },
    { "num",        test_num     },
    { 0,            0            }
};

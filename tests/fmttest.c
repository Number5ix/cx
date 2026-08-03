#include <cx/container.h>
#include <cx/format.h>
#include <cx/string.h>

#include <math.h>

#include "fmttestobj.h"

#define TEST_FILE fmttest
#define TEST_FUNCS fmttest_funcs
#include "common.h"

static int test_string()
{
    string res = 0;
    string in1 = 0, in2 = 0, in3 = 0;

    strNConcat(&in1, _S"Te", _S"st1");
    strNConcat(&in2, _S"Te", _S"st2");
    strNConcat(&in3, _S"Te", _S"st3");

    strFormat(&res, _S"This is a ${string}, ${string}, ${string}",
              stvar(string, in1), stvar(string, in2), stvar(string, in3));

    if (!strEq(res, _S"This is a Test1, Test2, Test3"))
        return 1;

    strFormat(&res, _S"This is a ${string2}, ${string3}, ${string1}",
              stvar(string, in1), stvar(string, in2), stvar(string, in3));

    if (!strEq(res, _S"This is a Test2, Test3, Test1"))
        return 1;

    strFormat(&res, _S"This is a ${string2}, ${string1}, ${string}",
              stvar(string, in1), stvar(string, in2), stvar(string, in3));

    if (!strEq(res, _S"This is a Test2, Test1, Test1"))
        return 1;

    strFormat(&res, _S"${string1}:${string2(8,upper)}:${string3(8,right,lower)}:${string1(center,9)}",
              stvar(string, in1), stvar(string, in2), stvar(string, in3));

    if (!strEq(res, _S"Test1:TEST2   :   test3:  Test1  "))
        return 1;

    strDestroy(&in1);
    strDestroy(&in2);
    strDestroy(&in3);
    strDestroy(&res);

    return 0;
}

static int test_int()
{
    string res = 0;

    strFormat(&res, _S"This is a ${int}, ${uint}, ${int}, ${uint}, ${int}, ${uint} test",
              stvar(int8, 17), stvar(int16, -23102), stvar(int32, 1000000001),
              stvar(uint16, 65535), stvar(uint64, 0xffffffffffffffff), stvar(uint32, 0));

    if (!strEq(res, _S"This is a 17, 65535, -23102, 18446744073709551615, 1000000001, 0 test"))
        return 1;

    strFormat(&res, _S"This is a ${int(7)}:${int(7,left)}:${0int(7)}:${0int(7)}:${-int}:${-int}:${+int(7)}:${int(min:7)}:${int(min:7)} test",
              stvar(int32, 17), stvar(int32, -17), stvar(int32, 17), stvar(int32, -17), stvar(int32, 17), stvar(int32, -17),
              stvar(int32, 17), stvar(int32, 17), stvar(int32, -17));

    if (!strEq(res, _S"This is a      17:-17    :0000017:-000017: 17:-17:    +17:0000017:-0000017 test"))
        return 1;

    strFormat(&res, _S"This is a ${uint(hex)}, ${uint(octal)}, ${uint(binary)}, ${uint(octal,prefix)}, ${uint(prefix,upper,hex)} test",
              stvar(uint32, 0xbeeff00d), stvar(uint32, 0377), stvar(uint32, 273), stvar(uint32, 0755), stvar(uint32, 0xd00daf0e));

    if (!strEq(res, _S"This is a beeff00d, 377, 100010001, 0755, 0xD00DAF0E test"))
        return 1;

    strFormat(&res, _S"This is a ${int(utfchar)}${int(utfchar)}${int(utfchar)} test",
              stvar(int32, 0x306f), stvar(int32, 0x3067), stvar(int32, 0x306a));

    if (!strEq(res, _SU"This is a はでな test"))
        return 1;

    strDestroy(&res);

    return 0;
}

static int test_float()
{
    string res = 0;

    strFormat(&res, _S"This is a ${float}, ${float}, ${float}, ${float}, ${float}, ${float} test",
              stvar(float32, 1.75f), stvar(float64, -1728.023), stvar(float64, 10.83e+22),
              stvar(float32, 65535.f), stvar(float64, 3.1415e-11), stvar(float64, -4.455e-50));

    if (!strEq(res, _S"This is a 1.75, -1728.023, 1.083e+23, 65535, 3.1415e-11, -4.455e-50 test"))
        return 1;

    strFormat(&res, _S"This is a ${float}, ${float}, ${float} test",
              stvar(float32, NAN), stvar(float32, INFINITY), stvar(float64, -INFINITY));

    if (!strEq(res, _S"This is a NaN, Inf, -Inf test"))
        return 1;

    strFormat(&res, _S"This is a ${float(sig:4)}, ${float(sig:4)}, ${float(dec:4)}, ${float(dec:4,zero)}, ${float(dec:4,zero)} test",
              stvar(float64, -173.2134), stvar(float64, 1.67988e+18), stvar(float64, 5.48108), stvar(float64, 5.479969), stvar(float64, -1.1));

    if (!strEq(res, _S"This is a -173.2, 1.68e+18, 5.4811, 5.4800, -1.1000 test"))
        return 1;

    strFormat(&res, _S"This is a ${float(fixed)}, ${float(fixed,zero)}, ${float(fixed,dec:3)}, ${float(fixed,dec:3)}, ${float(fixed,dec:30)} test",
              stvar(float64, 0.0043834582), stvar(float64, 1732000), stvar(float64, 1.7834e+22), stvar(float64, 1.7834e-22), stvar(float64, 1.7834e-22));

    if (!strEq(res, _S"This is a 0.004383, 1732000.000000, 17834000000000000000000, 0, 0.00000000000000000000017834 test"))
        return 1;

    strFormat(&res, _S"This is a ${0float1(10)}, ${0float1(7)}, ${0float1(5)}, ${0float1(4)} test",
              stvar(float64, -1.17384));

    if (!strEq(res, _S"This is a -001.17384, -1.1738, -1.17, -1.2 test"))
        return 1;

    strDestroy(&res);
    return 0;
}

static int test_ptr()
{
    string res = 0;
    void *testptr = (void*)0x1234dead;

    strFormat(&res, _S"This is a ${ptr1(upper)}, ${ptr1(prefix)} test", stvar(ptr, testptr));

    if (!strEq(res, _S"This is a 1234DEAD, 0x1234dead test"))
        return 1;

    testptr = (void*)0x45babe;
    strFormat(&res, _S"This is a ${0ptr(prefix)} test", stvar(ptr, testptr));
#ifdef _64BIT
    if (!strEq(res, _S"This is a 0x000000000045babe test"))
        return 1;
#else
    if (!strEq(res, _S"This is a 0x0045babe test"))
        return 1;
#endif

    strDestroy(&res);
    return 0;
}

static int test_suid()
{
    string res = 0;

    SUID testsuid;
    // not a real SUID, just a test pattern
    testsuid.high = 0x123456789abcdef0;
    testsuid.low = 1;

    strFormat(&res, _S"This is a ${suid} test", stvar(suid, testsuid));

    if (!strEq(res, _S"This is a 0j6hb7h6nwvvr0000000000001 test"))
        return 1;

    strDestroy(&res);
    return 0;
}

static int test_object()
{
    string res = 0;

    FmtTestClass *o1, *o2, *o3, *o4, *o5;

    o1 = fmttestclassCreate(1, _S"Test");
    o2 = fmttestclassCreate(2, _S"Lest");
    o3 = fmttestclassCreate(3, _S"Best");
    o4 = fmttestclassCreate(4, _S"Fest");
    o5 = fmttestclassCreate(5, _S"Behest");

    strFormat(&res, _S"This is a ${object}, ${object}, ${object}, ${object}, ${object} test",
              stvar(object, o1), stvar(object, o2), stvar(object, o3), stvar(object, o4), stvar(object, o5));

    if (!strEq(res, _S"This is a Object(Test:One), Object(Lest:Two), Object(Best:Three), Object(Fest:Four), Object(Behest:Five) test"))
        return 1;

    o1->iv = 5;
    o2->iv = 4;
    o4->iv = 2;
    o5->iv = 1;

    strFormat(&res, _S"This is a ${object}, ${object}, ${object}, ${object}, ${object} test",
              stvar(object, o1), stvar(object, o2), stvar(object, o3), stvar(object, o4), stvar(object, o5));

    if (!strEq(res, _S"This is a Object(Test:Five), Object(Lest:Four), Object(Best:Three), Object(Fest:Two), Object(Behest:One) test"))
        return 1;

    objRelease(&o1);
    objRelease(&o2);
    objRelease(&o3);
    objRelease(&o4);
    objRelease(&o5);
    strDestroy(&res);

    // try it again but using a class that implements Convertible instead of Formattable
    FmtTestClass2 *oo1, *oo2, *oo3, *oo4, *oo5;

    oo1 = fmttestclass2Create(1, _S"Test");
    oo2 = fmttestclass2Create(2, _S"Lest");
    oo3 = fmttestclass2Create(3, _S"Best");
    oo4 = fmttestclass2Create(4, _S"Fest");
    oo5 = fmttestclass2Create(5, _S"Behest");

    strFormat(&res, _S"This is a ${object}, ${object}, ${object}, ${object}, ${object} test",
              stvar(object, oo1), stvar(object, oo2), stvar(object, oo3), stvar(object, oo4), stvar(object, oo5));

    if (!strEq(res, _S"This is a Object(Test:One), Object(Lest:Two), Object(Best:Three), Object(Fest:Four), Object(Behest:Five) test"))
        return 1;

    oo1->iv = 5;
    oo2->iv = 4;
    oo4->iv = 2;
    oo5->iv = 1;

    strFormat(&res, _S"This is a ${object}, ${object}, ${object}, ${object}, ${object} test",
              stvar(object, oo1), stvar(object, oo2), stvar(object, oo3), stvar(object, oo4), stvar(object, oo5));

    if (!strEq(res, _S"This is a Object(Test:Five), Object(Lest:Four), Object(Best:Three), Object(Fest:Two), Object(Behest:One) test"))
        return 1;

    objRelease(&oo1);
    objRelease(&oo2);
    objRelease(&oo3);
    objRelease(&oo4);
    objRelease(&oo5);
    strDestroy(&res);

    return 0;
}

static int test_array()
{
    string res = 0;

    sa_int32 intarray;
    saInit(&intarray, int32, 5);
    sa_string strarray;
    saInit(&strarray, string, 5);

    saPush(&intarray, int32, 32);
    saPush(&intarray, int32, 33);
    saPush(&intarray, int32, 34);
    saPush(&intarray, int32, 35);
    saPush(&intarray, int32, 36);

    saPush(&strarray, string, _S"Test");
    saPush(&strarray, string, _S"Of");
    saPush(&strarray, string, _S"Array");
    saPush(&strarray, string, _S"Formatting");
    saPush(&strarray, string, _S"Awesome");

    strFormat(&res, _S"This is a ${int[4]}, ${int[1]}, ${int[3]}, ${int[0]}, ${int[2]} test",
              stvar(sarray, intarray));

    if (!strEq(res, _S"This is a 36, 33, 35, 32, 34 test"))
        return 1;

    strFormat(&res, _S"This is a ${int[]}, ${int[]}, ${int[]}, ${int[]}, ${int[]} test",
              stvar(sarray, intarray));

    if (!strEq(res, _S"This is a 32, 33, 34, 35, 36 test"))
        return 1;

    strFormat(&res, _S"This is a ${string[0]} ${string[1]} ${string[4]} ${string[2]} ${string[3]}",
              stvar(sarray, strarray));

    if (!strEq(res, _S"This is a Test Of Awesome Array Formatting"))
        return 1;

    strFormat(&res, _S"This is an ${string[4]} ${string[0]} ${string[1]} ${string[0]} ${string[3]}",
              stvar(sarray, intarray), stvar(sarray, strarray));

    if (!strEq(res, _S"This is an Awesome Test Of Test Formatting"))
        return 1;

    // The subscript binds tighter than the formatting applied to the result, so it is
    // written first: index, then pad the element that came out.
    strFormat(&res, _S"[${int[1](6)}][${int[0](6,left)}][${0int[2](5)}]",
              stvar(sarray, intarray));

    if (!strEq(res, _S"[    33][32    ][00034]"))
        return 1;

    // ...and the reverse order is rejected rather than silently accepted
    if (strFormat(&res, _S"${int(6)[1]}", stvar(sarray, intarray)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    // a subscript still combines with a default
    strFormat(&res, _S"${int[9];none}", stvar(sarray, intarray));
    if (!strEq(res, _S"none"))
        return 1;

    saDestroy(&intarray);
    saDestroy(&strarray);

    strDestroy(&res);
    return 0;
}

static int test_hash()
{
    string res = 0;
    hashtable testht;
    htInit(&testht, string, float64, 8);

    htInsert(&testht, string, _S"one", float64, 1);
    htInsert(&testht, string, _S"sqrttwo", float64, 1.41421);
    htInsert(&testht, string, _S"two", float64, 2);
    htInsert(&testht, string, _S"e", float64, 2.71828);
    htInsert(&testht, string, _S"three", float64, 3);
    htInsert(&testht, string, _S"pi", float64, 3.14159);
    htInsert(&testht, string, _S"four", float64, 4);

    strFormat(&res, _S"It's easy as ${float[one]}, ${float[two]}, ${float[three]}",
              stvar(hashtable, testht));

    if (!strEq(res, _S"It's easy as 1, 2, 3"))
        return 1;

    strFormat(&res, _S"It's easy as ${float[four]}, ${float[pi]}, ${float[e]}?",
              stvar(hashtable, testht));

    if (!strEq(res, _S"It's easy as 4, 3.14159, 2.71828?"))
        return 1;

    strFormat(&res, _S"sqrt(${float[two]}) = ${float[sqrttwo]}",
              stvar(hashtable, testht));

    if (!strEq(res, _S"sqrt(2) = 1.41421"))
        return 1;

    htDestroy(&testht);

    // A numeric-looking hashtable key would otherwise parse as an array index, so it has
    // to be backtick-escaped -- the same escape character the rest of the grammar uses.
    hashtable numht;
    htInit(&numht, string, int32, 8);
    htInsert(&numht, string, _S"0", int32, 111);
    htInsert(&numht, string, _S"1", int32, 222);

    strFormat(&res, _S"${int[`0]} ${int[`1]}", stvar(hashtable, numht));
    if (!strEq(res, _S"111 222"))
        return 1;

    // The array-vs-hashtable decision is syntactic, not driven by which arguments are
    // present: with both an sarray and a hashtable in the list, an unescaped number is
    // still an array index and an escaped one is still a hashtable key.
    sa_int32 nums;
    saInit(&nums, int32, 4);
    saPush(&nums, int32, 777);
    saPush(&nums, int32, 888);

    strFormat(&res, _S"${int[1]} ${int[`1]}", stvar(sarray, nums), stvar(hashtable, numht));
    if (!strEq(res, _S"888 222"))
        return 1;

    // ...and the same format string keeps its meaning when the sarray is absent, rather
    // than silently falling back to a hashtable lookup for "1"
    if (strFormat(&res, _S"${int[1]}", stvar(hashtable, numht)) != false)
        return 1;

    saDestroy(&nums);
    htDestroy(&numht);
    strDestroy(&res);
    return 0;
}

static int test_default()
{
    string res = 0;

    strFormat(&res, _S"This is a ${int;55} ${string;unused} ${string;default} ${int;0} test",
              stvar(int32, 1702), stvar(string, _S"specified"));

    if (!strEq(res, _S"This is a 1702 specified default 0 test"))
        return 1;

    strDestroy(&res);
    return 0;
}

static int test_error()
{
    string res = 0;

    strDup(&res, _S"canary");

    if (strFormat(&res, _S"This ${int} ${int} ${int} test should fail",
        stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    if (strFormat(&res, _S"This ${int[0]} test should fail",
                  stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    if (strFormat(&res, _S"This ${string} test should fail",
                  stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    if (strFormat(&res, _S"This ${int[hash]} test should fail",
                  stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    if (strFormat(&res, _S"This ${int test should fail",
                  stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    if (strFormat(&res, _S"This ${int(asdf} test should fail",
                  stvar(int32, 5), stvar(int32, 10)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    strDestroy(&res);
    return 0;
}

static int test_keyed()
{
    string res  = 0;
    string host = 0;

    strNConcat(&host, _S"web", _S"01");

    // basic keyed lookup, both typed and typeless
    strFormat(&res, _S"${string:host} took ${int:ms}ms",
              stvark(host, string, host), stvark(ms, int32, 250));
    if (!strEq(res, _S"web01 took 250ms"))
        return 1;

    strFormat(&res, _S"${:host} took ${:ms}ms",
              stvark(host, string, host), stvark(ms, int32, 250));
    if (!strEq(res, _S"web01 took 250ms"))
        return 1;

    // order-independence: keys resolve regardless of argument position, and repeating a
    // key resolves to the same argument every time
    strFormat(&res, _S"${:ms} ${:host} ${:ms}",
              stvark(host, string, host), stvark(ms, int32, 250));
    if (!strEq(res, _S"250 web01 250"))
        return 1;

    // keys compose with format options and defaults
    strFormat(&res, _S"[${string:host(8,left)}][${0int:ms(6)}][${string:nope;fallback}]",
              stvark(host, string, host), stvark(ms, int32, 250));
    if (!strEq(res, _S"[web01   ][000250][fallback]"))
        return 1;

    // Keyed and positional placeholders interleave without disturbing each other. The
    // keyed argument sits in the middle of three positional ints and must be invisible to
    // all of them -- if it were consumed positionally, the trailing ${int}s would shift.
    strFormat(&res, _S"${int} ${int:ms} ${int} ${int}",
              stvar(int32, 1), stvark(ms, int32, 250), stvar(int32, 2), stvar(int32, 3));
    if (!strEq(res, _S"1 250 2 3"))
        return 1;

    // ...which is the same thing stated from the other side: an unkeyed placeholder never
    // matches a keyed argument, so this resolves to the one unkeyed int
    strFormat(&res, _S"${int}", stvark(ms, int32, 7), stvar(int32, 8));
    if (!strEq(res, _S"8"))
        return 1;

    // and with no unkeyed argument to find, it fails rather than silently taking the
    // keyed one
    if (strFormat(&res, _S"${int}", stvark(ms, int32, 7)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    // a missing key, or a key whose argument is the wrong type, fails like any other
    // unresolvable variable
    if (strFormat(&res, _S"${string:missing}", stvark(ms, int32, 250)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;
    if (strFormat(&res, _S"${string:ms}", stvark(ms, int32, 250)) != false)
        return 1;
    if (!strEmpty(res))
        return 1;

    // an empty key is a parse error, not a match against unkeyed arguments
    if (strFormat(&res, _S"${string:}", stvar(string, host)) != false)
        return 1;

    strDestroy(&host);
    strDestroy(&res);
    return 0;
}

static int test_keyed_subscript()
{
    string res = 0;

    sa_int32 sizes;
    saInit(&sizes, int32, 4);
    saPush(&sizes, int32, 10);
    saPush(&sizes, int32, 20);
    saPush(&sizes, int32, 30);

    sa_int32 other;
    saInit(&other, int32, 4);
    saPush(&other, int32, 77);

    hashtable hdrs;
    htInit(&hdrs, string, string, 8);
    htInsert(&hdrs, string, _S"accept", string, _S"text/html");
    htInsert(&hdrs, string, _S"agent", string, _S"cx/1");

    // a keyed array subscripts like a positional one
    strFormat(&res, _S"${int:sizes[0]} ${int:sizes[2]}", stvark(sizes, sarray, sizes));
    if (!strEq(res, _S"10 30"))
        return 1;

    // a keyed hashtable likewise, using the bracket form
    strFormat(&res, _S"${string:hdrs[agent]}", stvark(hdrs, hashtable, hdrs));
    if (!strEq(res, _S"cx/1"))
        return 1;

    // the key selects *which* container, so two same-typed arrays stay distinguishable --
    // this is the whole point, and is not expressible positionally
    strFormat(&res, _S"${int:sizes[1]} ${int:other[0]}",
              stvark(sizes, sarray, sizes), stvark(other, sarray, other));
    if (!strEq(res, _S"20 77"))
        return 1;

    // typeless keyed subscripting takes its type from the element/value type
    strFormat(&res, _S"${:sizes[1]} ${:hdrs[accept]}",
              stvark(sizes, sarray, sizes), stvark(hdrs, hashtable, hdrs));
    if (!strEq(res, _S"20 text/html"))
        return 1;

    // and it still composes with format options and defaults
    strFormat(&res, _S"[${int:sizes[2](6)}][${string:hdrs[nope];none}]",
              stvark(sizes, sarray, sizes), stvark(hdrs, hashtable, hdrs));
    if (!strEq(res, _S"[    30][none]"))
        return 1;

    // out-of-range index, missing hash key, and wrong container kind all fail rather than
    // silently falling back to the unsubscripted argument
    if (strFormat(&res, _S"${int:sizes[9]}", stvark(sizes, sarray, sizes)) != false)
        return 1;
    if (strFormat(&res, _S"${string:hdrs[missing]}", stvark(hdrs, hashtable, hdrs)) != false)
        return 1;
    if (strFormat(&res, _S"${int:sizes[key]}", stvark(sizes, sarray, sizes)) != false)
        return 1;

    // a keyed container is still invisible to positional container placeholders
    if (strFormat(&res, _S"${int[0]}", stvark(sizes, sarray, sizes)) != false)
        return 1;

    saDestroy(&sizes);
    saDestroy(&other);
    htDestroy(&hdrs);
    strDestroy(&res);
    return 0;
}

static int test_keyed_stvl()
{
    stvlist list;
    stvar args[] = { stvar(string, _S"positional"),
                     stvark(timeout, int32, 250),
                     stvark(label, string, _S"tagged") };
    stvlInit(&list, 3, args);

    // keyed lookup finds regardless of order and does not move the cursor
    int32 timeout = 0;
    if (!stvlFind(list, timeout, int32, &timeout) || timeout != 250)
        return 1;

    string label = 0;
    if (!stvlFind(list, label, string, &label) || !strEq(label, _S"tagged"))
        return 1;

    if (!stvlHasKey(list, timeout) || stvlHasKey(list, nosuchkey))
        return 1;

    // wrong type for an existing key does not match
    string wrong = 0;
    if (stvlFind(list, timeout, string, &wrong))
        return 1;

    // the cursor is untouched by all of the above, so positional walking still starts at
    // the beginning of the list
    string first = 0;
    if (!stvlNext(&list, string, &first) || !strEq(first, _S"positional"))
        return 1;

    // ...and positional walking skips keyed variants entirely, so the keyed string is not
    // reachable this way and the walk is now exhausted
    string second = 0;
    if (stvlNext(&list, string, &second))
        return 1;

    return 0;
}

static int test_keyed_copy()
{
    // names survive stvarCopy (pointer-copied), and are cleared by destroy
    stvar src = stvark(host, string, _S"web01");
    stvar dst;
    stvarCopy(&dst, src);

    if (!stvarName(&dst) || !strEq((strref)stvarName(&dst), _S"host"))
        return 1;
    if (!stvarIs(&dst, string) || !strEq(stvarString(&dst), _S"web01"))
        return 1;

    stvarDestroy(&dst);
    if (stvarName(&dst) != NULL)
        return 1;

    // a key is metadata: it must not affect compare or hash, or attaching one would
    // change how a variant behaves as a container element
    stvar keyed   = stvark(host, int32, 42);
    stvar unkeyed = stvar(int32, 42);
    stvar other   = stvark(elsewhere, int32, 42);

    if (stCmp(stvar, keyed, unkeyed, 0) != 0 || stCmp(stvar, keyed, other, 0) != 0)
        return 1;
    if (stHash(stvar, keyed, 0) != stHash(stvar, unkeyed, 0))
        return 1;

    // stvarSet clears a stale key; stvarSetK replaces value and key together
    stvar v = stvNone;
    stvarSetK(&v, first, int32, 1);
    if (!stvarName(&v) || !strEq((strref)stvarName(&v), _S"first"))
        return 1;
    stvarSet(&v, int32, 2);
    if (stvarName(&v) != NULL)
        return 1;
    stvarSetK(&v, second, string, _S"x");
    if (!stvarName(&v) || !strEq((strref)stvarName(&v), _S"second"))
        return 1;
    stvarDestroy(&v);

    return 0;
}

testfunc fmttest_funcs[] = {
    { "int", test_int },
    { "float", test_float },
    { "string", test_string },
    { "ptr", test_ptr },
    { "suid", test_suid },
    { "object", test_object },
    { "array", test_array },
    { "hash", test_hash },
    { "keyed", test_keyed },
    { "keyedsub", test_keyed_subscript },
    { "keyedstvl", test_keyed_stvl },
    { "keyedcopy", test_keyed_copy },
    { "default", test_default },
    { "error", test_error },
    { 0, 0 }
};

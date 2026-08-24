#include <cx/container.h>
#include <cx/parse.h>
#include <cx/string.h>

#include "parsetestobj.h"

#define TEST_FILE parsetest
#define TEST_FUNCS parsetest_funcs
#include "common.h"

static int test_basic()
{
    int ret       = 0;
    string method = 0, target = 0;
    uint8 minor   = 0;

    // keyed placeholders, bound by name in any order
    if (!strParse(_S"GET /index.html HTTP/1.1",
                  _S"${string:m} ${string:t} HTTP/1.${uint:v}",
                  stvpk(v, uint8, &minor),
                  stvpk(m, string, &method),
                  stvpk(t, string, &target)) ||
        !strEq(method, _S"GET") || !strEq(target, _S"/index.html") || minor != 1)
        ret = 1;

    // unkeyed placeholders, bound in order
    uint32 a = 0, b = 0;
    if (!strParse(_S"10,20", _S"${uint},${uint}", stvp(uint32, &a), stvp(uint32, &b)) || a != 10 ||
        b != 20)
        ret = 1;

    // a compiled pattern can be matched over and over
    StrPattern* p = strPatternCreate(_S"${uint:x}-${uint:y}");
    if (!p)
        return 1;

    uint32 x = 0, y = 0;
    if (!strPatternMatch(p, _S"3-4", stvpk(x, uint32, &x), stvpk(y, uint32, &y)) || x != 3 ||
        y != 4)
        ret = 1;
    if (!strPatternMatch(p, _S"70-80", stvpk(x, uint32, &x), stvpk(y, uint32, &y)) || x != 70 ||
        y != 80)
        ret = 1;
    strPatternDestroy(&p);
    if (p)
        ret = 1;

    // the whole string has to match
    if (strParse(_S"3-4 extra", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &x)))
        ret = 1;
    if (strParse(_S"3-", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &x)))
        ret = 1;

    // a literal space matches any run of whitespace, unless the pattern says otherwise
    string w1 = 0, w2 = 0;
    if (!strParse(_S"one \t  two", _S"${string:a} ${string:b}", stvpk(a, string, &w1),
                  stvpk(b, string, &w2)) ||
        !strEq(w1, _S"one") || !strEq(w2, _S"two"))
        ret = 1;

    StrPattern* pex = strPatternCreate(_S"a ${uint:v}", STRPAT_ExactWS);
    if (!pex)
        return 1;
    uint32 exv = 0;
    if (!strPatternMatch(pex, _S"a 1", stvpk(v, uint32, &exv)) || exv != 1)
        ret = 1;
    if (strPatternMatch(pex, _S"a  1", stvpk(v, uint32, &exv)))
        ret = 1;
    strPatternDestroy(&pex);

    // ...where the default is to accept any run of spacing
    StrPattern* pws = strPatternCreate(_S"a ${uint:v}");
    if (!pws)
        return 1;
    if (!strPatternMatch(pws, _S"a  \t 1", stvpk(v, uint32, &exv)) || exv != 1)
        ret = 1;
    strPatternDestroy(&pws);

    // case-insensitive literals
    StrPattern* pci = strPatternCreate(_S"HTTP/${uint:v}", STRPAT_CaseI);
    if (!pci)
        return 1;
    if (!strPatternMatch(pci, _S"http/1", stvpk(v, uint8, &minor)) || minor != 1)
        ret = 1;
    strPatternDestroy(&pci);

    // backticks escape the metacharacters
    string inner = 0;
    if (!strParse(_S"(x)", _S"`(${string:i}`)", stvpk(i, string, &inner)) ||
        !strEq(inner, _S"x"))
        ret = 1;
    // a pattern with nothing to bind still needs an argument, which is what stvpNone is for
    if (!strParse(_S"${}", _S"`${`}", stvpNone))
        ret = 1;
    if (strParse(_S"other", _S"`${`}", stvpNone))
        ret = 1;

    StrPattern* pesc = strPatternCreate(_S"a`|b");
    if (!pesc)
        ret = 1;
    if (!strPatternMatch(pesc, _S"a|b", stvpNone))
        ret = 1;
    strPatternDestroy(&pesc);

    strDestroy(&method);
    strDestroy(&target);
    strDestroy(&w1);
    strDestroy(&w2);
    strDestroy(&inner);
    return ret;
}

static int test_types()
{
    int ret = 0;

    int32 i = 0;
    if (!strParse(_S"-42", _S"${int:v}", stvpk(v, int32, &i)) || i != -42)
        ret = 1;

    // uint refuses a sign, which is the whole difference between the two
    if (strParse(_S"-42", _S"${uint:v}", stvpk(v, int32, &i)))
        ret = 1;

    float64 f = 0;
    if (!strParse(_S"3.5e2", _S"${float:v}", stvpk(v, float64, &f)) || f != 350.0)
        ret = 1;
    if (!strParse(_S"-0.25", _S"${float:v}", stvpk(v, float64, &f)) || f != -0.25)
        ret = 1;

    bool bl = false;
    if (!strParse(_S"YES", _S"${bool:v}", stvpk(v, bool, &bl)) || !bl)
        ret = 1;
    if (!strParse(_S"0", _S"${bool:v}", stvpk(v, bool, &bl)) || bl)
        ret = 1;
    if (strParse(_S"perhaps", _S"${bool:v}", stvpk(v, bool, &bl)))
        ret = 1;

    // the placeholder type says how to match; the destination says what to store into
    string s = 0;
    if (!strParse(_S"1234", _S"${uint:v}", stvpk(v, string, &s)) || !strEq(s, _S"1234"))
        ret = 1;

    uint16 u16 = 0;
    if (!strParse(_S"65535", _S"${uint:v}", stvpk(v, uint16, &u16)) || u16 != 65535)
        ret = 1;
    // matched fine, but will not fit where it was asked to go
    if (strParse(_S"65536", _S"${uint:v}", stvpk(v, uint16, &u16)) || u16 != 65535)
        ret = 1;

    // any type the conversion system can build from a string works as a destination
    SUID id = { 0 };
    if (!strParse(_S"id=00000000000000000000000005", _S"id=${string:v}", stvpk(v, suid, &id)) ||
        id.high != 0 || id.low != 5)
        ret = 1;

    // numbers are matched strictly: no leading space, no 0x
    if (strParse(_S" 5", _S"${uint:v}", stvpk(v, int32, &i)))
        ret = 1;
    if (!strParse(_S" 5", _S"${uint:v(ws)}", stvpk(v, int32, &i)) || i != 5)
        ret = 1;
    if (strParse(_S"0x10", _S"${uint:v}", stvpk(v, int32, &i)))
        ret = 1;

    strDestroy(&s);
    return ret;
}

static int test_opts()
{
    int ret  = 0;
    uint32 u = 0;
    string s = 0;

    // bases
    if (!strParse(_S"ff", _S"${uint:v(hex)}", stvpk(v, uint32, &u)) || u != 255)
        ret = 1;
    if (!strParse(_S"777", _S"${uint:v(octal)}", stvpk(v, uint32, &u)) || u != 511)
        ret = 1;
    if (!strParse(_S"1011", _S"${uint:v(binary)}", stvpk(v, uint32, &u)) || u != 11)
        ret = 1;
    if (!strParse(_S"zz", _S"${uint:v(base:36)}", stvpk(v, uint32, &u)) || u != 1295)
        ret = 1;
    // hex means hex digits, not the "0x" spelling
    if (strParse(_S"0xff", _S"${uint:v(hex)}", stvpk(v, uint32, &u)))
        ret = 1;

    // digit counts
    if (!strParse(_S"06", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)) || u != 6)
        ret = 1;
    if (strParse(_S"6", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)))
        ret = 1;
    if (strParse(_S"123", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)))
        ret = 1;
    if (!strParse(_S"12x", _S"${uint:v(maxdigits:2)}x", stvpk(v, uint32, &u)) || u != 12)
        ret = 1;

    // range checks happen while matching, so they can fail a branch
    if (!strParse(_S"12", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)) || u != 12)
        ret = 1;
    if (strParse(_S"21", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)))
        ret = 1;
    if (strParse(_S"9", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)))
        ret = 1;

    // float
    float64 f = 0;
    if (strParse(_S"1e3", _S"${float:v(fixed)}", stvpk(v, float64, &f)))
        ret = 1;
    if (!strParse(_S"1.5", _S"${float:v(fixed)}", stvpk(v, float64, &f)) || f != 1.5)
        ret = 1;

    // string shapes
    if (!strParse(_S"abcdef", _S"${string:v(len:3)}def", stvpk(v, string, &s)) ||
        !strEq(s, _S"abc"))
        ret = 1;
    if (!strParse(_S"key=value;x", _S"key=${string:v(until:;)};x", stvpk(v, string, &s)) ||
        !strEq(s, _S"value"))
        ret = 1;
    if (!strParse(_S"12ab", _S"${string:v(chars:0123456789)}ab", stvpk(v, string, &s)) ||
        !strEq(s, _S"12"))
        ret = 1;
    if (!strParse(_S"12ab", _S"${string:v(notchars:ab)}ab", stvpk(v, string, &s)) ||
        !strEq(s, _S"12"))
        ret = 1;
    if (!strParse(_S"one two", _S"${string:v(word)} two", stvpk(v, string, &s)) ||
        !strEq(s, _S"one"))
        ret = 1;
    if (!strParse(_S"a=b c d", _S"a=${string:v(rest)}", stvpk(v, string, &s)) ||
        !strEq(s, _S"b c d"))
        ret = 1;
    if (!strParse(_S"\"a\\\"b\" x", _S"${string:v(quoted)} x", stvpk(v, string, &s)) ||
        !strEq(s, _S"a\"b"))
        ret = 1;
    if (!strParse(_S"[  pad  ]", _S"[${string:v(trim)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"pad"))
        ret = 1;
    if (!strParse(_S"[abc]", _S"[${string:v(upper)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"ABC"))
        ret = 1;
    if (!strParse(_S"[ABC]", _S"[${string:v(lower)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"abc"))
        ret = 1;

    // skip: matched, never bound, and does not consume a positional slot either
    uint32 p1 = 0;
    if (!strParse(_S"7,8", _S"${uint(skip)},${uint}", stvp(uint32, &p1)) || p1 != 8)
        ret = 1;

    strDestroy(&s);
    return ret;
}

static int test_group()
{
    int ret     = 0;
    string host = 0;
    uint16 port = 443;
    bool hasport = true;

    // an optional group that is not there leaves its fields alone
    if (!strParse(_S"example.com",
                  _S"${string:host}(:${uint:port})?:hasport",
                  stvpk(host, string, &host),
                  stvpk(port, uint16, &port),
                  stvpk(hasport, bool, &hasport)) ||
        !strEq(host, _S"example.com") || port != 443 || hasport)
        ret = 1;

    port = 443;
    if (!strParse(_S"example.com:8080",
                  _S"${string:host}(:${uint:port})?:hasport",
                  stvpk(host, string, &host),
                  stvpk(port, uint16, &port),
                  stvpk(hasport, bool, &hasport)) ||
        !strEq(host, _S"example.com") || port != 8080 || !hasport)
        ret = 1;

    // alternation, with an integer group key saying which branch it was
    uint32 day = 0;
    string mon = 0;
    uint8 form = 0;

    STR_PATTERN(kDate, "(${uint:d1} ${string:m1}|${string:m2} ${uint:d2}):form");

    if (!strPatternMatch(strPat(kDate),
                         _S"06 Nov",
                         stvpk(d1, uint32, &day),
                         stvpk(m1, string, &mon),
                         stvpk(d2, uint32, &day),
                         stvpk(m2, string, &mon),
                         stvpk(form, uint8, &form)) ||
        day != 6 || !strEq(mon, _S"Nov") || form != 1)
        ret = 1;

    if (!strPatternMatch(strPat(kDate),
                         _S"Nov 6",
                         stvpk(d1, uint32, &day),
                         stvpk(m1, string, &mon),
                         stvpk(d2, uint32, &day),
                         stvpk(m2, string, &mon),
                         stvpk(form, uint8, &form)) ||
        day != 6 || !strEq(mon, _S"Nov") || form != 2)
        ret = 1;

    // the same declaration compiles once and keeps working
    if (strPat(kDate) != strPat(kDate))
        ret = 1;

    // a group with no key still works, it just cannot be observed directly
    uint32 v1 = 0, v2 = 9;
    if (!strParse(_S"1", _S"${uint:a}(.${uint:b})?", stvpk(a, uint32, &v1),
                  stvpk(b, uint32, &v2)) ||
        v1 != 1 || v2 != 9)
        ret = 1;

    // one key shared by every alternative: only the branch that matched produces a value,
    // so one destination serves them all
    STR_PATTERN(kShared, "(${uint:d} ${string:m}|${string:m} ${uint:d}):which");

    uint32 sday  = 0;
    string smon  = 0;
    uint8 which  = 0;
    if (!strPatternMatch(strPat(kShared), _S"06 Nov", stvpk(d, uint32, &sday),
                         stvpk(m, string, &smon), stvpk(which, uint8, &which)) ||
        sday != 6 || !strEq(smon, _S"Nov") || which != 1)
        ret = 1;

    sday = 0;
    if (!strPatternMatch(strPat(kShared), _S"Nov 6", stvpk(d, uint32, &sday),
                         stvpk(m, string, &smon), stvpk(which, uint8, &which)) ||
        sday != 6 || !strEq(smon, _S"Nov") || which != 2)
        ret = 1;

    // a shared key in a branch that does not match writes nothing, so a pre-set destination
    // survives
    STR_PATTERN(kShareOpt, "x(${uint:v}|y${uint:v})?");
    uint32 sv = 77;
    if (!strPatternMatch(strPat(kShareOpt), _S"x", stvpk(v, uint32, &sv)) || sv != 77)
        ret = 1;
    if (!strPatternMatch(strPat(kShareOpt), _S"xy5", stvpk(v, uint32, &sv)) || sv != 5)
        ret = 1;

    strDestroy(&smon);

    // nested groups
    string sch = 0;
    if (!strParse(_S"https://h/p",
                  _S"(${string:sch}://)?${string:h}(/${string:pth})?",
                  stvpk(sch, string, &sch),
                  stvpk(h, string, &host),
                  stvpk(pth, string, &mon)) ||
        !strEq(sch, _S"https") || !strEq(host, _S"h") || !strEq(mon, _S"p"))
        ret = 1;

    strDestroy(&host);
    strDestroy(&mon);
    strDestroy(&sch);
    return ret;
}

static int test_default()
{
    int ret  = 0;
    uint32 x = 99, y = 99;

    // the field's text is missing but its literals are not
    if (!strParse(_S"a=,b=5", _S"a=${uint:x;0},b=${uint:y}", stvpk(x, uint32, &x),
                  stvpk(y, uint32, &y)) ||
        x != 0 || y != 5)
        ret = 1;

    if (!strParse(_S"a=7,b=5", _S"a=${uint:x;0},b=${uint:y}", stvpk(x, uint32, &x),
                  stvpk(y, uint32, &y)) ||
        x != 7 || y != 5)
        ret = 1;

    // a string default fires when the matched text would have been empty
    string s = 0;
    if (!strParse(_S"[]", _S"[${string:v;none}]", stvpk(v, string, &s)) || !strEq(s, _S"none"))
        ret = 1;
    if (!strParse(_S"[x]", _S"[${string:v;none}]", stvpk(v, string, &s)) || !strEq(s, _S"x"))
        ret = 1;

    // the surrounding literals still have to match
    if (strParse(_S"a=,c=5", _S"a=${uint:x;0},b=${uint:y}", stvpk(x, uint32, &x)))
        ret = 1;

    // a `} in a default is a literal brace
    if (!strParse(_S"[]", _S"[${string:v;a`}b}]", stvpk(v, string, &s)) || !strEq(s, _S"a}b"))
        ret = 1;

    strDestroy(&s);
    return ret;
}

static int test_untouched()
{
    int ret  = 0;
    uint32 a = 11, b = 22;
    string s = 0;
    strDup(&s, _S"original");

    // a failed match writes nothing at all, even for the fields that did match
    if (strParse(_S"1,2,nope", _S"${uint:a},${uint:b},${uint:c}", stvpk(a, uint32, &a),
                 stvpk(b, uint32, &b)))
        ret = 1;
    if (a != 11 || b != 22)
        ret = 1;

    if (strParse(_S"hello", _S"${string:s} ${uint:n}", stvpk(s, string, &s)))
        ret = 1;
    if (!strEq(s, _S"original"))
        ret = 1;

    // a value that matched but cannot be stored is also a failed match
    uint8 small = 5;
    if (strParse(_S"9999", _S"${uint:v}", stvpk(v, uint8, &small)) || small != 5)
        ret = 1;

    // ...and the destination it could not go into is untouched along with all the others
    bool flag  = true;
    uint32 ok1 = 0;
    if (strParse(_S"1,perhaps", _S"${uint:n},${string:b}", stvpk(n, uint32, &ok1),
                 stvpk(b, bool, &flag)) ||
        ok1 != 0 || !flag)
        ret = 1;

    // a skipped optional group leaves its fields exactly as the caller left them
    uint32 port = 8080;
    string host = 0;
    if (!strParse(_S"h", _S"${string:host}(:${uint:port})?", stvpk(host, string, &host),
                  stvpk(port, uint32, &port)) ||
        port != 8080 || !strEq(host, _S"h"))
        ret = 1;

    // and so does a key the pattern never mentions binding to
    uint32 unused = 3;
    if (!strParse(_S"5", _S"${uint:v}", stvpk(v, uint32, &a), stvpk(v, uint32, &unused)) ||
        a != 5 || unused != 5)
        ret = 1;

    // a pattern key with nothing bound to it is fine
    if (!strParse(_S"1-2", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &a)) || a != 1)
        ret = 1;

    strDestroy(&s);
    strDestroy(&host);
    return ret;
}

static int test_matchat()
{
    int ret = 0;

    StrPattern* p = strPatternCreate(_S"${string:k}=${string:v};");
    if (!p)
        return 1;

    strref list = _S"a=1;bb=22;ccc=333;";
    int32 pos   = 0;
    string k = 0, v = 0;
    int n       = 0;

    while (pos < (int32)strLen(list)) {
        if (!strPatternMatchAt(&pos, p, list, stvpk(k, string, &k), stvpk(v, string, &v)))
            break;
        n++;
        if (n == 2 && (!strEq(k, _S"bb") || !strEq(v, _S"22")))
            ret = 1;
    }

    if (n != 3 || pos != (int32)strLen(list))
        ret = 1;

    // a failed match reports how far the best attempt got
    pos = 0;
    if (strPatternMatchAt(&pos, p, _S"a=1", stvpk(k, string, &k), stvpk(v, string, &v)))
        ret = 1;
    if (pos <= 0)
        ret = 1;

    strPatternDestroy(&p);
    strDestroy(&k);
    strDestroy(&v);
    return ret;
}

static int test_object()
{
    int ret = 0;

    ParseTestClass* o = parsetestclassCreate();

    if (!strParse(_S"[thing:42]", _S"[${object:v}]", stvpk(v, object, &o)) ||
        !strEq(o->sv, _S"thing") || o->iv != 42)
        ret = 1;

    // options the compiler has no meaning for reach the object verbatim
    if (!strParse(_S"[other:two]", _S"[${object:v(words)}]", stvpk(v, object, &o)) ||
        !strEq(o->sv, _S"other") || o->iv != 2)
        ret = 1;

    // an object that refuses the text fails the match
    if (strParse(_S"[nocolon]", _S"[${object:v}]", stvpk(v, object, &o)))
        ret = 1;

    objRelease(&o);
    return ret;
}

static int test_error()
{
    int ret = 0;
    StrPattern* p;

    static strref bad[] = {
        _S"${uint:x",               // no closing brace
        _S"${uint:x(hex}",          // no closing paren on the options
        _S"(${uint:x}",             // no closing paren on the group
        _S"${uint:x})",             // a metacharacter with no group to belong to
        _S"a|b",                    // same
        _S"(literal)",              // a group that cannot affect anything
        _S"(${uint})?",             // an unkeyed placeholder inside a group
        _S"(${uint:x;0})?",         // a default inside a group
        _S"${uint:port;https}",     // a default that is not a uint
        _S"${float:f;abc}",         // same for a float
        _S"${bool:b;perhaps}",      // and for a bool
        _S"${uint:a}-${uint:a}",    // duplicate key
        _S"${uint:a}(x)?:a",        // duplicate key, group against placeholder
        _S"(${uint:a}-${uint:a}|b)",   // duplicate key within one alternative
        _S"${uint:a}(-${uint:a}|b)",   // duplicate key, outer against a branch
        _S"((${uint:a}|c) ${uint:a}|b)",   // duplicate key, nested group against its parent
        _S"${nosuchtype:a}",        // no such placeholder type
        _S"${string2:a}",           // instance numbers are a formatter idea, not a parser one
        _S"${uint:a(nosuchopt)}",   // unknown option
        _S"${uint:a(len:3)}",       // an option that belongs to another type
        _S"${string:a(base:16)}",   // same
        _S"${uint:a(base:99)}",     // base out of range
        _S"${string:}",             // empty key
        _S"(${uint:a}):",           // empty group key
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        p = strPatternCreate(bad[i]);
        if (p) {
            printf("pattern %d should not have compiled\n", (int)i);
            strPatternDestroy(&p);
            ret = 1;
        }
    }

    // escaping turns the rejected cases back into ordinary text
    static strref good[] = {
        _S"`(none`)",
        _S"a`|b",
        _S"`${not a placeholder`}",
        _S"(${uint:a})?",
        _S"${uint:a;0}",
    };

    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        p = strPatternCreate(good[i]);
        if (!p) {
            printf("pattern %d should have compiled\n", (int)i);
            ret = 1;
        }
        strPatternDestroy(&p);
    }

    if (!strParse(_S"(none)", _S"`(none`)", stvpNone))
        ret = 1;

    // a destination whose key is not in the pattern is a typo, and fails
    uint32 v = 3;
    if (strParse(_S"5", _S"${uint:known}", stvpk(unknown, uint32, &v)) || v != 3)
        ret = 1;

    // so is asking for more positional values than the pattern has
    uint32 w = 0;
    if (strParse(_S"5", _S"${uint}", stvp(uint32, &v), stvp(uint32, &w)))
        ret = 1;

    // a pattern that will not compile makes every match against it fail rather than crash
    if (strPatternMatch(NULL, _S"x", stvp(uint32, &v)))
        ret = 1;

    // Nested groups multiply the number of ways a pattern can be tried, so a deep enough
    // one has to give up rather than run forever. This is the shape that would hang.
    StrPattern* pdeep = strPatternCreate(
        _S"(${string:a}|)?(${string:b}|)?(${string:c}|)?(${string:d}|)?"
        "(${string:e}|)?(${string:f}|)?(${string:g}|)?(${string:h}|)?X");
    if (!pdeep)
        return 1;

    string junk = 0;
    // no 'X' anywhere, so every combination gets tried and every one of them fails
    if (strPatternMatch(pdeep,
                        _S"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        stvpk(a, string, &junk)))
        ret = 1;
    strPatternDestroy(&pdeep);
    strDestroy(&junk);

    return ret;
}

testfunc parsetest_funcs[] = {
    { "basic", test_basic },     { "types", test_types },         { "opts", test_opts },
    { "group", test_group },     { "default", test_default },     { "untouched", test_untouched },
    { "matchat", test_matchat }, { "object", test_object },       { "error", test_error },
    { 0, 0 }
};

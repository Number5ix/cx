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
        TEST_FAILV(ret, 1, _SL("keyed strParse: method='${string}' target='${string}' minor=${uint}"), stvar(strref, method), stvar(strref, target), stvar(uint8, minor));

    // unkeyed placeholders, bound in order
    uint32 a = 0, b = 0;
    if (!strParse(_S"10,20", _S"${uint},${uint}", stvp(uint32, &a), stvp(uint32, &b)) || a != 10 ||
        b != 20)
        TEST_FAILV(ret, 1, _SL("unkeyed strParse: a=${uint} b=${uint}"), stvar(uint32, a), stvar(uint32, b));

    // a compiled pattern can be matched over and over
    StrPattern* p = strPatternCreate(_S"${uint:x}-${uint:y}");
    if (!p)
        TEST_FAIL(1, _SL("strPatternCreate failed for the digit-pair pattern"), stvNone);

    uint32 x = 0, y = 0;
    if (!strPatternMatch(p, _S"3-4", stvpk(x, uint32, &x), stvpk(y, uint32, &y)) || x != 3 ||
        y != 4)
        TEST_FAILV(ret, 1, _SL("digit-pair match '3-4': x=${uint} y=${uint}"), stvar(uint32, x), stvar(uint32, y));
    if (!strPatternMatch(p, _S"70-80", stvpk(x, uint32, &x), stvpk(y, uint32, &y)) || x != 70 ||
        y != 80)
        TEST_FAILV(ret, 1, _SL("digit-pair match '70-80': x=${uint} y=${uint}"), stvar(uint32, x), stvar(uint32, y));
    strPatternDestroy(&p);
    if (p)
        TEST_FAILV(ret, 1, _SL("strPatternDestroy left p=${ptr} (expected NULL)"), stvar(ptr, p));

    // the whole string has to match
    if (strParse(_S"3-4 extra", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &x)))
        TEST_FAILV(ret, 1, _SL("strParse unexpectedly matched '3-4 extra' (trailing text should fail the whole-string match)"), stvNone);
    if (strParse(_S"3-", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &x)))
        TEST_FAILV(ret, 1, _SL("strParse unexpectedly matched '3-' (incomplete match should fail)"), stvNone);

    // a literal space matches any run of whitespace, unless the pattern says otherwise
    string w1 = 0, w2 = 0;
    if (!strParse(_S"one \t  two", _S"${string:a} ${string:b}", stvpk(a, string, &w1),
                  stvpk(b, string, &w2)) ||
        !strEq(w1, _S"one") || !strEq(w2, _S"two"))
        TEST_FAILV(ret, 1, _SL("whitespace-run parse: w1='${string}' w2='${string}'"), stvar(strref, w1), stvar(strref, w2));

    StrPattern* pex = strPatternCreate(_S"a ${uint:v}", STRPAT_ExactWS);
    if (!pex)
        TEST_FAIL(1, _SL("strPatternCreate failed for the exact-whitespace pattern"), stvNone);
    uint32 exv = 0;
    if (!strPatternMatch(pex, _S"a 1", stvpk(v, uint32, &exv)) || exv != 1)
        TEST_FAILV(ret, 1, _SL("exact-ws match 'a 1': exv=${uint}"), stvar(uint32, exv));
    if (strPatternMatch(pex, _S"a  1", stvpk(v, uint32, &exv)))
        TEST_FAILV(ret, 1, _SL("exact-ws pattern unexpectedly matched 'a  1' (double space should fail STRPAT_ExactWS)"), stvNone);
    strPatternDestroy(&pex);

    // ...where the default is to accept any run of spacing
    StrPattern* pws = strPatternCreate(_S"a ${uint:v}");
    if (!pws)
        TEST_FAIL(1, _SL("strPatternCreate failed for the default-whitespace pattern"), stvNone);
    if (!strPatternMatch(pws, _S"a  \t 1", stvpk(v, uint32, &exv)) || exv != 1)
        TEST_FAILV(ret, 1, _SL("default-ws match: exv=${uint}"), stvar(uint32, exv));
    strPatternDestroy(&pws);

    // case-insensitive literals
    StrPattern* pci = strPatternCreate(_S"HTTP/${uint:v}", STRPAT_CaseI);
    if (!pci)
        TEST_FAIL(1, _SL("strPatternCreate failed for the case-insensitive pattern"), stvNone);
    if (!strPatternMatch(pci, _S"http/1", stvpk(v, uint8, &minor)) || minor != 1)
        TEST_FAILV(ret, 1, _SL("case-insensitive match 'http/1': minor=${uint}"), stvar(uint8, minor));
    strPatternDestroy(&pci);

    // backticks escape the metacharacters
    string inner = 0;
    if (!strParse(_S"(x)", _S"`(${string:i}`)", stvpk(i, string, &inner)) ||
        !strEq(inner, _S"x"))
        TEST_FAILV(ret, 1, _SL("backtick-escape parse '(x)': inner='${string}'"), stvar(strref, inner));
    // a pattern with nothing to bind still needs an argument, which is what stvpNone is for
    if (!strParse(_S"${}", _S"`${`}", stvpNone))
        TEST_FAILV(ret, 1, _SL("escaped-brace pattern failed to match empty-braces input"), stvNone);
    if (strParse(_S"other", _S"`${`}", stvpNone))
        TEST_FAILV(ret, 1, _SL("escaped-brace pattern unexpectedly matched literal text 'other'"), stvNone);

    StrPattern* pesc = strPatternCreate(_S"a`|b");
    if (!pesc)
        TEST_FAILV(ret, 1, _SL("strPatternCreate failed for the escaped-pipe pattern"), stvNone);
    if (!strPatternMatch(pesc, _S"a|b", stvpNone))
        TEST_FAILV(ret, 1, _SL("escaped-pipe pattern failed to match literal 'a|b'"), stvNone);
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
        TEST_FAILV(ret, 1, _SL("signed-int parse '-42': i=${int}"), stvar(int32, i));

    // uint refuses a sign, which is the whole difference between the two
    if (strParse(_S"-42", _S"${uint:v}", stvpk(v, int32, &i)))
        TEST_FAILV(ret, 1, _SL("uint parse unexpectedly accepted signed input '-42'"), stvNone);

    float64 f = 0;
    if (!strParse(_S"3.5e2", _S"${float:v}", stvpk(v, float64, &f)) || f != 350.0)
        TEST_FAILV(ret, 1, _SL("float parse '3.5e2': f=${float}"), stvar(float64, f));
    if (!strParse(_S"-0.25", _S"${float:v}", stvpk(v, float64, &f)) || f != -0.25)
        TEST_FAILV(ret, 1, _SL("float parse '-0.25': f=${float}"), stvar(float64, f));

    bool bl = false;
    if (!strParse(_S"YES", _S"${bool:v}", stvpk(v, bool, &bl)) || !bl)
        TEST_FAILV(ret, 1, _SL("bool parse 'YES': bl=${int}"), stvar(int32, (int32)bl));
    if (!strParse(_S"0", _S"${bool:v}", stvpk(v, bool, &bl)) || bl)
        TEST_FAILV(ret, 1, _SL("bool parse '0': bl=${int}"), stvar(int32, (int32)bl));
    if (strParse(_S"perhaps", _S"${bool:v}", stvpk(v, bool, &bl)))
        TEST_FAILV(ret, 1, _SL("bool parse unexpectedly accepted invalid input 'perhaps'"), stvNone);

    // the placeholder type says how to match; the destination says what to store into
    string s = 0;
    if (!strParse(_S"1234", _S"${uint:v}", stvpk(v, string, &s)) || !strEq(s, _S"1234"))
        TEST_FAILV(ret, 1, _SL("uint-placeholder into string destination: s='${string}'"), stvar(strref, s));

    uint16 u16 = 0;
    if (!strParse(_S"65535", _S"${uint:v}", stvpk(v, uint16, &u16)) || u16 != 65535)
        TEST_FAILV(ret, 1, _SL("uint16 boundary parse '65535': u16=${uint}"), stvar(uint16, u16));
    // matched fine, but will not fit where it was asked to go
    if (strParse(_S"65536", _S"${uint:v}", stvpk(v, uint16, &u16)) || u16 != 65535)
        TEST_FAILV(ret, 1, _SL("uint16 overflow parse '65536' should fail and leave destination untouched: u16=${uint}"), stvar(uint16, u16));

    // any type the conversion system can build from a string works as a destination
    SUID id = { 0 };
    if (!strParse(_S"id=00000000000000000000000005", _S"id=${string:v}", stvpk(v, suid, &id)) ||
        id.high != 0 || id.low != 5)
        TEST_FAILV(ret, 1, _SL("suid-destination parse: id=${suid}"), stvar(suid, id));

    // numbers are matched strictly: no leading space, no 0x
    if (strParse(_S" 5", _S"${uint:v}", stvpk(v, int32, &i)))
        TEST_FAILV(ret, 1, _SL("strict uint parse unexpectedly accepted leading whitespace ' 5'"), stvNone);
    if (!strParse(_S" 5", _S"${uint:v(ws)}", stvpk(v, int32, &i)) || i != 5)
        TEST_FAILV(ret, 1, _SL("ws-option uint parse ' 5': i=${int}"), stvar(int32, i));
    if (strParse(_S"0x10", _S"${uint:v}", stvpk(v, int32, &i)))
        TEST_FAILV(ret, 1, _SL("strict uint parse unexpectedly accepted 0x-prefixed input '0x10'"), stvNone);

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
        TEST_FAILV(ret, 1, _SL("hex-base parse 'ff': u=${uint}"), stvar(uint32, u));
    if (!strParse(_S"777", _S"${uint:v(octal)}", stvpk(v, uint32, &u)) || u != 511)
        TEST_FAILV(ret, 1, _SL("octal-base parse '777': u=${uint}"), stvar(uint32, u));
    if (!strParse(_S"1011", _S"${uint:v(binary)}", stvpk(v, uint32, &u)) || u != 11)
        TEST_FAILV(ret, 1, _SL("binary-base parse '1011': u=${uint}"), stvar(uint32, u));
    if (!strParse(_S"zz", _S"${uint:v(base:36)}", stvpk(v, uint32, &u)) || u != 1295)
        TEST_FAILV(ret, 1, _SL("base-36 parse 'zz': u=${uint}"), stvar(uint32, u));
    // hex means hex digits, not the "0x" spelling
    if (strParse(_S"0xff", _S"${uint:v(hex)}", stvpk(v, uint32, &u)))
        TEST_FAILV(ret, 1, _SL("hex-base parse unexpectedly accepted 0x-prefixed input '0xff'"), stvNone);

    // digit counts
    if (!strParse(_S"06", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)) || u != 6)
        TEST_FAILV(ret, 1, _SL("digits:2 parse '06': u=${uint}"), stvar(uint32, u));
    if (strParse(_S"6", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)))
        TEST_FAILV(ret, 1, _SL("digits:2 parse unexpectedly accepted short input '6'"), stvNone);
    if (strParse(_S"123", _S"${uint:v(digits:2)}", stvpk(v, uint32, &u)))
        TEST_FAILV(ret, 1, _SL("digits:2 parse unexpectedly accepted over-length input '123'"), stvNone);
    if (!strParse(_S"12x", _S"${uint:v(maxdigits:2)}x", stvpk(v, uint32, &u)) || u != 12)
        TEST_FAILV(ret, 1, _SL("maxdigits:2 parse '12x': u=${uint}"), stvar(uint32, u));

    // range checks happen while matching, so they can fail a branch
    if (!strParse(_S"12", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)) || u != 12)
        TEST_FAILV(ret, 1, _SL("range-checked parse '12' (min:10,max:20): u=${uint}"), stvar(uint32, u));
    if (strParse(_S"21", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)))
        TEST_FAILV(ret, 1, _SL("range-checked parse unexpectedly accepted above-max input '21'"), stvNone);
    if (strParse(_S"9", _S"${uint:v(min:10,max:20)}", stvpk(v, uint32, &u)))
        TEST_FAILV(ret, 1, _SL("range-checked parse unexpectedly accepted below-min input '9'"), stvNone);

    // float
    float64 f = 0;
    if (strParse(_S"1e3", _S"${float:v(fixed)}", stvpk(v, float64, &f)))
        TEST_FAILV(ret, 1, _SL("fixed-notation float parse unexpectedly accepted exponent input '1e3'"), stvNone);
    if (!strParse(_S"1.5", _S"${float:v(fixed)}", stvpk(v, float64, &f)) || f != 1.5)
        TEST_FAILV(ret, 1, _SL("fixed-notation float parse '1.5': f=${float}"), stvar(float64, f));

    // string shapes
    if (!strParse(_S"abcdef", _S"${string:v(len:3)}def", stvpk(v, string, &s)) ||
        !strEq(s, _S"abc"))
        TEST_FAILV(ret, 1, _SL("len:3 string parse 'abcdef': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"key=value;x", _S"key=${string:v(until:;)};x", stvpk(v, string, &s)) ||
        !strEq(s, _S"value"))
        TEST_FAILV(ret, 1, _SL("until-delimiter string parse 'key=value;x': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"12ab", _S"${string:v(chars:0123456789)}ab", stvpk(v, string, &s)) ||
        !strEq(s, _S"12"))
        TEST_FAILV(ret, 1, _SL("chars-set string parse '12ab': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"12ab", _S"${string:v(notchars:ab)}ab", stvpk(v, string, &s)) ||
        !strEq(s, _S"12"))
        TEST_FAILV(ret, 1, _SL("notchars-set string parse '12ab': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"one two", _S"${string:v(word)} two", stvpk(v, string, &s)) ||
        !strEq(s, _S"one"))
        TEST_FAILV(ret, 1, _SL("word string parse 'one two': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"a=b c d", _S"a=${string:v(rest)}", stvpk(v, string, &s)) ||
        !strEq(s, _S"b c d"))
        TEST_FAILV(ret, 1, _SL("rest string parse 'a=b c d': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"\"a\\\"b\" x", _S"${string:v(quoted)} x", stvpk(v, string, &s)) ||
        !strEq(s, _S"a\"b"))
        TEST_FAILV(ret, 1, _SL("quoted string parse: s='${string}'"), stvar(strref, s));
    if (!strParse(_S"[  pad  ]", _S"[${string:v(trim)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"pad"))
        TEST_FAILV(ret, 1, _SL("trim string parse '[  pad  ]': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"[abc]", _S"[${string:v(upper)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"ABC"))
        TEST_FAILV(ret, 1, _SL("upper string parse '[abc]': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"[ABC]", _S"[${string:v(lower)}]", stvpk(v, string, &s)) ||
        !strEq(s, _S"abc"))
        TEST_FAILV(ret, 1, _SL("lower string parse '[ABC]': s='${string}'"), stvar(strref, s));

    // a skipped placeholder binds nowhere, so it is allowed inside a group unkeyed even
    // though every other placeholder there has to carry a key
    uint32 sk = 0;
    if (!strParse(_S"a 5", _S"(${string(skip)} )?${uint:v}", stvpk(v, uint32, &sk)) || sk != 5)
        TEST_FAILV(ret, 1, _SL("skip-group parse 'a 5': sk=${uint}"), stvar(uint32, sk));
    sk = 0;
    if (!strParse(_S"5", _S"(${string(skip)} )?${uint:v}", stvpk(v, uint32, &sk)) || sk != 5)
        TEST_FAILV(ret, 1, _SL("skip-group parse '5' (group absent): sk=${uint}"), stvar(uint32, sk));

    // skip: matched, never bound, and does not consume a positional slot either
    uint32 p1 = 0;
    if (!strParse(_S"7,8", _S"${uint(skip)},${uint}", stvp(uint32, &p1)) || p1 != 8)
        TEST_FAILV(ret, 1, _SL("skip-placeholder positional parse '7,8': p1=${uint}"), stvar(uint32, p1));

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
        TEST_FAILV(ret, 1, _SL("optional-group parse (no port): host='${string}' port=${uint} hasport=${int}"), stvar(strref, host), stvar(uint16, port), stvar(int32, (int32)hasport));

    port = 443;
    if (!strParse(_S"example.com:8080",
                  _S"${string:host}(:${uint:port})?:hasport",
                  stvpk(host, string, &host),
                  stvpk(port, uint16, &port),
                  stvpk(hasport, bool, &hasport)) ||
        !strEq(host, _S"example.com") || port != 8080 || !hasport)
        TEST_FAILV(ret, 1, _SL("optional-group parse (with port): host='${string}' port=${uint} hasport=${int}"), stvar(strref, host), stvar(uint16, port), stvar(int32, (int32)hasport));

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
        TEST_FAILV(ret, 1, _SL("alternation match '06 Nov': day=${uint} mon='${string}' form=${uint}"), stvar(uint32, day), stvar(strref, mon), stvar(uint8, form));

    if (!strPatternMatch(strPat(kDate),
                         _S"Nov 6",
                         stvpk(d1, uint32, &day),
                         stvpk(m1, string, &mon),
                         stvpk(d2, uint32, &day),
                         stvpk(m2, string, &mon),
                         stvpk(form, uint8, &form)) ||
        day != 6 || !strEq(mon, _S"Nov") || form != 2)
        TEST_FAILV(ret, 1, _SL("alternation match 'Nov 6': day=${uint} mon='${string}' form=${uint}"), stvar(uint32, day), stvar(strref, mon), stvar(uint8, form));

    // the same declaration compiles once and keeps working
    if (strPat(kDate) != strPat(kDate))
        TEST_FAILV(ret, 1, _SL("strPat(kDate) did not return the same compiled-pattern pointer across calls"), stvNone);

    // a group with no key still works, it just cannot be observed directly
    uint32 v1 = 0, v2 = 9;
    if (!strParse(_S"1", _S"${uint:a}(.${uint:b})?", stvpk(a, uint32, &v1),
                  stvpk(b, uint32, &v2)) ||
        v1 != 1 || v2 != 9)
        TEST_FAILV(ret, 1, _SL("unkeyed group parse '1' (b unset): v1=${uint} v2=${uint}"), stvar(uint32, v1), stvar(uint32, v2));

    // one key shared by every alternative: only the branch that matched produces a value,
    // so one destination serves them all
    STR_PATTERN(kShared, "(${uint:d} ${string:m}|${string:m} ${uint:d}):which");

    uint32 sday  = 0;
    string smon  = 0;
    uint8 which  = 0;
    if (!strPatternMatch(strPat(kShared), _S"06 Nov", stvpk(d, uint32, &sday),
                         stvpk(m, string, &smon), stvpk(which, uint8, &which)) ||
        sday != 6 || !strEq(smon, _S"Nov") || which != 1)
        TEST_FAILV(ret, 1, _SL("shared-key alternation '06 Nov': sday=${uint} smon='${string}' which=${uint}"), stvar(uint32, sday), stvar(strref, smon), stvar(uint8, which));

    sday = 0;
    if (!strPatternMatch(strPat(kShared), _S"Nov 6", stvpk(d, uint32, &sday),
                         stvpk(m, string, &smon), stvpk(which, uint8, &which)) ||
        sday != 6 || !strEq(smon, _S"Nov") || which != 2)
        TEST_FAILV(ret, 1, _SL("shared-key alternation 'Nov 6': sday=${uint} smon='${string}' which=${uint}"), stvar(uint32, sday), stvar(strref, smon), stvar(uint8, which));

    // a shared key in a branch that does not match writes nothing, so a pre-set destination
    // survives
    STR_PATTERN(kShareOpt, "x(${uint:v}|y${uint:v})?");
    uint32 sv = 77;
    if (!strPatternMatch(strPat(kShareOpt), _S"x", stvpk(v, uint32, &sv)) || sv != 77)
        TEST_FAILV(ret, 1, _SL("optional shared-key branch 'x' (unset): sv=${uint}"), stvar(uint32, sv));
    if (!strPatternMatch(strPat(kShareOpt), _S"xy5", stvpk(v, uint32, &sv)) || sv != 5)
        TEST_FAILV(ret, 1, _SL("optional shared-key branch 'xy5': sv=${uint}"), stvar(uint32, sv));

    strDestroy(&smon);

    // nested groups
    string sch = 0;
    if (!strParse(_S"https://h/p",
                  _S"(${string:sch}://)?${string:h}(/${string:pth})?",
                  stvpk(sch, string, &sch),
                  stvpk(h, string, &host),
                  stvpk(pth, string, &mon)) ||
        !strEq(sch, _S"https") || !strEq(host, _S"h") || !strEq(mon, _S"p"))
        TEST_FAILV(ret, 1, _SL("nested-group parse: sch='${string}' host='${string}' path='${string}'"), stvar(strref, sch), stvar(strref, host), stvar(strref, mon));

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
        TEST_FAILV(ret, 1, _SL("default-value parse 'a=,b=5': x=${uint} y=${uint}"), stvar(uint32, x), stvar(uint32, y));

    if (!strParse(_S"a=7,b=5", _S"a=${uint:x;0},b=${uint:y}", stvpk(x, uint32, &x),
                  stvpk(y, uint32, &y)) ||
        x != 7 || y != 5)
        TEST_FAILV(ret, 1, _SL("default-value parse 'a=7,b=5' (field present): x=${uint} y=${uint}"), stvar(uint32, x), stvar(uint32, y));

    // a string default fires when the matched text would have been empty
    string s = 0;
    if (!strParse(_S"[]", _S"[${string:v;none}]", stvpk(v, string, &s)) || !strEq(s, _S"none"))
        TEST_FAILV(ret, 1, _SL("string default '[]': s='${string}'"), stvar(strref, s));
    if (!strParse(_S"[x]", _S"[${string:v;none}]", stvpk(v, string, &s)) || !strEq(s, _S"x"))
        TEST_FAILV(ret, 1, _SL("string default '[x]' (field present): s='${string}'"), stvar(strref, s));

    // the surrounding literals still have to match
    if (strParse(_S"a=,c=5", _S"a=${uint:x;0},b=${uint:y}", stvpk(x, uint32, &x)))
        TEST_FAILV(ret, 1, _SL("default-value parse unexpectedly matched with wrong surrounding literal 'a=,c=5'"), stvNone);

    // a `} in a default is a literal brace
    if (!strParse(_S"[]", _S"[${string:v;a`}b}]", stvpk(v, string, &s)) || !strEq(s, _S"a}b"))
        TEST_FAILV(ret, 1, _SL("escaped-brace default '[]': s='${string}'"), stvar(strref, s));

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
        TEST_FAILV(ret, 1, _SL("strParse unexpectedly matched with invalid trailing field '1,2,nope'"), stvNone);
    if (a != 11 || b != 22)
        TEST_FAILV(ret, 1, _SL("failed match wrote to destinations: a=${uint} b=${uint} (expected untouched 11/22)"), stvar(uint32, a), stvar(uint32, b));

    if (strParse(_S"hello", _S"${string:s} ${uint:n}", stvpk(s, string, &s)))
        TEST_FAILV(ret, 1, _SL("strParse unexpectedly matched incomplete input 'hello'"), stvNone);
    if (!strEq(s, _S"original"))
        TEST_FAILV(ret, 1, _SL("failed match overwrote destination: s='${string}' (expected 'original')"), stvar(strref, s));

    // a value that matched but cannot be stored is also a failed match
    uint8 small = 5;
    if (strParse(_S"9999", _S"${uint:v}", stvpk(v, uint8, &small)) || small != 5)
        TEST_FAILV(ret, 1, _SL("overflow-into-uint8 parse '9999' should fail and leave destination untouched: small=${uint}"), stvar(uint8, small));

    // ...and the destination it could not go into is untouched along with all the others
    bool flag  = true;
    uint32 ok1 = 0;
    if (strParse(_S"1,perhaps", _S"${uint:n},${string:b}", stvpk(n, uint32, &ok1),
                 stvpk(b, bool, &flag)) ||
        ok1 != 0 || !flag)
        TEST_FAILV(ret, 1, _SL("bool-destination parse failure leaves both fields untouched: ok1=${uint} flag=${int}"), stvar(uint32, ok1), stvar(int32, (int32)flag));

    // a skipped optional group leaves its fields exactly as the caller left them
    uint32 port = 8080;
    string host = 0;
    if (!strParse(_S"h", _S"${string:host}(:${uint:port})?", stvpk(host, string, &host),
                  stvpk(port, uint32, &port)) ||
        port != 8080 || !strEq(host, _S"h"))
        TEST_FAILV(ret, 1, _SL("skipped-optional-group parse 'h': host='${string}' port=${uint}"), stvar(strref, host), stvar(uint32, port));

    // and so does a key the pattern never mentions binding to
    uint32 unused = 3;
    if (!strParse(_S"5", _S"${uint:v}", stvpk(v, uint32, &a), stvpk(v, uint32, &unused)) ||
        a != 5 || unused != 5)
        TEST_FAILV(ret, 1, _SL("duplicate-destination-key parse '5': a=${uint} unused=${uint}"), stvar(uint32, a), stvar(uint32, unused));

    // a pattern key with nothing bound to it is fine
    if (!strParse(_S"1-2", _S"${uint:x}-${uint:y}", stvpk(x, uint32, &a)) || a != 1)
        TEST_FAILV(ret, 1, _SL("unbound-key parse '1-2': a=${uint}"), stvar(uint32, a));

    strDestroy(&s);
    strDestroy(&host);
    return ret;
}

static int test_matchat()
{
    int ret = 0;

    StrPattern* p = strPatternCreate(_S"${string:k}=${string:v};");
    if (!p)
        TEST_FAIL(1, _SL("strPatternCreate failed for the key-value pattern"), stvNone);

    strref list = _S"a=1;bb=22;ccc=333;";
    int32 pos   = 0;
    string k = 0, v = 0;
    int n       = 0;

    while (pos < (int32)strLen(list)) {
        if (!strPatternMatchAt(&pos, p, list, stvpk(k, string, &k), stvpk(v, string, &v)))
            break;
        n++;
        if (n == 2 && (!strEq(k, _S"bb") || !strEq(v, _S"22")))
            TEST_FAILV(ret, 1, _SL("matchAt iteration 2: k='${string}' v='${string}' (expected bb/22)"), stvar(strref, k), stvar(strref, v));
    }

    if (n != 3 || pos != (int32)strLen(list))
        TEST_FAILV(ret, 1, _SL("matchAt loop: n=${int} pos=${int} (expected n=3, pos=${int})"), stvar(int32, n), stvar(int32, pos), stvar(int32, (int32)strLen(list)));

    // a failed match reports how far the best attempt got
    pos = 0;
    if (strPatternMatchAt(&pos, p, _S"a=1", stvpk(k, string, &k), stvpk(v, string, &v)))
        TEST_FAILV(ret, 1, _SL("matchAt unexpectedly matched incomplete input 'a=1' (missing trailing separator)"), stvNone);
    if (pos <= 0)
        TEST_FAILV(ret, 1, _SL("matchAt failure left pos=${int} (expected > 0, reporting best-attempt progress)"), stvar(int32, pos));

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
        TEST_FAILV(ret, 1, _SL("object-destination parse '[thing:42]': sv='${string}' iv=${int}"), stvar(strref, o->sv), stvar(int32, o->iv));

    // options the compiler has no meaning for reach the object verbatim
    if (!strParse(_S"[other:two]", _S"[${object:v(words)}]", stvpk(v, object, &o)) ||
        !strEq(o->sv, _S"other") || o->iv != 2)
        TEST_FAILV(ret, 1, _SL("object-destination parse with options '[other:two]': sv='${string}' iv=${int}"), stvar(strref, o->sv), stvar(int32, o->iv));

    // an object that refuses the text fails the match
    if (strParse(_S"[nocolon]", _S"[${object:v}]", stvpk(v, object, &o)))
        TEST_FAILV(ret, 1, _SL("object-destination parse unexpectedly accepted malformed input '[nocolon]'"), stvNone);

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
            TEST_FAILV(ret, 1, _SL("bad pattern ${int} unexpectedly compiled: '${string}'"), stvar(int32, (int32)i), stvar(strref, bad[i]));
            strPatternDestroy(&p);
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
            TEST_FAILV(ret, 1, _SL("good pattern ${int} failed to compile: '${string}'"), stvar(int32, (int32)i), stvar(strref, good[i]));
        }
        strPatternDestroy(&p);
    }

    if (!strParse(_S"(none)", _S"`(none`)", stvpNone))
        TEST_FAILV(ret, 1, _SL("escaped-literal pattern failed to match '(none)'"), stvNone);

    // a destination whose key is not in the pattern is a typo, and fails
    uint32 v = 3;
    if (strParse(_S"5", _S"${uint:known}", stvpk(unknown, uint32, &v)) || v != 3)
        TEST_FAILV(ret, 1, _SL("mismatched destination key: v=${uint} (expected untouched 3)"), stvar(uint32, v));

    // so is asking for more positional values than the pattern has
    uint32 w = 0;
    if (strParse(_S"5", _S"${uint}", stvp(uint32, &v), stvp(uint32, &w)))
        TEST_FAILV(ret, 1, _SL("strParse unexpectedly matched with excess positional destinations"), stvNone);

    // a pattern that will not compile makes every match against it fail rather than crash
    if (strPatternMatch(NULL, _S"x", stvp(uint32, &v)))
        TEST_FAILV(ret, 1, _SL("strPatternMatch against NULL pattern unexpectedly succeeded"), stvNone);

    // Nested groups multiply the number of ways a pattern can be tried, so a deep enough
    // one has to give up rather than run forever. This is the shape that would hang.
    StrPattern* pdeep = strPatternCreate(
        _S"(${string:a}|)?(${string:b}|)?(${string:c}|)?(${string:d}|)?"
        "(${string:e}|)?(${string:f}|)?(${string:g}|)?(${string:h}|)?X");
    if (!pdeep)
        TEST_FAIL(1, _SL("strPatternCreate failed for the deep-nested-groups pattern"), stvNone);

    string junk = 0;
    // no 'X' anywhere, so every combination gets tried and every one of them fails
    if (strPatternMatch(pdeep,
                        _S"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        stvpk(a, string, &junk)))
        TEST_FAILV(ret, 1, _SL("deep-nested-groups pattern unexpectedly matched input with no terminating X"), stvNone);
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

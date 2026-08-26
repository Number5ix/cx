// Process-wide initialization for cxhttp.
//
// There is nothing to set up but the log channel, and no state that could fail, so this is much
// smaller than its cxtls counterpart. It still exists rather than being a static initializer
// because logChan() has to run after the log system is up, and a consumer should not have to know
// that.

#include "http_private.h"

#include <cx/utils/lazyinit.h>

LogChannel* HttpLogChannel;

static LazyInitState httpInit_done;

static void httpInitOnce(void* unused)
{
    unused_noeval(unused);
    HttpLogChannel = logChan(_SL("cx/http"));
}

void _httpInit(void)
{
    lazyInit(&httpInit_done, httpInitOnce, NULL);
}

bool _httpIsTokenChar(uint8 c)
{
    // RFC 9110 5.6.2: tchar = "!#$%&'*+-.^_`|~" / DIGIT / ALPHA. Written as a table rather than a
    // chain of comparisons because every header name is validated one character at a time.
    static const uint8 tchar[256] = {
        ['!'] = 1, ['#'] = 1, ['$'] = 1, ['%'] = 1, ['&'] = 1, ['\''] = 1, ['*'] = 1, ['+'] = 1,
        ['-'] = 1, ['.'] = 1, ['^'] = 1, ['_'] = 1, ['`'] = 1, ['|'] = 1,  ['~'] = 1, ['0'] = 1,
        ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1,  ['7'] = 1, ['8'] = 1,
        ['9'] = 1, ['A'] = 1, ['B'] = 1, ['C'] = 1, ['D'] = 1, ['E'] = 1,  ['F'] = 1, ['G'] = 1,
        ['H'] = 1, ['I'] = 1, ['J'] = 1, ['K'] = 1, ['L'] = 1, ['M'] = 1,  ['N'] = 1, ['O'] = 1,
        ['P'] = 1, ['Q'] = 1, ['R'] = 1, ['S'] = 1, ['T'] = 1, ['U'] = 1,  ['V'] = 1, ['W'] = 1,
        ['X'] = 1, ['Y'] = 1, ['Z'] = 1, ['a'] = 1, ['b'] = 1, ['c'] = 1,  ['d'] = 1, ['e'] = 1,
        ['f'] = 1, ['g'] = 1, ['h'] = 1, ['i'] = 1, ['j'] = 1, ['k'] = 1,  ['l'] = 1, ['m'] = 1,
        ['n'] = 1, ['o'] = 1, ['p'] = 1, ['q'] = 1, ['r'] = 1, ['s'] = 1,  ['t'] = 1, ['u'] = 1,
        ['v'] = 1, ['w'] = 1, ['x'] = 1, ['y'] = 1, ['z'] = 1,
    };
    return tchar[c] != 0;
}

_Use_decl_annotations_
bool _httpIsToken(strref s)
{
    if (strEmpty(s))
        return false;

    striter it;
    striBorrow(&it, s);
    uint8 c;
    while (striChar(&it, &c)) {
        if (!_httpIsTokenChar(c))
            return false;
    }
    return true;
}

_Use_decl_annotations_
void _httpTrimOWS(strhandle s)
{
    strTrim(s, *s, _SL(" \t"));
}

_Use_decl_annotations_
bool _httpAppendBytes(strhandle out, const uint8* data, size_t len)
{
    if (!data || len == 0)
        return true;

    while (len > 0) {
        uint32 run = (len > UINT32_MAX) ? UINT32_MAX : (uint32)len;
        if (!strAppendBytes(out, data, run))
            return false;

        data += run;
        len -= run;
    }
    return true;
}

_Use_decl_annotations_
bool _httpProgressDue(uint64 done, uint64* seen, size_t interval, bool final)
{
    if (done == *seen)
        return false;   // nothing new to report, including the final call after a full one

    if (!final && interval > 0 && done - *seen < interval)
        return false;

    *seen = done;
    return true;
}

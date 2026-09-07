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

_Use_decl_annotations_
bool _httpAlpnHas(TlsConfig* cfg, strref proto)
{
    sa_string protos;
    int32 n  = tlsconfigGetALPN(cfg, &protos);
    bool has = false;

    for (int32 i = 0; i < n; i++) {
        if (strEq(protos.a[i], proto)) {
            has = true;
            break;
        }
    }

    saDestroy(&protos);
    return has;
}

_Use_decl_annotations_
int32 _httpAlpnCount(TlsConfig* cfg)
{
    sa_string protos;
    int32 n = tlsconfigGetALPN(cfg, &protos);
    saDestroy(&protos);
    return n;
}

_Use_decl_annotations_
bool _httpAlpnAdd(TlsConfig* cfg, strref proto)
{
    sa_string protos;
    int32 n = tlsconfigGetALPN(cfg, &protos);

    bool has = false;
    for (int32 i = 0; i < n; i++) {
        if (strEq(protos.a[i], proto)) {
            has = true;
            break;
        }
    }

    // Asking a sealed configuration to change is an error rather than something to try and have
    // ignored, so it is not asked. One that already offers the protocol needs nothing anyway,
    // which is the case that lets both listeners share a configuration in either order.
    if (!has && tlsconfigSealed(cfg)) {
        saDestroy(&protos);
        return false;
    }

    if (!has) {
        saPush(&protos, strref, proto);
        tlsconfigSetALPN(cfg, &protos);
    }

    saDestroy(&protos);
    return true;
}

_Use_decl_annotations_
bool _httpDispatchHandoff(HttpDispatch* d, NetFlow* flow)
{
    if (!flow)
        return false;

    atomicStore(uint32, &d->pending, 1, Release);
    if (netflowAddTimer(flow, 0, NTF_None) == 0) {
        // The flow is already dying, so no worker is coming.
        atomicStore(uint32, &d->pending, 0, Relaxed);
        return false;
    }
    return true;
}

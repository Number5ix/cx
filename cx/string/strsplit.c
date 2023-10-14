#include "string_private.h"
#include "cx/container/sarray.h"

int32 strSplit(_Inout_ sa_string *out, _In_opt_ strref s, _In_opt_ strref sep, bool empty)
{
    if (!out)
        return 0;

    saClear(out);

    uint32 seplen = strLen(sep);
    int32 start = 0, next;
    string seg = 0;
    while ((next = strFind(s, start, sep)) != -1) {
        if (start != next || empty) {
            if (next != 0)      // degenerate case for starting with separator
                strSubStr(&seg, s, start, next);
            else
                strClear(&seg);
            saPushC(out, string, &seg);
        }
        start = next + seplen;
    }
    if (start < (int32)strLen(s) || empty) {
        strSubStr(&seg, s, start, strLen(s));
        saPushC(out, string, &seg);
    }

    return saSize(*out);
}

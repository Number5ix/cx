#include "format_private.h"
#include "cx/core/suid.h"

bool _fmtSUID(FMTVar *v, string *out)
{
    SUID *s = (SUID*)v->data;
    return suidEncode(out, s);
}

#include "format_private.h"
#include "formattable.h"

bool _fmtParseObjectOpt(FMTVar *v, strref opt)
{
    // only for objects, save all options into an array in the FMTVar structure so
    // that Formattable has access to them later
    saPush(&v->fmtopts, strref, opt);
    return true;
}

bool _fmtObject(FMTVar *v, string *out)
{
    Formattable *fmtif = (Formattable*)v->fmtdata[0];
    return fmtif->format(*(ObjInst**)v->data, v, out);
}

#include "format_private.h"
#include "formattable.h"

bool _fmtObject(FMTVar *v, string *out)
{
    Formattable *fmtif = (Formattable*)v->fmtdata[0];
    return fmtif->format(*(ObjInst**)v->data, v, out);
}

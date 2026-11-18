#include "format_private.h"

STR_CONST(kFmtTrue, "true");
STR_CONST(kFmtFalse, "false");

_Use_decl_annotations_
bool _fmtBool(FMTVar* v, string* out)
{
    strDup(out, *(bool*)v->data ? kFmtTrue : kFmtFalse);
    return true;
}

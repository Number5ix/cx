#include "cx/cx.h"

void stDtor_stvar(stype st, stgeneric *stgen, uint32 flags)
{
    stvar *stv = stGenVal(stvar, *stgen);

    _stDestroy(stv->type, NULL, &stv->data, flags);
    stv->type = 0;
}

intptr stCmp_stvar(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    stvar *stv1 = stGenVal(stvar, stgen1);
    stvar *stv2 = stGenVal(stvar, stgen2);

    if (stv1->type != stv2->type)
        return stv1->type - stv2->type;

    return _stCmp(stv1->type, NULL, stv1->data, stv2->data, flags);
}

void stCopy_stvar(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    stvar *dvar = stGenVal(stvar, *dest);
    stvar *svar = stGenVal(stvar, src);

    dvar->type = svar->type;
    _stCopy(svar->type, NULL, &dvar->data, svar->data, flags);
}

uint32 stHash_stvar(stype st, stgeneric stgen, uint32 flags)
{
    stvar *stv = stGenVal(stvar, stgen);
    return _stHash(stv->type, NULL, stv->data, flags);
}

#include "stype_stvar.h"
#include "cx/stype/stvar.h"

void stDtor_stvar(stype st, stgeneric* gen, uint32 flags)
{
    _stvarClear(gen->st_stvar, flags);
}

intptr stCmp_stvar(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    stvar* stv1 = gen1.st_stvar;
    stvar* stv2 = gen2.st_stvar;

    stype t1 = stvarType(stv1);
    stype t2 = stvarType(stv2);
    if (t1 != t2)
        return (intptr)((uintptr)t1 - (uintptr)t2);

    return _stCmp(t1, stv1->data, stv2->data, flags);
}

void stCopy_stvar(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                  flags_t flags)
{
    stvar* dvar = dest->st_stvar;
    stvar* svar = src.st_stvar;

    _stvarInit(dvar, stvarType(svar), svar->data);
}

uint32 stHash_stvar(stype st, stgeneric gen, uint32 flags)
{
    stvar* stv = gen.st_stvar;
    return _stHash(stvarType(stv), stv->data, flags);
}

_Success_(return) _Check_return_ bool
stConvert_stvar(stype destst, _stCopyDest_Anno_(destst) stgeneric* dest, stype srcst,
                _In_ stgeneric src, uint32 flags)
{
    stvar* svar = src.st_stvar;
    return _stConvert(destst, dest, stvarType(svar), svar->data, flags);
}

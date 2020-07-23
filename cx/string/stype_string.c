#include "cx/string.h"
#include "cx/utils/murmur.h"

void stDtor_string(stype st, stgeneric *gen, uint32 flags)
{
    strDestroy(&gen->st_string);
}

intptr stCmp_string(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return strCmp(gen1.st_string, gen2.st_string);
    else
        return strCmpi(gen1.st_string, gen2.st_string);
}

void stCopy_string(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    string(temp);
    strDup(&temp, src.st_string);
    dest->st_string = temp;
}

uint32 stHash_string(stype st, stgeneric gen, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return hashMurmur3Str(gen.st_string);
    else
        return hashMurmur3Stri(gen.st_string);
}

#include "cx/string.h"
#include "cx/utils/murmur.h"

void stDtor_string(stype st, stgeneric *stgen, uint32 flags)
{
    strDestroy(&stGenVal(string, *stgen));
}

intptr stCmp_string(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return strCmp(stGenVal(string, stgen1), stGenVal(string, stgen2));
    else
        return strCmpi(stGenVal(string, stgen1), stGenVal(string, stgen2));
}

void stCopy_string(stype st, stgeneric *dest, stgeneric src, uint32 flags)
{
    string temp = NULL;
    strDup(&temp, stGenVal(string, src));
    stGenVal(string, *dest) = temp;
}

uint32 stHash_string(stype st, stgeneric stgen, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return hashMurmur3Str(stGenVal(string, stgen));
    else
        return hashMurmur3Stri(stGenVal(string, stgen));
}

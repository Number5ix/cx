#include "cx/string.h"
#include "cx/utils/murmur.h"

void stDtor_string(stype st, void *ptr, uint32 flags)
{
    strDestroy((string*)ptr);
}

intptr stCmp_string(stype st, const void *ptr1, const void *ptr2, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return strCmp(*(string*)ptr1, *(string*)ptr2);
    else
        return strCmpi(*(string*)ptr1, *(string*)ptr2);
}

void stCopy_string(stype st, void *dest, const void *src, uint32 flags)
{
    string temp = NULL;
    strDup(&temp, *(string*)src);
    *(string*)dest = temp;
}

uint32 stHash_string(stype st, const void *ptr, uint32 flags)
{
    if (!(flags & STOPS_CaseInsensitive))
        return hashMurmur3Str(*(string*)ptr);
    else
        return hashMurmur3Stri(*(string*)ptr);
}

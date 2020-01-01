#include "suid.h"

intptr stCmp_suid(stype st, const void *ptr1, const void *ptr2, uint32 flags)
{
    const SUID *s1 = (const SUID*)ptr1;
    const SUID *s2 = (const SUID*)ptr2;

    return suidCmp(s1, s2);
}

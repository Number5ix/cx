#include "suid.h"

intptr stCmp_suid(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags)
{
    return suidCmp(stGenVal(suid, stgen1), stGenVal(suid, stgen2));
}

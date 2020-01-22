#include "suid.h"

intptr stCmp_suid(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    return suidCmp(gen1.st_suid, gen2.st_suid);
}

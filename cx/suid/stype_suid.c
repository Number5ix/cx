#include "suid.h"

intptr stCmp_suid(stype st, stgeneric gen1, stgeneric gen2, uint32 flags)
{
    return suidCmp(gen1.st_suid, gen2.st_suid);
}

bool stConvert_suid(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags)
{
    switch (stGetId(destst)) {
    case stTypeId(string):
        dest->st_string = 0;
        return suidEncode(&dest->st_string, src.st_suid);
    }

    return false;
}

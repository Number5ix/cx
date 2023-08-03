#include "cx/cx.h"

void stDtor_stvar(stype st, stgeneric *stgen, uint32 flags);
intptr stCmp_stvar(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags);
void stCopy_stvar(stype st, stgeneric *dest, stgeneric src, uint32 flags);
uint32 stHash_stvar(stype st, stgeneric stgen, uint32 flags);
bool stConvert_stvar(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags);

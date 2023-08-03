#include "cx/cx.h"

void stDtor_obj(stype st, stgeneric *stgen, uint32 flags);
intptr stCmp_obj(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags);
void stCopy_obj(stype st, stgeneric *dest, stgeneric src, uint32 flags);
uint32 stHash_obj(stype st, stgeneric stgen, uint32 flags);
bool stConvert_obj(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags);

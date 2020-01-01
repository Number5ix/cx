#include "cx/cx.h"

void stDtor_obj(stype st, void *ptr, uint32 flags);
intptr stCmp_obj(stype st, const void *ptr1, const void *ptr2, uint32 flags);
void stCopy_obj(stype st, void *dest, const void *src, uint32 flags);
uint32 stHash_obj(stype st, const void *ptr, uint32 flags);

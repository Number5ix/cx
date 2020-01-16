#pragma once

#include "cx/cx.h"

void stDtor_string(stype st, stgeneric *stgen, uint32 flags);
intptr stCmp_string(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags);
void stCopy_string(stype st, stgeneric *dest, stgeneric src, uint32 flags);
uint32 stHash_string(stype st, stgeneric stgen, uint32 flags);

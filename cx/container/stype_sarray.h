#pragma once

#include "cx/cx.h"

void stDtor_sarray(stype st, stgeneric *stgen, uint32 flags);
void stCopy_sarray(stype st, stgeneric *dest, stgeneric src, uint32 flags);
uint32 stHash_sarray(stype st, stgeneric stgen, uint32 flags);

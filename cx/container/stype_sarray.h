#pragma once

#include "cx/cx.h"

void stDtor_sarray(stype st, stgeneric *stgen, flags_t flags);
void stCopy_sarray(stype st, stgeneric *dest, stgeneric src, flags_t flags);
uint32 stHash_sarray(stype st, stgeneric stgen, flags_t flags);

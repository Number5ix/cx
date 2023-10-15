#pragma once

#include "cx/cx.h"

void stDtor_sarray(stype st, stgeneric *stgen, flags_t flags);
void stCopy_sarray(stype st, _stCopyDest_Anno_(st) stgeneric *dest, _In_ stgeneric src, flags_t flags);
uint32 stHash_sarray(stype st, stgeneric stgen, flags_t flags);

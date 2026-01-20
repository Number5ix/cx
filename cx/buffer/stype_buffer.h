#pragma once

#include "cx/cx.h"

void stDtor_buffer(stype st, _Pre_notnull_ _Post_invalid_ stgeneric *stgen, flags_t flags);
void stCopy_buffer(stype st, _stCopyDest_Anno_(st) stgeneric *dest, _In_ stgeneric src, flags_t flags);
intptr stCmp_buffer(stype st, _In_ stgeneric stgen1, _In_ stgeneric stgen2, uint32 flags);
uint32 stHash_buffer(stype st, _In_ stgeneric stgen, flags_t flags);

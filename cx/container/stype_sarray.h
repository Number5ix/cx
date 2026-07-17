#pragma once

#include "cx/cx.h"

CX_C_BEGIN

void stDtor_sarray(stype st, _Pre_notnull_ _Post_invalid_ stgeneric* stgen, flags_t flags);
intptr stCmp_sarray(stype st, stgeneric gen1, stgeneric gen2, flags_t flags);
void stCopy_sarray(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                   flags_t flags);
uint32 stHash_sarray(stype st, _In_ stgeneric stgen, flags_t flags);

CX_C_END

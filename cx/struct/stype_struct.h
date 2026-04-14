#pragma once

#include "cx/cx.h"

void stDtor_struct(stype st, stgeneric* gen, uint32 flags);
intptr stCmp_struct(stype st, stgeneric gen1, stgeneric gen2, uint32 flags);
void stCopy_struct(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                   flags_t flags);
uint32 stHash_struct(stype st, stgeneric gen, uint32 flags);

void stDtor_structp(stype st, stgeneric* gen, uint32 flags);
intptr stCmp_structp(stype st, stgeneric gen1, stgeneric gen2, uint32 flags);
void stCopy_structp(stype st, _stCopyDest_Anno_(st) stgeneric* dest, _In_ stgeneric src,
                    flags_t flags);
uint32 stHash_structp(stype st, stgeneric gen, uint32 flags);

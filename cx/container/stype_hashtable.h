#pragma once

#include "cx/cx.h"

void stDtor_hashtable(stype st, stgeneric *stgen, flags_t flags);
void stCopy_hashtable(stype st, _stCopyDest_Anno_(st) stgeneric *dest, _In_ stgeneric src, flags_t flags);
uint32 stHash_hashtable(stype st, stgeneric stgen, flags_t flags);

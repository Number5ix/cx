#pragma once

#include "cx/cx.h"

void stDtor_hashtable(stype st, stgeneric *stgen, uint32 flags);
void stCopy_hashtable(stype st, stgeneric *dest, stgeneric src, uint32 flags);
uint32 stHash_hashtable(stype st, stgeneric stgen, uint32 flags);

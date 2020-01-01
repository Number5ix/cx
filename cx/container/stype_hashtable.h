#pragma once

#include "cx/cx.h"

void stDtor_hashtable(stype st, void *ptr, uint32 flags);
void stCopy_hashtable(stype st, void *dest, const void *src, uint32 flags);
uint32 stHash_hashtable(stype st, const void *ptr, uint32 flags);

#pragma once

#include "cx/cx.h"

void stDtor_sarray(stype st, void *ptr, uint32 flags);
void stCopy_sarray(stype st, void *dest, const void *src, uint32 flags);
uint32 stHash_sarray(stype st, const void *ptr, uint32 flags);

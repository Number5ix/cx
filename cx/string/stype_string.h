#pragma once

#include "cx/cx.h"

void stDtor_string(stype st, void *ptr, uint32 flags);
intptr stCmp_string(stype st, const void *ptr1, const void *ptr2, uint32 flags);
void stCopy_string(stype st, void *dest, const void *src, uint32 flags);
uint32 stHash_string(stype st, const void *ptr, uint32 flags);

#pragma once

#include <cx/platform/cpp.h>

CX_C_BEGIN

size_t cstrLen(const char *s);
char *cstrDup(const char *s);
size_t cstrLenw(const unsigned short *s);
unsigned short *cstrDupw(const unsigned short *s);
int cstrCmpi(const char *s1, const char *s2);

CX_C_END

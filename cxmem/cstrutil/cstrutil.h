#pragma once

#include <cx/core/cpp.h>

_EXTERN_C_BEGIN

size_t cstrLen(const char *s);
char *cstrDup(const char *s);
size_t cstrLenw(const short *s);
short *cstrDupw(const short *s);
int cstrCmpi(const char *s1, const char *s2);

_EXTERN_C_END

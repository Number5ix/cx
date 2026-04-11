#pragma once

#include "sarray.h"

#define ELEMPTR(hdr, i) \
    ((void*)((uintptr)(&hdr->data[0]) + (uintptr)(i) * stGetSize(hdr->elemtype)))

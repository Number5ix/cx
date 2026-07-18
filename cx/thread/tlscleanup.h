#pragma once

#include <cx/cx.h>

typedef void (*TLSCleanupCB)(void* data);

CX_C void thrRegisterCleanup(TLSCleanupCB cb, void* data);

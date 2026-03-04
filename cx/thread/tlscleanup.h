#pragma once

#include <cx/cx.h>

typedef void (*TLSCleanupCB)(void *data);

void thrRegisterCleanup(TLSCleanupCB cb, void *data);

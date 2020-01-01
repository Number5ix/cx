#pragma once

#include "cx/sys/hostid.h"
#include <mbedtls/md.h>

int32 hostIdPlatformInit(mbedtls_md_context_t *shactx);

// this function is not allowed to fail!!!
int32 hostIdPlatformInitFallback(mbedtls_md_context_t *shactx);

#if defined(_WIN32)
#include "cx/platform/win/win_hostid.h"
#else
#include "cx/platform/unix/unix_hostid.h"
#endif

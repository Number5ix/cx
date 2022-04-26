#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

// Kernel semaphore interface

// User programs should not use this directly. The cross-platform cx semaphore
// (and the higher-level synchronziation primitives built on it) uses userspace
// synchronization where possible and only defers to this kernel semaphore when
// it needs to sleep. As a result it is much more performant.

#if defined(_PLATFORM_WIN)
#include <cx/platform/win/win_ksema.h>
#elif defined(_PLATFORM_UNIX)
#include <cx/platform/unix/unix_ksema.h>
#elif defined(_PLATFORM_WASM)
#include <cx/platform/wasm/wasm_ksema.h>
#endif

typedef char kernelSema[KERNEL_SEMA_SIZE];

bool kernelSemaInit(kernelSema *sema, int32 count);
bool kernelSemaDestroy(kernelSema *sema);
bool kernelSemaDec(kernelSema *sema, bool platformevents);
bool kernelSemaTryDec(kernelSema *sema);
bool kernelSemaTryDecTimeout(kernelSema *sema, int64 timeout, bool platformevents);
bool kernelSemaInc(kernelSema *sema, int32 count);

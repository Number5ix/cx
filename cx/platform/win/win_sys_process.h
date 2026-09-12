#pragma once

// Windows-internal declarations for the process backend. The platform contract itself lives in
// cx/sys/process_private.h, which tail-includes this file.

#include "cx/sys/process.h"

// Vista and later. Succeeds against far more processes than PROCESS_QUERY_INFORMATION does,
// and covers what the per-process statistics calls need, so procOpen asks for it first and
// falls back only if it is refused. It is a plain constant rather than an import, so naming it
// costs nothing on an OS that does not recognize it -- OpenProcess simply fails and the
// fallback runs.
//
// Deliberately cx's own name rather than the SDK's. This header is reached through
// process_private.h, whose public half must never include <Windows.h>, so anything defined here
// lands before the SDK's own definition rather than after it -- an #ifndef guard cannot help,
// and the SDK spells the value (0x1000) where this would spell it 0x1000, which is a different
// token sequence and so a C4005 macro redefinition. Under /WX that fails the build, and only on
// the SDK versions that declare it, which is why it survived one toolchain and broke another.
#define CX_PROCESS_QUERY_LIMITED_INFORMATION 0x1000

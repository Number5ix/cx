#pragma once

// Windows-internal declarations for the process backend. The platform contract itself lives in
// cx/sys/process_private.h, which tail-includes this file.

#include "cx/sys/process.h"

// Vista and later. Succeeds against far more processes than PROCESS_QUERY_INFORMATION does,
// and covers what a future per-process stats call needs, so procOpen asks for it first and
// falls back only if it is refused. Defined here because the XP-era SDKs cx can be built with
// do not declare it; it is a plain constant, not an import, so using it costs nothing on an OS
// that does not recognize it -- OpenProcess simply fails and the fallback runs.
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

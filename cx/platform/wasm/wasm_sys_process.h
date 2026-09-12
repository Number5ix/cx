#pragma once

// wasm-internal declarations for the process backend. The platform contract itself lives in
// cx/sys/process_private.h, which tail-includes this file. There is no Process subclass here,
// because nothing on this platform ever constructs one.

#include "cx/sys/process.h"

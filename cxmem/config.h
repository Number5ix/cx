#pragma once

// -------------------------- Global Options -------------------------
// These must be defined once when the library is built.

// Exactly one of these must be defined

// Use jemalloc as default allocator
#define XALLOC_USE_JEMALLOC 1
// Use MSVCRT malloc
// #define XALLOC_USE_MSVCRT 1

// -------------------------- Local Options --------------------------
// These can be project-specific but must be set in every C file,
// usually by means of a compiler flag.

// Remap all standard malloc/free calls to use whichever library
// xalloc is configured to use.
// #define XALLOC_REMAP_MALLOC 1

#pragma once

#include <cxmem/config.h>
#include <cx/core/cpp.h>

// ------------------------- Project Options -------------------------
// These can be enabled on a project-by-project basis by setting them
// in the main C file. Define the options first, then include
// <cx/static_config.h>

// Extra sanity checking (assert on error)
// #define XALLOC_DEBUG_CHECKS 1

// Allocator-specific options

// Zero all memory by default
// #define XALLOC_JEMALLOC_ZERO 1
// Junk-fill on alloc/free
// #define XALLOC_JEMALLOC_JUNK 1
// Don't use multiple arenas (slight performance boost on single-threaded programs)
// #define XALLOC_JEMALLOC_SINGLE_ARENA 1
// Disable the thread cache
// #define XALLOC_JEMALLOC_NO_TCACHE 1
// Allocate redzones (what CRT calls NoMansLand)
// #define XALLOC_JEMALLOC_REDZONES 1
// Any other custom malloc_conf options
// #define XALLOC_JEMALLOC_CUSTOM_OPTS 1

// Some rules-based defaults
#if DEBUG_LEVEL >= 2 && defined(XALLOC_USE_JEMALLOC)
#ifndef XALLOC_JEMALLOC_JUNK
#define XALLOC_JEMALLOC_JUNK 1
#endif
#endif

#if defined(XALLOC_USE_JEMALLOC)

#ifdef XALLOC_DEBUG_CHECKS
#define _XA_ABORT ",abort:true"
#else
#define _XA_ABORT ""
#endif

#ifdef XALLOC_JEMALLOC_ZERO
#define _XA_ZERO ",zero:true"
#else
#define _XA_ZERO ""
#endif

#ifdef XALLOC_JEMALLOC_JUNK
#define _XA_JUNK ",junk:true"
#else
#define _XA_JUNK ""
#endif

#ifdef XALLOC_JEMALLOC_SINGLE_ARENA
#define _XA_ARENA ",narenas:1"
#else
#define _XA_ARENA ""
#endif

#ifdef XALLOC_JEMALLOC_NO_TCACHE
#define _XA_TCACHE ",tcache:false"
#else
#define _XA_TCACHE ""
#endif

#ifdef XALLOC_JEMALLOC_CUSTOM_OPTS
#define _XA_CUSTOM "," XALLOC_JEMALLOC_CUSTOM_OPTS
#else
#define _XA_CUSTOM ""
#endif

#ifdef XALLOC_JEMALLOC_NO_BACKGROUND_THREAD
#define _XA_BACKGROUND_THREAD ""
#else
#define _XA_BACKGROUND_THREAD ",background_thread:true"
#endif

#define XALLOC_STATIC_CONFIG CX_C const char *je_malloc_conf = "dirty_decay_ms:60000,muzzy_decay_ms:120000" \
    _XA_BACKGROUND_THREAD _XA_ABORT _XA_ZERO _XA_JUNK _XA_ARENA _XA_TCACHE _XA_CUSTOM;

#elif defined(XALLOC_USE_MIMALLOC)

#define XALLOC_STATIC_CONFIG

#elif defined(XALLOC_USE_MSVCRT)

#ifdef XALLOC_DEBUG_CHECKS
#define XALLOC_STATIC_CONFIG CX_C const int xalloc_msvcrt_debug = 1;
#else
#define XALLOC_STATIC_CONFIG CX_C const int xalloc_msvcrt_debug = 0;
#endif

#endif

CX_C XALLOC_STATIC_CONFIG

CX_C const int you_forgot_to_include_cx_static_config_h = 0;

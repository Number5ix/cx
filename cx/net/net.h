#pragma once

#include <cx/net/addr.h>
#include <cx/net/queue.h>
#include <cx/net/socket.h>

/// @file net.h
/// @brief Main public network API

/// @defgroup net_core Core Networking API
/// @ingroup net
/// @{
/// Queue management and socket functions and definitions.

/// Create a network queue with the specified number of worker threads.
/// @param nthreads Number of worker threads, or 0 for polled mode
/// @param flags Creation flags (NetQueueFlags)
/// @return Pointer to the created NetQueue (must be released with objRelease)
/// @note If the select() backend is being used with a thread pool, one additional thread will be
/// created to handle the select() loop.
NetQueue* netqueueCreate(int32 nthreads, flags_t flags);

/// @}

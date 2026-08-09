#pragma once

#include <cx/log/log.h>
#include <cx/net/addr.h>
#include <cx/net/filter.h>
#include <cx/net/flow.h>
#include <cx/net/pool.h>
#include <cx/net/queue.h>
#include <cx/net/socket.h>

/// @file net.h
/// @brief Main public network API

/// @addtogroup net_misc
/// @{
/// Queue management and socket functions and definitions.

/// Log channel for the networking module
///
/// All diagnostics the net layer emits (such as the dev-build slow-callback warning) go through
/// this channel. Log channels are filtered by pointer identity, so this is public precisely so
/// applications can pass it as the channel filter to logRegisterDest() to route or suppress net
/// diagnostics as a group. Created during net initialization; NULL until the first
/// netqueueCreate().
extern LogChannel* NetLogChannel;

/// @}

/// @addtogroup net_queue
/// @{

// The one part of queue creation that cannot be a class member: it dispatches to the
// platform-specific backend factory, which picks and constructs the derived queue class.

/// Create a network queue
///
/// @param conf Creation configuration; start from a preset and override what you need
/// @return Pointer to the created NetQueue (must be released with objRelease)
/// @note If the select() backend is being used with a thread pool, one additional thread will be
/// created to handle the select() loop.
///
/// Example:
/// @code
///   NetQueueConfig conf;
///   netqueuePresetServer(&conf);
///   conf.maxflows = 50000;
///   NetQueue* q = netqueueCreate(&conf);
/// @endcode
_Ret_maybenull_ NetQueue* netqueueCreate(_In_ const NetQueueConfig* conf);

/// @}

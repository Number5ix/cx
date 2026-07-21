#pragma once

#include <cx/buffer/bufchain.h>
#include <cx/buffer/bufpool.h>
#include <cx/buffer/bufring.h>

/// @file buffer.h
/// @brief Buffer management utilities aggregated header

/// @defgroup buffer Buffers
/// @{
/// Efficient buffer management for streaming I/O and data processing.
///
/// The buffer module provides buffer implementations optimized for streaming
/// data operations with minimal memory allocation overhead.
///
/// - @ref buffer_simple "Simple Buffers" -- a sized block of bytes, the common currency
/// - @ref buffer_bufring "Buffer Ring" -- ring buffer, best for byte streams and small ops
/// - @ref buffer_bufchain "Buffer Chain" -- segment chain, best for zero-copy message handoff
/// - @ref buffer_bufpool "Buffer Pool" -- thread-safe freelist of fixed-size buffers

/// @}
// end of buffer group

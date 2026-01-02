/// @file serialize.h
/// @brief Serialization and streaming utilities
///
/// @defgroup serialize Serialization
/// @{
///
/// The serialization module provides stream buffers and utilities for efficient
/// data streaming and serialization. Stream buffers support both push and pull modes
/// with automatic buffering and flow control.
///
/// **Key Components:**
/// - **StreamBuffer** - Core buffering system with producer/consumer model
/// - **File Streaming** - Stream to/from files
/// - **String Streaming** - Stream to/from strings
///
/// @}

#pragma once

#include <cx/serialize/streambuf.h>
#include <cx/serialize/sbfile.h>
#include <cx/serialize/sbstring.h>

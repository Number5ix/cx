#pragma once

/// @file log.h
/// @brief Logging system includes

/// @defgroup log Logging System
/// @{
///
/// CX provides a flexible, multi-backend logging system with support for
/// channel-based logging, filtering, and multiple output destinations including
/// files, memory buffers, and retention rings for early-startup capture.

#include <cx/log/log.h>
#include <cx/log/logconsole.h>
#include <cx/log/logctx.h>
#include <cx/log/logfile.h>
#include <cx/log/loggroup.h>
#include <cx/log/logmembuf.h>
#include <cx/log/logring.h>
#include <cx/log/logserializer.h>
#include <cx/log/logvolume.h>

/// @}  // end of log group

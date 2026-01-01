/// @file time.h
/// @brief Time and clock functions

#pragma once

/// @defgroup time Time
/// @{
///
/// CX and related systems represent time as the number of microseconds since
/// noon on January 1, 4713 BCE (UTC) - the Julian date epoch.
///
/// This provides an unambiguous time representation that:
/// - Covers dates from 4713 BCE through approximately 294,276 CE
/// - Uses a single 64-bit signed integer
/// - Has microsecond precision
/// - Is independent of time zones
/// - Does not require leap second handling
///
/// The time system includes:
/// - Clock functions for wall time and monotonic timers
/// - Conversion to/from standard C time types (time_t, timespec)
/// - Time decomposition and composition (TimeParts)
/// - Local time zone conversion
///
/// @}

#include <cx/time/clock.h>
#include <cx/time/time.h>

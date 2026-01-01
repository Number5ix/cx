/// @file clock.h
/// @brief System clock functions

#pragma once

#include <cx/cx.h>

/// @defgroup time_clock Clock Functions
/// @ingroup time
/// @{
///
/// Functions for obtaining current time from system clocks.

/// Gets the current "wall time" (system time).
///
/// Returns the current system time in the Julian date epoch format.
/// This time may jump forward or backward if the system clock is adjusted.
///
/// @return Number of microseconds since the Julian date epoch
int64 clockWall();

/// Gets the current wall time adjusted for the local time zone.
///
/// Similar to clockWall(), but adjusts the time for local time zone rules currently
/// in effect. This is not a proper time reference and may be discontinuous -- it
/// is intended only for displaying the current date and time to users.
///
/// @return Number of microseconds since the Julian date epoch, in local time
int64 clockWallLocal();

/// Gets a monotonically increasing time for measuring intervals.
///
/// Returns a monotonically increasing time that is guaranteed to not go backwards or
/// skip around when the clock is reset. The actual value does not have any
/// particular meaning other than being measured in microseconds, and may not be
/// unique across operating system restarts. This should be used for timing events
/// and measuring durations.
///
/// @return Monotonic timestamp in microseconds
int64 clockTimer();

/// @}
// end of time_clock group

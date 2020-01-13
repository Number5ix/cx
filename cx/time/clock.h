#pragma once

#include <cx/cx.h>

// Gets the current "wall time" (system time). Returns the number of microseconds
// since the julian date epoch.
int64 clockWall();

// Gets a monotonically increasing time that is guaranteed to not go backwards or
// skip around when the clock is reset. The actual value does not have any
// particular meaning other than being measured in microseconds, and may not be
// unique across operating system restarts. This should be used for timing events.
int64 clockTimer();

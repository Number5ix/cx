#pragma once

#include <cx/cx.h>

// Gets the current "wall time" (system time). Returns the number of microseconds
// since the julian date epoch.
uint64 clockWall();

// Gets a monotonically increasing time that is guaranteed to not go backwards or
// skip around when the clock is reset. This should be used for timing events.
uint64 clockTimer();

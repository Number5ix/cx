#pragma once

#include <cx/closure/closure.h>

// commonly used callback functions that do useful things, to be used as closures

// Signals the event passed as ptr in the first cvar when called
bool ccbSignalEvent(stvlist* cvars, stvlist* args);

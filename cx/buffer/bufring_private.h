#include "bufring.h"

// Returns how much data can be read contiguously from the head of the ring buffer.
// This is an internal function intended for use by the socket code only.
size_t _bufringReadContigAvail(_In_ BufRing* ring);

// Tries to steal the head buffer if it is 0-aligned. This is an internal function intended
// for use by the socket code only.
Buffer _bufringStealHead(_Inout_ BufRing* ring);

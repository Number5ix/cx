#pragma once

#include <cx/serialize/streambuf.h>
#include <cx/string/strbase.h>

// Stock streambuffer producer for strings

// Pushes the entire contents of a string into a streambuffer.
// Will auto-chunk based on the streambuffer's target size.
bool sbufStrIn(StreamBuffer *sb, strref str);

// -- or --
// 
// Registers as a producer with the streambuffer in pull mode.
bool sbufStrPRegisterPull(StreamBuffer *sb, strref str);

// ======================================================================

// Stock streambuffer consumer that outputs to a string

// Consumes all available data from the buffer and outputs to a string.
// This overwrites any existing string.
bool sbufStrOut(StreamBuffer *sb, string *strout);

// -- or -- 

// Registers as a consumer with the streambuffer in push mode.
// This consumer appends all output to the given string.
bool sbufStrCRegisterPush(StreamBuffer *sb, string *strout);

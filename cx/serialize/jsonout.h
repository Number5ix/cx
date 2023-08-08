#pragma once

#include "jsoncommon.h"

typedef struct JSONOut JSONOut;
typedef struct SSDNode SSDNode;
typedef struct SSDLock SSDLock;

#define JSON_Indent(x) (x & JSON_Indent_Mask)
enum JSON_OUT_FLAGS {
  //JSON_Indent       = 0x00000000,             // Indent a certain number of spaces for each level in the hierarchy
    JSON_Indent_Mask  = 0x0000001f,
    JSON_Compact      = 0x00000100,             // Omit spaces around { }, [ ], and :
    JSON_Single_Line  = 0x00000200,             // Do not insert linefeed characters, this causes indent to be ignored
    JSON_ASCII_Only   = 0x00000400,             // escape all non-ASCII characters in the output

    JSON_Unix_EOL     = 0x00010000,             // Force UNIX-style EOL regardless of operating system
    JSON_Windows_EOL  = 0x00020000,             // Force Windows-style EOL regardless of operating system

    // presets
    JSON_Pretty       = 0x00000004,             // 4-space indent
    JSON_Minimal      = 0x00000300,             // Compact | Single Line
};

// JSON OUTPUT
// These functions write to a streambuffer in PUSH mode.
// They should be repeatedly called with parse events in order to build the output.
JSONOut *jsonOutBegin(StreamBuffer *sb, uint32 flags);
bool jsonOut(JSONOut *jo, JSONParseEvent *ev);
void jsonOutEnd(JSONOut **jo);

// JSON TREE OUTPUT
// Serialize an SSD tree to a streambuffer as JSON
bool jsonOutTree(StreamBuffer *sb, SSDNode *tree, uint32 flags, SSDLock *lock_opt);

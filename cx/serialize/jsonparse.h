#pragma once

#include <cx/serialize/jsoncommon.h>
#include <cx/ssdtree/node/ssdnode.h>

typedef void (*jsonParseCB)(JSONParseEvent *ev, void *userdata);

// JSON STREAM PARSER
// Parses JSON data from a streambuffer, which must support PULL mode.
// Repeatedly calls the callback with the user-supplied data.
bool jsonParse(StreamBuffer *sb, jsonParseCB callback, void *userdata);

// JSON TREE PARSER
// Parses JSON data from a streambuffer, which must support PULL mode.
// Fully loads the data into a semi-structured data tree.
SSDNode *jsonParseTree(StreamBuffer *sb);

#pragma once

#include <cx/serialize/jsoncommon.h>

typedef struct SSDNode SSDNode;
typedef struct SSDTree SSDTree;
typedef void (*jsonParseCB)(JSONParseEvent *ev, void *userdata);

// JSON STREAM PARSER
// Parses JSON data from a streambuffer, which must support PULL mode.
// Repeatedly calls the callback with the user-supplied data.
bool jsonParse(StreamBuffer *sb, jsonParseCB callback, void *userdata);

// JSON TREE PARSER
// Parses JSON data from a streambuffer, which must support PULL mode.
// Fully loads the data into a semi-structured data tree.
SSDNode *jsonParseTree(StreamBuffer *sb);
SSDNode *jsonParseTreeCustom(StreamBuffer *sb, SSDTree *tree);

// Parses JSON data from a string.
SSDNode *jsonTreeFromString(strref str);

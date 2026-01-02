/// @file jsonparse.h
/// @brief JSON parsing interface

#pragma once

#include <cx/serialize/jsoncommon.h>

typedef struct SSDNode SSDNode;
typedef struct SSDTree SSDTree;

/// @defgroup serialize_json_parse JSON Parsing
/// @ingroup serialize_json
/// @{
///
/// Parse JSON from stream buffers into SSD trees or via callbacks.
///
/// The JSON parser supports two modes:
///
/// **Event-Driven Parsing (jsonParse):**
/// Invokes a callback for each JSON element as it's parsed. Suitable for
/// streaming large files or custom data processing.
///
/// **Tree Parsing (jsonParseTree):**
/// Fully loads JSON into an SSD tree for convenient random access and
/// manipulation.
///
/// Both modes require a stream buffer in **PULL mode**.
///
/// Example (event-driven):
/// @code
///   void handleEvent(JSONParseEvent *ev, void *ctx) {
///       if (ev->etype == JSON_String) {
///           printf("String: %s\n", strC(ev->edata.strData));
///       }
///   }
///
///   VFSFile *file = vfsOpen(vfs, _S"data.json", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   jsonParse(sb, handleEvent, NULL);
/// @endcode
///
/// Example (tree parsing):
/// @code
///   VFSFile *file = vfsOpen(vfs, _S"config.json", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   SSDNode *root = jsonParseTree(sb);
///
///   string name = 0;
///   ssdVal(root, _S"/user/name", string, &name);
///   objRelease(&root);
/// @endcode

/// void (*jsonParseCB)(JSONParseEvent *ev, void *userdata)
///
/// Callback function type for event-driven JSON parsing.
///
/// This callback is invoked for each JSON element as it's parsed. The
/// JSONParseEvent contains the event type, current parser context, and
/// event-specific data.
///
/// **IMPORTANT:** String data in events (strData) is only valid during the
/// callback. Copy the string if you need to retain it.
///
/// @param ev Parse event containing type, context, and data
/// @param userdata User context pointer passed to jsonParse()
typedef void (*jsonParseCB)(_In_ JSONParseEvent* ev, _Inout_opt_ void* userdata);

/// bool jsonParse(StreamBuffer *sb, jsonParseCB callback, void *userdata)
///
/// Parses JSON data using an event-driven callback interface.
///
/// The stream buffer must be configured in PULL mode before calling this
/// function. The parser invokes the callback for each JSON element:
/// objects, arrays, strings, numbers, booleans, and null values.
///
/// This mode is ideal for:
/// - Processing large JSON files with low memory overhead
/// - Custom data transformations during parsing
/// - Selective data extraction without building full tree
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb Stream buffer in pull mode (invalidated after call)
/// @param callback Function to invoke for each parse event
/// @param userdata User context passed to callbacks
/// @return true on successful parse, false on error
///
/// Example:
/// @code
///   typedef struct {
///       int objectCount;
///       int arrayCount;
///   } Stats;
///
///   void countElements(JSONParseEvent *ev, void *ctx) {
///       Stats *stats = (Stats *)ctx;
///       if (ev->etype == JSON_Object_Begin) stats->objectCount++;
///       if (ev->etype == JSON_Array_Begin) stats->arrayCount++;
///   }
///
///   Stats stats = {0};
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   jsonParse(sb, countElements, &stats);
/// @endcode
bool jsonParse(_Inout_ StreamBuffer* sb, _In_ jsonParseCB callback, _Inout_opt_ void* userdata);

/// SSDNode *jsonParseTree(StreamBuffer *sb)
///
/// Parses JSON data into an SSD tree.
///
/// Fully loads the JSON data into a semi-structured data tree for convenient
/// access and manipulation. The returned tree root must be released with
/// objRelease() when done.
///
/// The stream buffer must be configured in PULL mode before calling this
/// function.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb Stream buffer in pull mode (invalidated after call)
/// @return Root node of parsed tree, or NULL on error
///
/// Example:
/// @code
///   VFSFile *file = vfsOpen(vfs, _S"data.json", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///
///   SSDNode *root = jsonParseTree(sb);
///   if (root) {
///       string value = 0;
///       ssdVal(root, _S"/path/to/value", string, &value);
///       objRelease(&root);
///   }
/// @endcode
_Ret_opt_valid_ SSDNode* jsonParseTree(_Inout_ StreamBuffer* sb);

/// SSDNode *jsonParseTreeCustom(StreamBuffer *sb, SSDTree *tree)
///
/// Parses JSON data into an existing SSD tree.
///
/// Like jsonParseTree(), but allows using a pre-existing SSDTree for node
/// allocation. Useful when you need to control tree properties or maintain
/// multiple related trees.
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb Stream buffer in pull mode (invalidated after call)
/// @param tree Existing SSD tree to allocate nodes from (optional, NULL creates new tree)
/// @return Root node of parsed tree, or NULL on error
_Ret_opt_valid_ SSDNode* jsonParseTreeCustom(_Inout_ StreamBuffer* sb, _In_opt_ SSDTree* tree);

/// SSDNode *jsonTreeFromString(strref str)
///
/// Parses JSON data from a string into an SSD tree.
///
/// Convenience function that internally creates a stream buffer, parses the
/// JSON string, and returns the resulting tree. Equivalent to manually
/// setting up a stream buffer with sbufStrPRegisterPull() and calling
/// jsonParseTree().
///
/// @param str JSON string to parse
/// @return Root node of parsed tree, or NULL on error
///
/// Example:
/// @code
///   SSDNode *root = jsonTreeFromString(_S"{\"name\": \"test\", \"value\": 42}");
///   if (root) {
///       int32 value;
///       ssdVal(root, _S"/value", int32, &value);
///       objRelease(&root);
///   }
/// @endcode
_Ret_opt_valid_ SSDNode* jsonTreeFromString(_In_opt_ strref str);

/// @}  // end of serialize_json_parse

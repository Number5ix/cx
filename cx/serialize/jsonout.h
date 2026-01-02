/// @file jsonout.h
/// @brief JSON serialization interface

#pragma once

#include <cx/ssdtree/ssdshared.h>
#include "jsoncommon.h"

typedef struct JSONOut JSONOut;

/// @defgroup serialize_json_output JSON Output
/// @ingroup serialize_json
/// @{
///
/// Serialize data to JSON format via stream buffers.
///
/// The JSON output system supports two modes:
///
/// **Event-Driven Output (jsonOut):**
/// Build JSON by sending a sequence of JSONParseEvent structures. Useful for
/// custom serialization or transforming data during output.
///
/// **Tree Serialization (jsonOutTree):**
/// Automatically serialize an entire SSD tree to JSON with configurable
/// formatting.
///
/// Both modes write to stream buffers in **PUSH mode**.

/// @defgroup serialize_json_flags JSON Output Flags
/// @ingroup serialize_json_output
/// @{
///
/// Configuration flags for JSON output formatting.

/// Helper macro to create indent flags
///
/// Use this to specify a custom number of spaces for indentation.
///
/// Example: `JSON_Indent(2)` for 2-space indent
#define JSON_Indent(x) (x & JSON_Indent_Mask)

/// JSON output formatting flags
enum JSON_OUT_FLAGS {
    /// Mask for extracting indent value (0-31 spaces)
    JSON_Indent_Mask = 0x0000001f,
    /// Omit spaces around { }, [ ], and : for compact output
    JSON_Compact     = 0x00000100,
    /// Do not insert newlines (causes indent to be ignored)
    JSON_Single_Line = 0x00000200,
    /// Escape all non-ASCII characters in output
    JSON_ASCII_Only  = 0x00000400,

    /// Force Unix-style line endings (LF) regardless of OS
    JSON_Unix_EOL    = 0x00010000,
    /// Force Windows-style line endings (CRLF) regardless of OS
    JSON_Windows_EOL = 0x00020000,

    /// Preset: 4-space indent with newlines (readable output)
    JSON_Pretty  = 0x00000004,
    /// Preset: Compact single-line output (minimal size)
    JSON_Minimal = 0x00000300,
};

/// @}  // end of serialize_json_flags

/// @defgroup serialize_json_stream Stream Output
/// @ingroup serialize_json_output
/// @{
///
/// Event-driven JSON output using parse events.

/// JSONOut *jsonOutBegin(StreamBuffer *sb, flags_t flags)
///
/// Begins event-driven JSON output to a stream buffer.
///
/// Initializes a JSON output context that writes to the stream buffer in
/// PUSH mode. After calling this, repeatedly call jsonOut() with
/// JSONParseEvent structures to build the output, then call jsonOutEnd()
/// to finalize.
///
/// @param sb Stream buffer to write to
/// @param flags Output formatting flags from JSON_OUT_FLAGS
/// @return JSON output context, or NULL on error
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   string output = 0;
///   sbufStrCRegisterPush(sb, &output);
///
///   JSONOut *jo = jsonOutBegin(sb, JSON_Pretty);
///
///   JSONParseEvent ev = {0};
///   ev.etype = JSON_Object_Begin;
///   jsonOut(jo, &ev);
///
///   ev.etype = JSON_Object_Key;
///   ev.edata.strData = _S"name";
///   jsonOut(jo, &ev);
///
///   ev.etype = JSON_String;
///   ev.edata.strData = _S"value";
///   jsonOut(jo, &ev);
///
///   ev.etype = JSON_Object_End;
///   jsonOut(jo, &ev);
///
///   jsonOutEnd(&jo);
/// @endcode
_Ret_opt_valid_ JSONOut* jsonOutBegin(_Inout_ StreamBuffer* sb, flags_t flags);

/// bool jsonOut(JSONOut *jo, JSONParseEvent *ev)
///
/// Writes a JSON element using a parse event.
///
/// Call this repeatedly with appropriate JSONParseEvent structures to build
/// the JSON output. Events should be sent in a valid sequence (e.g., object
/// keys must be followed by values).
///
/// @param jo JSON output context from jsonOutBegin()
/// @param ev Parse event describing the element to output
/// @return true on success, false on error
_Check_return_ bool jsonOut(_Inout_ JSONOut* jo, _In_ JSONParseEvent* ev);

/// void jsonOutEnd(JSONOut **jo)
///
/// Finalizes JSON output and cleans up the context.
///
/// Flushes any remaining data to the stream buffer, finishes the producer,
/// and frees the JSON output context.
///
/// @param jo Pointer to JSON output context (set to NULL after call)
_At_(*jo, _Pre_valid_ _Post_invalid_) void jsonOutEnd(_Inout_ JSONOut** jo);

/// @}  // end of serialize_json_stream

/// @defgroup serialize_json_tree Tree Output
/// @ingroup serialize_json_output
/// @{
///
/// Automatic JSON serialization from SSD trees.

/// bool jsonOutTree(StreamBuffer *sb, SSDNode *tree, [flags])
///
/// Serializes an SSD tree to a stream buffer as JSON.
///
/// Automatically traverses the entire tree and outputs properly formatted
/// JSON. The stream buffer should have a consumer registered (e.g.,
/// sbufStrCRegisterPush() or sbufFileCRegisterPush()).
///
/// **IMPORTANT:** The stream buffer is invalidated after this call.
///
/// @param sb Stream buffer to write to (invalidated after call)
/// @param tree Root node of SSD tree to serialize
/// @param ... (flags) Optional output formatting flags (defaults to platform EOL with no indent)
/// @return true on success, false on error
///
/// Example:
/// @code
///   SSDNode *root = ssdCreate(0);
///   ssdSet(root, _S"/name", stvar(string, _S"test"));
///   ssdSet(root, _S"/value", stvar(int32, 42));
///
///   VFSFile *file = vfsOpen(vfs, _S"output.json", FS_Write | FS_Create);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFileCRegisterPush(sb, file, true);
///   jsonOutTree(sb, root, JSON_Pretty);
///
///   objRelease(&root);
/// @endcode
#define jsonOutTree(sb, tree, ...) \
    _jsonOutTree(sb, tree, opt_flags(__VA_ARGS__), (SSDLockState*)_ssdCurrentLockState)
bool _jsonOutTree(_Inout_ StreamBuffer* sb, _In_ SSDNode* tree, flags_t flags,
                  _Inout_opt_ SSDLockState* _ssdCurrentLockState);

/// bool jsonTreeToString(string *out, SSDNode *tree, [flags])
///
/// Serializes an SSD tree to a string as JSON.
///
/// Convenience function that internally creates a stream buffer and writes
/// the JSON output to a string. Equivalent to manually setting up a stream
/// buffer with sbufStrCRegisterPush() and calling jsonOutTree().
///
/// @param out String to receive JSON output
/// @param tree Root node of SSD tree to serialize
/// @param ... (flags) Optional output formatting flags (defaults to platform EOL with no indent)
/// @return true on success, false on error
///
/// Example:
/// @code
///   SSDNode *root = ssdCreate(0);
///   ssdSet(root, _S"/items/0", stvar(int32, 1));
///   ssdSet(root, _S"/items/1", stvar(int32, 2));
///
///   string json = 0;
///   jsonTreeToString(&json, root, JSON_Pretty);
///   // json now contains: {\n  "items": [1, 2]\n}\n
///
///   strDestroy(&json);
///   objRelease(&root);
/// @endcode
#define jsonTreeToString(out, tree, ...) \
    _jsonTreeToString(out, tree, opt_flags(__VA_ARGS__), (SSDLockState*)_ssdCurrentLockState)
bool _jsonTreeToString(_Inout_ string* out, _In_ SSDNode* tree, flags_t flags,
                       _Inout_opt_ SSDLockState* _ssdCurrentLockState);

/// @}  // end of serialize_json_tree
/// @}  // end of serialize_json_output

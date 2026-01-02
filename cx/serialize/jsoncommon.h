/// @file jsoncommon.h
/// @brief Common types for JSON parsing and output
///
/// @defgroup serialize_json JSON
/// @ingroup serialize
/// @{
///
/// JSON parsing and serialization built on stream buffers.
///
/// The JSON module provides comprehensive support for parsing and generating
/// JSON data using the stream buffer abstraction. It bridges stream buffers
/// with SSD (Semi-Structured Data) trees for easy manipulation of JSON.
///
/// **Key Features:**
/// - Stream-based parsing for low memory overhead
/// - Event-driven callback interface for custom processing
/// - Direct SSD tree integration for convenient data access
/// - Configurable output formatting (pretty-print, compact, minimal)
/// - Full Unicode support with optional ASCII-only escaping
///
/// **Use Cases:**
/// - Load/save configuration files
/// - REST API data exchange
/// - Structured logging output
/// - Data serialization and deserialization

#pragma once

#include <cx/serialize/streambuf.h>

/// @defgroup serialize_json_types JSON Types
/// @ingroup serialize_json
/// @{
///
/// Common types used for JSON parsing and output.

/// JSON parser context types
typedef enum JSON_PARSE_CONTEXT {
    /// Top-level context (bare value)
    JSON_Top,
    /// Currently parsing an object
    JSON_Object,
    /// Currently parsing an array
    JSON_Array,
} JsonContextType;

/// JSON parse event types
typedef enum JSON_PARSE_EVENT {
    /// Never sent; indicates a parse error
    JSON_Parse_Unknown,
    /// New object starts at current context
    JSON_Object_Begin,
    /// Valid JSON string parsed in object key context
    JSON_Object_Key,
    /// End of current object
    JSON_Object_End,
    /// New array starts at current context
    JSON_Array_Begin,
    /// End of current array
    JSON_Array_End,
    /// String value parsed
    JSON_String,
    /// Number parsed that fits in int64
    JSON_Int,
    /// Number parsed that requires float64
    JSON_Float,
    /// Boolean true value parsed
    JSON_True,
    /// Boolean false value parsed
    JSON_False,
    /// Null value parsed
    JSON_Null,
    /// Parse error occurred; strData contains error message (JSON_End follows)
    JSON_Error,
    /// Parsing finished; cleanup user context if needed
    JSON_End
} JsonEventType;

typedef struct JSONParseContext JSONParseContext;

/// JSON parser context stack
///
/// Tracks the current parsing position in the JSON hierarchy. The parser
/// maintains a stack of these contexts as it descends into nested objects
/// and arrays.
typedef struct JSONParseContext {
    /// Parent context in the stack
    JSONParseContext* parent;
    /// Type of current context
    JsonContextType ctype;

    /// Context-specific data
    union {
        /// Current key being parsed (for objects)
        string curKey;
        /// Current array index (for arrays)
        int32 curIdx;
    } cdata;
} JSONParseContext;

/// JSON parse event
///
/// Delivered to callbacks during parsing. Contains the event type, current
/// context, and event-specific data.
typedef struct JSONParseEvent {
    /// Current parser context
    JSONParseContext* ctx;

    /// Type of event
    JsonEventType etype;
    /// Event-specific data
    union {
        /// Integer value (for JSON_Int)
        int64 intData;
        /// Float value (for JSON_Float)
        double floatData;
        /// String value (for JSON_String, JSON_Object_Key, JSON_Error)
        string strData;
    } edata;
} JSONParseEvent;

/// @}  // end of serialize_json_types
/// @}  // end of serialize_json

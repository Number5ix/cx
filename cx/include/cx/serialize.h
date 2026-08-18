/// @file serialize.h
/// @brief Serialization and streaming utilities
///
/// @defgroup serialize Serialization
/// @{
///
/// Writes any stype-described value -- scalars, containers, structs, and opted-in object
/// classes -- to a wire format and reads it back, driven by the same reflection metadata
/// (`StructInfo`, class metadata) the rest of cx already uses. JSON, a compact binary format,
/// and SSD trees ship as backends; an application can add its own without touching cx.
///
/// **Key Components:**
/// - **The traverser** - one generic walk over cx values: `serWrite()` / `serRead()`
/// - **Backends** - `SerWriter` / `SerReader`: JSON (@ref serialize_json_backend), a compact
///   binary format (@ref serialize_binary_backend), and SSD trees (@ref serialize_ssd) ship
///   with cx; an application can add its own the same way.
/// - **`STypeInfoExt`** - a value's declared, wire-facing type (`cx/stype/stype.h`); the
///   traverser works from this rather than from a plain `stype`
/// - **`StreamBuffer`** (@ref serialize_streambuf) - the producer/consumer transport that JSON
///   and binary documents stream over, with adapters for strings, files, and consoles
///
/// @defgroup serialize_overview Overview
/// @ingroup serialize
/// @{
///
/// @section serialize_usage Writing and reading a value
///
/// Every call names a backend, a type, and the value -- it reads like any other generic cx
/// call, with the type name immediately preceding the value it describes:
///
/// @code
///   string json = 0;
///   StreamBuffer *sb = sbufStrCreatePush(&json, 4096);
///
///   SerWriter *w = serJsonWriterCreate(sb, SER_JSON_Pretty);
///   serWrite(w, MyStruct, val);
///   if (!serWriterFinish(w))
///       logFmt(Error, _SL("write failed at ${string}: ${string}"),
///              stvar(string, w->err.path), stvar(string, w->err.msg));
///   serWriterDestroy(&w);
///   sbufRelease(&sb);
///
///   // ... send or store json ...
///
///   sb = sbufCreate(4096);
///   sbufStrPRegisterPull(sb, json);
///   SerReader *r = serJsonReaderCreate(sb, 0);
///   MyStruct out;
///   structInit(MyStruct, &out);
///   serRead(r, MyStruct, &out);
///   serReaderDestroy(&r);
///   sbufRelease(&sb);
/// @endcode
///
/// Backends follow **Create/Destroy**: each supplies its own create function taking whatever
/// transport it needs (a `StreamBuffer` for JSON and binary, nothing for SSD trees) and
/// returns a plain `SerWriter*` / `SerReader*` -- the concrete backend is never named again
/// after that. `serWriterFinish()` is the writer's last chance to report failure; either way,
/// release the handle with `serWriterDestroy()` / `serReaderDestroy()`.
///
/// A struct member declared `[default value]` (see @ref struct) is omitted from the document
/// when it still holds that value, and an omitted member reads back as that value -- for every
/// backend, since it's the traverser doing the omitting, not the format. Pass `SER_EmitDefaults`
/// to write those members anyway.
///
/// @section serialize_schema Describing a slot the macro can't name
///
/// The macros above work from a **schema** -- an `STypeInfoExt`, which says what a slot is
/// *declared* to be, exactly, including nested and nominal names. An ordinary `stype` isn't
/// enough on its own: every object class collapses to plain `object`, and a container's element
/// type lives in the container header rather than in its descriptor, while the wire needs the
/// class's actual name and the container's actual element type.
///
/// `serWrite(w, X, val)` finds that schema for you, as `stExt(X)`. Some slots have no single
/// type name to give it, though -- a struct member's declared element types, a container you
/// described by hand, or a `structp` in a `structset` slot, where the schema and the value's
/// type come from different names. Those call `_serWrite()` / `_serRead()` with the descriptor
/// directly:
///
/// @code
///   const StructMemberDesc *m = ...;                 // e.g. from a StructInfo member table
///   _serWrite(w, m->schema, stArg(structp, shape));
/// @endcode
///
/// The underscore forms are the same traversal with the schema supplied explicitly; they are
/// fair game where the macro can't reach, but the macro is what application code should use.
/// A schema is never NULL -- a slot whose type isn't fixed by its own declaration passes the
/// built-in descriptor for whatever kind of value it holds, e.g. `stExt(stvar)`.
///
/// @section serialize_sets Slots that hold more than one type
///
/// A `structp` member declared over a `structset`, or an `object` member declared over a
/// `classset`, resolves itself: the set travels with the slot's schema, so a reader
/// reconstructs the right concrete type from a wire name with nothing registered. See @ref
/// struct for the struct side (`structset`); `classset` is the same idea for classes:
///
/// @code
///   classset NodeSet { TextNode, ImageNode }
///
///   [serialize] class Document {
///       object[NodeSet] root;       // any class in NodeSet
///       sarray[object[NodeSet]] history;
///   }
/// @endcode
///
/// Writing a value whose type isn't in the declared set fails, and a document naming a type
/// outside it is rejected on the way in -- the set is the slot's vocabulary in both
/// directions.
///
/// A document whose *top-level* value is dynamic, or a slot declared as bare `object` or
/// `structp`, has no set to resolve through -- hand the reader explicit resolvers instead:
///
/// @code
///   serReaderAddResolver(r, serStructSetResolver, (void *)&ShapeSet_structset);
///   serReaderAddResolver(r, serClassSetResolver, (void *)&NodeSet_classset);
/// @endcode
///
/// @section serialize_classes Object classes
///
/// A class opts into the same reflection a struct gets automatically -- annotate it
/// `[serialize]` for member-by-member reflection like a struct, or implement the
/// `Serializable` interface for full control over its own wire format. Either way the class
/// carries a wire name and reads and writes like any other value; see @ref obj_class for the
/// class-side details.
///
/// @}  // end of serialize_overview
///
/// @}  // end of serialize group

#pragma once

#include <cx/serialize/sbfile.h>
#include <cx/serialize/sbfsfile.h>
#include <cx/serialize/sbstring.h>
#include <cx/serialize/serbinary.h>
#include <cx/serialize/serjson.h>
#include <cx/serialize/serreader.h>
#include <cx/serialize/serssd.h>
#include <cx/serialize/sertype.h>
#include <cx/serialize/serwriter.h>
#include <cx/serialize/streambuf.h>

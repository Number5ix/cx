/// @file serssd.h
/// @brief Serialization backend that reads and writes SSD trees

#pragma once

#include <cx/serialize/serreader.h>
#include <cx/serialize/serwriter.h>
#include <cx/ssdtree/ssdtree.h>

CX_C_BEGIN

/// @defgroup serialize_ssd SSD Tree Backend
/// @ingroup serialize
/// @{
///
/// An SSD tree is a backend like any other, which bridges the schema'd and schema-free halves
/// of cx: `struct` to `SSDNode` on the way out, `SSDNode` to `struct` on the way back.
///
/// It is also the abstraction's sanity check. A tree builder is about as different from a
/// byte stream as a backend can be, and the data model accommodates it without special-casing
/// -- no encoding, no parsing, nothing but the vocabulary itself.

/// Creates a writer that builds an SSD tree.
///
/// The tree is created by the writer and owned by it until `serSsdWriterRoot()` is called, so
/// unlike the byte-stream backends there is no caller-supplied transport.
///
/// @param flags SerFlagsEnum options; none are specific to this backend
/// @return A new writer; destroy with serWriterDestroy()
///
/// Example:
/// @code
///   SerWriter *w = serSsdWriterCreate(0);
///   serWrite(w, MyStruct, val);
///   serWriterFinish(w);
///   SSDNode *tree = serSsdWriterRoot(w);
///   serWriterDestroy(&w);
///   // ... use tree ...
///   objRelease(&tree);
/// @endcode
_Ret_notnull_ SerWriter* serSsdWriterCreate(flags_t flags);

/// Returns the tree the writer built.
///
/// Call after serWriterFinish(). The caller receives a new reference and must release it;
/// the writer keeps its own, so the tree survives serWriterDestroy().
///
/// @param w Writer to take the result from
/// @return The root node, or NULL if nothing was written
_Ret_maybenull_ SSDNode* serSsdWriterRoot(_In_ SerWriter* w);

/// Creates a reader over an existing SSD tree.
///
/// @param root  Root node to read from; a reference is acquired for the reader's lifetime
/// @param flags SerFlagsEnum options
/// @return A new reader; destroy with serReaderDestroy()
///
/// Example:
/// @code
///   SerReader *r = serSsdReaderCreate(tree, 0);
///   MyStruct out;
///   structInit(MyStruct, &out);
///   serRead(r, MyStruct, &out);
///   serReaderDestroy(&r);
/// @endcode
_Ret_notnull_ SerReader* serSsdReaderCreate(_In_ SSDNode* root, flags_t flags);

/// @}

CX_C_END

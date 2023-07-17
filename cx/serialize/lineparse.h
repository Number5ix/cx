#include <cx/serialize/streambuf.h>

// Basic line parser that is a streambuffer consumer.
// Useful for reading a file line by line, for example.

enum LINEPARSER_FLAGS_ENUM {
    LPARSE_Auto         = 0,
    LPARSE_CRLF         = 1,
    LPARSE_CR           = 2,
    LPARSE_Mixed        = 3,
    LPARSE_EOL_MASK     = 3,
    LPARSE_EOL_COUNT    = 4,

    // Include the EOL markers in the returned line.
    LPARSE_IncludeEOL   = 8,

    // by default if the last line does not have an EOL character,
    // it is still returned as if it had one. If this flag is set,
    // the last line will not be parsed unless it has an EOL character.
    LPARSE_NoIncomplete = 16
};

// -------- Pull mode --------

// Register the line parser in pull mode. Once registered, repeatedly call
// lparseLine() to get the next line.
bool lparseRegisterPull(StreamBuffer *sb, uint32 flags);
// Returns false when there are no more lines.
bool lparseLine(StreamBuffer *sb, string *out);

// -------- Push mode --------

// Callback to pass to lparseRegisterPush. Should return true to continue parsing
// or false to stop.
typedef bool (*lparseLineCB)(strref line, void *ctx);

bool lparseRegisterPush(StreamBuffer *sb, lparseLineCB pline, sbufCleanupCB pcleanup);

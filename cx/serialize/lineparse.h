/// @file lineparse.h
/// @brief Line-oriented stream buffer consumer
///
/// @defgroup serialize_lineparse Line Parser
/// @ingroup serialize
/// @{
///
/// Stream buffer consumer that parses line-by-line input.
///
/// The line parser reads a stream buffer one line at a time. It handles the different line ending
/// conventions (LF, CRLF, or mixed) and works in both stream buffer modes.
///
/// **Common Use Cases:**
/// - Reading text files line by line
/// - Processing log files
/// - Parsing configuration files
/// - Any line-oriented data processing
///
/// **Pull Mode:**
/// Create the parser with lparseCreatePull(), then call lparseLine() for each line.
///
/// **Push Mode:**
/// Create the parser with lparseCreatePush() and give it a callback that runs for each line as
/// data arrives.
///
/// Either way, destroy the parser with lparseDestroy() when done. A push parser is registered as
/// the stream buffer's consumer and detaches when it is destroyed.
///
/// Example (pull mode):
/// @code
///   VFSFile *file = vfsOpen(vfs, _SL("config.txt"), FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   LineParser *lp = lparseCreatePull(sb, LPARSE_Auto);
///
///   string line = 0;
///   while (lparseLine(lp, &line)) {
///       // process line
///   }
///   strDestroy(&line);
///   lparseDestroy(&lp);
///   sbufClose(sb);
///   sbufRelease(&sb);
/// @endcode
///
/// Example (push mode):
/// @code
///   bool processLine(strref line, void *ctx) {
///       // process each line
///       return true;  // continue parsing
///   }
///
///   VFSFile *file = vfsOpen(vfs, _SL("data.txt"), FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   LineParser *lp = lparseCreatePush(sb, processLine, NULL, NULL, LPARSE_LF);
///   sbufFileIn(sb, file, true);   // calls processLine for each line
///   sbufClose(sb);                  // flushes a last line with no EOL
///   lparseDestroy(&lp);
///   sbufRelease(&sb);
/// @endcode

#include <cx/serialize/streambuf.h>

CX_C_BEGIN

typedef struct LineParser LineParser;

/// @defgroup serialize_lineparse_flags Line Parser Flags
/// @ingroup serialize_lineparse
/// @{
///
/// Configuration flags for line parser behavior.

/// Line parser configuration flags
enum LINEPARSER_FLAGS_ENUM {
    /// Auto-detect line ending style from first line found
    LPARSE_Auto      = 0,
    /// Expect CR+LF (Windows-style) line endings
    LPARSE_CRLF      = 1,
    /// Expect LF (Unix-style) line endings
    LPARSE_LF        = 2,
    /// Accept mixed CR+LF and LF line endings
    LPARSE_Mixed     = 3,
    /// Mask for extracting EOL mode
    LPARSE_EOL_MASK  = 3,
    /// Number of EOL modes (internal use)
    LPARSE_EOL_COUNT = 4,

    /// Include the EOL markers in the returned line (by default they are stripped)
    LPARSE_IncludeEOL = 8,

    /// By default if the last line does not have an EOL character,
    /// it is still returned as if it had one. If this flag is set,
    /// the last line will not be parsed unless it has an EOL character.
    LPARSE_NoIncomplete = 16
};

/// @}  // end of serialize_lineparse_flags

/// Destroys a line parser.
///
/// A push parser detaches from its stream buffer here, which runs the cleanup callback it was
/// created with. Does not end the stream, and does not release the caller's reference to it.
///
/// @param lp Pointer to the line parser
void lparseDestroy(_Inout_ LineParser** lp);

/// @defgroup serialize_lineparse_pull Pull Mode
/// @ingroup serialize_lineparse
/// @{
///
/// Pull-mode line parsing where the consumer explicitly requests each line.

// Internal function - use lparseCreatePull() macro instead
_Ret_valid_ LineParser* _lparseCreatePull(_Inout_ StreamBuffer* sb, flags_t flags);

/// LineParser *lparseCreatePull(StreamBuffer *sb, [flags])
///
/// Creates a line parser that reads on demand.
///
/// The parser is the driving consumer, so it registers nothing with the stream buffer; call
/// lparseLine() for each line. Use this on a stream buffer that has a pull producer attached.
///
/// @param sb The stream buffer
/// @param ... (flags) Configuration flags from LINEPARSER_FLAGS_ENUM
/// @return New line parser (destroy with lparseDestroy)
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   LineParser *lp = lparseCreatePull(sb, LPARSE_Auto);
///
///   string line = 0;
///   while (lparseLine(lp, &line)) {
///       // Process each line
///       printf("Line: %s\n", strZ(line));
///   }
///   strDestroy(&line);
///   lparseDestroy(&lp);
/// @endcode
#define lparseCreatePull(sb, ...) _lparseCreatePull(sb, opt_flags(__VA_ARGS__))

/// Retrieves the next line from the stream buffer.
///
/// Call this repeatedly after lparseCreatePull(). It returns false once the input runs out, which
/// happens when the stream ends, when it fails, or when the producer detaches.
///
/// @param lp The line parser
/// @param out String to receive the line content (cleared and populated)
/// @return true if a line was read, false when no more lines are available
_Success_(return) _Check_return_ bool lparseLine(_Inout_ LineParser* lp, _Inout_ string* out);

/// @}  // end of serialize_lineparse_pull

/// @defgroup serialize_lineparse_push Push Mode
/// @ingroup serialize_lineparse
/// @{
///
/// Push-mode line parsing where a callback is invoked for each line automatically.

/// bool (*lparseLineCB)(strref line, void *ctx)
///
/// Callback function type for push-mode line parsing.
///
/// This callback is invoked for each line found in the stream buffer. The callback
/// should return true to continue parsing or false to stop.
///
/// **IMPORTANT:** The line string reference is only valid during the callback.
/// If you need to retain the line data, copy it.
///
/// @param line The parsed line (valid only during callback)
/// @param ctx User context pointer passed to lparseCreatePush()
/// @return true to continue parsing, false to stop
typedef bool (*lparseLineCB)(_In_opt_ strref line, _Pre_opt_valid_ void* ctx);

// Internal function - use lparseCreatePush() macro instead
_Ret_opt_valid_ LineParser* _lparseCreatePush(_Inout_ StreamBuffer* sb, _In_ lparseLineCB pline,
                                              _In_opt_ sbufCleanupCB pcleanup,
                                              _Inout_opt_ void* ctx, flags_t flags);

/// LineParser *lparseCreatePush(StreamBuffer *sb, lparseLineCB pline, sbufCleanupCB pcleanup,
/// void *ctx, [flags])
///
/// Creates a line parser that is called as data arrives.
///
/// The parser registers as the stream buffer's consumer, so the producer drives: the callback runs
/// for each line as soon as enough data is there. A last line with no EOL is delivered when the
/// producer calls sbufClose().
///
/// @param sb The stream buffer
/// @param pline Callback function invoked for each line
/// @param pcleanup Optional cleanup callback for ctx, run when the parser is destroyed
/// @param ctx User context pointer passed to callbacks
/// @param ... (flags) Configuration flags from LINEPARSER_FLAGS_ENUM
/// @return New line parser, or NULL if a consumer is already attached to the stream buffer
///
/// Example:
/// @code
///   bool handleLine(strref line, void *ctx) {
///       int *count = (int *)ctx;
///       (*count)++;
///       printf("Line %d: %s\n", *count, strC(line));
///       return true;  // continue processing
///   }
///
///   int lineCount = 0;
///   StreamBuffer *sb = sbufCreate(4096);
///   LineParser *lp = lparseCreatePush(sb, handleLine, NULL, &lineCount, LPARSE_LF);
///   sbufFileIn(sb, file, true);   // triggers callbacks automatically
///   sbufClose(sb);
///   lparseDestroy(&lp);
/// @endcode
#define lparseCreatePush(sb, pline, pcleanup, ctx, ...) \
    _lparseCreatePush(sb, pline, pcleanup, ctx, opt_flags(__VA_ARGS__))

/// @}  // end of serialize_lineparse_push
/// @}  // end of serialize_lineparse

CX_C_END

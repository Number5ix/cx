/// @file lineparse.h
/// @brief Line-oriented stream buffer consumer
///
/// @defgroup serialize_lineparse Line Parser
/// @ingroup serialize
/// @{
///
/// Stream buffer consumer that parses line-by-line input.
///
/// The line parser is a specialized consumer that processes stream buffer data
/// one line at a time. It automatically handles different line ending conventions
/// (LF, CRLF, or mixed) and provides both pull and push mode operation.
///
/// **Common Use Cases:**
/// - Reading text files line by line
/// - Processing log files
/// - Parsing configuration files
/// - Any line-oriented data processing
///
/// **Pull Mode:**
/// Register the parser with lparseRegisterPull(), then repeatedly call lparseLine()
/// to retrieve each line.
///
/// **Push Mode:**
/// Register with lparseRegisterPush() and provide a callback that is invoked for
/// each line as data becomes available.
///
/// Example (pull mode):
/// @code
///   VFSFile *file = vfsOpen(vfs, _S"config.txt", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   lparseRegisterPull(sb, LPARSE_Auto);
///
///   string line = 0;
///   while (lparseLine(sb, &line)) {
///       // process line
///   }
///   strDestroy(&line);
/// @endcode
///
/// Example (push mode):
/// @code
///   bool processLine(strref line, void *ctx) {
///       // process each line
///       return true;  // continue parsing
///   }
///
///   VFSFile *file = vfsOpen(vfs, _S"data.txt", FS_Read);
///   StreamBuffer *sb = sbufCreate(4096);
///   lparseRegisterPush(sb, processLine, NULL, NULL, LPARSE_LF);
///   sbufFileIn(sb, file, true);  // automatically calls processLine for each line
/// @endcode

#include <cx/serialize/streambuf.h>

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

/// @defgroup serialize_lineparse_pull Pull Mode
/// @ingroup serialize_lineparse
/// @{
///
/// Pull-mode line parsing where the consumer explicitly requests each line.

/// bool lparseRegisterPull(StreamBuffer *sb, uint32 flags)
///
/// Registers the line parser as a consumer with the stream buffer in pull mode.
///
/// After registration, repeatedly call lparseLine() to retrieve each line from
/// the stream buffer. This mode gives you explicit control over when lines are
/// processed.
///
/// @param sb The stream buffer
/// @param flags Configuration flags from LINEPARSER_FLAGS_ENUM
/// @return true on success, false if registration failed
///
/// Example:
/// @code
///   StreamBuffer *sb = sbufCreate(4096);
///   sbufFilePRegisterPull(sb, file, true);
///   lparseRegisterPull(sb, LPARSE_Auto);
///
///   string line = 0;
///   while (lparseLine(sb, &line)) {
///       // Process each line
///       printf("Line: %s\n", strZ(line));
///   }
///   strDestroy(&line);
/// @endcode
_Check_return_ bool lparseRegisterPull(_Inout_ StreamBuffer* sb, uint32 flags);

/// bool lparseLine(StreamBuffer *sb, string *out)
///
/// Retrieves the next line from the stream buffer.
///
/// Call this function repeatedly after lparseRegisterPull() to process lines
/// one at a time. The function returns false when there are no more lines to read.
///
/// **IMPORTANT:** The stream buffer is invalidated when this function returns false.
///
/// @param sb The stream buffer (invalidated when returning false)
/// @param out String to receive the line content (cleared and populated)
/// @return true if a line was read, false when no more lines are available
_Success_(return) _Check_return_ bool
lparseLine(_Inout_ _On_failure_(_Post_invalid_) StreamBuffer* sb, _Inout_ string* out);

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
/// @param ctx User context pointer passed to lparseRegisterPush()
/// @return true to continue parsing, false to stop
typedef bool (*lparseLineCB)(_In_opt_ strref line, _Pre_opt_valid_ void* ctx);

/// bool lparseRegisterPush(StreamBuffer *sb, lparseLineCB pline, sbufCleanupCB pcleanup, void *ctx,
/// uint32 flags)
///
/// Registers the line parser as a consumer with the stream buffer in push mode.
///
/// In push mode, the provided callback is automatically invoked for each line as
/// data becomes available from the producer. This is useful for asynchronous
/// processing or when you want lines to be handled as soon as they're ready.
///
/// @param sb The stream buffer
/// @param pline Callback function invoked for each line
/// @param pcleanup Optional cleanup callback invoked when parsing completes
/// @param ctx User context pointer passed to callbacks
/// @param flags Configuration flags from LINEPARSER_FLAGS_ENUM
/// @return true on success, false if registration failed
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
///   lparseRegisterPush(sb, handleLine, NULL, &lineCount, LPARSE_LF);
///   sbufFileIn(sb, file, true);  // triggers callbacks automatically
/// @endcode
_Check_return_ bool
lparseRegisterPush(_Inout_ StreamBuffer* sb, _In_ lparseLineCB pline,
                   _In_opt_ sbufCleanupCB pcleanup, _Inout_opt_ void* ctx, uint32 flags);

/// @}  // end of serialize_lineparse_push
/// @}  // end of serialize_lineparse

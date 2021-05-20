#pragma once

#include <cx/cx.h>
#include <cx/string.h>
#include <cx/core/stvar.h>

enum LOG_LEVEL_ENUM {
    LOG_Fatal,
    LOG_Error,
    LOG_Warn,
    LOG_Notice,
    LOG_Info,
    LOG_Verbose,
    LOG_Debug,          // Compiled out of non-development builds
    LOG_Trace,          // Only available in debug builds

    LOG_Count           // Not a real level, for static assertions
};
extern strref LogLevelNames[];
extern strref LogLevelAbbrev[];

typedef struct LogCategory {
    string name;
} LogCategory;
typedef struct LogDest LogDest;
extern LogCategory* LogDefault;

LogCategory *logCreateCat(strref name);
void _logStr(int level, LogCategory *cat, strref str);
void _logFmt(int level, LogCategory *cat, strref fmtstr, int n, stvar *args);

// NOTE: will be called with level == -1 to indicate that the destination has been removed or the
// log system is shutting down. The destination provider should assume it will never be called again
// and close log files, etc.
typedef void(*LogDestFunc)(int level, LogCategory *cat, int64 timestamp, strref msg, void *userdata);

LogDest *logRegisterDest(int maxlevel, LogCategory *catfilter, LogDestFunc dest, void *userdata);
bool logUnregisterDest(LogDest *dhandle);
void logFlush(void);
void logShutdown(void);

void logBatchBegin(void);
void logBatchEnd(void);

#define strFormat(out, fmt, ...) _strFormat(out, fmt, count_macro_args(__VA_ARGS__), (stvar[]){ __VA_ARGS__ })

#define logStr(level, str)       _logStr_##level(LOG_##level, LogDefault, str)
#define logStrC(level, cat, str) _logStr_##level(LOG_##level, cat, str)
#define logFmt(level, fmt, ...)       _logFmt_##level(LOG_##level, LogDefault, fmt, count_macro_args(__VA_ARGS__), (stvar[]){ __VA_ARGS__ })
#define logFmtC(level, cat, fmt, ...) _logFmt_##level(LOG_##level, cat, fmt, count_macro_args(__VA_ARGS__), (stvar[]){ __VA_ARGS__ })

#if DEBUG_LEVEL >= 2
#define _logStr_Trace(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Trace(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#else
#define _logStr_Trace(level, cat, str) ((void)0)
#define _logFmt_Trace(level, cat, fmt, nargs, args) ((void)0)
#endif

#if DEBUG_LEVEL >= 1
#define _logStr_Debug(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Debug(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_DevVerbose(level, cat, str) _logStr(LOG_Verbose, cat, str)
#define _logFmt_DevVerbose(level, cat, fmt, nargs, args) _logFmt(LOG_Verbose, cat, fmt, nargs, args)
#define _logStr_DevInfo(level, cat, str) _logStr(LOG_Info, cat, str)
#define _logFmt_DevInfo(level, cat, fmt, nargs, args) _logFmt(LOG_Info, cat, fmt, nargs, args)
#define _logStr_DevNotice(level, cat, str) _logStr(LOG_Notice, cat, str)
#define _logFmt_DevNotice(level, cat, fmt, nargs, args) _logFmt(LOG_Notice, cat, fmt, nargs, args)
#define _logStr_DevWarn(level, cat, str) _logStr(LOG_Warn, cat, str)
#define _logFmt_DevWarn(level, cat, fmt, nargs, args) _logFmt(LOG_Warn, cat, fmt, nargs, args)
#define _logStr_DevError(level, cat, str) _logStr(LOG_Error, cat, str)
#define _logFmt_DevError(level, cat, fmt, nargs, args) _logFmt(LOG_Error, cat, fmt, nargs, args)
#else
#define _logStr_Debug(level, cat, str) ((void)0)
#define _logFmt_Debug(level, cat, str) ((void)0)
#define _logStr_DevVerbose(level, cat, str) ((void)0)
#define _logFmt_DevVerbose(level, cat, str) ((void)0)
#define _logStr_DevInfo(level, cat, str) ((void)0)
#define _logFmt_DevInfo(level, cat, str) ((void)0)
#define _logStr_DevNotice(level, cat, str) ((void)0)
#define _logFmt_DevNotice(level, cat, str) ((void)0)
#define _logStr_DevWarn(level, cat, str) ((void)0)
#define _logFmt_DevWarn(level, cat, str) ((void)0)
#define _logStr_DevError(level, cat, str) ((void)0)
#define _logFmt_DevError(level, cat, str) ((void)0)
#endif

#define _logStr_Verbose(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Verbose(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_Info(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Info(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_Notice(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Notice(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_Warn(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Warn(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_Error(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Error(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)
#define _logStr_Fatal(level, cat, str) _logStr(level, cat, str)
#define _logFmt_Fatal(level, cat, fmt, nargs, args) _logFmt(level, cat, fmt, nargs, args)

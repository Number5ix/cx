#include "logconsole.h"
#include <cx/string.h>

typedef struct LogConsoleData {
    LogConsoleConfig config;
    LogSerializer* ser;   // owned; how a record becomes the bytes this console shows
    ConStream* out;
    ConStream* err;
} LogConsoleData;

// Fatal bright white on red; Error bright red; Warn bright yellow; Notice bright white;
// Info default; Verbose/Diag dim; Debug/Trace dim cyan.
static const ConStyle kDefaultLevelStyle[LOG_Count] = {
    [LOG_Fatal]   = { CON_BrightWhite, CON_Red, 0 },
    [LOG_Error]   = { CON_BrightRed, CON_ColorDefault, 0 },
    [LOG_Warn]    = { CON_BrightYellow, CON_ColorDefault, 0 },
    [LOG_Notice]  = { CON_BrightWhite, CON_ColorDefault, 0 },
    [LOG_Info]    = { CON_ColorDefault, CON_ColorDefault, 0 },
    [LOG_Verbose] = { CON_ColorDefault, CON_ColorDefault, CON_Dim },
    [LOG_Diag]    = { CON_ColorDefault, CON_ColorDefault, CON_Dim },
    [LOG_Debug]   = { CON_Cyan, CON_ColorDefault, CON_Dim },
    [LOG_Trace]   = { CON_Cyan, CON_ColorDefault, CON_Dim },
};

static ConStyle resolveLevelStyle(_In_ LogConsoleData* lcd, int level)
{
    ConStyle style = lcd->config.levelStyle[level];
    if (style.fg == 0 && style.bg == 0 && style.attr == 0)
        return kDefaultLevelStyle[level];
    return style;
}

static bool shouldStyle(_In_ LogConsoleData* lcd, _In_ ConStream* con)
{
    if (lcd->config.colorMode == LOGCON_ColorOff)
        return false;
    if (lcd->config.colorMode == LOGCON_ColorOn)
        return true;

    ConCaps caps;
    conGetCaps(con, &caps);
    return caps.color != CON_ColorNone;
}

_Use_decl_annotations_
LogConsoleData* logconsoleCreate(ConStream* out, ConStream* err, LogConsoleConfig* config,
                                 LogSerializer* ser)
{
    LogConsoleData* ret = xaAlloc(sizeof(LogConsoleData));
    ret->config         = *config;
    ret->ser            = ser ? ser : logTextSerializer(NULL);
    ret->out            = out ? out : conOut();
    ret->err            = err ? err : conErr();
    return ret;
}

// this function is always called from the log thread and does not need to worry about concurrency
_Use_decl_annotations_
void logconsoleMsgFunc(const LogRecord* rec, void* userdata)
{
    LogConsoleData* lcd = (LogConsoleData*)userdata;
    if (!lcd)
        return;

    ConStream* con = (rec->level <= lcd->config.stderrLevel) ? lcd->err : lcd->out;

    // the serializer decides what a record looks like; this transport routes it by severity,
    // styles it, and terminates the line
    string logline = 0;
    logSerialize(&logline, lcd->ser, rec);

    if (shouldStyle(lcd, con))
        conPutsS(con, resolveLevelStyle(lcd, rec->level), logline);
    else
        conPuts(con, logline);
    conNL(con);

    strDestroy(&logline);
}

_Use_decl_annotations_
void logconsoleBatchFunc(uint32 batchid, void* userdata)
{
    LogConsoleData* lcd = (LogConsoleData*)userdata;
    if (!lcd)
        return;

    // do console flushing between batches, same as logfileBatchFunc does for rotation
    conFlush(lcd->out);
    conFlush(lcd->err);
}

_Use_decl_annotations_
void logconsoleCloseFunc(void* userdata)
{
    LogConsoleData* lcd = (LogConsoleData*)userdata;
    if (!lcd)
        return;

    logSerializerDestroy(&lcd->ser);
    xaFree(lcd);
}

_Use_decl_annotations_
LogDest* logconsoleRegister(int maxlevel, strref chanfilter, LogConsoleData* console)
{
    return logRegisterDest(maxlevel,
                           chanfilter,
                           logconsoleMsgFunc,
                           logconsoleBatchFunc,
                           logconsoleCloseFunc,
                           console);
}

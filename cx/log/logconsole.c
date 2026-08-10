#include "logconsole.h"
#include <cx/log/logdefer.h>
#include <cx/string.h>

typedef struct LogConsoleData {
    LogConsoleConfig config;
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
LogConsoleData* logconsoleCreate(ConStream* out, ConStream* err, LogConsoleConfig* config)
{
    LogConsoleData* ret = xaAlloc(sizeof(LogConsoleData));
    ret->config          = *config;
    ret->out             = out ? out : conOut();
    ret->err             = err ? err : conErr();
    return ret;
}

// this function is always called from the log thread and does not need to worry about concurrency
_Use_decl_annotations_
void logconsoleMsgFunc(int level, LogChannel* chan, int64 timestamp, strref msg, uint32 batchid,
                       void* userdata)
{
    LogConsoleData* lcd = (LogConsoleData*)userdata;
    if (!lcd)
        return;

    ConStream* con = (level <= lcd->config.stderrLevel) ? lcd->err : lcd->out;

    string logline = 0;
    string logdate = 0, loglevel = 0, logchan = 0, logspaces = 0;

    int nspaces = lcd->config.spacing ? lcd->config.spacing : 2;
    uint8* sbuf = strBuffer(&logspaces, nspaces + (lcd->config.flags & LOG_AddColon ? 1 : 0));
    memset(sbuf, ' ', nspaces);
    if (lcd->config.flags & LOG_AddColon)
        sbuf[0] = ':';

    logFormatDate(&logdate, lcd->config.dateFormat, lcd->config.flags, timestamp);
    logFormatLevel(&loglevel, level, lcd->config.flags);
    logFormatChannel(&logchan, chan, lcd->config.flags);

    if (lcd->config.flags & LOG_ChannelFirst)
        strNConcat(&logline, logdate, logchan, loglevel, logspaces, msg);
    else
        strNConcat(&logline, logdate, loglevel, logchan, logspaces, msg);
    strDestroy(&logdate);
    strDestroy(&loglevel);
    strDestroy(&logchan);
    strDestroy(&logspaces);

    if (shouldStyle(lcd, con))
        conPutsS(con, resolveLevelStyle(lcd, level), logline);
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

    xaFree(lcd);
}

_Use_decl_annotations_
LogDest* logconsoleRegister(int maxlevel, LogChannel* chanfilter, LogConsoleData* console)
{
    return logRegisterDest(maxlevel,
                           chanfilter,
                           logconsoleMsgFunc,
                           logconsoleBatchFunc,
                           logconsoleCloseFunc,
                           console);
}

_Use_decl_annotations_
LogDest* logconsoleRegisterWithDefer(int maxlevel, LogChannel* chanfilter, LogConsoleData* console,
                                     LogDest* deferdest)
{
    return logRegisterDestWithDefer(maxlevel,
                                    chanfilter,
                                    logconsoleMsgFunc,
                                    logconsoleBatchFunc,
                                    logconsoleCloseFunc,
                                    console,
                                    deferdest);
}

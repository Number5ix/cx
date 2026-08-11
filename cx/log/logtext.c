// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/container/foreach.h>
#include <cx/format.h>
#include <cx/string.h>
#include <cx/time.h>

// The human-readable one-line rendering, and the pieces it is assembled from. The console
// destination borrows the same pieces directly.

STR_CONST(
    kLogDateISOTZ,
    "${0int(4)}-${0uint(2)}-${0uint(2)}T${0uint(2)}:${0uint(2)}:${0uint(2)}${+int(min:2)}:${0int(2)}");
STR_CONST(kLogDateISOZulu,
          "${0int(4)}-${0uint(2)}-${0uint(2)}T${0uint(2)}:${0uint(2)}:${0uint(2)}Z");
STR_CONST(kLogDateISOCompact,
          "${0int(4)}-${0uint(2)}-${0uint(2)} ${0uint(2)}:${0uint(2)}:${0uint(2)}");
STR_CONST(
    kLogDateNCSA,
    "${0uint(2)}/${string(3)}/${0int(4)}:${0uint(2)}:${0uint(2)}:${0uint(2)} ${+int(min:2)}${0int(2)}");
STR_CONST(kLogDateSyslog, "${string(3)} ${uint(2)} ${0uint(2)}:${0uint(2)}:${0uint(2)}");
STR_CONST(kLogDateISOCompactMs,
          "${0int(4)}-${0uint(2)}-${0uint(2)} ${0uint(2)}:${0uint(2)}:${0uint(2)}.${0uint(3)}");
STR_CONST(kLogBracketFmt, " [${string}]");
STR_CONST(kLogJustifyFmt, " ${string(7)}");
STR_CONST(kLogSpace, " ");
STR_CONST(kLogCtxSep, ", ");
STR_CONST(kLogCtxOpen, " [");

// Config plus whatever it was cheaper to work out once than per record.
typedef struct LogTextState {
    LogTextConfig config;
    string ctxstr;         // owned copy of config.ctxfields, which the caller need not keep
    sa_string ctxfields;   // parsed from it; empty means every field
} LogTextState;

// Renders the record's context as " [key:value key2:value2]", innermost first, with shadowed
// outer fields skipped.
static void logTextContext(_Inout_ string* out, _In_ LogTextState* st, _In_ const LogRecord* rec)
{
    strClear(out);
    if (!rec->ctx)
        return;

    string val = 0;
    bool first = true;

    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c)) {
        const stvar* vars = logCtxVars(c);
        uint32 n          = logCtxNumVars(c);

        for (uint32 i = 0; i < n; i++) {
            const char* key = stvarKey(&vars[i]);
            if (!key || logCtxShadowed(rec->ctx, c, i, key))
                continue;

            if (saSize(st->ctxfields) > 0) {
                bool wanted = false;
                foreach (sarray, fidx, string, want, st->ctxfields) {
                    if (strEq(want, (strref)key)) {
                        wanted = true;
                        break;
                    }
                }
                if (!wanted)
                    continue;
            }

            strAppend(out, first ? kLogCtxOpen : kLogSpace);
            first = false;

            strAppend(out, (strref)key);
            strAppendChar(out, ':');
            logVarText(&val, &vars[i]);
            strAppend(out, val);
        }
    }

    if (!first)
        strAppendChar(out, ']');

    strDestroy(&val);
}

_Use_decl_annotations_
void logFormatDate(string* out, int dateFormat, uint32 flags, int64 timestamp)
{
    int64 toffsetraw = 0;
    TimeParts tp     = { 0 };
    if (flags & LOG_LocalTime) {
        timestamp = timeLocal(timestamp, &toffsetraw);
    }

    int toffset = (int32)timeToSeconds(toffsetraw) / 60;   // need offset in minutes for formatting
    timeDecompose(&tp, timestamp);

    switch (dateFormat) {
    case LOG_DateISO:
        if (toffset != 0) {
            // ISO8601 with time zone
            strFormat(out,
                      kLogDateISOTZ,
                      stvar(int32, tp.year),
                      stvar(uint8, tp.month),
                      stvar(uint8, tp.day),
                      stvar(uint8, tp.hour),
                      stvar(uint8, tp.minute),
                      stvar(uint8, tp.second),
                      stvar(int32, toffset / 60),
                      stvar(int32, (toffset >= 0 ? toffset : -toffset) % 60));
        } else {
            // ISO8601 with zulu time
            strFormat(out,
                      kLogDateISOZulu,
                      stvar(int32, tp.year),
                      stvar(uint8, tp.month),
                      stvar(uint8, tp.day),
                      stvar(uint8, tp.hour),
                      stvar(uint8, tp.minute),
                      stvar(uint8, tp.second));
        }
        break;
    case LOG_DateISOCompact:
        // simplifed ISO-like format with no time zone
        strFormat(out,
                  kLogDateISOCompact,
                  stvar(int32, tp.year),
                  stvar(uint8, tp.month),
                  stvar(uint8, tp.day),
                  stvar(uint8, tp.hour),
                  stvar(uint8, tp.minute),
                  stvar(uint8, tp.second));
        break;
    case LOG_DateNCSA:
        // NCSA common log date format
        strFormat(out,
                  kLogDateNCSA,
                  stvar(uint8, tp.day),
                  stvar(strref, timeMonthAbbrev[tp.month]),
                  stvar(int32, tp.year),
                  stvar(uint8, tp.hour),
                  stvar(uint8, tp.minute),
                  stvar(uint8, tp.second),
                  stvar(int32, toffset / 60),
                  stvar(int32, (toffset >= 0 ? toffset : -toffset) % 60));
        break;
    case LOG_DateSyslog:
        // BSD-style syslog format (without year)
        strFormat(out,
                  kLogDateSyslog,
                  stvar(strref, timeMonthAbbrev[tp.month]),
                  stvar(uint8, tp.day),
                  stvar(uint8, tp.hour),
                  stvar(uint8, tp.minute),
                  stvar(uint8, tp.second));
        break;
    case LOG_DateISOCompactMsec:
        // simplifed ISO-like format with no time zone and milliseconds
        strFormat(out,
                  kLogDateISOCompactMs,
                  stvar(int32, tp.year),
                  stvar(uint8, tp.month),
                  stvar(uint8, tp.day),
                  stvar(uint8, tp.hour),
                  stvar(uint8, tp.minute),
                  stvar(uint8, tp.second),
                  stvar(uint32, tp.usec / 1000));
        break;
    }
}

_Use_decl_annotations_
void logFormatLevel(string* out, int level, uint32 flags)
{
    if (flags & LOG_OmitLevel) {
        strDestroy(out);
        return;
    }

    strref* lvarr = (flags & LOG_ShortLevel) ? LogLevelAbbrev : LogLevelNames;
    int lvmaxlen  = (flags & LOG_ShortLevel) ? 1 : 7;
    if (flags & LOG_BracketLevel) {
        if (flags & LOG_JustifyLevel) {
            // justified with brackets... yuck
            int llen    = strLen(lvarr[level]);
            uint8* temp = strBuffer(out, lvmaxlen + 3);
            memset(temp, ' ', (size_t)lvmaxlen + 3);
            temp[1]        = '[';
            temp[llen + 2] = ']';
            memcpy(temp + 2, strC(lvarr[level]), llen);
        } else {
            strFormat(out, kLogBracketFmt, stvar(strref, lvarr[level]));
        }
    } else if (flags & LOG_JustifyLevel) {
        if (flags & LOG_ShortLevel) {
            strConcat(out, kLogSpace, lvarr[level]);
        } else {
            strFormat(out, kLogJustifyFmt, stvar(strref, lvarr[level]));
        }
    } else {
        strConcat(out, kLogSpace, lvarr[level]);
    }
}

_Use_decl_annotations_
void logFormatChannel(string* out, LogChannel* chan, uint32 flags)
{
    if (!(flags & LOG_IncludeChannel) || !chan || strEmpty(chan->path)) {
        strDestroy(out);
        return;
    }

    if (flags & LOG_BracketChannel) {
        strFormat(out, kLogBracketFmt, stvar(strref, chan->path));
    } else {
        strConcat(out, kLogSpace, chan->path);
    }
}

static void logTextSerialize(_Inout_ string* out, _In_ const LogRecord* rec, _In_opt_ void* userdata)
{
    LogTextState* st   = (LogTextState*)userdata;
    LogTextConfig* cfg = &st->config;

    string msg = 0;
    string logdate = 0, loglevel = 0, logchan = 0, logspaces = 0, logctx = 0;

    // The colon takes a slot of its own ahead of the spaces, so fill the whole run first and then
    // overwrite the first byte with it.
    int nspaces = cfg->spacing ? cfg->spacing : 2;
    int nprefix = nspaces + ((cfg->flags & LOG_AddColon) ? 1 : 0);
    strFillChar(&logspaces, ' ', nprefix);
    if (cfg->flags & LOG_AddColon)
        strSetChar(&logspaces, 0, ':');

    logRecordRender(&msg, rec);
    logFormatDate(&logdate, cfg->dateFormat, cfg->flags, rec->timestamp);
    logFormatLevel(&loglevel, rec->level, cfg->flags);
    logFormatChannel(&logchan, rec->chan, cfg->flags);

    if (cfg->flags & LOG_IncludeContext)
        logTextContext(&logctx, st, rec);

    // context goes after the message, where it reads as an annotation rather than as part of
    // the prefix a reader scans down
    if (cfg->flags & LOG_ChannelFirst)
        strNConcat(out, logdate, logchan, loglevel, logspaces, msg, logctx);
    else
        strNConcat(out, logdate, loglevel, logchan, logspaces, msg, logctx);

    strDestroy(&logctx);
    strDestroy(&msg);
    strDestroy(&logdate);
    strDestroy(&loglevel);
    strDestroy(&logchan);
    strDestroy(&logspaces);
}

static void logTextClose(_In_opt_ void* userdata)
{
    LogTextState* st = (LogTextState*)userdata;
    strDestroy(&st->ctxstr);
    saDestroy(&st->ctxfields);
    xaFree(st);
}

_Use_decl_annotations_
LogSerializer* logTextSerializer(LogTextConfig* config)
{
    LogTextState* st = xaAllocStruct(LogTextState, XA_Zero);
    if (config)
        st->config = *config;

    // the caller's list is not required to outlive the call, so take a copy and split it once
    st->config.ctxfields = NULL;
    saInit(&st->ctxfields, string, 4);
    if (config && !strEmpty(config->ctxfields)) {
        strDup(&st->ctxstr, config->ctxfields);
        // split on comma *or* space, so that a list written the way a human writes one --
        // "req_id, user" -- does not produce a field with a leading space that matches nothing
        strSplitAny(&st->ctxfields, st->ctxstr, kLogCtxSep, false);
    }

    return logSerializerCreate(logTextSerialize, logTextClose, st);
}

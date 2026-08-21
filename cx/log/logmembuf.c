// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "logmembuf.h"
#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>
#include <cx/time.h>

// The ring is a transport like any other: it owns where bytes go and how records are separated,
// and a serializer decides what they look like. The default text serializer is configured below
// to match the ring's compact format.
static const LogTextConfig kMembufDefaultText = {
    .dateFormat = LOG_DateISOCompact,
    .spacing    = 1,
    .flags      = LOG_ShortLevel | LOG_IncludeChannel | LOG_BracketChannel | LOG_AddColon,
};

_Use_decl_annotations_
LogMembufData* logmembufCreate(uint32 size, LogSerializer* ser)
{
    LogMembufData* ret = xaAlloc(sizeof(LogMembufData), XA_Zero);
    ret->size          = size;
    ret->buf           = xaAlloc(size, XA_Zero);
    ret->ser           = ser ? ser : logTextSerializer((LogTextConfig*)&kMembufDefaultText);
    return ret;
}

// for use with logRegisterDest along with the userdata returned from logmembufCreate
_Use_decl_annotations_
void logmembufMsgFunc(const LogRecord* rec, void* userdata)
{
    LogMembufData* lmd = (LogMembufData*)userdata;
    if (!lmd)
        return;

    string logline = 0;
    logSerialize(&logline, lmd->ser, rec);
    strAppendChar(&logline, '\n');

    uint32 len = strLen(logline);
    if (len < lmd->size) {
        if (len + 1 < lmd->size - lmd->cur) {
            strCopyOut(logline, 0, (uint8*)lmd->buf + lmd->cur, lmd->size - lmd->cur);
            lmd->cur += len;
        } else {
            // overflow, go back to the beginning
            strCopyOut(logline, 0, (uint8*)lmd->buf, lmd->size);
            lmd->cur = len;
        }
    }

    strDestroy(&logline);
}

_Use_decl_annotations_
void logmembufCloseFunc(void* userdata)
{
    LogMembufData* lmd = (LogMembufData*)userdata;
    if (!lmd)
        return;

    // closing log
    logSerializerDestroy(&lmd->ser);
    xaFree(lmd->buf);
    xaFree(lmd);
}

_Use_decl_annotations_
LogDest* logmembufRegister(int maxlevel, strref chanfilter, uint32 size, LogSerializer* ser)
{
    LogMembufData* lmd = logmembufCreate(size, ser);

    LogDest* ret = logRegisterDest(maxlevel,
                                   chanfilter,
                                   logmembufMsgFunc,
                                   NULL,
                                   logmembufCloseFunc,
                                   lmd);

    // the destination owns the buffer once it is registered; if registration failed, nothing ever
    // will, so free it here rather than leaking it and the serializer with it
    if (!ret)
        logmembufCloseFunc(lmd);

    return ret;
}

_Use_decl_annotations_
LogMembufData* logmembufData(LogDest* dest)
{
    // the userdata of a destination is only a LogMembufData if this module put it there, and
    // msgfunc is what says so
    if (!dest || dest->msgfunc != logmembufMsgFunc)
        return NULL;

    return (LogMembufData*)dest->userdata;
}

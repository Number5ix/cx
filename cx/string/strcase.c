#include "string_private.h"

void strUpper(_Inout_ string *io)
{
    uint32 i, len;
    uint8 *buf;

    if (!io || !STR_CHECK_VALID(*io))
        return;

    len = strLen(*io);
    buf = strBuffer(io, 0);

    for (i = 0; i < len; ++i)
        buf[i] = (char)toupper(buf[i]);
}

void strLower(_Inout_ string *io)
{
    uint32 i, len;
    uint8 *buf;

    if (!io || !STR_CHECK_VALID(*io))
        return;
    len = strLen(*io);
    buf = strBuffer(io, 0);

    for (i = 0; i < len; ++i)
        buf[i] = (char)tolower(buf[i]);
}

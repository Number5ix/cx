#pragma once

#include <cx/log/log.h>

// memory buffer log target for testing and debugging

typedef struct LogMembufData {
    uint32 size;
    uint32 cur;
    char *buf;
} LogMembufData;

LogMembufData *logmembufCreate(uint32 size);

// for use with logRegisterDest along with the userdata returned from logmembufCreate
void logmembufDest(int level, LogCategory *cat, int64 timestamp, strref msg, void *userdata);

#pragma once

#include <cx/cx.h>

typedef struct HostID {
    uint64 id[4];
    int32 source;       // platform-specific
} HostID;

_EXTERN_C bool hostId(HostID *id);

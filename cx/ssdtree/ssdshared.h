#pragma once

#include <cx/thread/rwlock.h>

enum SSD_FLAGS_ENUM {
    SSD_CaseInsensitive = 0x0001,       // keys in hashtables are case-insensitive
};

typedef struct SSDLock
{
    bool init;                          // lock structure initialized
    bool rdlock;                        // read lock held by current thread
    bool wrlock;                        // write lock held by current thread
} SSDLock;

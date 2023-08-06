#pragma once

#include <cx/thread/rwlock.h>

typedef struct SSDTree SSDTree;
typedef struct SSDNode SSDNode;
typedef SSDNode *(*SSDNodeFactory)(SSDTree *info);

enum SSD_FLAGS_ENUM {
    SSD_CaseInsensitive = 0x0001,       // keys in hashtables are case-insensitive
};

enum SSD_CREATE_TYPE_ENUM {
    SSD_Create_None = 0,
    SSD_Create_Hashtable,
    SSD_Create_Array,
    SSD_Create_Single,

    SSD_Create_Count
};

typedef struct SSDLock
{
    bool init;                          // lock structure initialized
    bool rdlock;                        // read lock held by current thread
    bool wrlock;                        // write lock held by current thread
} SSDLock;

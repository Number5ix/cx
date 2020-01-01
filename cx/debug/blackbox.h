#pragma once

#include "cx/cx.h"
#include "cx/string.h"

_EXTERN_C_BEGIN

#define BLACKBOX_SIZE 65535
extern char dbgBlackBox[];

// blackbox internal structures for offline parsing
typedef struct BlackBoxEnt {
    uint16 prev;
    uint16 next;
    uint8 flags;
    uint8 namelen;
    uint16 vallen;
    char name[1];
    // char val[];
} BlackBoxEnt;
#define bboxGetVal(ent) ((char*)ent + offsetof(BlackBoxEnt, name) + ent->namelen)
#define bboxEntSize(ent) (offsetof(BlackBoxEnt, name) + ent->namelen + ent->vallen)

enum BLACKBOX_FLAGS {
    BBox_Private        = 0x01, // potentially private; allow user to opt-out of including in crash reports
};

void bboxInit();
void bboxSet(string name, string val, uint8 flags);
void bboxDelete(string name);

_EXTERN_C_END

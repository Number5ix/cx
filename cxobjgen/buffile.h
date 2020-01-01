#pragma once

#include <cx/fs/file.h>

typedef struct BufFile BufFile;

BufFile *bfCreate(FSFile *file, bool write);
void bfWriteStr(BufFile *bf, string str);
void bfWriteLine(BufFile *bf, string str);
bool bfReadLine(BufFile *bf, string *out);
void bfClose(BufFile *bf);

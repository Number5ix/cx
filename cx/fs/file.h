#pragma once

// CX basic low-level file i/o

// This acts as a bridge to the operating system. It does synchronous I/O with
// only the OS-provided buffering. Unlike the C standard library, I/O is not
// line-oriented in any way.

#include <cx/cx.h>

_EXTERN_C_BEGIN

typedef struct FSFile FSFile;

enum FSOpenFlags {
    FS_Read     = 1,
    FS_Write    = 2,
    FS_Create   = 4,
    FS_Truncate = 8,
};

enum FSSeekType {
    FS_Set,
    FS_Cur,
    FS_End
};

FSFile *fsOpen(string path, int flags);
bool fsClose(FSFile *file);

bool fsRead(FSFile *file, void *buf, size_t sz, size_t *bytesread);
bool fsWrite(FSFile *file, void *buf, size_t sz, size_t *byteswritten);
int64 fsTell(FSFile *file);
int64 fsSeek(FSFile *file, int64 off, int seektype);

bool fsFlush(FSFile *file);

_EXTERN_C_END

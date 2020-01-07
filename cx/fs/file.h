#pragma once

// CX basic low-level file i/o

// This acts as a bridge to the operating system. It does synchronous I/O with
// only the OS-provided buffering. Unlike the C standard library, I/O is not
// line-oriented in any way.

#include <cx/cx.h>

CX_C_BEGIN

typedef struct FSFile FSFile;

enum FSOpenFlags {
    FS_         = 0,
    FS_Read     = 1,
    FS_Write    = 2,
    FS_Create   = 4,
    FS_Truncate = 8,
    FS_Overwrite = (FS_Write | FS_Create | FS_Truncate),
};

enum FSSeekType {
    FS_Set,
    FS_Cur,
    FS_End
};

FSFile *_fsOpen(string path, int flags);
#define fsOpen(path, ...) _fsOpen(path, func_flags(FS, __VA_ARGS__))
bool fsClose(FSFile *file);

bool fsRead(FSFile *file, void *buf, size_t sz, size_t *bytesread);
bool fsWrite(FSFile *file, void *buf, size_t sz, size_t *byteswritten);
int64 fsTell(FSFile *file);
int64 fsSeek(FSFile *file, int64 off, int seektype);

bool fsFlush(FSFile *file);

CX_C_END

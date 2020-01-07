#include <cx/platform/base.h>
#include <cx/fs/file.h>
#include <cx/string.h>

#define BUFSIZE 1024

typedef struct BufFile {
    FSFile *file;
    char *buf;
    uint32 bufsz;
    uint32 bufused;
    uint32 bufpos;
    bool write;
} BufFile;

BufFile *bfCreate(FSFile *file, bool write)
{
    BufFile *ret = xaAlloc(sizeof(BufFile), Zero);
    ret->file = file;
    ret->buf = xaAlloc(BUFSIZE);
    ret->bufsz = BUFSIZE;
    ret->write = write;
    return ret;
}

void bfWriteStr(BufFile *bf, string str)
{
    if (!bf->write)
        return;

    uint32 len = strLen(str);

    if (bf->bufsz - bf->bufused < len + 1 && bf->bufused > 0) {
        fsWrite(bf->file, bf->buf, bf->bufused, NULL);
        bf->bufused = 0;
    }

    while (bf->bufsz < len + 1) {
        bf->bufsz *= 2;
        xaFree(bf->buf);
        bf->buf = xaAlloc(bf->bufsz);
    }

    strCopyOut(str, 0, bf->buf + bf->bufused, bf->bufsz - bf->bufused);
    bf->bufused += len;
}

void bfWriteLine(BufFile *bf, string str)
{
    bfWriteStr(bf, str);
#ifdef _PLATFORM_WIN
    bfWriteStr(bf, _S"\r\n");
#else
    bfWriteStr(bf, _S"\n");
#endif
}

static int32 nexteol(BufFile *bf)
{
    for (uint32 i = bf->bufpos; i < bf->bufused; i++) {
        if (i < bf->bufused - 1 && bf->buf[i] == '\r' && bf->buf[i+1] == '\n')
            return (int32)i;
        if (bf->buf[i] == '\n')
            return (int32)i;
    }
    return -1;
}

bool bfReadLine(BufFile *bf, string *out)
{
    if (bf->write)
        return false;

    strClear(out);

    for (;;) {
        int32 eol = nexteol(bf);
        if (eol == -1) {
            if (bf->bufpos == bf->bufused) {
                bf->bufpos = bf->bufused = 0;
            }
            if (bf->bufused == bf->bufsz) {
                bf->bufsz *= 2;
                bf->buf = xaResize(bf->buf, bf->bufsz);
            }
            size_t didread;
            if (!fsRead(bf->file, bf->buf + bf->bufused, bf->bufsz - bf->bufused, &didread) || didread == 0)
                return false;
            bf->bufused += (uint32)didread;
            continue;
        }

        char *dest = strBuffer(out, eol - bf->bufpos);
        memcpy(dest, bf->buf + bf->bufpos, eol - bf->bufpos);
        if (bf->buf[eol] == '\n')
            bf->bufpos = eol + 1;
        else if (bf->buf[eol] == '\r')
            bf->bufpos = eol + 2;

        return true;
    }
}

void bfClose(BufFile *bf)
{
    if (bf->bufused > 0)
        fsWrite(bf->file, bf->buf, bf->bufused, NULL);
    fsClose(bf->file);
    xaFree(bf->buf);
    xaFree(bf);
}

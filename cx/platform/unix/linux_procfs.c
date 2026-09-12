#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/platform/unix/linux_procfs.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ssize_t _linuxReadProcFile(const char* path, char* buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, sz - 1);
    close(fd);

    if (n < 0)
        return -1;

    buf[n] = 0;
    return n;
}

char* _linuxReadProcFileAlloc(const char* path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    size_t cap = 8192, len = 0;
    char* buf  = xaAlloc(cap);

    for (;;) {
        // Grow before reading rather than after, so the read below is never handed a zero-sized
        // window -- which would look exactly like end of file.
        if (len + 1 >= cap) {
            cap *= 2;
            xaResize(&buf, cap);
        }

        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n < 0) {
            xaFree(buf);
            close(fd);
            return NULL;
        }

        if (n == 0)
            break;

        len += (size_t)n;
    }

    close(fd);
    buf[len] = 0;
    return buf;
}

bool _linuxParseStatField(const char* stat, int idx, int64* out)
{
    const char* p = strrchr(stat, ')');
    if (!p)
        return false;

    p++;   // just past the comm field; what follows is " <state> <ppid> ..."

    for (int i = 0;; i++) {
        while (*p == ' ') p++;
        if (!*p)
            return false;
        if (i == idx)
            break;
        while (*p && *p != ' ') p++;
    }

    char* end   = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p)
        return false;

    *out = (int64)v;
    return true;
}

bool _linuxProcFileField(const char* buf, const char* key, int64* out)
{
    size_t keylen = strlen(key);
    const char* p = buf;

    while (*p) {
        // Only a match at the start of a line is the field being asked for. "MemFree" appears
        // inside "SwapFree" nowhere, but "Rss" is a prefix of several real keys, so anchoring
        // matters.
        if (strncmp(p, key, keylen) == 0) {
            const char* v = p + keylen;
            while (*v == ' ' || *v == '\t') v++;

            char* end   = NULL;
            long long n = strtoll(v, &end, 10);
            if (end == v)
                return false;

            *out = (int64)n;
            return true;
        }

        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }

    return false;
}

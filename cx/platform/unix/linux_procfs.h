#pragma once

// Small shared readers for /proc, used by the Linux process and statistics backends. Nothing
// here is cx API; it exists so the two backends parse the same files the same way.

#include <cx/cx.h>

#include <sys/types.h>

// Read a small /proc file whole. These are generated on read and always tiny, so one read is
// enough. Returns the byte count, or -1 if the file could not be read -- which for a per-process
// file usually means the process exited while we were looking at it, and for /proc/<pid>/io
// means it belongs to another user.
ssize_t _linuxReadProcFile(const char* path, char* buf, size_t sz);

// Read a /proc file whose length cannot be bounded in advance. /proc/stat in particular grows
// with both the processor count and the number of interrupt sources, so the fields at the end of
// it sit well past any fixed buffer on a large machine. Returns a NUL-terminated buffer to free
// with xaFree(), or NULL if the file could not be read.
char* _linuxReadProcFileAlloc(const char* path);

// Read one numeric field from a /proc/<pid>/stat line, counting from the field after the
// executable name: index 0 is the state, 1 the parent pid, 11 and 12 the user and system times,
// 19 the start time, 20 the address space size and 21 the resident set (fields 3, 4, 14, 15,
// 22, 23 and 24 of the line as documented in proc(5)).
//
// Field 2 is the executable name in parentheses, and it can itself contain both spaces and
// parentheses -- a process named "ev(il) name" is legal. Scanning for the LAST ')' is the only
// reliable way to find where the fixed-width fields begin; sscanf on the whole line gets this
// wrong for any such process.
bool _linuxParseStatField(const char* stat, int idx, int64* out);

// Read one numeric value out of the "Name:<whitespace>value" lines that /proc/meminfo and
// /proc/<pid>/{status,io} are made of. key must include the colon. The value is returned as
// written, so a line ending in "kB" yields kilobytes, not bytes.
bool _linuxProcFileField(const char* buf, const char* key, int64* out);

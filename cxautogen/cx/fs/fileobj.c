// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "fileobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

bool File_writeString(_In_ File* self, _In_opt_ strref str, _Out_opt_ size_t* byteswritten)
{
    size_t written = 0, wstep = 0;
    bool ret = true;

    striter iter;
    striBorrow(&iter, str);
    while (iter.len > 0) {
        if (!fileWrite(self, iter.bytes, iter.len, &wstep)) {
            ret = false;
            break;
        }
        written += wstep;
        striNext(&iter);
    }

    if (byteswritten)
        *byteswritten = written;
    return ret;
}

// Autogen begins -----
// clang-format off
#include "fileobj.auto.inc"
// clang-format on
// Autogen ends -------

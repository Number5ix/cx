// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "platform/win/win_net_socket.h"
// clang-format on
// ==================== Auto-generated section ends ======================

bool NetSocketWin_send(_In_ NetSocketWin* self, _In_ uint8* data, size_t len, _In_opt_ NetAddr* dest, flags_t flags)
{
    return 0;
}

bool NetSocketWin_close(_In_ NetSocketWin* self)
{
    return false;
}

void NetSocketWin_destroy(_In_ NetSocketWin* self)
{
    return;
}


// Autogen begins -----
// clang-format off
#include "platform/win/win_net_socket.auto.inc"
// clang-format on
// Autogen ends -------

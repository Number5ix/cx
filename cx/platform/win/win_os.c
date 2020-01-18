#include "cx/platform/os.h"
#include "cx/platform/win.h"

bool osIsWine()
{
    static int result = -1;

    if (result < 0) {
        HMODULE ntdllModule = GetModuleHandleW(L"ntdll.dll");
        void* wine_get_version;

        if (!ntdllModule) {
            result = 0;
            return result;
        }

        wine_get_version = GetProcAddress(ntdllModule, "wine_get_version");
        result = wine_get_version ? 1 : 0;
    }

    return result;
}

void osYield()
{
    SwitchToThread();
}

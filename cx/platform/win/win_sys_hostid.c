#include "cx/sys/hostid_private.h"
#include "cx/string.h"
#include "cx/utils/scratch.h"
#include "cx/platform/win.h"

#include <mbedtls/entropy.h>

static int32 hostIdTryRegistry(HKEY hive, int32 retval, mbedtls_md_context_t *shactx)
{
    int32 ret = 0;
    HKEY key;

    if (RegCreateKeyExW(hive, L"SOFTWARE\\CX", 0, NULL, 0,
                        STANDARD_RIGHTS_REQUIRED | KEY_QUERY_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        DWORD sz = 0;
        if (RegQueryValueExW(key, L"HostId", NULL, NULL, NULL, &sz) == ERROR_SUCCESS) {
            if (sz == 64) {
                uint8 *buf = scratchGet(sz);
                RegQueryValueExW(key, L"HostId", NULL, NULL, (uint8*)buf, &sz);
                mbedtls_md_update(shactx, buf, sz);
                ret = retval;
            }
        }

        // couldn't get good data from registry, try to create it
        if (!ret) {
            RegCloseKey(key);
            if (RegCreateKeyExW(hive, L"SOFTWARE\\CX", 0, NULL, 0,
                                STANDARD_RIGHTS_REQUIRED | KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
                mbedtls_entropy_context entropy;
                uint8 randbuf[64];

                mbedtls_entropy_init(&entropy);
                mbedtls_entropy_func(&entropy, (unsigned char*)randbuf, sizeof(randbuf));
                mbedtls_entropy_free(&entropy);

                if (RegSetValueExW(key, L"HostId", 0, REG_BINARY, randbuf, sizeof(randbuf)) == ERROR_SUCCESS) {
                    mbedtls_md_update(shactx, randbuf, sizeof(randbuf));
                    ret = retval;
                }
            }
        }
        RegCloseKey(key);
    }

    return ret;
}

int32 hostIdPlatformInit(mbedtls_md_context_t *shactx)
{
    int32 ret = 0;
    HKEY key;

    // prefer machine-wide registry entry if possible
    ret = hostIdTryRegistry(HKEY_LOCAL_MACHINE, HID_SourceMachineRegistry, shactx);

    // but if running as non-admin it's not likely it can be created, so save it per-user instead
    if (!ret)
        ret = hostIdTryRegistry(HKEY_CURRENT_USER, HID_SourceUserRegistry, shactx);

    // mostly unique but may not be reliable on wine
    if (!ret &&
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD sz = 0;
        if (RegQueryValueExW(key, L"MachineGuid", NULL, NULL, NULL, &sz) == ERROR_SUCCESS) {
            wchar_t *buf = scratchGet(sz);
            RegQueryValueExW(key, L"MachineGuid", NULL, NULL, (uint8*)buf, &sz);
            string guid = 0;
            strFromUTF16(&guid, buf, cstrLenw(buf));
            mbedtls_md_update(shactx, strC(&guid), strLen(guid));
            strDestroy(&guid);
            ret = HID_SourceCrypto;
        }
        RegCloseKey(key);
    }

    return ret;
}

int32 hostIdPlatformInitFallback(mbedtls_md_context_t *shactx)
{
    // ultimate panic fallback
    char buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD bufsz = sizeof(buf);
    GetComputerNameA(buf, &bufsz);
    mbedtls_md_update(shactx, buf, cstrLen(buf));
    return HID_SourceComputerName;
}

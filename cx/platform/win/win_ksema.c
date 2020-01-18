#include <cx/platform/ksema.h>
#include <cx/time.h>
#include <cx/platform/win.h>

bool kernelSemaInit(kernelSema *sema, int32 count)
{
    HANDLE *psem = (HANDLE*)sema;
    *psem = CreateSemaphoreW(NULL, count, LONG_MAX, NULL);
    return *psem;
}

bool kernelSemaDestroy(kernelSema *sema)
{
    HANDLE *psem = (HANDLE*)sema;
    bool ret = CloseHandle(*psem);
    *psem = NULL;
    return ret;
}

bool kernelSemaDec(kernelSema *sema)
{
    HANDLE sem = *(HANDLE*)sema;
    return WaitForSingleObject(sem, INFINITE) == WAIT_OBJECT_0;
}

bool kernelSemaTryDec(kernelSema *sema)
{
    HANDLE sem = *(HANDLE*)sema;
    return WaitForSingleObject(sem, 0) == WAIT_OBJECT_0;
}

bool kernelSemaTryDecTimeout(kernelSema *sema, int64 timeout)
{
    HANDLE sem = *(HANDLE*)sema;
    return WaitForSingleObject(sem, (DWORD)timeToMsec(timeout)) == WAIT_OBJECT_0;
}

bool kernelSemaInc(kernelSema *sema, int32 count)
{
    HANDLE sem = *(HANDLE*)sema;
    return ReleaseSemaphore(sem, count, NULL);
}

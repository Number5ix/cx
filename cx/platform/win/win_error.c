#include "cx/platform/win.h"
#include "cx/core/error.h"

bool winMapLastError()
{
    DWORD err = GetLastError();

    switch (err) {
    case ERROR_FILE_NOT_FOUND:
        cxerr = CX_FileNotFound;
        break;
    case ERROR_ACCESS_DENIED:
        cxerr = CX_AccessDenied;
        break;
    default:
        cxerr = CX_Unspecified;
    }

    // for caller convenience
    return false;
}

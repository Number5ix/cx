#include "cx/debug/error.h"
#include "cx/platform/win.h"

bool winMapLastError()
{
    DWORD err = GetLastError();

    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        cxerr = CX_FileNotFound;
        break;
    case ERROR_INVALID_PARAMETER:
    case ERROR_BAD_ARGUMENTS:
        cxerr = CX_InvalidArgument;
        break;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
        cxerr = CX_NotSupported;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        cxerr = CX_AccessDenied;
        break;
    default:
        cxerr = CX_Unspecified;
    }

    // for caller convenience
    return false;
}

#include "win32_ui.h"

// ---- monitors ----

static BOOL CALLBACK addMonitor(HMONITOR mon, HDC dc, LPRECT rc, LPARAM param)
{
    unused_noeval(dc);
    unused_noeval(rc);
    sa_CxUiMonitor* out = (sa_CxUiMonitor*)param;
    MONITORINFO mi      = { sizeof(mi) };
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;

    CxUiMonitor m = {
        .bounds   = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top },
        .workArea = { mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top },
        .scale    = _cxuiWin32MonitorScale(mon),
        .flags    = CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown,
    };
    if (mi.dwFlags & MONITORINFOF_PRIMARY) {
        m.flags |= CXUI_MON_Primary;
        saInsert(out, 0, opaque, m);
    } else {
        saPush(out, opaque, m);
    }
    return TRUE;
}

void _cxuiWin32Monitors(sa_CxUiMonitor* out)
{
    EnumDisplayMonitors(NULL, NULL, addMonitor, (LPARAM)out);
}

// ---- clipboard ----

// Another program may have it open for a moment.
static bool clipboardOpen(void)
{
    for (int i = 0; i < 10; i++) {
        if (OpenClipboard(_cxuiWin32DispatchWindow()))
            return true;
        Sleep(5);
    }
    return false;
}

static bool clipboardPut(strref text)
{
    // Counts the terminator; 0 for text that isn't valid UTF-8
    size_t units = strEmpty(text) ? 1 : strToUTF16(text, NULL, 0);
    HGLOBAL mem  = units ? GlobalAlloc(GMEM_MOVEABLE, units * sizeof(WCHAR)) : NULL;
    bool ok      = false;
    if (mem) {
        WCHAR* buf = GlobalLock(mem);
        buf[0]     = 0;
        if (units > 1)
            strToUTF16(text, (uint16*)buf, units);
        GlobalUnlock(mem);
        if (clipboardOpen()) {
            EmptyClipboard();
            ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
            CloseClipboard();
        }
        // The system owns it once set.
        if (!ok)
            GlobalFree(mem);
    }
    return ok;
}

_Use_decl_annotations_
bool _cxuiWin32ClipboardSetText(strref text)
{
    string crlf = 0;
    strReplace(&crlf, text, _SL("\r\n"), _SL("\n"), 0);
    strReplace(&crlf, crlf, _SL("\n"), _SL("\r\n"), 0);
    bool ok = clipboardPut(crlf);
    strDestroy(&crlf);
    return ok;
}

// Who owns the clipboard makes no difference to reading it here.
_Use_decl_annotations_
bool _cxuiWin32TestClipboardForeign(strref text)
{
    return clipboardPut(text);
}

static bool clipboardGet(string* out)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !clipboardOpen())
        return false;

    bool ok      = false;
    HANDLE mem   = GetClipboardData(CF_UNICODETEXT);
    WCHAR* wide  = mem ? GlobalLock(mem) : NULL;
    if (wide) {
        // The data may not end where its block does.
        size_t max   = GlobalSize(mem) / sizeof(WCHAR);
        size_t units = 0;
        while (units < max && wide[units])
            units++;
        ok = units == 0 || strFromUTF16(out, (const uint16*)wide, units);
        GlobalUnlock(mem);
    }
    CloseClipboard();
    return ok;
}

_Use_decl_annotations_
bool _cxuiWin32ClipboardGetText(string* out)
{
    bool ok = clipboardGet(out);
    if (ok)
        strReplace(out, *out, _SL("\r\n"), _SL("\n"), 0);
    return ok;
}

_Use_decl_annotations_
bool _cxuiWin32TestClipboardRaw(string* out)
{
    return clipboardGet(out);
}

// The display configuration, which names monitors, isn't in the XP build's headers; this file
// takes Windows 7's declarations, and finds each such function at run time, so its imports stay
// XP's.
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06010000
#undef WINVER
#define WINVER 0x0601
#endif

#include "win32_ui.h"

// ---- monitors ----

typedef LONG(WINAPI* GetDisplayConfigBufferSizesFn)(UINT32 flags, UINT32* paths, UINT32* modes);
typedef LONG(WINAPI* QueryDisplayConfigFn)(UINT32 flags, UINT32* npaths, DISPLAYCONFIG_PATH_INFO* paths, UINT32* nmodes,
                                          DISPLAYCONFIG_MODE_INFO* modes, DISPLAYCONFIG_TOPOLOGY_ID* topology);
typedef LONG(WINAPI* DisplayConfigGetDeviceInfoFn)(DISPLAYCONFIG_DEVICE_INFO_HEADER* info);

static void fromWide(string* out, const WCHAR* w)
{
    strFromUTF16(out, (const uint16*)w, wcslen(w));
}

// The monitor shown through a GDI device such as \\.\DISPLAY1: its device path, which stays the
// same for the same monitor on the same connector, and its own name.
static void monitorNames(const WCHAR* device, CxUiMonitor* m)
{
    static GetDisplayConfigBufferSizesFn sizesFn;
    static QueryDisplayConfigFn queryFn;
    static DisplayConfigGetDeviceInfoFn infoFn;
    static bool looked;
    if (!looked) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        looked         = true;
        if (user32) {
            sizesFn = (GetDisplayConfigBufferSizesFn)GetProcAddress(user32, "GetDisplayConfigBufferSizes");
            queryFn = (QueryDisplayConfigFn)GetProcAddress(user32, "QueryDisplayConfig");
            infoFn  = (DisplayConfigGetDeviceInfoFn)GetProcAddress(user32, "DisplayConfigGetDeviceInfo");
        }
    }
    if (!sizesFn || !queryFn || !infoFn)
        return;

    DISPLAYCONFIG_PATH_INFO* paths = NULL;
    DISPLAYCONFIG_MODE_INFO* modes = NULL;
    UINT32 npaths = 0, nmodes = 0;
    // Again if the configuration grew between the two calls
    for (int32 tries = 0; tries < 3; tries++) {
        if (sizesFn(QDC_ONLY_ACTIVE_PATHS, &npaths, &nmodes) != ERROR_SUCCESS) {
            npaths = 0;
            break;
        }
        xaFree(paths);
        xaFree(modes);
        paths       = xaAlloc(max(npaths, 1) * sizeof(*paths), XA_Zero);
        modes       = xaAlloc(max(nmodes, 1) * sizeof(*modes), XA_Zero);
        LONG result = queryFn(QDC_ONLY_ACTIVE_PATHS, &npaths, paths, &nmodes, modes, NULL);
        if (result == ERROR_SUCCESS)
            break;
        npaths = 0;
        if (result != ERROR_INSUFFICIENT_BUFFER)
            break;
    }

    for (UINT32 i = 0; i < npaths; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = { 0 };
        source.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size      = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id        = paths[i].sourceInfo.id;
        if (infoFn(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, device) != 0)
            continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = { 0 };
        target.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size      = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id        = paths[i].targetInfo.id;
        if (infoFn(&target.header) == ERROR_SUCCESS) {
            fromWide(&m->id, target.monitorDevicePath);
            fromWide(&m->name, target.monitorFriendlyDeviceName);
        }
        break;
    }
    xaFree(paths);
    xaFree(modes);
}

static BOOL CALLBACK addMonitor(HMONITOR mon, HDC dc, LPRECT rc, LPARAM param)
{
    unused_noeval(dc);
    unused_noeval(rc);
    sa_CxUiMonitor* out = (sa_CxUiMonitor*)param;
    MONITORINFOEXW mi   = { .cbSize = sizeof(mi) };
    if (!GetMonitorInfoW(mon, (MONITORINFO*)&mi))
        return TRUE;

    CxUiMonitor m = {
        .bounds   = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top },
        .workArea = { mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top },
        .scale    = _cxuiWin32MonitorScale(mon),
        .flags    = CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown,
    };
    DEVMODEW dm = { .dmSize = sizeof(dm) };
    // 0 and 1 stand for the hardware's default rate.
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        m.refreshHz = (float32)dm.dmDisplayFrequency;
    monitorNames(mi.szDevice, &m);
    if (mi.dwFlags & MONITORINFOF_PRIMARY) {
        m.flags |= CXUI_MON_Primary;
        saInsert(out, 0, CxUiMonitor, m);
    } else {
        saPush(out, CxUiMonitor, m);
    }
    cxuiMonitorDestroy(&m);
    return TRUE;
}

void _cxuiWin32Monitors(sa_CxUiMonitor* out)
{
    EnumDisplayMonitors(NULL, NULL, addMonitor, (LPARAM)out);
}

// Where _cxuiWin32Monitors lists a monitor: the primary first, then the others as Windows gives
// them
typedef struct MonitorIndex {
    HMONITOR want;
    int32 seen;
    int32 found;
} MonitorIndex;

static BOOL CALLBACK indexMonitor(HMONITOR mon, HDC dc, LPRECT rc, LPARAM param)
{
    unused_noeval(dc);
    unused_noeval(rc);
    MonitorIndex* mi   = (MonitorIndex*)param;
    MONITORINFO info   = { sizeof(info) };
    if (!GetMonitorInfoW(mon, &info))
        return TRUE;
    bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    if (mon == mi->want)
        mi->found = primary ? 0 : mi->seen + 1;
    if (!primary)
        mi->seen++;
    return TRUE;
}

int32 _cxuiWin32WindowMonitor(HWND hwnd)
{
    MonitorIndex mi = { MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL), 0, -1 };
    if (!mi.want)
        return -1;
    EnumDisplayMonitors(NULL, NULL, indexMonitor, (LPARAM)&mi);
    return mi.found;
}

// Every top-level window hears of a change; they make one notification.
static bool changePending;

static bool displayChangedTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    changePending = false;
    _cxuiNativeEnter();
    _cxuiMonitorsChanged();
    _cxuiNativeLeave();
    return true;
}

void _cxuiWin32DisplayChanged(void)
{
    if (changePending)
        return;
    changePending = true;
    cxuiPost(closureCreate(displayChangedTask, stvNone));
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

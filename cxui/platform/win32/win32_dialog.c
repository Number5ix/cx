#include "win32_ui.h"

#include <commctrl.h>
#include <cx/utils/lazyinit.h>

#pragma comment(lib, "user32.lib")

// Message boxes use TaskDialogIndirect, and text fields their cue banner, which need comctl32
// version 6. A static library can't add the manifest that would select it, so cxui makes its own
// activation context from the comctl6 manifest shell32.dll carries: TaskDialogIndirect comes from
// the comctl32 that context loads, and controls created under it are version 6's. Without version
// 6 (XP without it, Wine) message boxes use MessageBoxW, and the core builds custom ones.
//
// The task dialog's config, buttons and callback are declared here with the SDK's layout, because
// headers targeting XP leave them out.

#pragma pack(push, 1)
typedef struct CxUiTaskDialogButton {
    int id;
    PCWSTR text;
} CxUiTaskDialogButton;

typedef HRESULT(CALLBACK* CxUiTaskDialogCallback)(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LONG_PTR data);

typedef struct CxUiTaskDialogConfig {
    UINT cbSize;
    HWND hwndParent;
    HINSTANCE hInstance;
    int dwFlags;
    int dwCommonButtons;
    PCWSTR pszWindowTitle;
    union {
        HICON hMainIcon;
        PCWSTR pszMainIcon;
    };
    PCWSTR pszMainInstruction;
    PCWSTR pszContent;
    UINT cButtons;
    const CxUiTaskDialogButton* pButtons;
    int nDefaultButton;
    UINT cRadioButtons;
    const CxUiTaskDialogButton* pRadioButtons;
    int nDefaultRadioButton;
    PCWSTR pszVerificationText;
    PCWSTR pszExpandedInformation;
    PCWSTR pszExpandedControlText;
    PCWSTR pszCollapsedControlText;
    union {
        HICON hFooterIcon;
        PCWSTR pszFooterIcon;
    };
    PCWSTR pszFooter;
    CxUiTaskDialogCallback pfCallback;
    LONG_PTR lpCallbackData;
    UINT cxWidth;
} CxUiTaskDialogConfig;
#pragma pack(pop)

#if defined(NTDDI_VISTA) && NTDDI_VERSION >= NTDDI_VISTA
_Static_assert(sizeof(CxUiTaskDialogConfig) == sizeof(TASKDIALOGCONFIG), "task dialog config layout");
#endif

#define CXUI_TDF_USE_HICON_MAIN              0x0002
#define CXUI_TDF_ALLOW_DIALOG_CANCELLATION   0x0008
#define CXUI_TDF_POSITION_RELATIVE_TO_WINDOW 0x1000
#define CXUI_TDCBF_OK                        0x0001
#define CXUI_TDCBF_YES                       0x0002
#define CXUI_TDCBF_NO                        0x0004
#define CXUI_TDCBF_CANCEL                    0x0008
#define CXUI_TDN_CREATED                     0
#define CXUI_TDM_CLICK_BUTTON                (WM_USER + 102)
#define CXUI_TD_WARNING_ICON                 MAKEINTRESOURCEW(-1)
#define CXUI_TD_ERROR_ICON                   MAKEINTRESOURCEW(-2)
#define CXUI_TD_INFORMATION_ICON             MAKEINTRESOURCEW(-3)

typedef struct CxUiDllVersion {
    DWORD cbSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformID;
} CxUiDllVersion;

typedef HRESULT(WINAPI* DllGetVersionFn)(CxUiDllVersion* v);
typedef HRESULT(WINAPI* TaskDialogIndirectFn)(const CxUiTaskDialogConfig* config, int* button, int* radio,
                                              BOOL* verified);

typedef BOOL(WINAPI* InitCommonControlsExFn)(const INITCOMMONCONTROLSEX* icc);

static TaskDialogIndirectFn pTaskDialogIndirect;
static LazyInitState comctlInit;
static HANDLE comctlCtx = INVALID_HANDLE_VALUE;
static bool comctl6;

static void comctlInitOnce(void* unused)
{
    unused_noeval(unused);

    WCHAR sys[MAX_PATH], path[MAX_PATH + 16];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    wcscpy_s(path, MAX_PATH + 16, sys);
    wcscat_s(path, MAX_PATH + 16, L"\\shell32.dll");

    // shell32.dll carries a comctl6 dependency manifest as resource 124, from XP to Windows 11.
    // The context lasts for the process.
    ACTCTXW ac             = { sizeof(ac) };
    ac.dwFlags             = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
    ac.lpSource            = path;
    ac.lpResourceName      = MAKEINTRESOURCEW(124);
    ac.lpAssemblyDirectory = sys;
    HANDLE ctx             = CreateActCtxW(&ac);
    comctlCtx              = ctx;

    ULONG_PTR cookie = 0;
    bool active      = ctx != INVALID_HANDLE_VALUE && ActivateActCtx(ctx, &cookie);
    HMODULE cc       = LoadLibraryW(L"comctl32.dll");
    // The slider, progress bar and tab classes are registered only when asked for.
    InitCommonControlsExFn init = cc ? (InitCommonControlsExFn)GetProcAddress(cc, "InitCommonControlsEx") : NULL;
    INITCOMMONCONTROLSEX icc    = { sizeof(icc), ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES };
    if (init)
        init(&icc);
    if (active)
        DeactivateActCtx(0, cookie);
    if (!cc)
        return;

    // Only the major version means anything: Windows 10 reports 6.16 and 5.82 with a stale build.
    CxUiDllVersion ver   = { sizeof(ver) };
    DllGetVersionFn getv = (DllGetVersionFn)GetProcAddress(cc, "DllGetVersion");
    comctl6 = getv && SUCCEEDED(getv(&ver)) && ver.dwMajorVersion >= 6;
    if (comctl6)
        pTaskDialogIndirect = (TaskDialogIndirectFn)GetProcAddress(cc, "TaskDialogIndirect");
}

_Use_decl_annotations_
bool _cxuiWin32Comctl6Begin(ULONG_PTR* cookie)
{
    lazyInit(&comctlInit, comctlInitOnce, NULL);
    *cookie = 0;
    return comctlCtx != INVALID_HANDLE_VALUE && ActivateActCtx(comctlCtx, cookie);
}

void _cxuiWin32Comctl6End(ULONG_PTR cookie)
{
    DeactivateActCtx(0, cookie);
}

bool _cxuiWin32HasComctl6(void)
{
    lazyInit(&comctlInit, comctlInitOnce, NULL);
    return comctl6;
}

bool _cxuiWin32HasComctl610(void)
{
    lazyInit(&comctlInit, comctlInitOnce, NULL);
    return pTaskDialogIndirect != NULL;
}

// ---- open boxes ----

// Open boxes, innermost first, so tests can answer one. Its window is found when it is created.
typedef struct Win32Box {
    HWND hwnd;
    bool task;
    const int* ids;
    int32 nids;
    struct Win32Box* outer;
} Win32Box;

static Win32Box* openBoxes;   // UI thread only
static HHOOK boxHook;

// Catches the window MessageBoxW creates, which it doesn't otherwise reveal.
static LRESULT CALLBACK boxHookProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND && openBoxes && !openBoxes->hwnd) {
        CBT_CREATEWNDW* cw = (CBT_CREATEWNDW*)lp;
        if (cw->lpcs->lpszClass == MAKEINTATOM(32770))   // the dialog class
            openBoxes->hwnd = (HWND)wp;
    }
    return CallNextHookEx(boxHook, code, wp, lp);
}

static HRESULT CALLBACK taskCallback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LONG_PTR data)
{
    unused_noeval(wp);
    unused_noeval(lp);
    if (msg == CXUI_TDN_CREATED)
        ((Win32Box*)data)->hwnd = hwnd;
    return S_OK;
}

// ---- message boxes ----

static const int idsOk[]          = { IDOK };
static const int idsOkCancel[]    = { IDOK, IDCANCEL };
static const int idsYesNo[]       = { IDYES, IDNO };
static const int idsYesNoCancel[] = { IDYES, IDNO, IDCANCEL };
static const int idsCustom[]      = { 1001, 1002, 1003 };

static int32 buttonIndex(Win32Box* box, int id, int32 cancel)
{
    for (int32 i = 0; i < box->nids; i++) {
        if (box->ids[i] == id)
            return i + 1;
    }
    // Escape on an OK box closes it with IDCANCEL in a task dialog, and IDOK in MessageBoxW.
    if (id == IDCANCEL)
        return cancel ? cancel : box->nids == 1 ? 1 : 0;
    return 0;
}

static int32 taskDialogBox(Win32Box* box, const CxUiMessageBoxDesc* desc, HWND owner, LPCWSTR title,
                           LPCWSTR text)
{
    static const int commonButtons[] = {
        [CXUI_MB_Ok]          = CXUI_TDCBF_OK,
        [CXUI_MB_OkCancel]    = CXUI_TDCBF_OK | CXUI_TDCBF_CANCEL,
        [CXUI_MB_YesNo]       = CXUI_TDCBF_YES | CXUI_TDCBF_NO,
        [CXUI_MB_YesNoCancel] = CXUI_TDCBF_YES | CXUI_TDCBF_NO | CXUI_TDCBF_CANCEL,
    };

    CxUiTaskDialogConfig tc = { sizeof(tc) };
    tc.hwndParent           = owner;
    tc.dwFlags              = owner ? CXUI_TDF_POSITION_RELATIVE_TO_WINDOW : 0;
    bool custom             = desc->buttons == CXUI_MB_Custom;
    if (custom ? desc->cancelButton > 0 : (box->nids == 1 || box->ids[box->nids - 1] == IDCANCEL))
        tc.dwFlags |= CXUI_TDF_ALLOW_DIALOG_CANCELLATION;
    CxUiTaskDialogButton buttons[3];
    uint16* titles[3] = { 0 };
    if (custom) {
        for (int32 i = 0; i < box->nids; i++) {
            titles[i]  = strToUTF16A(desc->custom[i]);
            buttons[i] = (CxUiTaskDialogButton){ box->ids[i], titles[i] ? (PCWSTR)titles[i] : L"" };
        }
        tc.cButtons = (UINT)box->nids;
        tc.pButtons = buttons;
    } else {
        tc.dwCommonButtons = commonButtons[desc->buttons];
    }
    tc.pszWindowTitle  = title;
    tc.pszContent      = text;
    switch (desc->kind) {
    case CXUI_MB_Warning:
        tc.pszMainIcon = CXUI_TD_WARNING_ICON;
        break;
    case CXUI_MB_Error:
        tc.pszMainIcon = CXUI_TD_ERROR_ICON;
        break;
    case CXUI_MB_Question:
        // Task dialogs have no question icon of their own.
        tc.dwFlags |= CXUI_TDF_USE_HICON_MAIN;
        tc.hMainIcon = LoadIconW(NULL, IDI_QUESTION);
        break;
    default:
        tc.pszMainIcon = CXUI_TD_INFORMATION_ICON;
        break;
    }
    if (desc->defaultButton > 0 && desc->defaultButton <= box->nids)
        tc.nDefaultButton = box->ids[desc->defaultButton - 1];
    tc.pfCallback     = taskCallback;
    tc.lpCallbackData = (LONG_PTR)box;

    int id    = 0;
    HRESULT r = pTaskDialogIndirect(&tc, &id, NULL, NULL);
    for (int32 i = 0; i < 3; i++)
        xaFree(titles[i]);
    if (FAILED(r))
        return 0;
    return buttonIndex(box, id, custom ? desc->cancelButton : 0);
}

static int32 classicBox(Win32Box* box, const CxUiMessageBoxDesc* desc, HWND owner, LPCWSTR title, LPCWSTR text)
{
    static const UINT buttonTypes[] = {
        [CXUI_MB_Ok]          = MB_OK,
        [CXUI_MB_OkCancel]    = MB_OKCANCEL,
        [CXUI_MB_YesNo]       = MB_YESNO,
        [CXUI_MB_YesNoCancel] = MB_YESNOCANCEL,
    };
    static const UINT icons[] = {
        [CXUI_MB_Info]     = MB_ICONINFORMATION,
        [CXUI_MB_Warning]  = MB_ICONWARNING,
        [CXUI_MB_Error]    = MB_ICONERROR,
        [CXUI_MB_Question] = MB_ICONQUESTION,
    };
    static const UINT defaults[] = { MB_DEFBUTTON1, MB_DEFBUTTON2, MB_DEFBUTTON3 };

    UINT type = buttonTypes[desc->buttons] | icons[desc->kind < 4 ? desc->kind : 0];
    if (desc->defaultButton > 0 && desc->defaultButton <= box->nids)
        type |= defaults[desc->defaultButton - 1];

    HHOOK outerHook = boxHook;
    boxHook         = SetWindowsHookExW(WH_CBT, boxHookProc, NULL, GetCurrentThreadId());
    int id          = MessageBoxW(owner, text, title, type);
    if (boxHook)
        UnhookWindowsHookEx(boxHook);
    boxHook = outerHook;
    return buttonIndex(box, id, 0);
}

int32 _cxuiWin32MessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent)
{
    lazyInit(&comctlInit, comctlInitOnce, NULL);

    static const struct {
        const int* ids;
        int32 n;
    } sets[] = {
        [CXUI_MB_Ok]          = { idsOk, 1 },
        [CXUI_MB_OkCancel]    = { idsOkCancel, 2 },
        [CXUI_MB_YesNo]       = { idsYesNo, 2 },
        [CXUI_MB_YesNoCancel] = { idsYesNoCancel, 3 },
    };
    const CxUiMessageBoxDesc* d = desc;
    if (d->buttons == CXUI_MB_Custom && !pTaskDialogIndirect)
        return CXUI_MB_Build;

    Win32Box box = { .outer = openBoxes, .task = pTaskDialogIndirect != NULL };
    if (d->buttons == CXUI_MB_Custom) {
        box.ids = idsCustom;
        while (box.nids < 3 && !strEmpty(d->custom[box.nids]))
            box.nids++;
    } else {
        box.ids  = sets[d->buttons].ids;
        box.nids = sets[d->buttons].n;
    }
    openBoxes = &box;

    uint16* title = strToUTF16A(d->title);
    uint16* text  = strToUTF16A(d->text);
    HWND owner    = _cxuiWin32PeerHwnd(parent);
    int32 result  = box.task ? taskDialogBox(&box, d, owner, (LPCWSTR)title, (LPCWSTR)text)
                             : classicBox(&box, d, owner, (LPCWSTR)title, (LPCWSTR)text);
    xaFree(title);
    xaFree(text);

    openBoxes = box.outer;
    return result;
}

// The id of the box's button: Windows gives an OK-only MessageBoxW box's button IDCANCEL, so Esc
// works, and returns IDOK for it; Wine's is IDOK.
static int boxButtonId(HWND dlg, int id, int32 nids)
{
    if (GetDlgItem(dlg, id))
        return id;
    if (nids == 1 && GetDlgItem(dlg, IDCANCEL))
        return IDCANCEL;
    return 0;
}

typedef struct FindBox {
    int id;
    int32 nids;
    HWND found;
} FindBox;

static BOOL CALLBACK findBox(HWND hwnd, LPARAM lp)
{
    FindBox* fb = (FindBox*)lp;
    WCHAR cls[16];
    if (!IsWindowVisible(hwnd) || !boxButtonId(hwnd, fb->id, fb->nids) || !GetClassNameW(hwnd, cls, 16) ||
        wcscmp(cls, L"#32770") != 0)
        return TRUE;
    fb->found = hwnd;
    return FALSE;
}

bool _cxuiWin32TestAnswerMessageBox(int32 button)
{
    Win32Box* box = openBoxes;
    if (!box || !box->hwnd || button < 1 || button > box->nids)
        return false;
    int id = box->ids[button - 1];
    if (box->task) {
        PostMessageW(box->hwnd, CXUI_TDM_CLICK_BUTTON, (WPARAM)id, 0);
        return true;
    }
    // The window the hook caught, if it is the box; otherwise the UI thread's box with that button
    HWND dlg = boxButtonId(box->hwnd, id, box->nids) ? box->hwnd : NULL;
    if (!dlg) {
        FindBox fb = { .id = id, .nids = box->nids };
        EnumThreadWindows(GetCurrentThreadId(), findBox, (LPARAM)&fb);
        dlg = fb.found;
    }
    if (!dlg)
        return false;
    int ctl = boxButtonId(dlg, id, box->nids);
    PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(ctl, BN_CLICKED), (LPARAM)GetDlgItem(dlg, ctl));
    return true;
}

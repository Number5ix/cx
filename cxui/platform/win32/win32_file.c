#include "win32_ui.h"

#include <commdlg.h>
#include <colordlg.h>
#include <dlgs.h>
// The XP SDK's shlobj.h has an empty typedef that newer compilers warn about
#pragma warning(push)
#pragma warning(disable: 4091)
#include <shlobj.h>
#pragma warning(pop)

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// File dialogs are the Vista dialog object (IFileDialog), driven from C through its vtable. The
// interfaces are declared here with the SDK's layout, because headers targeting XP leave them out.
// Where it can't be had (XP, or a UI thread whose COM apartment isn't single-threaded), files use
// GetOpenFileNameW and GetSaveFileNameW, and folders SHBrowseForFolderW.

typedef struct CxUiShellItem CxUiShellItem;
typedef struct CxUiShellItemVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(CxUiShellItem* self, REFIID riid, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(CxUiShellItem* self);
    ULONG(STDMETHODCALLTYPE* Release)(CxUiShellItem* self);
    void* BindToHandler;
    void* GetParent;
    HRESULT(STDMETHODCALLTYPE* GetDisplayName)(CxUiShellItem* self, DWORD sigdn, LPWSTR* name);
    void* GetAttributes;
    void* Compare;
} CxUiShellItemVtbl;
struct CxUiShellItem {
    const CxUiShellItemVtbl* lpVtbl;
};

typedef struct CxUiFilterSpec {
    LPCWSTR name;
    LPCWSTR spec;
} CxUiFilterSpec;

typedef struct CxUiFileDlg CxUiFileDlg;
typedef struct CxUiFileDlgVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(CxUiFileDlg* self, REFIID riid, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(CxUiFileDlg* self);
    ULONG(STDMETHODCALLTYPE* Release)(CxUiFileDlg* self);
    HRESULT(STDMETHODCALLTYPE* Show)(CxUiFileDlg* self, HWND owner);
    HRESULT(STDMETHODCALLTYPE* SetFileTypes)(CxUiFileDlg* self, UINT n, const CxUiFilterSpec* specs);
    HRESULT(STDMETHODCALLTYPE* SetFileTypeIndex)(CxUiFileDlg* self, UINT index);
    void* GetFileTypeIndex;
    void* Advise;
    void* Unadvise;
    HRESULT(STDMETHODCALLTYPE* SetOptions)(CxUiFileDlg* self, DWORD options);
    HRESULT(STDMETHODCALLTYPE* GetOptions)(CxUiFileDlg* self, DWORD* options);
    void* SetDefaultFolder;
    HRESULT(STDMETHODCALLTYPE* SetFolder)(CxUiFileDlg* self, CxUiShellItem* item);
    void* GetFolder;
    void* GetCurrentSelection;
    HRESULT(STDMETHODCALLTYPE* SetFileName)(CxUiFileDlg* self, LPCWSTR name);
    void* GetFileName;
    HRESULT(STDMETHODCALLTYPE* SetTitle)(CxUiFileDlg* self, LPCWSTR title);
    void* SetOkButtonLabel;
    void* SetFileNameLabel;
    HRESULT(STDMETHODCALLTYPE* GetResult)(CxUiFileDlg* self, CxUiShellItem** item);
    void* AddPlace;
    HRESULT(STDMETHODCALLTYPE* SetDefaultExtension)(CxUiFileDlg* self, LPCWSTR ext);
    HRESULT(STDMETHODCALLTYPE* Close)(CxUiFileDlg* self, HRESULT hr);
    void* SetClientGuid;
    void* ClearClientData;
    void* SetFilter;
} CxUiFileDlgVtbl;
struct CxUiFileDlg {
    const CxUiFileDlgVtbl* lpVtbl;
};

typedef struct CxUiOleWindow CxUiOleWindow;
typedef struct CxUiOleWindowVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(CxUiOleWindow* self, REFIID riid, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(CxUiOleWindow* self);
    ULONG(STDMETHODCALLTYPE* Release)(CxUiOleWindow* self);
    HRESULT(STDMETHODCALLTYPE* GetWindow)(CxUiOleWindow* self, HWND* hwnd);
    void* ContextSensitiveHelp;
} CxUiOleWindowVtbl;
struct CxUiOleWindow {
    const CxUiOleWindowVtbl* lpVtbl;
};

static const GUID clsidFileOpenDialog = { 0xdc1c5a9c, 0xe88a, 0x4dde, { 0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7 } };
static const GUID clsidFileSaveDialog = { 0xc0b4e2f3, 0xba21, 0x4773, { 0x8d, 0xba, 0x33, 0x5e, 0xc9, 0x46, 0xeb, 0x8b } };
// IFileOpenDialog and IFileSaveDialog begin with IFileDialog's methods; Wine's objects don't give
// out IFileDialog itself.
static const GUID iidFileOpenDialog   = { 0xd57c7288, 0xd4ad, 0x4768, { 0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60 } };
static const GUID iidFileSaveDialog   = { 0x84bccd23, 0x5fde, 0x4cdb, { 0xae, 0xa4, 0xaf, 0x64, 0xb8, 0x3d, 0x78, 0xab } };
static const GUID iidShellItem        = { 0x43826d1e, 0xe718, 0x42ee, { 0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe } };
static const GUID iidOleWindow        = { 0x00000114, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

#define CXUI_FOS_OVERWRITEPROMPT 0x00000002
#define CXUI_FOS_NOCHANGEDIR     0x00000008
#define CXUI_FOS_PICKFOLDERS     0x00000020
#define CXUI_FOS_FORCEFILESYSTEM 0x00000040
#define CXUI_SIGDN_FILESYSPATH   0x80058000

typedef HRESULT(WINAPI* SHCreateItemFromParsingNameFn)(PCWSTR path, void* bindCtx, REFIID riid, void** out);

// Open dialogs, innermost first, so tests can answer one
typedef struct Win32FileBox {
    CxUiFileDlg* dlg;   // NULL for the XP dialogs
    uint32 classic;     // CXUI_FILE_* of an XP dialog being shown, or (uint32)-1
    HWND owner;
    struct Win32FileBox* outer;
} Win32FileBox;

static Win32FileBox* openFileBoxes;   // UI thread only

static WCHAR* wide(strref s)
{
    return (WCHAR*)strToUTF16A(s);
}

// "desc\0patterns\0...\0\0" for the XP dialogs, or the specs for the dialog object
static WCHAR* filterList(const CxUiFileDialogDesc* desc, CxUiFilterSpec* specs)
{
    string all = 0;
    for (int32 i = 0; i < desc->nfilters; i++) {
        strAppend(&all, desc->filters[i].desc);
        strAppend(&all, _S"\x01");
        strAppend(&all, strEq(desc->filters[i].patterns, _S"*") ? _S"*.*" : desc->filters[i].patterns);
        strAppend(&all, _S"\x01");
    }
    strAppend(&all, _S"\x01");
    WCHAR* w = wide(all);
    strDestroy(&all);
    // The separators become the terminators each part needs.
    int32 part = 0;
    WCHAR* start = w;
    for (WCHAR* c = w; c && *c; c++) {
        if (*c != 1)
            continue;
        *c = 0;
        if (specs && part / 2 < desc->nfilters) {
            if (part % 2 == 0)
                specs[part / 2].name = start;
            else
                specs[part / 2].spec = start;
        }
        part++;
        start = c + 1;
    }
    return w;
}

static bool itemPath(CxUiShellItem* item, string* out)
{
    LPWSTR p = NULL;
    if (!item || FAILED(item->lpVtbl->GetDisplayName(item, CXUI_SIGDN_FILESYSPATH, &p)) || !p)
        return false;
    strFromUTF16(out, (const uint16*)p, (int32)wcslen(p));
    CoTaskMemFree(p);
    return true;
}

// False if the dialog object can't be had; *chosen says whether the user chose something.
static bool objectDialog(Win32FileBox* box, const CxUiFileDialogDesc* desc, HWND owner, string* out, bool* chosen)
{
    static SHCreateItemFromParsingNameFn pCreateItem;
    if (!pCreateItem) {
        HMODULE shell = GetModuleHandleW(L"shell32.dll");
        pCreateItem   = shell ? (SHCreateItemFromParsingNameFn)GetProcAddress(shell, "SHCreateItemFromParsingName") : NULL;
    }
    CxUiFileDlg* dlg = NULL;
    bool save        = desc->mode == CXUI_FILE_Save;
    HRESULT made     = pCreateItem ? CoCreateInstance(save ? &clsidFileSaveDialog : &clsidFileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                                      save ? &iidFileSaveDialog : &iidFileOpenDialog, (void**)&dlg)
                                   : E_NOTIMPL;
    if (FAILED(made)) {
        logFmt(Info, _SL("cxui uses the XP file dialogs (error 0x${uint(hex)})"), stvar(uint32, (uint32)made));
        return false;
    }

    DWORD options = 0;
    dlg->lpVtbl->GetOptions(dlg, &options);
    options |= CXUI_FOS_FORCEFILESYSTEM | CXUI_FOS_NOCHANGEDIR;
    if (desc->mode == CXUI_FILE_Folder)
        options |= CXUI_FOS_PICKFOLDERS;
    if (desc->mode == CXUI_FILE_Save && (desc->flags & CXUI_FILE_NoOverwritePrompt))
        options &= ~CXUI_FOS_OVERWRITEPROMPT;
    dlg->lpVtbl->SetOptions(dlg, options);

    WCHAR* title = wide(desc->title);
    if (title)
        dlg->lpVtbl->SetTitle(dlg, title);
    CxUiFilterSpec specs[16];
    WCHAR* filters = NULL;
    if (desc->nfilters > 0) {
        CxUiFileDialogDesc d = *desc;
        d.nfilters           = min(d.nfilters, 16);
        filters              = filterList(&d, specs);
        dlg->lpVtbl->SetFileTypes(dlg, (UINT)d.nfilters, specs);
        dlg->lpVtbl->SetFileTypeIndex(dlg, (UINT)min(desc->filterIndex, d.nfilters - 1) + 1);
    }
    WCHAR* ext = wide(desc->defaultExt);
    if (desc->mode == CXUI_FILE_Save && ext)
        dlg->lpVtbl->SetDefaultExtension(dlg, ext);
    WCHAR* name = wide(desc->initialFile);
    if (desc->mode == CXUI_FILE_Save && name)
        dlg->lpVtbl->SetFileName(dlg, name);
    WCHAR* dir = wide(desc->initialDir);
    CxUiShellItem* folder = NULL;
    if (dir && SUCCEEDED(pCreateItem(dir, NULL, &iidShellItem, (void**)&folder))) {
        dlg->lpVtbl->SetFolder(dlg, folder);
        folder->lpVtbl->Release(folder);
    }

    box->dlg   = dlg;
    HRESULT hr = dlg->lpVtbl->Show(dlg, owner);
    box->dlg   = NULL;
    if (SUCCEEDED(hr)) {
        CxUiShellItem* item = NULL;
        *chosen             = SUCCEEDED(dlg->lpVtbl->GetResult(dlg, &item)) && itemPath(item, out);
        if (item)
            item->lpVtbl->Release(item);
    }
    dlg->lpVtbl->Release(dlg);
    xaFree(title);
    xaFree(filters);
    xaFree(ext);
    xaFree(name);
    xaFree(dir);
    return true;
}

// ---- the XP dialogs ----

static int CALLBACK folderCallback(HWND hwnd, UINT msg, LPARAM lp, LPARAM data)
{
    unused_noeval(lp);
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

static bool classicFolder(const CxUiFileDialogDesc* desc, HWND owner, string* out)
{
    WCHAR* title = wide(desc->title);
    WCHAR* dir   = wide(desc->initialDir);
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner   = owner;
    bi.lpszTitle   = title;
    bi.ulFlags     = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn        = folderCallback;
    bi.lParam      = (LPARAM)dir;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    WCHAR path[MAX_PATH];
    bool chosen = id && SHGetPathFromIDListW(id, path);
    if (chosen)
        strFromUTF16(out, (const uint16*)path, (int32)wcslen(path));
    if (id)
        CoTaskMemFree(id);
    xaFree(title);
    xaFree(dir);
    return chosen;
}

static bool classicFile(const CxUiFileDialogDesc* desc, HWND owner, string* out)
{
    WCHAR path[4096] = { 0 };
    WCHAR* name      = wide(desc->initialFile);
    if (desc->mode == CXUI_FILE_Save && name)
        wcsncpy_s(path, 4096, name, _TRUNCATE);
    WCHAR* title   = wide(desc->title);
    WCHAR* dir     = wide(desc->initialDir);
    WCHAR* ext     = wide(desc->defaultExt);
    WCHAR* filters = desc->nfilters > 0 ? filterList(desc, NULL) : NULL;

    OPENFILENAMEW ofn   = { sizeof(ofn) };
    ofn.hwndOwner       = owner;
    ofn.lpstrFilter     = filters;
    ofn.nFilterIndex    = (DWORD)desc->filterIndex + 1;
    ofn.lpstrFile       = path;
    ofn.nMaxFile        = 4096;
    ofn.lpstrInitialDir = dir;
    ofn.lpstrTitle      = title;
    ofn.lpstrDefExt     = desc->mode == CXUI_FILE_Save ? ext : NULL;
    ofn.Flags           = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (desc->mode == CXUI_FILE_Save) {
        if (!(desc->flags & CXUI_FILE_NoOverwritePrompt))
            ofn.Flags |= OFN_OVERWRITEPROMPT;
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    }
    bool chosen = desc->mode == CXUI_FILE_Save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (chosen)
        strFromUTF16(out, (const uint16*)path, (int32)wcslen(path));
    xaFree(name);
    xaFree(title);
    xaFree(dir);
    xaFree(ext);
    xaFree(filters);
    return chosen;
}

bool _cxuiWin32FileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out)
{
    HWND owner       = _cxuiWin32PeerHwnd(parent);
    Win32FileBox box = { .classic = (uint32)-1, .owner = owner, .outer = openFileBoxes };
    openFileBoxes    = &box;

    // The dialog object needs a single-threaded apartment; one the thread already has in another
    // mode leaves the XP dialogs.
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(init))
        logFmt(Info, _SL("cxui uses the XP file dialogs (error 0x${uint(hex)})"), stvar(uint32, (uint32)init));
    bool chosen = false;
    if (FAILED(init) || !objectDialog(&box, desc, owner, out, &chosen)) {
        box.classic = desc->mode;
        chosen = desc->mode == CXUI_FILE_Folder ? classicFolder(desc, owner, out) : classicFile(desc, owner, out);
    }
    if (SUCCEEDED(init))
        CoUninitialize();

    openFileBoxes = box.outer;
    return chosen;
}

typedef struct FindDialog {
    HWND owner;
    HWND found;
} FindDialog;

static BOOL CALLBACK findDialog(HWND hwnd, LPARAM lp)
{
    FindDialog* fd = (FindDialog*)lp;
    WCHAR cls[16];
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != fd->owner || !GetClassNameW(hwnd, cls, 16) ||
        wcscmp(cls, L"#32770") != 0)
        return TRUE;
    fd->found = hwnd;
    return FALSE;
}

// An XP dialog, found among the UI thread's windows: the path typed in, or for folders selected,
// then OK pressed; or Cancel
static bool answerClassic(Win32FileBox* box, strref path)
{
    FindDialog fd = { .owner = box->owner };
    EnumThreadWindows(GetCurrentThreadId(), findDialog, (LPARAM)&fd);
    if (!fd.found)
        return false;
    if (!path) {
        PostMessageW(fd.found, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        return true;
    }
    WCHAR* w = wide(path);
    if (box->classic == CXUI_FILE_Folder) {
        SendMessageW(fd.found, BFFM_SETSELECTIONW, TRUE, (LPARAM)w);
    } else {
        // The name box is a combo box from Windows 2000 on, an edit control before
        SendMessageW(fd.found, CDM_SETCONTROLTEXT, cmb13, (LPARAM)w);
        SendMessageW(fd.found, CDM_SETCONTROLTEXT, edt1, (LPARAM)w);
    }
    xaFree(w);
    PostMessageW(fd.found, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    return true;
}

// Types the path into the dialog and presses its OK button, as the user would; cancelling closes
// it the way its own Cancel does.
bool _cxuiWin32TestAnswerFileDialog(strref path)
{
    Win32FileBox* box = openFileBoxes;
    if (box && !box->dlg && box->classic != (uint32)-1)
        return answerClassic(box, path);
    if (!box || !box->dlg)
        return false;
    CxUiFileDlg* dlg = box->dlg;
    if (!path) {
        dlg->lpVtbl->Close(dlg, HRESULT_FROM_WIN32(ERROR_CANCELLED));
        return true;
    }
    CxUiOleWindow* ow = NULL;
    HWND hwnd         = NULL;
    if (SUCCEEDED(dlg->lpVtbl->QueryInterface(dlg, &iidOleWindow, (void**)&ow))) {
        ow->lpVtbl->GetWindow(ow, &hwnd);
        ow->lpVtbl->Release(ow);
    }
    if (!hwnd)
        return false;
    WCHAR* w = wide(path);
    dlg->lpVtbl->SetFileName(dlg, w ? w : L"");
    xaFree(w);
    PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    return true;
}

// ---- color dialog ----

typedef struct Win32ColorBox {
    HWND dlg;   // once the dialog has started
    HWND owner;
    LPCWSTR title;
    struct Win32ColorBox* outer;
} Win32ColorBox;

static Win32ColorBox* openColorBoxes;   // UI thread only
// The dialog's custom colors, kept for the session as programs usually do
static COLORREF customColors[16];
static bool customColorsSet;

static UINT_PTR CALLBACK colorHook(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    unused_noeval(wp);
    if (msg != WM_INITDIALOG)
        return 0;
    Win32ColorBox* box = (Win32ColorBox*)((CHOOSECOLORW*)lp)->lCustData;
    box->dlg           = dlg;
    if (box->title)
        SetWindowTextW(dlg, box->title);
    _cxuiWin32MoveNear(dlg, box->owner, CXUI_NEAR_Center);
    return 1;
}

bool _cxuiWin32ColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color)
{
    if (!customColorsSet) {
        for (int i = 0; i < 16; i++)
            customColors[i] = RGB(255, 255, 255);
        customColorsSet = true;
    }
    WCHAR* title       = strEmpty(desc->title) ? NULL : wide(desc->title);
    Win32ColorBox box  = { .owner = _cxuiWin32PeerHwnd(parent), .title = title, .outer = openColorBoxes };
    openColorBoxes     = &box;
    CHOOSECOLORW cc    = { sizeof(cc) };
    cc.hwndOwner       = box.owner;
    cc.rgbResult       = RGB(color->r, color->g, color->b);
    cc.lpCustColors    = customColors;
    cc.Flags           = CC_RGBINIT | CC_FULLOPEN | CC_ENABLEHOOK;
    cc.lCustData       = (LPARAM)&box;
    cc.lpfnHook        = colorHook;
    bool chosen        = ChooseColorW(&cc) != 0;
    openColorBoxes     = box.outer;
    xaFree(title);
    if (chosen)
        *color = (CxUiColor){ .r = GetRValue(cc.rgbResult), .g = GetGValue(cc.rgbResult), .b = GetBValue(cc.rgbResult), .a = 255 };
    return chosen;
}

// Sets the color the way a hook would, and types it into the red, green and blue fields for
// Wine, which ignores that; then presses OK. Cancelling presses Cancel.
bool _cxuiWin32TestAnswerColorDialog(const CxUiColor* color)
{
    Win32ColorBox* box = openColorBoxes;
    if (!box || !box->dlg)
        return false;
    if (color) {
        static UINT setRgb;
        if (!setRgb)
            setRgb = RegisterWindowMessageW(SETRGBSTRINGW);
        SendMessageW(box->dlg, setRgb, 0, RGB(color->r, color->g, color->b));
        SetDlgItemInt(box->dlg, COLOR_RED, color->r, FALSE);
        SetDlgItemInt(box->dlg, COLOR_GREEN, color->g, FALSE);
        SetDlgItemInt(box->dlg, COLOR_BLUE, color->b, FALSE);
    }
    PostMessageW(box->dlg, WM_COMMAND, MAKEWPARAM(color ? IDOK : IDCANCEL, BN_CLICKED), 0);
    return true;
}

#include "cxui_private.h"
#include <cx/fs.h>
#include <cxui/test/cxuitest.h>

// ---- message boxes built from cxui widgets ----

// For a custom box the platform can't show: a label and a row of buttons in a popup.
typedef struct BuiltBox {
    CxUiWindow* w;
    int32 depth;   // _cxui.messageBoxes while it is the innermost box
    struct BuiltBox* outer;
} BuiltBox;

static CxUiResult builtPressed(stvlist* cvars, CxUiActivateEvent* ev)
{
    cxuiWindowEndPopup(ev->base.window, stvlAt(cvars, 0, int32));
    return CXUI_Handled;
}

// Escape, with no cancel button to press, and the close box come here.
static CxUiResult builtClose(stvlist* cvars, CxUiCloseEvent* ev)
{
    int32 cancel = stvlAt(cvars, 0, int32);
    if (cancel > 0)
        cxuiWindowEndPopup(objDynCast(CxUiWindow, ev->base.target), cancel);
    return CXUI_Handled;
}

static int32 builtBox(const CxUiMessageBoxDesc* desc, int32 n, CxUiWindow* parent)
{
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = desc->title, .sizing = CXUI_SIZE_Fixed });
    CxUiBox* col  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 16, .margin = { 16, 16, 16, 16 } });
    CxUiLabel* text = cxuiLabelCreate(&(CxUiLabelDesc){ .text = desc->text, .wrap = true });
    cxuiWidgetSetNaturalSize(text, cxuiLen(360), cxuiLen(0));
    cxuiContainerAppend(col, text);
    objRelease(&text);

    CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 8 });
    CxUiFill* fill = cxuiFillCreate();
    cxuiContainerAppend(row, fill);
    objRelease(&fill);
    int32 def = desc->defaultButton > 0 && desc->defaultButton <= n ? desc->defaultButton : 1;
    for (int32 i = 0; i < n; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = desc->custom[i] });
        cxuiWidgetSetLayout(b, &(CxUiLayoutParams){ .normalizeGroup = 1, .minSize = { cxuiLen(80), cxuiLen(0) } });
        cxuiOnActivate(b, builtPressed, stvar(int32, i + 1));
        cxuiContainerAppend(row, b);
        if (i + 1 == def) {
            cxuiWindowSetDefaultEnter(w, b);
            cxuiWindowSetStartFocus(w, b);
        }
        objRelease(&b);
    }
    cxuiContainerAppend(col, row);
    objRelease(&row);
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiOnClose(w, builtClose, stvar(int32, desc->cancelButton));

    BuiltBox box     = { .w = w, .depth = ++_cxui.messageBoxes, .outer = _cxui.builtBoxes };
    _cxui.builtBoxes = &box;
    int32 result     = cxuiWindowPopup(w, parent);
    _cxui.builtBoxes = box.outer;
    _cxui.messageBoxes--;
    cxuiDestroy(w);
    objRelease(&w);
    return result;
}

CxUiWindow* cxuiTestBuiltMessageBox(void)
{
    BuiltBox* box = _cxui.builtBoxes;
    return box && box->depth == _cxui.messageBoxes ? box->w : NULL;
}

_Use_decl_annotations_
bool _cxuiTestAnswerBuiltBox(int32 button)
{
    BuiltBox* box = _cxui.builtBoxes;
    if (!box || box->depth != _cxui.messageBoxes || button < 1)
        return false;
    // Pressed as the user would, so a button that doesn't exist fails.
    CxUiWidget* row = cxuiContainerChild(cxuiContainerChild(box->w, 0), 1);
    CxUiWidget* b   = cxuiContainerChild(row, button);
    return b && _cxuiPress(b);
}

// ---- message boxes ----

static int32 customCount(const CxUiMessageBoxDesc* desc)
{
    int32 n = 0;
    while (n < 3 && !strEmpty(desc->custom[n]))
        n++;
    return n;
}

static int32 messageBoxOnUI(const CxUiMessageBoxDesc* desc)
{
    if (atomicLoad(bool, &_cxui.exitRequested, Acquire))
        return 0;

    CxUiWindow* parent = desc->parent;
    if (parent && !CxUiWindow_realize(parent))
        parent = NULL;

    // A custom box without titles is an OK box; the backend sees only what it can show.
    CxUiMessageBoxDesc d = *desc;
    int32 n              = d.buttons == CXUI_MB_Custom ? customCount(&d) : 0;
    if (d.buttons > CXUI_MB_Custom || (d.buttons == CXUI_MB_Custom && n == 0))
        d.buttons = CXUI_MB_Ok;
    if (d.cancelButton < 0 || d.cancelButton > n)
        d.cancelButton = 0;

    CxUiModalLock lock;
    _cxuiModalBegin(&lock, parent, NULL);
    _cxui.messageBoxes++;
    _cxuiModalLoopStarting();
    int32 result = _cxui.backend->messageBox(&d, parent ? parent->peer : NULL);
    _cxui.messageBoxes--;
    _cxuiModalEnd(&lock);
    if (result == CXUI_MB_Build)
        result = builtBox(&d, n, parent);

    return atomicLoad(bool, &_cxui.exitRequested, Acquire) ? 0 : result;
}

static bool messageBoxSync(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    int32* result = stvlAtPtr(cvars, 1);
    *result       = messageBoxOnUI(stvlAtPtr(cvars, 0));
    return true;
}

_Use_decl_annotations_
int32 cxuiMessageBox(const CxUiMessageBoxDesc* desc)
{
    if (cxuiIsUIThread())
        return messageBoxOnUI(desc);

    // The caller waits, so the descriptor stays valid for the UI thread.
    int32 result = 0;
    cxuiCallSync(closureCreate(messageBoxSync, stvar(ptr, (void*)desc), stvar(ptr, &result)));
    return result;
}

// ---- file dialogs ----

static void toPlatform(string* out, strref path)
{
    if (strEmpty(path))
        strClear(out);
    else
        pathToPlatform(out, path);
}

static uint32 fileDialogOnUI(const CxUiFileDialogDesc* desc, string* path)
{
    strClear(path);
    if (atomicLoad(bool, &_cxui.exitRequested, Acquire) || !_cxui.backend->fileDialog)
        return CXUI_FILE_Cancelled;

    CxUiWindow* parent = desc->parent;
    if (parent && !CxUiWindow_realize(parent))
        parent = NULL;

    // The backend sees platform paths, and only what it can show.
    CxUiFileDialogDesc d = *desc;
    string dir = 0, file = 0;
    toPlatform(&dir, desc->initialDir);
    d.initialDir  = dir;
    d.initialFile = desc->initialFile;
    if (d.mode > CXUI_FILE_Folder)
        d.mode = CXUI_FILE_Open;
    if (d.mode == CXUI_FILE_Folder || !d.filters || d.nfilters < 0)
        d.nfilters = 0;
    if (d.filterIndex < 0 || d.filterIndex >= d.nfilters)
        d.filterIndex = 0;

    CxUiModalLock lock;
    _cxuiModalBegin(&lock, parent, NULL);
    _cxui.fileDialogs++;
    _cxuiModalLoopStarting();
    bool chosen = _cxui.backend->fileDialog(&d, parent ? parent->peer : NULL, &file);
    _cxui.fileDialogs--;
    _cxuiModalEnd(&lock);
    strDestroy(&dir);

    if (!chosen) {
        strDestroy(&file);
        return CXUI_FILE_Cancelled;
    }
    pathFromPlatform(path, file);
    strDestroy(&file);

    // Where the platform didn't add the extension itself
    string name = 0;
    if (d.mode == CXUI_FILE_Save && !strEmpty(d.defaultExt) && pathFilename(&name, *path) && strFind(name, 0, _S".") < 0) {
        strAppend(path, _S".");
        strAppend(path, d.defaultExt);
    }
    strDestroy(&name);
    return fsExist(*path) ? CXUI_FILE_Existing : CXUI_FILE_New;
}

static bool fileDialogSync(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    uint32* result = stvlAtPtr(cvars, 2);
    *result        = fileDialogOnUI(stvlAtPtr(cvars, 0), stvlAtPtr(cvars, 1));
    return true;
}

_Use_decl_annotations_
uint32 cxuiFileDialog(const CxUiFileDialogDesc* desc, string* path)
{
    if (cxuiIsUIThread())
        return fileDialogOnUI(desc, path);

    // The caller waits, so the descriptor and path stay valid for the UI thread.
    uint32 result = CXUI_FILE_Cancelled;
    cxuiCallSync(closureCreate(fileDialogSync, stvar(ptr, (void*)desc), stvar(ptr, path), stvar(ptr, &result)));
    return result;
}

_Use_decl_annotations_
bool cxuiTestAnswerFileDialog(strref path)
{
    if (!_cxui.backend || !_cxui.backend->testAnswerFileDialog)
        return false;
    string native = 0;
    if (path)
        pathToPlatform(&native, path);
    bool ok = _cxui.backend->testAnswerFileDialog(path ? (strref)native : NULL);
    strDestroy(&native);
    return ok;
}

int32 cxuiTestFileDialogs(void)
{
    return _cxui.fileDialogs;
}

// ---- color dialogs ----

static bool colorDialogOnUI(const CxUiColorDialogDesc* desc, CxUiColor* color)
{
    if (atomicLoad(bool, &_cxui.exitRequested, Acquire) || !_cxui.backend->colorDialog)
        return false;

    CxUiWindow* parent = desc->parent;
    if (parent && !CxUiWindow_realize(parent))
        parent = NULL;

    CxUiColor c = *color;
    CxUiModalLock lock;
    _cxuiModalBegin(&lock, parent, NULL);
    _cxui.colorDialogs++;
    _cxuiModalLoopStarting();
    bool chosen = _cxui.backend->colorDialog(desc, parent ? parent->peer : NULL, &c);
    _cxui.colorDialogs--;
    _cxuiModalEnd(&lock);

    if (!chosen || atomicLoad(bool, &_cxui.exitRequested, Acquire))
        return false;
    *color = (CxUiColor){ .r = c.r, .g = c.g, .b = c.b, .a = color->a };
    return true;
}

static bool colorDialogSync(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool* result = stvlAtPtr(cvars, 2);
    *result      = colorDialogOnUI(stvlAtPtr(cvars, 0), stvlAtPtr(cvars, 1));
    return true;
}

_Use_decl_annotations_
bool cxuiColorDialog(const CxUiColorDialogDesc* desc, CxUiColor* color)
{
    if (cxuiIsUIThread())
        return colorDialogOnUI(desc, color);

    // The caller waits, so the descriptor and color stay valid for the UI thread.
    bool result = false;
    cxuiCallSync(closureCreate(colorDialogSync, stvar(ptr, (void*)desc), stvar(ptr, color), stvar(ptr, &result)));
    return result;
}

_Use_decl_annotations_
bool cxuiTestAnswerColorDialog(const CxUiColor* color)
{
    return _cxui.backend && _cxui.backend->testAnswerColorDialog && _cxui.backend->testAnswerColorDialog(color);
}

int32 cxuiTestColorDialogs(void)
{
    return _cxui.colorDialogs;
}

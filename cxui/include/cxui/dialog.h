#pragma once

/// @file dialog.h
/// @brief Native dialogs

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuiprogress.h>

CX_C_BEGIN

/// @defgroup cxui_dialog Dialogs
/// @ingroup cxui
/// @{
/// Native dialogs are single calls that return the user's answer. They can be called from any
/// thread: from another thread the call waits while the dialog runs on the UI thread. On the UI
/// thread, the UI queue, timers and other windows' painting keep running while a dialog is open,
/// so a dialog can open another, and posted work never waits for the user.
///
/// @code
///   int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent  = mainWin,
///                                                   .title   = _S"Unsaved changes",
///                                                   .text    = _S"Save before closing?",
///                                                   .kind    = CXUI_MB_Question,
///                                                   .buttons = CXUI_MB_YesNoCancel });
///   if (r == 1)
///       saveDocument();
///   else if (r != 2)
///       return CXUI_Handled;   // cancelled: keep the window open
/// @endcode

/// The icon a message box shows
enum CxUiMessageKinds {
    CXUI_MB_Info = 0,
    CXUI_MB_Warning,
    CXUI_MB_Error,
    /// A question. Linux shows no icon for any kind.
    CXUI_MB_Question,
};

/// The buttons a message box offers, numbered from 1 in the order listed
enum CxUiMessageButtons {
    /// OK (1)
    CXUI_MB_Ok = 0,
    /// OK (1), Cancel (2)
    CXUI_MB_OkCancel,
    /// Yes (1), No (2)
    CXUI_MB_YesNo,
    /// Yes (1), No (2), Cancel (3)
    CXUI_MB_YesNoCancel,
    /// One to three buttons with the titles in CxUiMessageBoxDesc.custom
    CXUI_MB_Custom,
};

/// What cxuiMessageBox() shows
typedef struct CxUiMessageBoxDesc {
    /// The window the box belongs to and is centered on, or NULL for the whole program
    CxUiWindow* parent;
    strref title;
    /// The message; may have several lines
    strref text;
    /// CxUiMessageKinds (default CXUI_MB_Info)
    uint32 kind;
    /// CxUiMessageButtons (default CXUI_MB_Ok)
    uint32 buttons;
    /// The button Enter presses, from 1 (0 for the first)
    int32 defaultButton;
    /// For CXUI_MB_Custom: the buttons' titles, in order, up to the first left empty. A `&` marks
    /// the mnemonic.
    strref custom[3];
    /// For CXUI_MB_Custom: the button Escape and the close box answer, from 1; 0 for none, which
    /// leaves only the buttons to close it with
    int32 cancelButton;
} CxUiMessageBoxDesc;

/// Shows a message box and waits for an answer
///
/// The parent, or with no parent every window, can't be used until the box is answered. Escape
/// and the close box answer Cancel where there is one, OK on an OK box, and cancelButton on a
/// custom one. Where the platform has no box of its own with custom buttons (Windows XP), cxui
/// builds one, without an icon. Any thread.
///
/// @param desc What to show
/// @return The button pressed, from 1 as numbered in CxUiMessageButtons; 0 if the box was closed
/// some other way, cxui shut down while it was open, or it couldn't be shown
int32 cxuiMessageBox(_In_ const CxUiMessageBoxDesc* desc);

/// What cxuiFileDialog() asks for
enum CxUiFileDialogMode {
    /// An existing file to open
    CXUI_FILE_Open = 0,
    /// A file to save to, which may exist already
    CXUI_FILE_Save,
    /// An existing folder
    CXUI_FILE_Folder,
};

/// Flags for CxUiFileDialogDesc
enum CxUiFileDialogFlags {
    /// Saving over an existing file doesn't ask first, where the platform allows it (Windows)
    CXUI_FILE_NoOverwritePrompt = 0x0001,
};

/// What cxuiFileDialog() returns
enum CxUiFileStatus {
    /// Nothing was chosen
    CXUI_FILE_Cancelled = 0,
    /// The path chosen exists
    CXUI_FILE_Existing,
    /// The path chosen doesn't exist yet: a new file to save
    CXUI_FILE_New,
};

/// A choice of the files a file dialog lists
typedef struct CxUiFileFilter {
    /// What the user sees, such as "Images"
    strref desc;
    /// Patterns separated by `;`, such as "*.png;*.jpg"; "*" lists every file
    strref patterns;
} CxUiFileFilter;

/// What cxuiFileDialog() shows; paths are cx paths
typedef struct CxUiFileDialogDesc {
    /// The window the dialog belongs to, or NULL for the whole program
    CxUiWindow* parent;
    strref title;
    /// CxUiFileDialogMode (default CXUI_FILE_Open)
    uint32 mode;
    /// The filters to choose from, and how many; none lists every file. Not for folders.
    const CxUiFileFilter* filters;
    int32 nfilters;
    /// The filter chosen at first, from 0
    int32 filterIndex;
    /// For CXUI_FILE_Save: added, without the dot, to a name typed without an extension
    strref defaultExt;
    /// The folder shown at first; empty for the platform's choice
    strref initialDir;
    /// For CXUI_FILE_Save: the name filled in at first
    strref initialFile;
    /// CxUiFileDialogFlags
    flags_t flags;
} CxUiFileDialogDesc;

/// Asks the user for a file or folder with the platform's own dialog
///
/// The parent, or with no parent every window, can't be used until the dialog closes. Any thread.
///
/// @param desc What to ask for
/// @param path Receives the cx path chosen; cleared when nothing was
/// @return CxUiFileStatus: CXUI_FILE_Cancelled if nothing was chosen, cxui shut down while the
/// dialog was open, or it couldn't be shown
///
/// Example:
/// @code
///   static const CxUiFileFilter filters[] = { { _S"Models", _S"*.gltf;*.glb" }, { _S"All files", _S"*" } };
///   string path = 0;
///   if (cxuiFileDialog(&(CxUiFileDialogDesc){ .parent = mainWin, .title = _S"Import model",
///                                              .filters = filters, .nfilters = 2 }, &path))
///       importModel(path);
///   strDestroy(&path);
/// @endcode
uint32 cxuiFileDialog(_In_ const CxUiFileDialogDesc* desc, _Inout_ string* path);

/// What cxuiColorDialog() shows
typedef struct CxUiColorDialogDesc {
    /// The window the dialog belongs to and is centered on, or NULL for the whole program
    CxUiWindow* parent;
    /// The title; empty for the platform's own
    strref title;
} CxUiColorDialogDesc;

/// Asks the user for a color with the platform's own dialog
///
/// The parent, or with no parent every window, can't be used until the dialog closes. The color's
/// alpha is neither shown nor changed. Any thread.
///
/// @param desc What to show
/// @param color The color the dialog starts with; receives the one chosen
/// @return true if a color was chosen; false if the dialog was cancelled, cxui shut down while it
/// was open, or it couldn't be shown
///
/// Example:
/// @code
///   CxUiColor c = item->color;
///   if (cxuiColorDialog(&(CxUiColorDialogDesc){ .parent = mainWin }, &c))
///       setItemColor(item, c);
/// @endcode
bool cxuiColorDialog(_In_ const CxUiColorDialogDesc* desc, _Inout_ CxUiColor* color);

/// CxUiProgressDialog* cxuiProgressDialogCreate(const CxUiProgressDialogDesc* desc)
///
/// Creates a progress dialog
///
/// It is a window of its own, so it doesn't wait for anything: show it with
/// cxuiWindowShowModal() to keep its parent from being used while the work runs, update it as
/// the work goes on, and close or destroy it when the work is over. Work on the UI thread calls
/// cxuiFlush() now and then, so the dialog repaints and its Cancel button works; work on another
/// thread updates it through cxuiPost().
///
/// @param ... (desc) Initial settings, or NULL for one that shows activity and can't be cancelled
/// @return The dialog, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiProgressDialog* d = cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){
///       .title = _S"Installing", .text = _S"Copying files", .total = nfiles, .cancel = true });
///   cxuiWindowShowModal(d, mainWin, NULL);
///   for (int32 i = 0; i < nfiles && !cxuiProgressDialogCancelled(d); i++) {
///       copyFile(i);
///       cxuiProgressDialogSetCount(d, i + 1);
///       cxuiFlush();
///   }
///   cxuiDestroy(d);
///   objRelease(&d);
/// @endcode
#define cxuiProgressDialogCreate(...) CxUiProgressDialog_createWith(__VA_ARGS__)

/// @}  // end of cxui_dialog group

CX_C_END

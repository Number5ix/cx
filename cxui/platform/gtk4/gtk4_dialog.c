#include "gtk4_ui.h"

// GtkAlertDialog is asynchronous; a nested loop waits for its answer. The state is on the heap: a
// box left behind when cxui shuts down is cancelled, and its callback still comes later.
typedef struct Gtk4Box {
    atomic(bool) done;
    int32 result;
    int32 answer;     // set by the test hook before it cancels the box
    bool abandoned;   // the caller has returned; the callback frees the box
    GCancellable* cancel;
    struct Gtk4Box* outer;
} Gtk4Box;

static Gtk4Box* openBoxes;   // innermost first; UI thread only

static void boxFree(Gtk4Box* box)
{
    g_object_unref(box->cancel);
    xaFree(box);
}

static void onChosen(GObject* source, GAsyncResult* res, gpointer ud)
{
    Gtk4Box* box = ud;
    GError* err  = NULL;
    int index    = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, &err);
    g_clear_error(&err);

    if (box->abandoned) {
        boxFree(box);
        return;
    }
    box->result = box->answer > 0 ? box->answer : (index >= 0 ? index + 1 : 0);
    atomicStore(bool, &box->done, true, Release);
}

// A title with cxui's `&` mnemonic in GTK's form, which marks it with `_`
static void underlined(string* out, strref title)
{
    strClear(out);
    uint32 n = strLen(title);
    for (uint32 i = 0; i < n; i++) {
        char c = (char)strGetChar(title, (int32)i);
        if (c == '&' && i + 1 < n) {
            c = (char)strGetChar(title, (int32)++i);
            strAppend(out, c == '&' ? _S"&" : _S"_");
            if (c == '&')
                continue;
        }
        if (c == '_')
            strAppend(out, _S"_");
        char one[2] = { c, 0 };
        strAppend(out, (strref)one);
    }
}

int32 _cxuiGtk4MessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent)
{
    // GTK's own translations of its button labels
    const char* ok     = g_dgettext("gtk40", "_OK");
    const char* cancel = g_dgettext("gtk40", "_Cancel");
    const char* yes    = g_dgettext("gtk40", "_Yes");
    const char* no     = g_dgettext("gtk40", "_No");

    const char* labels[4] = { 0 };
    string custom[3]      = { 0 };
    int cancelIndex       = -1;   // what Escape answers
    switch (desc->buttons) {
    case CXUI_MB_Custom:
        for (int32 i = 0; i < 3 && !strEmpty(desc->custom[i]); i++) {
            underlined(&custom[i], desc->custom[i]);
            labels[i] = strC(custom[i]);
        }
        cancelIndex = desc->cancelButton - 1;
        break;
    case CXUI_MB_OkCancel:
        labels[0]   = ok;
        labels[1]   = cancel;
        cancelIndex = 1;
        break;
    case CXUI_MB_YesNo:
        labels[0] = yes;
        labels[1] = no;
        break;
    case CXUI_MB_YesNoCancel:
        labels[0]   = yes;
        labels[1]   = no;
        labels[2]   = cancel;
        cancelIndex = 2;
        break;
    default:
        labels[0]   = ok;
        cancelIndex = 0;
        break;
    }

    // The message is shown large and the detail below it; GNOME's dialogs have no window title.
    GtkAlertDialog* dlg = gtk_alert_dialog_new("%s", strEmpty(desc->title) ? strC(desc->text) : strC(desc->title));
    if (!strEmpty(desc->title))
        gtk_alert_dialog_set_detail(dlg, strC(desc->text));
    gtk_alert_dialog_set_buttons(dlg, labels);
    gtk_alert_dialog_set_cancel_button(dlg, cancelIndex);
    gtk_alert_dialog_set_default_button(dlg, desc->defaultButton > 0 ? desc->defaultButton - 1 : 0);
    gtk_alert_dialog_set_modal(dlg, TRUE);
    for (int32 i = 0; i < 3; i++)
        strDestroy(&custom[i]);

    Gtk4Box* box = xaAllocStruct(Gtk4Box, XA_Zero);
    box->cancel  = g_cancellable_new();
    box->outer   = openBoxes;
    openBoxes    = box;
    gtk_alert_dialog_choose(dlg, _cxuiGtk4PeerWindow(parent), box->cancel, onChosen, box);
    g_object_unref(dlg);

    _cxui.backend->runNested(&box->done);

    openBoxes = box->outer;
    if (!atomicLoad(bool, &box->done, Acquire)) {
        // cxui is shutting down: close the dialog and let its callback free the box.
        box->abandoned = true;
        g_cancellable_cancel(box->cancel);
        return 0;
    }
    int32 result = box->result;
    boxFree(box);
    return result;
}

bool _cxuiGtk4TestAnswerMessageBox(int32 button)
{
    Gtk4Box* box = openBoxes;
    if (!box || button < 1 || box->answer > 0 || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->answer = button;
    g_cancellable_cancel(box->cancel);
    return true;
}

// ---- file dialogs ----

// GtkFileDialog is asynchronous too, and kept on the heap the same way.
typedef struct Gtk4FileBox {
    atomic(bool) done;
    uint32 mode;
    bool chosen;
    bool answered;    // the test hook set the answer before it cancelled the dialog
    bool abandoned;
    string path;
    GCancellable* cancel;
    struct Gtk4FileBox* outer;
} Gtk4FileBox;

static Gtk4FileBox* openFileBoxes;   // innermost first; UI thread only

static void fileBoxFree(Gtk4FileBox* box)
{
    g_object_unref(box->cancel);
    strDestroy(&box->path);
    xaFree(box);
}

static void onFileChosen(GObject* source, GAsyncResult* res, gpointer ud)
{
    Gtk4FileBox* box   = ud;
    GtkFileDialog* dlg = GTK_FILE_DIALOG(source);
    GError* err        = NULL;
    GFile* file        = box->mode == CXUI_FILE_Save     ? gtk_file_dialog_save_finish(dlg, res, &err)
                         : box->mode == CXUI_FILE_Folder ? gtk_file_dialog_select_folder_finish(dlg, res, &err)
                                                         : gtk_file_dialog_open_finish(dlg, res, &err);
    g_clear_error(&err);
    if (box->abandoned) {
        g_clear_object(&file);
        fileBoxFree(box);
        return;
    }
    if (file && !box->answered) {
        char* p = g_file_get_path(file);
        strDup(&box->path, (strref)p);
        box->chosen = p != NULL;
        g_free(p);
    }
    g_clear_object(&file);
    atomicStore(bool, &box->done, true, Release);
}

// One GtkFileFilter per cxui filter, its patterns split at `;`
static GListStore* filtersFor(const CxUiFileDialogDesc* desc)
{
    GListStore* store = g_list_store_new(GTK_TYPE_FILE_FILTER);
    string pattern    = 0;
    for (int32 i = 0; i < desc->nfilters; i++) {
        GtkFileFilter* f = gtk_file_filter_new();
        gtk_file_filter_set_name(f, strC(desc->filters[i].desc));
        strref pats = desc->filters[i].patterns;
        int32 len = (int32)strLen(pats), start = 0;
        while (start <= len) {
            int32 end = strFind(pats, start, _S";");
            if (end < 0)
                end = len;
            strSubStr(&pattern, pats, start, end);
            strTrim(&pattern, pattern, 0);
            if (!strEmpty(pattern))
                gtk_file_filter_add_pattern(f, strC(pattern));
            start = end + 1;
        }
        g_list_store_append(store, f);
        g_object_unref(f);
    }
    strDestroy(&pattern);
    return store;
}

// GTK always asks before saving over a file.
bool _cxuiGtk4FileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out)
{
    GtkFileDialog* dlg = gtk_file_dialog_new();
    if (!strEmpty(desc->title))
        gtk_file_dialog_set_title(dlg, strC(desc->title));
    gtk_file_dialog_set_modal(dlg, TRUE);
    if (desc->nfilters > 0) {
        GListStore* store = filtersFor(desc);
        gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store));
        GtkFileFilter* def = g_list_model_get_item(G_LIST_MODEL(store), (guint)desc->filterIndex);
        gtk_file_dialog_set_default_filter(dlg, def);
        g_object_unref(def);
        g_object_unref(store);
    }
    if (!strEmpty(desc->initialDir)) {
        GFile* dir = g_file_new_for_path(strC(desc->initialDir));
        gtk_file_dialog_set_initial_folder(dlg, dir);
        g_object_unref(dir);
    }
    if (desc->mode == CXUI_FILE_Save && !strEmpty(desc->initialFile))
        gtk_file_dialog_set_initial_name(dlg, strC(desc->initialFile));

    Gtk4FileBox* box = xaAllocStruct(Gtk4FileBox, XA_Zero);
    box->mode        = desc->mode;
    box->cancel      = g_cancellable_new();
    box->outer       = openFileBoxes;
    openFileBoxes    = box;
    GtkWindow* owner = _cxuiGtk4PeerWindow(parent);
    if (desc->mode == CXUI_FILE_Save)
        gtk_file_dialog_save(dlg, owner, box->cancel, onFileChosen, box);
    else if (desc->mode == CXUI_FILE_Folder)
        gtk_file_dialog_select_folder(dlg, owner, box->cancel, onFileChosen, box);
    else
        gtk_file_dialog_open(dlg, owner, box->cancel, onFileChosen, box);
    g_object_unref(dlg);

    _cxui.backend->runNested(&box->done);

    openFileBoxes = box->outer;
    if (!atomicLoad(bool, &box->done, Acquire)) {
        // cxui is shutting down: close the dialog and let its callback free the box.
        box->abandoned = true;
        g_cancellable_cancel(box->cancel);
        return false;
    }
    bool chosen = box->chosen;
    if (chosen)
        strDup(out, box->path);
    fileBoxFree(box);
    return chosen;
}

bool _cxuiGtk4TestAnswerFileDialog(strref path)
{
    Gtk4FileBox* box = openFileBoxes;
    if (!box || box->answered || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->answered = true;
    box->chosen   = path != NULL;
    strDup(&box->path, path);
    g_cancellable_cancel(box->cancel);
    return true;
}

// ---- color dialog ----

typedef struct Gtk4ColorBox {
    atomic(bool) done;
    bool chosen;
    bool answered;   // the test hook set the answer before it cancelled the dialog
    bool abandoned;
    GdkRGBA color;
    GCancellable* cancel;
    struct Gtk4ColorBox* outer;
} Gtk4ColorBox;

static Gtk4ColorBox* openColorBoxes;   // innermost first; UI thread only

static void colorBoxFree(Gtk4ColorBox* box)
{
    g_object_unref(box->cancel);
    xaFree(box);
}

static void onColorChosen(GObject* source, GAsyncResult* res, gpointer ud)
{
    Gtk4ColorBox* box = ud;
    GError* err       = NULL;
    GdkRGBA* c        = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(source), res, &err);
    g_clear_error(&err);
    if (box->abandoned) {
        if (c)
            gdk_rgba_free(c);
        colorBoxFree(box);
        return;
    }
    if (c && !box->answered) {
        box->color  = *c;
        box->chosen = true;
    }
    if (c)
        gdk_rgba_free(c);
    atomicStore(bool, &box->done, true, Release);
}

static uint8 colorByte(float v)
{
    return (uint8)(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool _cxuiGtk4ColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color)
{
    GtkColorDialog* dlg = gtk_color_dialog_new();
    if (!strEmpty(desc->title))
        gtk_color_dialog_set_title(dlg, strC(desc->title));
    gtk_color_dialog_set_modal(dlg, TRUE);
    gtk_color_dialog_set_with_alpha(dlg, FALSE);

    Gtk4ColorBox* box = xaAllocStruct(Gtk4ColorBox, XA_Zero);
    box->cancel       = g_cancellable_new();
    box->outer        = openColorBoxes;
    openColorBoxes    = box;
    GdkRGBA start     = { color->r / 255.0f, color->g / 255.0f, color->b / 255.0f, 1.0f };
    gtk_color_dialog_choose_rgba(dlg, _cxuiGtk4PeerWindow(parent), &start, box->cancel, onColorChosen, box);
    g_object_unref(dlg);

    _cxui.backend->runNested(&box->done);

    openColorBoxes = box->outer;
    if (!atomicLoad(bool, &box->done, Acquire)) {
        // cxui is shutting down: close the dialog and let its callback free the box.
        box->abandoned = true;
        g_cancellable_cancel(box->cancel);
        return false;
    }
    bool chosen = box->chosen;
    if (chosen)
        *color = (CxUiColor){ .r = colorByte(box->color.red), .g = colorByte(box->color.green),
                              .b = colorByte(box->color.blue), .a = 255 };
    colorBoxFree(box);
    return chosen;
}

bool _cxuiGtk4TestAnswerColorDialog(const CxUiColor* color)
{
    Gtk4ColorBox* box = openColorBoxes;
    if (!box || box->answered || atomicLoad(bool, &box->done, Acquire))
        return false;
    box->answered = true;
    box->chosen   = color != NULL;
    if (color)
        box->color = (GdkRGBA){ color->r / 255.0f, color->g / 255.0f, color->b / 255.0f, 1.0f };
    g_cancellable_cancel(box->cancel);
    return true;
}

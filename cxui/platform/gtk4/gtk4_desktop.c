#include "gtk4_ui.h"

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

// ---- monitors ----

void _cxuiGtk4Monitors(sa_CxUiMonitor* out)
{
    GdkDisplay* d = gdk_display_get_default();
    if (!d)
        return;
    GListModel* list    = gdk_display_get_monitors(d);
    GdkMonitor* primary = NULL;
    bool x11            = false;
#ifdef GDK_WINDOWING_X11
    // Deprecated with the rest of GTK's X11 backend from 4.18, and still there
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    if (GDK_IS_X11_DISPLAY(d)) {
        x11     = true;
        primary = gdk_x11_display_get_primary_monitor(d);
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif

    for (guint i = 0; i < g_list_model_get_n_items(list); i++) {
        GdkMonitor* mon = g_list_model_get_item(list, i);
        GdkRectangle g;
        gdk_monitor_get_geometry(mon, &g);
        CxUiMonitor m = {
            .bounds   = { g.x, g.y, g.width, g.height },
            .workArea = { g.x, g.y, g.width, g.height },
            .scale    = (float32)gdk_monitor_get_scale(mon),
        };
        if (x11) {
            m.flags |= CXUI_MON_PositionKnown | CXUI_MON_WorkAreaKnown;
#ifdef GDK_WINDOWING_X11
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            GdkRectangle wa;
            gdk_x11_monitor_get_workarea(mon, &wa);
            m.workArea = (CxUiRect){ wa.x, wa.y, wa.width, wa.height };
            G_GNUC_END_IGNORE_DEPRECATIONS
#endif
        }
        // Wayland has no primary monitor; the first one stands for it.
        if (mon == primary || (!primary && i == 0)) {
            m.flags |= CXUI_MON_Primary;
            saInsert(out, 0, opaque, m);
        } else {
            saPush(out, opaque, m);
        }
        g_object_unref(mon);
    }
}

// ---- clipboard ----

static GdkClipboard* clipboard(void)
{
    GdkDisplay* d = gdk_display_get_default();
    return d ? gdk_display_get_clipboard(d) : NULL;
}

_Use_decl_annotations_
bool _cxuiGtk4ClipboardSetText(strref text)
{
    GdkClipboard* cb = clipboard();
    if (!cb)
        return false;
    gdk_clipboard_set_text(cb, strC(text));
    return true;
}

// Another program's text arrives asynchronously, and a nested loop waits for it. The state is on
// the heap: a read left behind when cxui shuts down is cancelled, and its callback still comes.
typedef struct Gtk4ClipRead {
    atomic(bool) done;
    bool abandoned;   // the caller has returned; the callback frees the state
    bool ok;
    string text;
    GCancellable* cancel;
} Gtk4ClipRead;

#define CLIP_TIMEOUT_MS 2000

static void clipReadFree(Gtk4ClipRead* r)
{
    strDestroy(&r->text);
    g_object_unref(r->cancel);
    xaFree(r);
}

static void onClipRead(GObject* source, GAsyncResult* res, gpointer ud)
{
    Gtk4ClipRead* r = ud;
    GError* err     = NULL;
    char* text      = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), res, &err);
    g_clear_error(&err);
    if (r->abandoned) {
        g_free(text);
        clipReadFree(r);
        return;
    }
    r->ok = text != NULL;
    strDup(&r->text, (strref)text);
    g_free(text);
    atomicStore(bool, &r->done, true, Release);
}

static gboolean onClipTimeout(gpointer ud)
{
    g_cancellable_cancel(ud);
    return G_SOURCE_REMOVE;
}

_Use_decl_annotations_
bool _cxuiGtk4ClipboardGetText(string* out)
{
    GdkClipboard* cb = clipboard();
    if (!cb)
        return false;

    // Text this program put there is read at once.
    if (gdk_clipboard_is_local(cb)) {
        GdkContentProvider* cp = gdk_clipboard_get_content(cb);
        GValue v               = G_VALUE_INIT;
        g_value_init(&v, G_TYPE_STRING);
        bool ok = cp && gdk_content_provider_get_value(cp, &v, NULL);
        if (ok)
            strDup(out, (strref)g_value_get_string(&v));
        g_value_unset(&v);
        return ok;
    }

    GdkContentFormats* formats = gdk_clipboard_get_formats(cb);
    if (!formats || !gdk_content_formats_contain_gtype(formats, G_TYPE_STRING))
        return false;

    Gtk4ClipRead* r = xaAllocStruct(Gtk4ClipRead, XA_Zero);
    r->cancel       = g_cancellable_new();
    gdk_clipboard_read_text_async(cb, r->cancel, onClipRead, r);
    // A program that doesn't answer is given up on, as cancelling ends the read.
    GSource* timeout = g_timeout_source_new(CLIP_TIMEOUT_MS);
    g_source_set_callback(timeout, onClipTimeout, g_object_ref(r->cancel), g_object_unref);
    g_source_attach(timeout, NULL);

    _cxuiModalLoopStarting();
    _cxui.backend->runNested(&r->done);
    g_source_destroy(timeout);
    g_source_unref(timeout);

    if (!atomicLoad(bool, &r->done, Acquire)) {
        // cxui is shutting down: cancel the read and let its callback free the state.
        r->abandoned = true;
        g_cancellable_cancel(r->cancel);
        return false;
    }
    bool ok = r->ok;
    if (ok)
        strDup(out, r->text);
    clipReadFree(r);
    return ok;
}

typedef struct Gtk4ClipStore {
    bool done;
    bool abandoned;   // the wait gave up; the callback frees the state
} Gtk4ClipStore;

static void onStored(GObject* source, GAsyncResult* res, gpointer ud)
{
    Gtk4ClipStore* st = ud;
    gdk_clipboard_store_finish(GDK_CLIPBOARD(source), res, NULL);
    if (st->abandoned)
        xaFree(st);
    else
        st->done = true;
}

// A second connection to the X server stands in for another program. Kept until shutdown, as it
// must answer for the text it put there.
static GdkDisplay* foreignDisplay;

_Use_decl_annotations_
bool _cxuiGtk4TestClipboardForeign(strref text)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay* d = gdk_display_get_default();
    if (!d || !GDK_IS_X11_DISPLAY(d))
        return false;
    if (!foreignDisplay)
        foreignDisplay = gdk_display_open(gdk_display_get_name(d));
    if (!foreignDisplay)
        return false;
    gdk_clipboard_set_text(gdk_display_get_clipboard(foreignDisplay), strC(text));

    // Until this connection hears it no longer has the clipboard, and what the other one offers
    GdkClipboard* cb = clipboard();
    int64 deadline   = clockTimer() + timeS(2);
    while (clockTimer() < deadline) {
        GdkContentFormats* f = gdk_clipboard_get_formats(cb);
        if (!gdk_clipboard_is_local(cb) && f && gdk_content_formats_contain_gtype(f, G_TYPE_STRING))
            return true;
        if (!g_main_context_iteration(NULL, FALSE))
            osSleep(timeMS(5));
    }
    return false;
#else
    unused_noeval(text);
    return false;
#endif
}

// Hands text this program put on the clipboard to the desktop's clipboard manager, if there is
// one, so it stays after the program ends.
void _cxuiGtk4DesktopShutdown(void)
{
    if (foreignDisplay) {
        gdk_display_close(foreignDisplay);
        foreignDisplay = NULL;
    }
}

void _cxuiGtk4ClipboardStore(void)
{
    GdkClipboard* cb = clipboard();
    if (!cb || !gdk_clipboard_is_local(cb) || !gdk_clipboard_get_content(cb))
        return;
    Gtk4ClipStore* st = xaAllocStruct(Gtk4ClipStore, XA_Zero);
    gdk_clipboard_store_async(cb, G_PRIORITY_DEFAULT, NULL, onStored, st);
    int64 deadline = clockTimer() + timeMS(CLIP_TIMEOUT_MS);
    while (!st->done && clockTimer() < deadline) {
        if (!g_main_context_iteration(NULL, FALSE))
            osSleep(timeMS(5));
    }
    if (st->done)
        xaFree(st);
    else
        st->abandoned = true;
}

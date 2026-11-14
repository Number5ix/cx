#include "gtk4_ui.h"

// A window's size is its root widget's, the area the content gets. GTK sizes the GtkWindow, which
// includes the title bar it draws with client-side decorations, so a size asked for adds the
// difference last seen between the two, or before the first allocation, the title bar's height
// as measured.

typedef struct Gtk4Window {
    CxUiPeer base;
    GtkWindow* win;
    GdkSurface* surface;   // while the window is realized
    GtkWidget* root;
    // Holds the menu bar above the root
    GtkWidget* box;
    GtkWidget* menubar;
    CxUiPeer* menu;
    CxUiSize lastPx;
    // The GtkWindow's size less the root's, once known
    CxUiSize extra;
    bool extraKnown;
    float32 lastScale;
    uint32 lastState;
    // The window it belongs to, a weak pointer; a popup's owner stands in for it while it runs
    GtkWindow* owner;
    bool modal;
} Gtk4Window;

// ---- theme ----

// GTK's light or dark preference is process-wide. 4.20 replaced the boolean setting with an enum.
#define CXUI_GTK_SCHEME_DARK  2   // GTK_INTERFACE_COLOR_SCHEME_DARK
#define CXUI_GTK_SCHEME_LIGHT 3   // GTK_INTERFACE_COLOR_SCHEME_LIGHT

static bool schemeIsEnum(GtkSettings* s)
{
    return g_object_class_find_property(G_OBJECT_GET_CLASS(s), "gtk-interface-color-scheme") != NULL;
}

// What the settings said before cxui first changed them; read once, then again whenever they
// change other than by cxui. Once cxui has set the scheme for its own windows, GTK no longer
// passes on the system's changes to it, so windows that follow the system stop following.
static bool systemKnown;
static bool systemDark;
static bool writing;   // cxui is changing the setting itself

static bool gtk4ReadSystemDark(void)
{
    GtkSettings* s = gtk_settings_get_default();
    if (schemeIsEnum(s)) {
        int scheme = 0;
        g_object_get(s, "gtk-interface-color-scheme", &scheme, NULL);
        return scheme == CXUI_GTK_SCHEME_DARK;
    }

    gboolean preferDark = FALSE;
    char* theme         = NULL;
    g_object_get(s, "gtk-application-prefer-dark-theme", &preferDark, "gtk-theme-name", &theme, NULL);
    bool dark = preferDark || (theme && g_str_has_suffix(theme, "-dark"));
    g_free(theme);
    return dark;
}

CxUiThemeMode _cxuiGtk4SystemTheme(void)
{
    if (!systemKnown) {
        systemDark  = gtk4ReadSystemDark();
        systemKnown = true;
    }
    return systemDark ? CXUI_Theme_Dark : CXUI_Theme_Light;
}

// Only a window with a mode of its own changes the process-wide setting.
static void gtk4ApplyTheme(CxUiWindow* w, bool dark)
{
    if (!w || w->_theme == CXUI_Theme_System)
        return;
    _cxuiGtk4SystemTheme();   // read before it is overwritten

    GtkSettings* s = gtk_settings_get_default();
    writing        = true;
    if (schemeIsEnum(s))
        g_object_set(s, "gtk-interface-color-scheme", dark ? CXUI_GTK_SCHEME_DARK : CXUI_GTK_SCHEME_LIGHT, NULL);
    else
        g_object_set(s, "gtk-application-prefer-dark-theme", (gboolean)dark, NULL);
    writing = false;
}

static void onSettingChanged(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(obj);
    unused_noeval(pspec);
    unused_noeval(ud);
    if (writing)
        return;
    systemDark  = gtk4ReadSystemDark();
    systemKnown = true;
    _cxuiNativeEnter();
    _cxuiSystemThemeChanged();
    _cxuiNativeLeave();
}

void _cxuiGtk4ThemeWatch(bool on)
{
    GtkSettings* s = gtk_settings_get_default();
    if (!s)
        return;
    if (!on) {
        g_signal_handlers_disconnect_by_func(s, G_CALLBACK(onSettingChanged), NULL);
        return;
    }
    static const char* const props[] = { "notify::gtk-interface-color-scheme",
                                         "notify::gtk-application-prefer-dark-theme", "notify::gtk-theme-name" };
    for (int i = schemeIsEnum(s) ? 0 : 1; i < (schemeIsEnum(s) ? 1 : 3); i++)
        g_signal_connect(s, props[i], G_CALLBACK(onSettingChanged), NULL);
}

void _cxuiGtk4WindowSetDark(CxUiPeer* p, bool dark)
{
    gtk4ApplyTheme(p->w ? objDynCast(CxUiWindow, p->w) : NULL, dark);
}

bool _cxuiGtk4TestFrameDark(CxUiPeer* p, bool* dark)
{
    // The theme's text color: light text means a dark theme.
    GdkRGBA c;
    gtk_widget_get_color(GTK_WIDGET(((Gtk4Window*)p)->win), &c);
    *dark = (c.red + c.green + c.blue) / 3 > 0.5;
    return true;
}

// ---- events ----

static gboolean onCloseRequest(GtkWindow* win, gpointer ud)
{
    unused_noeval(win);
    _cxuiNativeEnter();
    _cxuiWindowCloseRequested(ud, CXUI_CLOSE_User);
    _cxuiNativeLeave();
    return TRUE;   // cxui decides what closing does
}

static void onActive(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(pspec);
    _cxuiNativeEnter();
    _cxuiWindowActivated(ud, gtk_window_is_active(GTK_WINDOW(obj)));
    _cxuiNativeLeave();
}

static void onSurfaceState(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(pspec);
    Gtk4Window* p         = ud;
    GdkToplevelState gst  = gdk_toplevel_get_state(GDK_TOPLEVEL(obj));
    uint32 state          = (gst & GDK_TOPLEVEL_STATE_MINIMIZED) ? CXUI_WINDOW_Minimized
                            : (gst & GDK_TOPLEVEL_STATE_MAXIMIZED) ? CXUI_WINDOW_Maximized
                                                                   : CXUI_WINDOW_Normal;
    if (state == p->lastState)
        return;
    p->lastState = state;
    _cxuiNativeEnter();
    _cxuiWindowStateChanged(&p->base, state);
    _cxuiNativeLeave();
}

static float32 surfaceScale(Gtk4Window* p)
{
    return p->surface ? (float32)gdk_surface_get_scale(p->surface) : 1.0f;
}

static CxUiSize rootPx(Gtk4Window* p, int width, int height, float32 scale)
{
    return (CxUiSize){ (int32)(width * scale + 0.5f), (int32)(height * scale + 0.5f) };
}

// The root is allocated after the window, so the window's size is current here. Layout still
// queued runs now, before the root places the canvases.
static void onRootAllocated(void* ud, int width, int height)
{
    Gtk4Window* p = ud;
    p->extra      = (CxUiSize){ gtk_widget_get_width(GTK_WIDGET(p->win)) - width,
                                gtk_widget_get_height(GTK_WIDGET(p->win)) - height };
    p->extraKnown = true;
    CxUiSize px   = rootPx(p, width, height, surfaceScale(p));
    _cxuiNativeEnter();
    if (px.w != p->lastPx.w || px.h != p->lastPx.h) {
        p->lastPx = px;
        _cxuiWindowResized(&p->base, px, (CxUiSize){ width, height });
    } else {
        _cxuiLayoutNow(&p->base);
    }
    _cxuiNativeLeave();
}

// Keys reach the window itself only when no canvas or text field has focus; a focused one has
// already bubbled them up.
static gboolean onWindowKey(GtkEventControllerLegacy* ctl, GdkEvent* ev, gpointer ud)
{
    Gtk4Window* p = ud;
    if (_cxuiGtk4FocusInPeer(p->win))
        return FALSE;
    unused_noeval(ctl);
    return _cxuiGtk4Key(&p->base, ev);
}

// The compositor keeps the window's logical size; only its pixels change.
static void onSurfaceScale(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(obj);
    unused_noeval(pspec);
    Gtk4Window* p = ud;
    float32 scale = surfaceScale(p);
    if (scale == p->lastScale)
        return;
    p->lastScale = scale;
    p->lastPx    = rootPx(p, gtk_widget_get_width(p->root), gtk_widget_get_height(p->root), scale);
    _cxuiNativeEnter();
    _cxuiWindowDpiChanged(&p->base, scale, p->lastPx);
    _cxuiNativeLeave();
}

static void onRealize(GtkWidget* widget, gpointer ud)
{
    Gtk4Window* p = ud;
    p->surface    = gtk_native_get_surface(GTK_NATIVE(widget));
    p->lastScale  = surfaceScale(p);
    g_signal_connect(p->surface, "notify::state", G_CALLBACK(onSurfaceState), p);
    g_signal_connect(p->surface, "notify::scale", G_CALLBACK(onSurfaceScale), p);
}

static void onUnrealize(GtkWidget* widget, gpointer ud)
{
    unused_noeval(widget);
    Gtk4Window* p = ud;
    if (p->surface)
        g_signal_handlers_disconnect_by_data(p->surface, p);
    p->surface = NULL;
}

// ---- operations ----

// The GtkWindow size that gives a client area of dips. Not the window's natural size: that is at
// least as wide as the title bar would like, title and all.
static CxUiSize windowExtra(Gtk4Window* p, int32 width)
{
    if (p->extraKnown)
        return p->extra;
    int minH = 0, natH = 0;
    gtk_widget_measure(GTK_WIDGET(p->win), GTK_ORIENTATION_VERTICAL, width, &minH, &natH, NULL, NULL);
    return (CxUiSize){ 0, max(natH - _cxuiGtk4RootNaturalSize(p->root).h, 0) };
}

static void setClientSize(Gtk4Window* p, CxUiSize dips)
{
    CxUiSize extra = windowExtra(p, dips.w);
    gtk_window_set_default_size(p->win, dips.w + extra.w, dips.h + extra.h);
}

CxUiPeer* _cxuiGtk4WindowCreate(CxUiWindow* w)
{
    Gtk4Window* p = xaAllocStruct(Gtk4Window, XA_Zero);
    p->win        = GTK_WINDOW(gtk_window_new());
    g_object_ref(p->win);   // GTK drops its own reference in gtk_window_destroy

    gtk_window_set_title(p->win, strC(w->_title));
    gtk_window_set_resizable(p->win, _cxuiWindowUserResizable(w));
    gtk_window_set_decorated(p->win, !(w->_flags & CXUI_WINDOW_Borderless));
    // GTK has no default size of its own.
    if (w->_size.w == 0)
        w->_size.w = 640;
    if (w->_size.h == 0)
        w->_size.h = 480;

    // Holds the content; its allocation reports size changes.
    p->root = _cxuiGtk4RootNew(onRootAllocated, p);
    _cxuiGtk4RootSetSizes(p->root, (CxUiSize){ 0 }, w->_size);
    gtk_widget_set_hexpand(p->root, TRUE);
    gtk_widget_set_vexpand(p->root, TRUE);
    p->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(p->box), p->root);
    gtk_window_set_child(p->win, p->box);

    g_signal_connect(p->win, "close-request", G_CALLBACK(onCloseRequest), p);
    GtkEventController* keys = gtk_event_controller_legacy_new();
    g_signal_connect(keys, "event", G_CALLBACK(onWindowKey), p);
    gtk_widget_add_controller(GTK_WIDGET(p->win), keys);
    g_signal_connect(p->win, "notify::is-active", G_CALLBACK(onActive), p);
    g_signal_connect(p->win, "realize", G_CALLBACK(onRealize), p);
    g_signal_connect(p->win, "unrealize", G_CALLBACK(onUnrealize), p);

    gtk4ApplyTheme(w, w->_dark);
    gtk_widget_realize(GTK_WIDGET(p->win));
    setClientSize(p, w->_size);

    w->_scale    = surfaceScale(p);
    w->_clientPx = rootPx(p, w->_size.w, w->_size.h, w->_scale);
    p->base.kind = CXUI_PEER_Window;
    return &p->base;
}

void _cxuiGtk4WindowShow(CxUiPeer* peer, uint32 mode)
{
    GtkWindow* win = ((Gtk4Window*)peer)->win;
    switch (mode) {
    case CXUI_SHOW_NoActivate:
        gtk_window_unminimize(win);
        gtk_widget_set_visible(GTK_WIDGET(win), TRUE);
        break;
    case CXUI_SHOW_Minimized:
        gtk_window_minimize(win);
        gtk_widget_set_visible(GTK_WIDGET(win), TRUE);
        break;
    case CXUI_SHOW_Maximized:
        gtk_window_maximize(win);
        gtk_window_present(win);
        break;
    default:
        gtk_window_unminimize(win);
        gtk_window_unmaximize(win);
        gtk_window_present(win);
        break;
    }
}

void _cxuiGtk4WindowHide(CxUiPeer* p)
{
    gtk_widget_set_visible(GTK_WIDGET(((Gtk4Window*)p)->win), FALSE);
}

void _cxuiGtk4WindowSetTitle(CxUiPeer* p, strref title)
{
    gtk_window_set_title(((Gtk4Window*)p)->win, strC(title));
}

void _cxuiGtk4WindowSetSize(CxUiPeer* peer, CxUiSize dips)
{
    Gtk4Window* p = (Gtk4Window*)peer;
    _cxuiGtk4RootSetSizes(p->root, _cxuiGtk4RootMinSize(p->root), dips);
    setClientSize(p, dips);
}

// GTK keeps the default size as the size the window goes back to when it is no longer
// maximized, and has no positions; the rectangle is the client area at 0,0.
void _cxuiGtk4WindowGetPlacement(CxUiPeer* peer, CxUiPlacement* out)
{
    Gtk4Window* p = (Gtk4Window*)peer;
    int w = 0, h = 0;
    gtk_window_get_default_size(p->win, &w, &h);
    CxUiSize extra = windowExtra(p, w);
    *out           = (CxUiPlacement){ .normal = { 0, 0, max(w - extra.w, 0), max(h - extra.h, 0) },
                                      .state  = p->lastState,
                                      .valid  = CXUI_PLACE_Size | CXUI_PLACE_State };
}

void _cxuiGtk4WindowSetPlacement(CxUiPeer* peer, const CxUiPlacement* pl, bool withState)
{
    Gtk4Window* p = (Gtk4Window*)peer;
    if (pl->valid & CXUI_PLACE_Size) {
        CxUiSize dips = { pl->normal.w, pl->normal.h };
        _cxuiGtk4WindowSetSize(peer, dips);
        CxUiWindow* w = p->base.w ? objDynCast(CxUiWindow, p->base.w) : NULL;
        // GTK reports the size once the window is next allocated.
        if (w && !gtk_window_is_maximized(p->win))
            w->_size = dips;
    }
    if (!withState)
        return;
    switch (pl->state) {
    case CXUI_WINDOW_Maximized:
        gtk_window_unminimize(p->win);
        gtk_window_maximize(p->win);
        break;
    case CXUI_WINDOW_Minimized:
        gtk_window_minimize(p->win);
        break;
    default:
        gtk_window_unminimize(p->win);
        gtk_window_unmaximize(p->win);
        break;
    }
}

void _cxuiGtk4WindowSetMinSize(CxUiPeer* peer, CxUiSize dips)
{
    Gtk4Window* p = (Gtk4Window*)peer;
    _cxuiGtk4RootSetSizes(p->root, dips, _cxuiGtk4RootNaturalSize(p->root));
}

void _cxuiGtk4WindowSetResizable(CxUiPeer* p, bool resizable)
{
    gtk_window_set_resizable(((Gtk4Window*)p)->win, resizable);
}

// The compositor moves the window from the press; it may refuse, as Wayland does without a
// recent enough press.
bool _cxuiGtk4WindowBeginDrag(CxUiPeer* p)
{
    GdkEvent* ev    = _cxuiGtk4PressEvent();
    GdkSurface* s   = gtk_native_get_surface(GTK_NATIVE(((Gtk4Window*)p)->win));
    double x = 0, y = 0;
    if (!ev || !s || !GDK_IS_TOPLEVEL(s) || !gdk_event_get_position(ev, &x, &y))
        return false;
    gdk_toplevel_begin_move(GDK_TOPLEVEL(s), gdk_event_get_device(ev), (int)gdk_button_event_get_button(ev), x, y,
                            gdk_event_get_time(ev));
    return true;
}

bool _cxuiGtk4TestBorderless(CxUiPeer* p, bool* borderless)
{
    *borderless = !gtk_window_get_decorated(((Gtk4Window*)p)->win);
    return true;
}

void _cxuiGtk4PeerSetVisible(CxUiPeer* p, bool visible)
{
    if (p->kind != CXUI_PEER_Window)
        gtk_widget_set_visible(_cxuiGtk4PeerWidget(p), visible);
}

void _cxuiGtk4PeerRaise(CxUiPeer* p)
{
    // The last child is drawn last, and takes the pointer first.
    GtkWidget* w = _cxuiGtk4PeerWidget(p);
    GtkWidget* parent = gtk_widget_get_parent(w);
    if (parent && gtk_widget_get_last_child(parent) != w)
        gtk_widget_insert_before(w, parent, NULL);
}

bool _cxuiGtk4TestWindowActive(CxUiPeer* p, bool* active)
{
    *active = gtk_window_is_active(((Gtk4Window*)p)->win);
    return true;
}

bool _cxuiGtk4TestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front)
{
    GtkWidget* root = _cxuiGtk4HostWidget(win);
    if (!root)
        return false;
    float32 s     = _cxuiGtk4WidgetScale(root);
    GtkWidget* at = gtk_widget_pick(root, px.x / s, px.y / s, GTK_PICK_DEFAULT);
    GtkWidget* w  = _cxuiGtk4PeerWidget(p);
    *front        = at && (at == w || gtk_widget_is_ancestor(at, w));
    return true;
}

bool _cxuiGtk4TestPeerShown(CxUiPeer* p, bool* shown)
{
    *shown = gtk_widget_get_visible(_cxuiGtk4PeerWidget(p));
    return true;
}

void _cxuiGtk4WindowSetBackground(CxUiPeer* p, CxUiColor c)
{
    _cxuiGtk4RootSetColors(((Gtk4Window*)p)->root, c, c, 0);
}

// GTK's modality blocks input to the program's other windows, so nothing is disabled by hand.
// Centering on the owner is left to the compositor.
void _cxuiGtk4WindowBeginModal(CxUiPeer* p, CxUiPeer* owner)
{
    GtkWindow* win          = ((Gtk4Window*)p)->win;
    ((Gtk4Window*)p)->modal = true;
    gtk_window_set_transient_for(win, owner ? ((Gtk4Window*)owner)->win : NULL);
    gtk_window_set_modal(win, TRUE);
}

void _cxuiGtk4WindowEndModal(CxUiPeer* p)
{
    Gtk4Window* w = (Gtk4Window*)p;
    w->modal      = false;
    gtk_window_set_modal(w->win, FALSE);
    gtk_window_set_transient_for(w->win, w->owner);
}

bool _cxuiGtk4TestOwnedBy(CxUiPeer* p, CxUiPeer* owner, bool* owned)
{
    *owned = gtk_window_get_transient_for(((Gtk4Window*)p)->win) == (owner ? ((Gtk4Window*)owner)->win : NULL);
    return true;
}

// A transient window stays above the one it is for; GTK lets go of it when that one is destroyed.
void _cxuiGtk4WindowSetOwner(CxUiPeer* p, CxUiPeer* owner)
{
    Gtk4Window* w = (Gtk4Window*)p;
    g_set_weak_pointer(&w->owner, owner ? ((Gtk4Window*)owner)->win : NULL);
    if (!w->modal)
        gtk_window_set_transient_for(w->win, w->owner);
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4WindowRoot(CxUiPeer* p)
{
    return p->kind == CXUI_PEER_Window ? ((Gtk4Window*)p)->root : NULL;
}

// The bar goes above the root; the client area keeps its size.
void _cxuiGtk4WindowSetMenu(CxUiPeer* peer, CxUiPeer* menu)
{
    Gtk4Window* p = (Gtk4Window*)peer;
    CxUiSize dips = { gtk_widget_get_width(p->root), gtk_widget_get_height(p->root) };
    if (dips.w <= 0 || dips.h <= 0)
        dips = peer->w ? ((CxUiWindow*)peer->w)->_size : dips;
    if (p->menubar)
        gtk_box_remove(GTK_BOX(p->box), p->menubar);
    p->menubar = NULL;
    p->menu    = menu;
    if (menu) {
        _cxuiGtk4MenuActionsTo(GTK_WIDGET(p->win));
        p->menubar = gtk_popover_menu_bar_new_from_model(_cxuiGtk4MenuModel(menu));
        gtk_box_prepend(GTK_BOX(p->box), p->menubar);
    }
    p->extraKnown = false;
    if (!gtk_window_is_maximized(p->win) && !gtk_window_is_fullscreen(p->win))
        setClientSize(p, dips);
}

CxUiPeer* _cxuiGtk4TestWindowMenu(CxUiPeer* peer)
{
    return ((Gtk4Window*)peer)->menu;
}

GtkWindow* _cxuiGtk4PeerWindow(CxUiPeer* p)
{
    return p ? ((Gtk4Window*)p)->win : NULL;
}

float32 _cxuiGtk4SystemScale(void)
{
    GdkDisplay* d   = gdk_display_get_default();
    GListModel* mon = d ? gdk_display_get_monitors(d) : NULL;
    GdkMonitor* m   = mon && g_list_model_get_n_items(mon) > 0 ? g_list_model_get_item(mon, 0) : NULL;
    if (!m)
        return 1.0f;
    float32 scale = (float32)gdk_monitor_get_scale(m);
    g_object_unref(m);
    return scale > 0 ? scale : 1.0f;
}

void _cxuiGtk4PeerDestroy(CxUiPeer* peer)
{
    if (peer->kind == CXUI_PEER_Canvas) {
        _cxuiGtk4CanvasDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Panel) {
        _cxuiGtk4PanelDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Text) {
        _cxuiGtk4TextDestroy(peer);
        return;
    }
    if (peer->kind == CXUI_PEER_Menu) {
        _cxuiGtk4MenuDestroy(peer);
        return;
    }
    if (_cxuiPeerIsControl(peer->kind)) {
        _cxuiGtk4ControlDestroy(peer);
        return;
    }
    Gtk4Window* p = (Gtk4Window*)peer;
    if (p->surface)
        g_signal_handlers_disconnect_by_data(p->surface, p);
    _cxuiGtk4RootDetach(p->root);
    g_signal_handlers_disconnect_by_data(p->win, p);
    _cxuiGtk4ControllersRelease(GTK_WIDGET(p->win), p);
    g_clear_weak_pointer(&p->owner);
    gtk_window_destroy(p->win);
    g_object_unref(p->win);
    xaFree(p);
}

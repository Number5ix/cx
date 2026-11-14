#include "gtk4_ui.h"

#include <math.h>

// Last: Xlib's macros would clash with names above.
#if defined(CXUI_GTK4_WAYLAND)
#include <gdk/wayland/gdkwayland.h>
#endif
#if defined(CXUI_GTK4_X11)
#include <gdk/x11/gdkx.h>
// GDK's X11 interface is deprecated as a whole; an X11 window is what a renderer needs there.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// A render view's native surface, on the toplevel the view is in, which exists once the view is
// mapped. Wayland: a subsurface of the toplevel's surface, desynchronized so the renderer's frames
// don't wait for GTK, above GTK's content, with an empty input region so the pointer goes to GTK.
// Its position is parent state, applied when GTK next commits the toplevel; the view paints a
// background, which makes GTK commit when the view moves. X11: a child window of the toplevel,
// with no background (the server never clears it) and no input.
struct Gtk4Surface {
    CxUiPeer* peer;
    GtkWidget* view;          // the canvas widget, which the peer holds
    GdkSurface* top;          // held while made
    GdkFrameClock* clock;     // held while its layout signal is connected
    gulong layout;
    gulong mapped, unmapped;
    bool made;
    bool shown;
    int32 x, y, w, h;         // where it was put, in the toplevel surface's logical pixels
    int32 scale;
#if defined(CXUI_GTK4_WAYLAND)
    struct wl_display* wlDpy;
    struct wl_surface* wlSurf;
    struct wl_subsurface* wlSub;
#endif
#if defined(CXUI_GTK4_X11)
    Display* xDpy;
    Window xWin;
#endif
};

#if defined(CXUI_GTK4_WAYLAND)
// Bound once for the process, through a registry on a queue of its own, so binding never
// dispatches GDK's events. GDK's display outlives every session.
static struct wl_subcompositor* subcomp;
static struct wl_display* subcompDpy;
static struct wl_event_queue* subcompQueue;

static void regGlobal(void* data, struct wl_registry* reg, uint32_t name, const char* iface, uint32_t ver)
{
    unused_noeval(data);
    unused_noeval(ver);
    if (strcmp(iface, wl_subcompositor_interface.name) == 0)
        subcomp = wl_registry_bind(reg, name, &wl_subcompositor_interface, 1);
}

static void regRemove(void* data, struct wl_registry* reg, uint32_t name)
{
    unused_noeval(data);
    unused_noeval(reg);
    unused_noeval(name);
}

static const struct wl_registry_listener regListener = { regGlobal, regRemove };

static struct wl_subcompositor* subcompFor(struct wl_display* dpy)
{
    if (subcompDpy == dpy)
        return subcomp;
    subcompDpy   = dpy;
    subcompQueue = wl_display_create_queue(dpy);
    struct wl_display* wrap = wl_proxy_create_wrapper(dpy);
    wl_proxy_set_queue((struct wl_proxy*)wrap, subcompQueue);
    struct wl_registry* reg = wl_display_get_registry(wrap);
    wl_registry_add_listener(reg, &regListener, NULL);
    wl_display_roundtrip_queue(dpy, subcompQueue);
    wl_registry_destroy(reg);
    wl_proxy_wrapper_destroy(wrap);
    if (!subcomp)
        logStr(Warn, _SL("cxui: the compositor has no wl_subcompositor; render views get no surface"));
    return subcomp;
}
#endif

static bool onWayland(Gtk4Surface* s)
{
#if defined(CXUI_GTK4_WAYLAND)
    return s->wlSurf != NULL;
#else
    unused_noeval(s);
    return false;
#endif
}

// Where the view is on its toplevel's surface, in logical pixels
static void viewAt(Gtk4Surface* s, int32* x, int32* y, int32* w, int32* h)
{
    GtkNative* native  = gtk_widget_get_native(s->view);
    graphene_point_t p = GRAPHENE_POINT_INIT(0, 0);
    double sx = 0, sy = 0;
    if (native) {
        if (!gtk_widget_compute_point(s->view, GTK_WIDGET(native), &GRAPHENE_POINT_INIT(0, 0), &p))
            p = GRAPHENE_POINT_INIT(0, 0);
        gtk_native_get_surface_transform(native, &sx, &sy);
    }
    *x = (int32)floor(p.x + sx + 0.5);
    *y = (int32)floor(p.y + sy + 0.5);
    *w = gtk_widget_get_width(s->view);
    *h = gtk_widget_get_height(s->view);
}

// After GTK's own layout and before it paints and commits the toplevel, so a new position goes
// out with the commit of the content it belongs to.
static void place(Gtk4Surface* s)
{
    int32 x, y, w, h;
    viewAt(s, &x, &y, &w, &h);
    int32 scale = gdk_surface_get_scale_factor(s->top);
    if (x == s->x && y == s->y && w == s->w && h == s->h && scale == s->scale)
        return;
    s->x = x, s->y = y, s->w = w, s->h = h;
#if defined(CXUI_GTK4_WAYLAND)
    if (s->wlSurf) {
        wl_subsurface_set_position(s->wlSub, x, y);
        if (scale != s->scale) {
            wl_surface_set_buffer_scale(s->wlSurf, scale);
            wl_surface_commit(s->wlSurf);
        }
    }
#endif
#if defined(CXUI_GTK4_X11)
    if (s->xWin) {
        XMoveResizeWindow(s->xDpy, s->xWin, x * scale, y * scale, (unsigned)max(w * scale, 1), (unsigned)max(h * scale, 1));
        XFlush(s->xDpy);
    }
#endif
    s->scale = scale;
}

static void onLayout(GdkFrameClock* clock, gpointer ud)
{
    unused_noeval(clock);
    Gtk4Surface* s = ud;
    if (s->made && gtk_widget_get_mapped(s->view))
        place(s);
}

static void make(Gtk4Surface* s)
{
    GtkNative* native = gtk_widget_get_native(s->view);
    GdkSurface* top   = native ? gtk_native_get_surface(native) : NULL;
    if (!top)
        return;
    GdkDisplay* d = gdk_surface_get_display(top);
    int32 x, y, w, h;
    viewAt(s, &x, &y, &w, &h);
    int32 scale = gdk_surface_get_scale_factor(top);
#if defined(CXUI_GTK4_WAYLAND)
    if (GDK_IS_WAYLAND_DISPLAY(d)) {
        struct wl_display* dpy     = gdk_wayland_display_get_wl_display(d);
        struct wl_compositor* comp = gdk_wayland_display_get_wl_compositor(d);
        struct wl_subcompositor* sc = subcompFor(dpy);
        struct wl_surface* parent  = gdk_wayland_surface_get_wl_surface(top);
        if (!sc || !comp || !parent)
            return;
        s->wlDpy  = dpy;
        s->wlSurf = wl_compositor_create_surface(comp);
        s->wlSub  = wl_subcompositor_get_subsurface(sc, s->wlSurf, parent);
        wl_subsurface_set_desync(s->wlSub);
        wl_subsurface_place_above(s->wlSub, parent);
        wl_subsurface_set_position(s->wlSub, x, y);
        struct wl_region* none = wl_compositor_create_region(comp);
        wl_surface_set_input_region(s->wlSurf, none);
        wl_region_destroy(none);
        wl_surface_set_buffer_scale(s->wlSurf, scale);
        wl_surface_commit(s->wlSurf);
        wl_display_flush(dpy);
    }
#endif
#if defined(CXUI_GTK4_X11)
    if (GDK_IS_X11_DISPLAY(d)) {
        s->xDpy                = gdk_x11_display_get_xdisplay(d);
        XSetWindowAttributes a = { .background_pixmap = None, .event_mask = 0 };
        s->xWin = XCreateWindow(s->xDpy, gdk_x11_surface_get_xid(top), x * scale, y * scale, (unsigned)max(w * scale, 1),
                                (unsigned)max(h * scale, 1), 0, CopyFromParent, InputOutput, CopyFromParent,
                                CWBackPixmap | CWEventMask, &a);
        XMapWindow(s->xDpy, s->xWin);
        XFlush(s->xDpy);
    }
#endif
    if (!onWayland(s)
#if defined(CXUI_GTK4_X11)
        && !s->xWin
#endif
    )
        return;
    s->top   = g_object_ref(top);
    s->made  = true;
    s->shown = true;
    s->x = x, s->y = y, s->w = w, s->h = h, s->scale = scale;
    s->clock  = g_object_ref(gdk_surface_get_frame_clock(top));
    s->layout = g_signal_connect_after(s->clock, "layout", G_CALLBACK(onLayout), s);
    // A toplevel commit applies the subsurface's position.
    gtk_widget_queue_draw(s->view);
    _cxuiNativeEnter();
    _cxuiRenderViewReady(s->peer);
    _cxuiNativeLeave();
}

// Hidden, a subsurface goes below the toplevel's content, which covers it: it can't be unmapped
// while the renderer may still present to it.
static void show(Gtk4Surface* s, bool shown)
{
    if (!s->made || s->shown == shown)
        return;
    s->shown = shown;
#if defined(CXUI_GTK4_WAYLAND)
    if (s->wlSurf) {
        struct wl_surface* parent = gdk_wayland_surface_get_wl_surface(s->top);
        if (shown)
            wl_subsurface_place_above(s->wlSub, parent);
        else
            wl_subsurface_place_below(s->wlSub, parent);
        gtk_widget_queue_draw(gtk_widget_get_parent(s->view) ? gtk_widget_get_parent(s->view) : s->view);
    }
#endif
#if defined(CXUI_GTK4_X11)
    if (s->xWin) {
        if (shown)
            XMapWindow(s->xDpy, s->xWin);
        else
            XUnmapWindow(s->xDpy, s->xWin);
        XFlush(s->xDpy);
    }
#endif
}

static void onMap(GtkWidget* w, gpointer ud)
{
    unused_noeval(w);
    Gtk4Surface* s = ud;
    if (!s->made)
        make(s);
    else
        show(s, true);
}

static void onUnmap(GtkWidget* w, gpointer ud)
{
    unused_noeval(w);
    show(ud, false);
}

_Use_decl_annotations_
Gtk4Surface* _cxuiGtk4SurfaceCreate(CxUiPeer* peer, GtkWidget* view)
{
    Gtk4Surface* s = xaAllocStruct(Gtk4Surface, XA_Zero);
    s->peer        = peer;
    s->view        = view;
    s->mapped      = g_signal_connect(view, "map", G_CALLBACK(onMap), s);
    s->unmapped    = g_signal_connect(view, "unmap", G_CALLBACK(onUnmap), s);
    return s;
}

_Use_decl_annotations_
void _cxuiGtk4SurfaceDestroy(Gtk4Surface* s)
{
    g_signal_handler_disconnect(s->view, s->mapped);
    g_signal_handler_disconnect(s->view, s->unmapped);
    if (s->layout)
        g_signal_handler_disconnect(s->clock, s->layout);
#if defined(CXUI_GTK4_WAYLAND)
    if (s->wlSurf) {
        wl_subsurface_destroy(s->wlSub);
        wl_surface_destroy(s->wlSurf);
        wl_display_flush(s->wlDpy);
    }
#endif
#if defined(CXUI_GTK4_X11)
    if (s->xWin) {
        // The toplevel, and the window with it, may have gone first.
        GdkDisplay* d = gdk_surface_get_display(s->top);
        gdk_x11_display_error_trap_push(d);
        XDestroyWindow(s->xDpy, s->xWin);
        XFlush(s->xDpy);
        gdk_x11_display_error_trap_pop_ignored(d);
    }
#endif
    if (s->clock)
        g_object_unref(s->clock);
    if (s->top)
        g_object_unref(s->top);
    xaFree(s);
}

_Use_decl_annotations_
bool _cxuiGtk4SurfaceGet(Gtk4Surface* s, CxUiNativeSurface* out)
{
    if (!s->made)
        return false;
#if defined(CXUI_GTK4_WAYLAND)
    if (s->wlSurf) {
        *out = (CxUiNativeSurface){ .kind = CXUI_SURFACE_Wayland, .native = s->wlSurf, .connection = s->wlDpy };
        return true;
    }
#endif
#if defined(CXUI_GTK4_X11)
    if (s->xWin) {
        *out = (CxUiNativeSurface){ .kind = CXUI_SURFACE_X11, .native = (void*)(uintptr_t)s->xWin, .connection = s->xDpy };
        return true;
    }
#endif
    return false;
}

#define MARK_PIXEL 0xff00ffu

// In the window's client area: less where cxui's root is on the toplevel. On X11 read back from
// the server; on Wayland where the subsurface was put, which the compositor can't be asked.
_Use_decl_annotations_
bool _cxuiGtk4SurfaceTestInfo(Gtk4Surface* s, CxUiTestSurfaceInfo* out)
{
    *out = (CxUiTestSurfaceInfo){ 0 };
    if (!s->made || !s->peer->w)
        return false;
    CxUiWindow* win    = _cxuiWindowOf(s->peer->w);
    GtkWidget* root    = win && CxUiWidget(win)->peer ? _cxuiGtk4WindowRoot(CxUiWidget(win)->peer) : NULL;
    GtkNative* native  = gtk_widget_get_native(s->view);
    graphene_point_t r = GRAPHENE_POINT_INIT(0, 0);
    double sx = 0, sy = 0;
    if (root && native && gtk_widget_compute_point(root, GTK_WIDGET(native), &GRAPHENE_POINT_INIT(0, 0), &r))
        gtk_native_get_surface_transform(native, &sx, &sy);
    int32 scale = max(s->scale, 1);
    int32 ox    = (int32)floor((r.x + sx) * scale + 0.5), oy = (int32)floor((r.y + sy) * scale + 0.5);
    out->bounds = (CxUiRect){ s->x * scale - ox, s->y * scale - oy, s->w * scale, s->h * scale };
    out->shown  = s->shown;
#if defined(CXUI_GTK4_X11)
    if (s->xWin) {
        int x, y;
        unsigned w, h, border, depth;
        Window rootWin;
        XGetGeometry(s->xDpy, s->xWin, &rootWin, &x, &y, &w, &h, &border, &depth);
        XWindowAttributes a;
        XGetWindowAttributes(s->xDpy, s->xWin, &a);
        out->bounds  = (CxUiRect){ x - ox, y - oy, (int32)w, (int32)h };
        out->shown   = a.map_state != IsUnmapped;
        out->canMark = true;
        XImage* img  = a.map_state == IsViewable ? XGetImage(s->xDpy, s->xWin, (int)w / 2, (int)h / 2, 1, 1, AllPlanes, ZPixmap)
                                                 : NULL;
        out->marked = img && (XGetPixel(img, 0, 0) & 0xffffffu) == MARK_PIXEL;
        if (img)
            XDestroyImage(img);
    }
#endif
    return true;
}

_Use_decl_annotations_
bool _cxuiGtk4SurfaceTestMark(Gtk4Surface* s)
{
#if defined(CXUI_GTK4_X11)
    if (s->made && s->xWin) {
        GC gc = XCreateGC(s->xDpy, s->xWin, 0, NULL);
        XSetForeground(s->xDpy, gc, MARK_PIXEL);
        XFillRectangle(s->xDpy, s->xWin, gc, 0, 0, (unsigned)max(s->w * s->scale, 1), (unsigned)max(s->h * s->scale, 1));
        XFreeGC(s->xDpy, gc);
        XSync(s->xDpy, False);
        return true;
    }
#endif
    unused_noeval(s);
    return false;
}

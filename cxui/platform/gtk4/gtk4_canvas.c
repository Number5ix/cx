#include "gtk4_ui.h"

#include <math.h>

// ---- the window's root widget ----

// Holds a window's content, or a panel's. A window's root reports its size, which is the
// window's client area. Either paints a background and border, and places each child at the
// bounds the core gave it, which every child widget carries (boundsKey).
typedef struct CxUiGtkRoot {
    GtkWidget parent_instance;
    Gtk4RootAllocated allocated;
    void* ud;
    CxUiSize min;       // DIPs
    CxUiSize natural;
    GdkRGBA bg, border;
    int32 borderPx;
    PangoLayout* title;      // a frame's, or NULL
    GdkRGBA titleColor;
    float titleIndent, titleGap;   // logical pixels
    bool hasBg;
    bool allocating;
} CxUiGtkRoot;

// The CxUiRect, in physical pixels within the parent, that a child widget is placed at
static GQuark boundsKey;
// The peer a canvas's or text field's widget belongs to
static GQuark peerKey;

typedef struct CxUiGtkRootClass {
    GtkWidgetClass parent_class;
} CxUiGtkRootClass;

G_DEFINE_TYPE(CxUiGtkRoot, cxui_gtk_root, GTK_TYPE_WIDGET)

// ---- canvases ----

typedef struct Gtk4Canvas Gtk4Canvas;

typedef struct CxUiGtkCanvas {
    GtkWidget parent_instance;
    Gtk4Canvas* peer;   // NULL once the peer is destroyed
} CxUiGtkCanvas;

typedef struct CxUiGtkCanvasClass {
    GtkWidgetClass parent_class;
} CxUiGtkCanvasClass;

G_DEFINE_TYPE(CxUiGtkCanvas, cxui_gtk_canvas, GTK_TYPE_WIDGET)

struct Gtk4Canvas {
    CxUiCanvasPeer cp;
    CxUiGtkCanvas* widget;   // held by a reference of our own
    Gtk4Surface* surface;    // a render view's, with no draw target
    CxUiRect bounds;         // physical pixels, within the root
    guint tick;              // a paint is waiting for the next frame
    CxUiPoint lastPointer;   // physical pixels, for events that come without a position
};

_Use_decl_annotations_
float32 _cxuiGtk4WidgetScale(GtkWidget* w)
{
    GtkNative* native = gtk_widget_get_native(w);
    GdkSurface* s     = native ? gtk_native_get_surface(native) : NULL;
    return s ? (float32)gdk_surface_get_scale(s) : 1.0f;
}

// ---- root class ----

static void rootMeasure(GtkWidget* w, GtkOrientation o, int forSize, int* min, int* nat, int* minBase, int* natBase)
{
    unused_noeval(forSize);
    CxUiGtkRoot* r = (CxUiGtkRoot*)w;
    bool across    = o == GTK_ORIENTATION_HORIZONTAL;
    *min           = across ? r->min.w : r->min.h;
    *nat           = max(*min, across ? r->natural.w : r->natural.h);
    *minBase = *natBase = -1;
}

static void rootAllocate(GtkWidget* w, int width, int height, int baseline)
{
    unused_noeval(baseline);
    CxUiGtkRoot* r = (CxUiGtkRoot*)w;
    // The core places the content from here, through canvasSetBounds.
    r->allocating = true;
    if (r->allocated)
        r->allocated(r->ud, width, height);
    r->allocating = false;

    float32 s = _cxuiGtk4WidgetScale(w);
    for (GtkWidget* child = gtk_widget_get_first_child(w); child; child = gtk_widget_get_next_sibling(child)) {
        if (!gtk_widget_get_visible(child))
            continue;
        CxUiRect* bp  = g_object_get_qdata(G_OBJECT(child), boundsKey);
        CxUiRect b    = bp ? *bp : (CxUiRect){ 0 };
        graphene_point_t at = GRAPHENE_POINT_INIT(b.x / s, b.y / s);
        gtk_widget_allocate(child, (int)ceilf(b.w / s), (int)ceilf(b.h / s), -1,
                            gsk_transform_translate(NULL, &at));
    }
}

static void rootDispose(GObject* obj)
{
    CxUiGtkRoot* r = (CxUiGtkRoot*)obj;
    g_clear_object(&r->title);
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(obj))))
        gtk_widget_unparent(child);
    G_OBJECT_CLASS(cxui_gtk_root_parent_class)->dispose(obj);
}

// A title moves the top edge down to the middle of its line, with a gap for the text.
static void rootSnapshot(GtkWidget* w, GtkSnapshot* snap)
{
    CxUiGtkRoot* r = (CxUiGtkRoot*)w;
    float wd = (float)gtk_widget_get_width(w), ht = (float)gtk_widget_get_height(w);
    if (r->hasBg) {
        gtk_snapshot_append_color(snap, &r->bg, &GRAPHENE_RECT_INIT(0, 0, wd, ht));
        float b = r->borderPx / _cxuiGtk4WidgetScale(w);
        int tw = 0, th = 0;
        if (r->title)
            pango_layout_get_pixel_size(r->title, &tw, &th);
        float top = r->title ? fmaxf((th - b) / 2, 0) : 0;
        float gap = r->title ? r->titleIndent + 2 * r->titleGap + tw : 0;
        if (b > 0) {
            const graphene_rect_t edges[5] = {
                GRAPHENE_RECT_INIT(0, top, r->title ? r->titleIndent : wd, b),
                GRAPHENE_RECT_INIT(gap, top, r->title ? wd - gap : 0, b),
                GRAPHENE_RECT_INIT(0, ht - b, wd, b),
                GRAPHENE_RECT_INIT(0, top + b, b, ht - top - 2 * b),
                GRAPHENE_RECT_INIT(wd - b, top + b, b, ht - top - 2 * b),
            };
            for (int i = 0; i < 5; i++) {
                if (edges[i].size.width > 0 && edges[i].size.height > 0)
                    gtk_snapshot_append_color(snap, &r->border, &edges[i]);
            }
        }
        if (r->title) {
            gtk_snapshot_save(snap);
            gtk_snapshot_translate(snap, &GRAPHENE_POINT_INIT(r->titleIndent + r->titleGap, 0));
            gtk_snapshot_append_layout(snap, r->title, &r->titleColor);
            gtk_snapshot_restore(snap);
        }
    }
    GTK_WIDGET_CLASS(cxui_gtk_root_parent_class)->snapshot(w, snap);
}

static void cxui_gtk_root_class_init(CxUiGtkRootClass* cls)
{
    GTK_WIDGET_CLASS(cls)->measure       = rootMeasure;
    GTK_WIDGET_CLASS(cls)->size_allocate = rootAllocate;
    GTK_WIDGET_CLASS(cls)->snapshot      = rootSnapshot;
    G_OBJECT_CLASS(cls)->dispose         = rootDispose;
    boundsKey                            = g_quark_from_static_string("cxui-bounds");
    peerKey                              = g_quark_from_static_string("cxui-peer");
}

static void cxui_gtk_root_init(CxUiGtkRoot* r)
{
    unused_noeval(r);
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4RootNew(Gtk4RootAllocated allocated, void* ud)
{
    CxUiGtkRoot* r = g_object_new(cxui_gtk_root_get_type(), NULL);
    r->allocated   = allocated;
    r->ud          = ud;
    return GTK_WIDGET(r);
}

_Use_decl_annotations_
void _cxuiGtk4RootDetach(GtkWidget* root)
{
    ((CxUiGtkRoot*)root)->allocated = NULL;
}

_Use_decl_annotations_
void _cxuiGtk4RootSetSizes(GtkWidget* root, CxUiSize min, CxUiSize natural)
{
    CxUiGtkRoot* r = (CxUiGtkRoot*)root;
    if (r->min.w == min.w && r->min.h == min.h && r->natural.w == natural.w && r->natural.h == natural.h)
        return;
    r->min     = min;
    r->natural = natural;
    gtk_widget_queue_resize(root);
}

_Use_decl_annotations_
CxUiSize _cxuiGtk4RootMinSize(GtkWidget* root)
{
    return ((CxUiGtkRoot*)root)->min;
}

_Use_decl_annotations_
CxUiSize _cxuiGtk4RootNaturalSize(GtkWidget* root)
{
    return ((CxUiGtkRoot*)root)->natural;
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4CanvasWidget(CxUiPeer* p)
{
    return GTK_WIDGET(((Gtk4Canvas*)p)->widget);
}

static GdkRGBA rgba(CxUiColor c)
{
    return (GdkRGBA){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
}

_Use_decl_annotations_
void _cxuiGtk4RootSetColors(GtkWidget* root, CxUiColor bg, CxUiColor border, int32 borderPx)
{
    CxUiGtkRoot* r = (CxUiGtkRoot*)root;
    r->bg          = rgba(bg);
    r->border      = rgba(border);
    r->borderPx    = borderPx;
    r->hasBg       = true;
    gtk_widget_queue_draw(root);
}

// The font's size is for the window's scale; the layout draws in logical pixels.
void _cxuiGtk4RootSetTitle(GtkWidget* root, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap)
{
    CxUiGtkRoot* r = (CxUiGtkRoot*)root;
    g_clear_object(&r->title);
    if (!strEmpty(title) && f) {
        float32 s               = cxuiFontScale(f);
        PangoFontDescription* d = _cxuiGtk4FontDescription(f);
        pango_font_description_set_absolute_size(d, pango_font_description_get_size(d) / s);
        r->title = gtk_widget_create_pango_layout(root, strC(title));
        pango_layout_set_font_description(r->title, d);
        pango_font_description_free(d);
        r->titleIndent = indent / s;
        r->titleGap    = gap / s;
    }
    r->titleColor = rgba(color);
    gtk_widget_queue_draw(root);
}

// ---- panels ----

// A scroll box's panel is a GtkScrolledWindow around its root, which is as large as the content.
typedef struct Gtk4Panel {
    CxUiPeer base;
    GtkWidget* widget;     // a root of our own, held by a reference
    GtkWidget* scroller;   // a scroll box's GtkScrolledWindow, held by a reference
    CxUiRect bounds;       // physical pixels, within the parent
} Gtk4Panel;

// The panel's own widget in its host
static GtkWidget* panelOuter(Gtk4Panel* p)
{
    return p->scroller ? p->scroller : p->widget;
}

// The widget a peer's children are placed in
_Use_decl_annotations_
GtkWidget* _cxuiGtk4HostWidget(CxUiPeer* host)
{
    if (!host)
        return NULL;
    if (host->kind == CXUI_PEER_Panel)
        return ((Gtk4Panel*)host)->widget;
    if (host->kind == CXUI_PEER_Tabs)
        return _cxuiGtk4TabsPage(host);
    return _cxuiGtk4WindowRoot(host);
}

_Use_decl_annotations_
void _cxuiGtk4ChildAttach(GtkWidget* w, CxUiPeer* p, CxUiRect* bounds, GtkWidget* parent)
{
    g_object_set_qdata(G_OBJECT(w), boundsKey, bounds);
    g_object_set_qdata(G_OBJECT(w), peerKey, p);
    gtk_widget_set_parent(w, parent);
}

_Use_decl_annotations_
void _cxuiGtk4ChildDetach(GtkWidget* w)
{
    g_object_set_qdata(G_OBJECT(w), boundsKey, NULL);
    g_object_set_qdata(G_OBJECT(w), peerKey, NULL);
    gtk_widget_unparent(w);
}

_Use_decl_annotations_
bool _cxuiGtk4FocusInPeer(GtkWindow* win)
{
    for (GtkWidget* w = gtk_window_get_focus(win); w; w = gtk_widget_get_parent(w)) {
        if (g_object_get_qdata(G_OBJECT(w), peerKey))
            return true;
    }
    return false;
}

// Moves a child within its parent, which lays it out again unless it is already doing so.
_Use_decl_annotations_
void _cxuiGtk4ChildPlace(GtkWidget* w)
{
    GtkWidget* parent = gtk_widget_get_parent(w);
    if (parent && G_TYPE_CHECK_INSTANCE_TYPE(parent, cxui_gtk_root_get_type()) && !((CxUiGtkRoot*)parent)->allocating)
        gtk_widget_queue_allocate(parent);
}

CxUiPeer* _cxuiGtk4PanelCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    GtkWidget* parent = _cxuiGtk4HostWidget(host);
    if (!parent)
        return NULL;
    Gtk4Panel* p      = xaAllocStruct(Gtk4Panel, XA_Zero);
    p->base.kind      = CXUI_PEER_Panel;
    p->widget         = g_object_ref_sink(_cxuiGtk4RootNew(NULL, NULL));
    CxUiScrollBox* sb = objDynCast(CxUiScrollBox, w);
    if (sb) {
        p->scroller = g_object_ref_sink(gtk_scrolled_window_new());
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroller),
                                       (sb->_axes & CXUI_EXPAND_H) ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
                                       (sb->_axes & CXUI_EXPAND_V) ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(p->scroller), p->widget);
        // The core scrolls to the focus itself; GTK's own goes to a new widget before it is placed.
        GtkWidget* vp = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(p->scroller));
        if (GTK_IS_VIEWPORT(vp))
            gtk_viewport_set_scroll_to_focus(GTK_VIEWPORT(vp), FALSE);
    }
    _cxuiGtk4ChildAttach(panelOuter(p), NULL, &p->bounds, parent);
    return &p->base;
}

void _cxuiGtk4PanelSetBounds(CxUiPeer* peer, CxUiRect px)
{
    Gtk4Panel* p = (Gtk4Panel*)peer;
    p->bounds    = px;
    _cxuiGtk4ChildPlace(panelOuter(p));
}

// The root asks for the content's size, in logical pixels; the scrolled window's viewport gives
// it at least that.
void _cxuiGtk4PanelSetContent(CxUiPeer* peer, CxUiSize px)
{
    Gtk4Panel* p   = (Gtk4Panel*)peer;
    CxUiGtkRoot* r = (CxUiGtkRoot*)p->widget;
    float32 s      = _cxuiGtk4WidgetScale(panelOuter(p));
    CxUiSize n     = { (int32)floorf(px.w / s), (int32)floorf(px.h / s) };
    if (n.w == r->natural.w && n.h == r->natural.h)
        return;
    r->min = r->natural = n;
    gtk_widget_queue_resize(p->widget);
}

void _cxuiGtk4PanelScrollTo(CxUiPeer* peer, CxUiRect px)
{
    Gtk4Panel* p = (Gtk4Panel*)peer;
    if (!p->scroller)
        return;
    float32 s          = _cxuiGtk4WidgetScale(p->scroller);
    GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(p->scroller);
    gtk_adjustment_clamp_page(gtk_scrolled_window_get_hadjustment(sw), px.x / s, (px.x + px.w) / s);
    gtk_adjustment_clamp_page(gtk_scrolled_window_get_vadjustment(sw), px.y / s, (px.y + px.h) / s);
}

bool _cxuiGtk4TestScrollPos(CxUiPeer* peer, CxUiPoint* out)
{
    Gtk4Panel* p = (Gtk4Panel*)peer;
    if (peer->kind != CXUI_PEER_Panel || !p->scroller)
        return false;
    float32 s             = _cxuiGtk4WidgetScale(p->scroller);
    GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(p->scroller);
    *out = (CxUiPoint){ (int32)floor(gtk_adjustment_get_value(gtk_scrolled_window_get_hadjustment(sw)) * s + 0.5),
                        (int32)floor(gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(sw)) * s + 0.5) };
    return true;
}

// The wheel moves the view by its own step for each notch, as GTK scrolls it.
bool _cxuiGtk4TestScrollWheel(CxUiPeer* peer, int32 notches)
{
    Gtk4Panel* p = (Gtk4Panel*)peer;
    if (peer->kind != CXUI_PEER_Panel || !p->scroller)
        return false;
    GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p->scroller));
    double step      = pow(gtk_adjustment_get_page_size(v), 2.0 / 3.0);
    gtk_adjustment_set_value(v, gtk_adjustment_get_value(v) + notches * step);
    return true;
}

void _cxuiGtk4PanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx)
{
    _cxuiGtk4RootSetColors(((Gtk4Panel*)p)->widget, bg, border, borderPx);
}

void _cxuiGtk4PanelSetTitle(CxUiPeer* p, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap)
{
    _cxuiGtk4RootSetTitle(((Gtk4Panel*)p)->widget, title, f, color, indent, gap);
}

void _cxuiGtk4PanelDestroy(CxUiPeer* peer)
{
    Gtk4Panel* p = (Gtk4Panel*)peer;
    _cxuiGtk4ChildDetach(panelOuter(p));
    if (p->scroller)
        g_object_unref(p->scroller);
    g_object_unref(p->widget);
    xaFree(p);
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4PeerWidget(CxUiPeer* p)
{
    switch (p->kind) {
    case CXUI_PEER_Canvas:
        return GTK_WIDGET(((Gtk4Canvas*)p)->widget);
    case CXUI_PEER_Panel:
        return panelOuter((Gtk4Panel*)p);
    case CXUI_PEER_Text:
        return _cxuiGtk4TextWidget(p);
    case CXUI_PEER_Window:
    case CXUI_PEER_Other:
        return GTK_WIDGET(_cxuiGtk4PeerWindow(p));
    default:
        return _cxuiGtk4ControlWidget(p);
    }
}

// ---- canvas class ----

static void canvasMeasure(GtkWidget* w, GtkOrientation o, int forSize, int* min, int* nat, int* minBase,
                          int* natBase)
{
    unused_noeval(w);
    unused_noeval(o);
    unused_noeval(forSize);
    *min = *nat = 0;
    *minBase = *natBase = -1;
}

// Appends what was drawn; painting itself happens before the frame, in canvasTick.
static void canvasSnapshot(GtkWidget* w, GtkSnapshot* snap)
{
    Gtk4Canvas* c = ((CxUiGtkCanvas*)w)->peer;
    if (!c)
        return;
    // Under a render view's surface. Something must move with the view for GTK to commit the
    // toplevel, which is what applies the surface's new place.
    if (!c->cp.ops) {
        static const GdkRGBA black = { 0, 0, 0, 1 };
        gtk_snapshot_append_color(snap, &black, &GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(w), gtk_widget_get_height(w)));
        return;
    }
    float32 s = _cxuiGtk4WidgetScale(w);
    gtk_snapshot_save(snap);
    gtk_snapshot_scale(snap, 1.0f / s, 1.0f / s);
    _cxuiGtk4TargetAppend(c->cp.target, snap);
    gtk_snapshot_restore(snap);
}

static void cxui_gtk_canvas_class_init(CxUiGtkCanvasClass* cls)
{
    GTK_WIDGET_CLASS(cls)->measure  = canvasMeasure;
    GTK_WIDGET_CLASS(cls)->snapshot = canvasSnapshot;
}

static void cxui_gtk_canvas_init(CxUiGtkCanvas* c)
{
    unused_noeval(c);
}

// Painting runs from the frame clock rather than from snapshot, so a paint handler can do
// anything, including destroying widgets or marking more to paint; whatever it marks waits for
// the next frame.
static gboolean canvasTick(GtkWidget* w, GdkFrameClock* clock, gpointer ud)
{
    unused_noeval(clock);
    Gtk4Canvas* c = ud;
    c->tick       = 0;
    g_object_ref(w);
    _cxuiNativeEnter();
    _cxuiLayoutNow(&c->cp.base);
    if (_cxuiCanvasPaint(&c->cp, NULL))
        gtk_widget_queue_draw(w);
    _cxuiNativeLeave();
    g_object_unref(w);
    return G_SOURCE_REMOVE;
}

// Input and focus. Each core call is inside _cxuiNativeEnter/Leave; c isn't touched after.
static gboolean canvasEvent(GtkEventControllerLegacy* ctl, GdkEvent* ev, gpointer ud)
{
    Gtk4Canvas* c = ud;
    GtkWidget* w  = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctl));
    GdkEventType type = gdk_event_get_event_type(ev);
    if (type == GDK_KEY_PRESS || type == GDK_KEY_RELEASE)
        return _cxuiGtk4Key(&c->cp.base, ev);
    return _cxuiGtk4Pointer(&c->cp.base, w, ev, &c->lastPointer);
}

// GTK hands crossings to controllers apart from other events, so these come from a motion
// controller rather than the legacy one.
static void canvasEnter(GtkEventControllerMotion* ctl, double x, double y, gpointer ud)
{
    GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctl));
    float32 s    = _cxuiGtk4WidgetScale(w);
    Gtk4Canvas* c  = ud;
    c->lastPointer = (CxUiPoint){ (int32)floor(x * s), (int32)floor(y * s) };
    _cxuiNativeEnter();
    _cxuiCanvasPointer(&c->cp.base, CXUI_PTR_Move, 0, 0, 0, c->lastPointer);
    _cxuiNativeLeave();
}

static void canvasLeave(GtkEventControllerMotion* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiCanvasPointer(&((Gtk4Canvas*)ud)->cp.base, CXUI_PTR_Left, 0, 0, 0, (CxUiPoint){ 0, 0 });
    _cxuiNativeLeave();
}

_Use_decl_annotations_
void _cxuiGtk4ControllersRelease(GtkWidget* w, void* data)
{
    GListModel* ctls = gtk_widget_observe_controllers(w);
    for (guint i = 0; i < g_list_model_get_n_items(ctls); i++) {
        GObject* ctl = g_list_model_get_item(ctls, i);
        g_signal_handlers_disconnect_by_data(ctl, data);
        g_object_unref(ctl);
    }
    g_object_unref(ctls);
}

static void canvasFocusIn(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Canvas*)ud)->cp.base, true);
    _cxuiNativeLeave();
}

static void canvasFocusOut(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Canvas*)ud)->cp.base, false);
    _cxuiNativeLeave();
}

CxUiPeer* _cxuiGtk4CanvasCreate(CxUiCanvas* cv, CxUiPeer* parent)
{
    GtkWidget* root = _cxuiGtk4HostWidget(parent);
    if (!root)
        return NULL;

    Gtk4Canvas* c      = xaAllocStruct(Gtk4Canvas, XA_Zero);
    c->cp.base.kind    = CXUI_PEER_Canvas;
    bool render        = objDynCast(CxUiRenderView, cv) != NULL;
    if (!render) {
        c->cp.ops    = &_cxuiGtk4GskOps;
        c->cp.target = c->cp.ops->targetCreate(&c->cp);
    }
    c->widget          = g_object_ref_sink(g_object_new(cxui_gtk_canvas_get_type(), NULL));
    c->widget->peer    = c;
    GtkWidget* w       = GTK_WIDGET(c->widget);
    if (render)
        c->surface = _cxuiGtk4SurfaceCreate(&c->cp.base, w);
    gtk_widget_set_focusable(w, cxuiWidgetCanFocus(cv));
    GtkEventController* legacy = gtk_event_controller_legacy_new();
    g_signal_connect(legacy, "event", G_CALLBACK(canvasEvent), c);
    gtk_widget_add_controller(w, legacy);
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(canvasEnter), c);
    g_signal_connect(motion, "leave", G_CALLBACK(canvasLeave), c);
    gtk_widget_add_controller(w, motion);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(canvasFocusIn), c);
    g_signal_connect(focus, "leave", G_CALLBACK(canvasFocusOut), c);
    gtk_widget_add_controller(w, focus);
    _cxuiGtk4ChildAttach(w, &c->cp.base, &c->bounds, root);
    return &c->cp.base;
}

void _cxuiGtk4CanvasSetBounds(CxUiPeer* p, CxUiRect px)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    c->bounds     = px;
    if (c->cp.ops)
        c->cp.ops->targetResize(c->cp.target, (CxUiSize){ px.w, px.h });
    _cxuiGtk4ChildPlace(GTK_WIDGET(c->widget));
}

void _cxuiGtk4CanvasInvalidate(CxUiPeer* p, CxUiRect px)
{
    (void)px;
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    if (!c->tick)
        c->tick = gtk_widget_add_tick_callback(GTK_WIDGET(c->widget), canvasTick, c, NULL);
}

// GTK can't show a frame synchronously; this paints now, and the result shows on the next frame.
void _cxuiGtk4CanvasPaintNow(CxUiPeer* p)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    if (_cxuiCanvasPaint(&c->cp, NULL))
        gtk_widget_queue_draw(GTK_WIDGET(c->widget));
}

// GTK grabs the pointer for the widget a button went down on, so there is nothing to do.
void _cxuiGtk4CanvasCapture(CxUiPeer* p, bool on)
{
    unused_noeval(p);
    unused_noeval(on);
}

// On the peer's own widget, which GTK also asks for the widgets inside it
void _cxuiGtk4PeerSetTip(CxUiPeer* p, strref tip)
{
    gtk_widget_set_tooltip_text(_cxuiGtk4PeerWidget(p), strEmpty(tip) ? NULL : strC(tip));
}

bool _cxuiGtk4TestTip(CxUiPeer* p, string* out)
{
    strCopy(out, (strref)gtk_widget_get_tooltip_text(_cxuiGtk4PeerWidget(p)));
    return true;
}

void _cxuiGtk4PeerFocus(CxUiPeer* p)
{
    GtkWidget* w = _cxuiPeerIsControl(p->kind)  ? _cxuiGtk4ControlFocusWidget(p)
                   : p->kind == CXUI_PEER_Text ? _cxuiGtk4TextFocusWidget(p)
                                               : _cxuiGtk4PeerWidget(p);
    gtk_widget_grab_focus(w);
}

void _cxuiGtk4CanvasSetCursor(CxUiPeer* p, uint32 cursor)
{
    static const char* const names[CXUI_CURSOR_Count] = {
        [CXUI_CURSOR_Arrow] = "default",      [CXUI_CURSOR_IBeam] = "text",
        [CXUI_CURSOR_Hand] = "pointer",       [CXUI_CURSOR_Wait] = "wait",
        [CXUI_CURSOR_Cross] = "crosshair",    [CXUI_CURSOR_SizeWE] = "ew-resize",
        [CXUI_CURSOR_SizeNS] = "ns-resize",   [CXUI_CURSOR_SizeNWSE] = "nwse-resize",
        [CXUI_CURSOR_SizeNESW] = "nesw-resize", [CXUI_CURSOR_Move] = "move",
        [CXUI_CURSOR_NotAllowed] = "not-allowed", [CXUI_CURSOR_None] = "none",
    };
    gtk_widget_set_cursor_from_name(GTK_WIDGET(((Gtk4Canvas*)p)->widget), cursor < CXUI_CURSOR_Count ? names[cursor] : NULL);
}

bool _cxuiGtk4TestExpose(CxUiPeer* p)
{
    gtk_widget_queue_draw(GTK_WIDGET(((Gtk4Canvas*)p)->widget));
    return true;
}

void _cxuiGtk4CanvasDestroy(CxUiPeer* p)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    // Controllers go with the widget, which can outlive c while GTK holds it.
    g_signal_handlers_disconnect_by_data(c->widget, c);
    _cxuiGtk4ControllersRelease(GTK_WIDGET(c->widget), c);
    if (c->tick)
        gtk_widget_remove_tick_callback(GTK_WIDGET(c->widget), c->tick);
    if (c->surface)
        _cxuiGtk4SurfaceDestroy(c->surface);
    c->widget->peer = NULL;
    _cxuiGtk4ChildDetach(GTK_WIDGET(c->widget));
    g_object_unref(c->widget);
    if (c->cp.ops)
        c->cp.ops->targetDestroy(c->cp.target);
    xaFree(c);
}

bool _cxuiGtk4SurfaceGetPeer(CxUiPeer* p, CxUiNativeSurface* out)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    return c->surface && _cxuiGtk4SurfaceGet(c->surface, out);
}

bool _cxuiGtk4TestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    return c->surface && _cxuiGtk4SurfaceTestInfo(c->surface, out);
}

bool _cxuiGtk4TestSurfaceMark(CxUiPeer* p)
{
    Gtk4Canvas* c = (Gtk4Canvas*)p;
    return c->surface && _cxuiGtk4SurfaceTestMark(c->surface);
}

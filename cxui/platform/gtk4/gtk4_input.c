#include "gtk4_ui.h"

#include <dlfcn.h>
#include <math.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

static CxUiKey keyvalToKey(guint kv)
{
    if (kv >= GDK_KEY_a && kv <= GDK_KEY_z)
        return (CxUiKey)(CXUI_KEY_A + (kv - GDK_KEY_a));
    if (kv >= GDK_KEY_A && kv <= GDK_KEY_Z)
        return (CxUiKey)(CXUI_KEY_A + (kv - GDK_KEY_A));
    if (kv >= GDK_KEY_0 && kv <= GDK_KEY_9)
        return (CxUiKey)(CXUI_KEY_0 + (kv - GDK_KEY_0));
    if (kv >= GDK_KEY_F1 && kv <= GDK_KEY_F24)
        return (CxUiKey)(CXUI_KEY_F1 + (kv - GDK_KEY_F1));
    if (kv >= GDK_KEY_KP_0 && kv <= GDK_KEY_KP_9)
        return (CxUiKey)(CXUI_KEY_Pad0 + (kv - GDK_KEY_KP_0));
    switch (kv) {
    case GDK_KEY_Up: case GDK_KEY_KP_Up:              return CXUI_KEY_Up;
    case GDK_KEY_Down: case GDK_KEY_KP_Down:          return CXUI_KEY_Down;
    case GDK_KEY_Left: case GDK_KEY_KP_Left:          return CXUI_KEY_Left;
    case GDK_KEY_Right: case GDK_KEY_KP_Right:        return CXUI_KEY_Right;
    case GDK_KEY_Home: case GDK_KEY_KP_Home:          return CXUI_KEY_Home;
    case GDK_KEY_End: case GDK_KEY_KP_End:            return CXUI_KEY_End;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:    return CXUI_KEY_PageUp;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return CXUI_KEY_PageDown;
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert:      return CXUI_KEY_Insert;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete:      return CXUI_KEY_Delete;
    case GDK_KEY_BackSpace:                           return CXUI_KEY_Backspace;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:      return CXUI_KEY_Tab;
    case GDK_KEY_Return:                              return CXUI_KEY_Enter;
    case GDK_KEY_KP_Enter:                            return CXUI_KEY_PadEnter;
    case GDK_KEY_Escape:                              return CXUI_KEY_Escape;
    case GDK_KEY_space:                               return CXUI_KEY_Space;
    case GDK_KEY_KP_Decimal:                          return CXUI_KEY_PadDecimal;
    case GDK_KEY_KP_Add:                              return CXUI_KEY_PadAdd;
    case GDK_KEY_KP_Subtract:                         return CXUI_KEY_PadSubtract;
    case GDK_KEY_KP_Multiply:                         return CXUI_KEY_PadMultiply;
    case GDK_KEY_KP_Divide:                           return CXUI_KEY_PadDivide;
    case GDK_KEY_period:                              return CXUI_KEY_Period;
    case GDK_KEY_comma:                               return CXUI_KEY_Comma;
    case GDK_KEY_slash:                               return CXUI_KEY_Slash;
    case GDK_KEY_minus:                               return CXUI_KEY_Minus;
    case GDK_KEY_equal:                               return CXUI_KEY_Equals;
    case GDK_KEY_semicolon:                           return CXUI_KEY_Semicolon;
    case GDK_KEY_apostrophe:                          return CXUI_KEY_Quote;
    case GDK_KEY_bracketleft:                         return CXUI_KEY_BracketLeft;
    case GDK_KEY_bracketright:                        return CXUI_KEY_BracketRight;
    case GDK_KEY_backslash:                           return CXUI_KEY_Backslash;
    case GDK_KEY_grave:                               return CXUI_KEY_Grave;
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:       return CXUI_KEY_Shift;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R:   return CXUI_KEY_Ctrl;
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:           return CXUI_KEY_Alt;
    case GDK_KEY_Super_L: case GDK_KEY_Super_R:
    case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:         return CXUI_KEY_Meta;
    case GDK_KEY_Caps_Lock:                           return CXUI_KEY_CapsLock;
    case GDK_KEY_Num_Lock:                            return CXUI_KEY_NumLock;
    case GDK_KEY_Scroll_Lock:                         return CXUI_KEY_ScrollLock;
    case GDK_KEY_Print:                               return CXUI_KEY_PrintScreen;
    case GDK_KEY_Pause:                               return CXUI_KEY_Pause;
    case GDK_KEY_Menu:                                return CXUI_KEY_ContextMenu;
    }
    return CXUI_KEY_None;
}

static uint32 stateMods(GdkModifierType st)
{
    uint32 mods = 0;
    if (st & GDK_SHIFT_MASK)
        mods |= CXUI_MOD_Shift;
    if (st & GDK_CONTROL_MASK)
        mods |= CXUI_MOD_Ctrl;
    if (st & GDK_ALT_MASK)
        mods |= CXUI_MOD_Alt;
    if (st & (GDK_SUPER_MASK | GDK_META_MASK))
        mods |= CXUI_MOD_Meta;
    return mods;
}

static uint32 stateButtons(GdkModifierType st)
{
    uint32 b = 0;
    if (st & GDK_BUTTON1_MASK)
        b |= CXUI_BTN_Left;
    if (st & GDK_BUTTON2_MASK)
        b |= CXUI_BTN_Middle;
    if (st & GDK_BUTTON3_MASK)
        b |= CXUI_BTN_Right;
    return b;
}

static uint32 gdkButton(guint b)
{
    switch (b) {
    case 1: return CXUI_BTN_Left;
    case 2: return CXUI_BTN_Middle;
    case 3: return CXUI_BTN_Right;
    case 8: return CXUI_BTN_X1;
    case 9: return CXUI_BTN_X2;
    }
    return 0;
}

_Use_decl_annotations_
bool _cxuiGtk4Key(CxUiPeer* p, GdkEvent* ev)
{
    GdkEventType type = gdk_event_get_event_type(ev);
    if (type != GDK_KEY_PRESS && type != GDK_KEY_RELEASE)
        return false;
    // The key without Shift or other levels, so Shift+1 is still 1
    guint keycode = gdk_key_event_get_keycode(ev);
    guint keyval  = gdk_key_event_get_keyval(ev);
    gdk_display_translate_key(gdk_event_get_display(ev), keycode, 0, (int)gdk_key_event_get_layout(ev), &keyval, NULL,
                              NULL, NULL);
    CxUiKey key = keyvalToKey(keyval);
    if (key == CXUI_KEY_None)
        key = keyvalToKey(gdk_key_event_get_keyval(ev));
    if (key == CXUI_KEY_None)
        return false;
    _cxuiNativeEnter();
    CxUiResult r = _cxuiKey(p, key, stateMods(gdk_event_get_modifier_state(ev)), keycode, type == GDK_KEY_PRESS);
    _cxuiNativeLeave();
    return r == CXUI_Handled;
}

// The event's position relative to w, in physical pixels; *last when the event has none (some
// scroll events), and updated otherwise.
static CxUiPoint eventPx(GtkWidget* w, GdkEvent* ev, CxUiPoint* last)
{
    double x = 0, y = 0, tx = 0, ty = 0;
    GtkNative* native = gtk_widget_get_native(w);
    if (!native || !gdk_event_get_position(ev, &x, &y) || isnan(x) || isnan(y))
        return *last;
    gtk_native_get_surface_transform(native, &tx, &ty);
    graphene_point_t in = GRAPHENE_POINT_INIT((float)(x - tx), (float)(y - ty)), out;
    if (!gtk_widget_compute_point(GTK_WIDGET(native), w, &in, &out))
        return (CxUiPoint){ 0 };
    float32 s = _cxuiGtk4WidgetScale(w);
    *last     = (CxUiPoint){ (int32)floorf(out.x * s), (int32)floorf(out.y * s) };
    return *last;
}

// Pixels a smooth scroll moves for one notch of a wheel; GTK reports touchpads in pixels.
#define PIXELS_PER_NOTCH 20.0

// The press being handled, for a window move started from its handler
static GdkEvent* pressEvent;

GdkEvent* _cxuiGtk4PressEvent(void)
{
    return pressEvent;
}

_Use_decl_annotations_
bool _cxuiGtk4Pointer(CxUiPeer* p, GtkWidget* w, GdkEvent* ev, CxUiPoint* last)
{
    GdkModifierType st = gdk_event_get_modifier_state(ev);
    uint32 mods        = stateMods(st);
    bool handled       = true;
    _cxuiNativeEnter();
    switch (gdk_event_get_event_type(ev)) {
    case GDK_MOTION_NOTIFY:
    case GDK_ENTER_NOTIFY:
        _cxuiCanvasPointer(p, CXUI_PTR_Move, 0, stateButtons(st), mods, eventPx(w, ev, last));
        break;
    case GDK_LEAVE_NOTIFY:
        _cxuiCanvasPointer(p, CXUI_PTR_Left, 0, stateButtons(st), mods, eventPx(w, ev, last));
        break;
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE: {
        uint32 button = gdkButton(gdk_button_event_get_button(ev));
        bool press    = gdk_event_get_event_type(ev) == GDK_BUTTON_PRESS;
        if (!button)
            break;
        GdkEvent* outer = pressEvent;
        if (press)
            pressEvent = ev;
        _cxuiCanvasPointer(p, press ? CXUI_PTR_Press : CXUI_PTR_Release, button, stateButtons(st), mods,
                           eventPx(w, ev, last));
        pressEvent = outer;
        break;
    }
    case GDK_GRAB_BROKEN:
        _cxuiCanvasCaptureLost(p);
        break;
    case GDK_SCROLL: {
        double dx = 0, dy = 0;
        switch (gdk_scroll_event_get_direction(ev)) {
        case GDK_SCROLL_UP:    dy = -1; break;
        case GDK_SCROLL_DOWN:  dy = 1; break;
        case GDK_SCROLL_LEFT:  dx = -1; break;
        case GDK_SCROLL_RIGHT: dx = 1; break;
        default:
            gdk_scroll_event_get_deltas(ev, &dx, &dy);
            if (gdk_scroll_event_get_unit(ev) == GDK_SCROLL_UNIT_SURFACE) {
                dx /= PIXELS_PER_NOTCH;
                dy /= PIXELS_PER_NOTCH;
            }
            break;
        }
        handled = _cxuiCanvasWheel(p, dx, dy, mods, eventPx(w, ev, last)) == CXUI_Handled;
        break;
    }
    default:
        handled = false;
        break;
    }
    _cxuiNativeLeave();
    return handled;
}

void _cxuiGtk4DoubleClick(int64* time, int32* distance)
{
    int ms = 400, dist = 5;
    if (gdk_display_get_default())
        g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &ms, "gtk-double-click-distance", &dist,
                     NULL);
    *time     = timeMS(ms);
    *distance = dist;
}

// ---- tests ----

#ifdef GDK_WINDOWING_X11
static guint keyToKeyval(CxUiKey key)
{
    if (key >= CXUI_KEY_A && key <= CXUI_KEY_Z)
        return GDK_KEY_a + (key - CXUI_KEY_A);
    if (key >= CXUI_KEY_0 && key <= CXUI_KEY_9)
        return GDK_KEY_0 + (key - CXUI_KEY_0);
    if (key >= CXUI_KEY_F1 && key <= CXUI_KEY_F12)
        return GDK_KEY_F1 + (key - CXUI_KEY_F1);
    switch (key) {
    case CXUI_KEY_Tab:       return GDK_KEY_Tab;
    case CXUI_KEY_Enter:     return GDK_KEY_Return;
    case CXUI_KEY_Escape:    return GDK_KEY_Escape;
    case CXUI_KEY_Space:     return GDK_KEY_space;
    case CXUI_KEY_Backspace: return GDK_KEY_BackSpace;
    case CXUI_KEY_Delete:    return GDK_KEY_Delete;
    case CXUI_KEY_Home:      return GDK_KEY_Home;
    case CXUI_KEY_End:       return GDK_KEY_End;
    case CXUI_KEY_Left:      return GDK_KEY_Left;
    case CXUI_KEY_Right:     return GDK_KEY_Right;
    case CXUI_KEY_Up:        return GDK_KEY_Up;
    case CXUI_KEY_Down:      return GDK_KEY_Down;
    default:                 return 0;
    }
}

typedef int (*XTestFakeKeyEventFn)(Display* d, unsigned int keycode, Bool down, unsigned long delay);
typedef KeyCode (*XKeysymToKeycodeFn)(Display* d, KeySym sym);
typedef int (*XSetInputFocusFn)(Display* d, Window w, int revert, Time t);
typedef int (*XFlushFn)(Display* d);

// X11 only, through XTest, loaded when first needed. The window is given the X input focus first;
// the key arrives asynchronously, as typing does.
bool _cxuiGtk4TestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods)
{
    static XTestFakeKeyEventFn fake;
    static XKeysymToKeycodeFn toCode;
    static XSetInputFocusFn setFocus;
    static XFlushFn flush;
    GdkDisplay* d = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(d))
        return false;
    if (!fake) {
        void* xtst = dlopen("libXtst.so.6", RTLD_NOW);
        void* x11  = dlopen("libX11.so.6", RTLD_NOW);
        if (!xtst || !x11)
            return false;
        toCode   = (XKeysymToKeycodeFn)dlsym(x11, "XKeysymToKeycode");
        setFocus = (XSetInputFocusFn)dlsym(x11, "XSetInputFocus");
        flush    = (XFlushFn)dlsym(x11, "XFlush");
        fake     = (XTestFakeKeyEventFn)dlsym(xtst, "XTestFakeKeyEvent");
        if (!toCode || !setFocus || !flush || !fake) {
            fake = NULL;
            return false;
        }
    }
    guint kv      = keyToKeyval(key);
    GtkNative* nt = gtk_widget_get_native(_cxuiGtk4PeerWidget(p));
    if (!kv || !nt)
        return false;
    // Deprecated with the rest of GTK's X11 backend from 4.18, and still there
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display* x = gdk_x11_display_get_xdisplay(d);
    Window xw  = gdk_x11_surface_get_xid(gtk_native_get_surface(nt));
    G_GNUC_END_IGNORE_DEPRECATIONS
    setFocus(x, xw, RevertToParent, CurrentTime);

    KeyCode held[3];
    int32 nheld = 0;
    if (mods & CXUI_MOD_Ctrl)
        held[nheld++] = toCode(x, GDK_KEY_Control_L);
    if (mods & CXUI_MOD_Shift)
        held[nheld++] = toCode(x, GDK_KEY_Shift_L);
    if (mods & CXUI_MOD_Alt)
        held[nheld++] = toCode(x, GDK_KEY_Alt_L);
    for (int32 i = 0; i < nheld; i++)
        fake(x, held[i], True, 0);
    KeyCode code = toCode(x, kv);
    fake(x, code, True, 0);
    fake(x, code, False, 0);
    for (int32 i = nheld - 1; i >= 0; i--)
        fake(x, held[i], False, 0);
    flush(x);
    return true;
}
#else
bool _cxuiGtk4TestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods)
{
    unused_noeval(p);
    unused_noeval(key);
    unused_noeval(mods);
    return false;
}
#endif

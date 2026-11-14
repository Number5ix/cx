#include "gtk4_ui.h"

#include <cx/format.h>

#include "core/cxuicontrols.h"

// Labels are GtkLabels, buttons GtkButtons, and check boxes and radio buttons GtkCheckButtons,
// each with a GtkLabel of its own. Fonts and text colors go through one CSS provider, with a class
// for each control. Keys go to the core first, from a capture-phase controller, as a text field's
// do.
//
// A radio button is grouped with a hidden check button of its own, which gives it the radio look;
// the core keeps one radio of a cxui group selected.
//
// A list is a GtkListView of a GtkStringList, with a single selection, in a scrolled window; a
// dropdown a GtkDropDown of one.
//
// A slider is a GtkScale over whole positions; a press and release on it, seen by a capture-phase
// controller, mark a drag, so a move outside one (a key, or the wheel) is done at once. A progress
// bar is a GtkProgressBar, pulsed by a timer while it shows activity.
//
// Mnemonics are cxui's: GTK's own would run before the core saw the key, pressing a button ahead
// of a handler that wants Alt with that letter. So the label is plain text, with the mnemonic
// underlined in markup while the window shows mnemonics (GTK sets that while Alt is held).

typedef struct Gtk4Control {
    CxUiPeer base;
    GtkWidget* widget;   // held by a reference of our own
    GtkWidget* label;    // the GtkLabel: the widget itself, or the button's
    CxUiRect bounds;     // physical pixels, within the parent
    uint32 id;           // its CSS class: cxui-c<id>
    string fontCss;      // declarations for the font
    string colorCss;     // a declaration for the text's color
    string text;         // as given, with its `&`
    GtkWindow* window;   // whose mnemonics-visible is followed, while mapped
    GtkWidget* partner;  // a radio button's hidden group, held by a reference of our own
    GtkStringList* model;        // a list's or dropdown's items, held by a reference of our own
    GtkSelectionModel* select;   // a list's, which its view holds: single, or multiple when multi
    bool multi;                  // a list where any number can be selected
    int32 pendingTop;            // the first item to show once the list has its size, or -1
    gulong topWatch;             // waiting for that
    guint topIdle;               // waiting for the list to be laid out again
    GtkWidget* view;             // a list's GtkListView, inside the scrolled window
    string bgCss;                // a list's background
    guint pulse;                 // a progress bar's timer, while it shows activity
    bool dragging;               // a slider's thumb is held
    bool setting;        // the core is setting a check button's state, a list's items or selection, or a tab
    GtkWidget* notebook;         // tabs' GtkNotebook of empty pages, held by a reference of our own
    GtkWidget* page;             // tabs' page surface, a root held by a reference of our own
    CxUiRect noteBounds, pageBounds;   // physical pixels, within the control
    sa_string titles;            // tabs' titles, as given
    CxUiEdges insets;            // the room around the notebook's page area as allocated, in DIPs
    bool insetsKnown;
    guint insetIdle;             // telling the core the insets changed
} Gtk4Control;

static GtkCssProvider* css;
static sa_ptr controls;   // Gtk4Control*, for rebuilding the CSS
static GtkWidget* scaleProbes[2][2];   // measure sliders before they exist: vertical, with marks
static uint32 nextId;

static void cssRebuild(void)
{
    if (!css) {
        css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    string all = 0, rule = 0;
    for (int32 i = 0; i < saSize(controls); i++) {
        Gtk4Control* c = controls.a[i];
        if (strEmpty(c->fontCss) && strEmpty(c->colorCss))
            continue;
        // A check button's color is its label's, leaving its box to the theme; a list's goes to its
        // view, with the background.
        bool check = c->base.kind == CXUI_PEER_Check || c->base.kind == CXUI_PEER_Radio;
        bool list  = c->base.kind == CXUI_PEER_List;
        // The tabs' font is their notebook's, not the pages' on the surface beside it.
        strFormat(&rule, _S".cxui-c${uint}${string} { ${string} ${string} }\n", stvar(uint32, c->id),
                  stvar(strref, c->base.kind == CXUI_PEER_Tabs ? _S" > notebook" : NULL), stvar(strref, c->fontCss),
                  stvar(strref, check || list ? NULL : (strref)c->colorCss));
        strAppend(&all, rule);
        if ((check || list) && !strEmpty(c->colorCss)) {
            strFormat(&rule, _S".cxui-c${uint} ${string} { ${string} ${string} }\n", stvar(uint32, c->id),
                      stvar(strref, check ? _S"> label" : _S"listview"), stvar(strref, c->colorCss),
                      stvar(strref, c->bgCss));
            strAppend(&all, rule);
        }
    }
    gtk_css_provider_load_from_string(css, strEmpty(all) ? "" : strC(all));
    strDestroy(&all);
    strDestroy(&rule);
}

static void appendEscaped(string* out, const char* s, gssize n)
{
    char* esc = g_markup_escape_text(s, n);
    strAppend(out, (strref)esc);
    g_free(esc);
}

// The text as markup, with the character after a single `&` underlined
static void mnemonicMarkup(string* out, strref text)
{
    strClear(out);
    const char* s = strEmpty(text) ? "" : strC(text);
    int32 len     = strLen(text);
    bool done     = false;
    for (int32 i = 0; i < len;) {
        const char* amp = memchr(s + i, '&', len - i);
        int32 end       = amp ? (int32)(amp - s) : len;
        appendEscaped(out, s + i, end - i);
        if (!amp || end + 1 >= len)
            break;
        if (s[end + 1] == '&') {
            strAppend(out, _S"&amp;");
            i = end + 2;
            continue;
        }
        // The whole character after it, which may take several bytes
        int32 next = end + 1;
        do
            next++;
        while (next < len && (s[next] & 0xC0) == 0x80);
        if (!done)
            strAppend(out, _S"<u>");
        appendEscaped(out, s + end + 1, next - end - 1);
        if (!done)
            strAppend(out, _S"</u>");
        done = true;
        i    = next;
    }
}

static bool showsMnemonics(Gtk4Control* c)
{
    return c->window && gtk_window_get_mnemonics_visible(c->window);
}

static void tabsRender(Gtk4Control* c);

static void render(Gtk4Control* c)
{
    if (c->base.kind == CXUI_PEER_Tabs)
        tabsRender(c);
    if (!c->label)
        return;
    string shown = 0;
    if (showsMnemonics(c)) {
        mnemonicMarkup(&shown, c->text);
        gtk_label_set_markup(GTK_LABEL(c->label), strEmpty(shown) ? "" : strC(shown));
    } else {
        _cxuiMnemonicStrip(&shown, c->text);
        gtk_label_set_text(GTK_LABEL(c->label), strEmpty(shown) ? "" : strC(shown));
    }
    strDestroy(&shown);
}

static void onMnemonicsVisible(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(obj);
    unused_noeval(pspec);
    render(ud);
}

static void follow(Gtk4Control* c, GtkWindow* win)
{
    if (c->window == win)
        return;
    if (c->window)
        g_signal_handlers_disconnect_by_func(c->window, onMnemonicsVisible, c);
    c->window = win;
    if (win)
        g_signal_connect(win, "notify::mnemonics-visible", G_CALLBACK(onMnemonicsVisible), c);
    render(c);
}

static void onMap(GtkWidget* w, gpointer ud)
{
    GtkRoot* root = gtk_widget_get_root(w);
    follow(ud, GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL);
}

static void onUnmap(GtkWidget* w, gpointer ud)
{
    unused_noeval(w);
    follow(ud, NULL);
}

static void setMnemonicText(Gtk4Control* c, strref text)
{
    strDup(&c->text, text);
    render(c);
}

static Gtk4Control* controlNew(CxUiPeerKind kind, GtkWidget* widget, GtkWidget* label, CxUiPeer* host)
{
    GtkWidget* parent = _cxuiGtk4HostWidget(host);
    if (!parent)
        return NULL;
    Gtk4Control* c = xaAllocStruct(Gtk4Control, XA_Zero);
    c->base.kind   = kind;
    c->id          = ++nextId;
    c->widget      = g_object_ref_sink(widget);
    c->label       = label;
    char cls[32];
    snprintf(cls, sizeof(cls), "cxui-c%u", c->id);
    gtk_widget_add_css_class(c->widget, cls);
    if (!saValid(controls))
        saInit(&controls, ptr, 8);
    saPush(&controls, ptr, c);
    g_signal_connect(c->widget, "map", G_CALLBACK(onMap), c);
    g_signal_connect(c->widget, "unmap", G_CALLBACK(onUnmap), c);
    _cxuiGtk4ChildAttach(c->widget, &c->base, &c->bounds, parent);
    return c;
}

// ---- labels ----

CxUiPeer* _cxuiGtk4LabelCreate(CxUiLabel* l, CxUiPeer* host)
{
    unused_noeval(l);
    GtkWidget* label = gtk_label_new(NULL);
    Gtk4Control* c   = controlNew(CXUI_PEER_Label, label, label, host);
    if (!c) {
        g_object_ref_sink(label);
        g_object_unref(label);
        return NULL;
    }
    return &c->base;
}

void _cxuiGtk4LabelSetText(CxUiPeer* p, strref text)
{
    setMnemonicText((Gtk4Control*)p, text);
}

void _cxuiGtk4LabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap)
{
    GtkLabel* l = GTK_LABEL(((Gtk4Control*)p)->label);
    gtk_label_set_wrap(l, wrap);
    gtk_label_set_wrap_mode(l, PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(l, (align & CXUI_TEXT_Right) ? 1.0f : (align & CXUI_TEXT_Center) ? 0.5f : 0.0f);
    gtk_label_set_justify(l, (align & CXUI_TEXT_Right)    ? GTK_JUSTIFY_RIGHT
                             : (align & CXUI_TEXT_Center) ? GTK_JUSTIFY_CENTER
                                                          : GTK_JUSTIFY_LEFT);
    gtk_label_set_ellipsize(l, ellipsis == CXUI_ELLIPSIS_End ? PANGO_ELLIPSIZE_END
                               : ellipsis == CXUI_ELLIPSIS_None ? PANGO_ELLIPSIZE_NONE
                                                                : PANGO_ELLIPSIZE_MIDDLE);
}

void _cxuiGtk4ControlSetColor(CxUiPeer* p, CxUiColor color)
{
    Gtk4Control* c = (Gtk4Control*)p;
    strCopy(&c->colorCss, _S"color: ");
    _cxuiGtk4CssColor(&c->colorCss, color);
    strAppend(&c->colorCss, _S";");
    cssRebuild();
}

static CxUiSize widgetMeasure(GtkWidget* w)
{
    int minW, natW, minH, natH;
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &natW, NULL, NULL);
    gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, natW, &minH, &natH, NULL, NULL);
    return (CxUiSize){ natW, natH };
}

// The widget's own measure, with its font; DIPs are GTK's logical pixels. Until it exists, the
// font's measure does.
CxUiSize _cxuiGtk4LabelMeasure(CxUiLabel* l, CxUiFont* f, strref text)
{
    unused_noeval(f);
    unused_noeval(text);
    CxUiPeer* p = CxUiWidget(l)->peer;
    return p ? widgetMeasure(((Gtk4Control*)p)->widget) : (CxUiSize){ 0 };
}

static int32 heightAt(GtkWidget* w, int32 width)
{
    int minH, natH;
    gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, max(width, 1), &minH, &natH, NULL, NULL);
    return natH;
}

int32 _cxuiGtk4LabelHeightFor(CxUiLabel* l, CxUiFont* f, strref text, int32 width)
{
    unused_noeval(f);
    unused_noeval(text);
    CxUiPeer* p = CxUiWidget(l)->peer;
    return p ? heightAt(((Gtk4Control*)p)->widget, width) : 0;
}

// ---- buttons ----

static void onClicked(GtkButton* b, gpointer ud)
{
    unused_noeval(b);
    _cxuiNativeEnter();
    _cxuiButtonClicked(&((Gtk4Control*)ud)->base);
    _cxuiNativeLeave();
}

static gboolean onKey(GtkEventControllerLegacy* ctl, GdkEvent* ev, gpointer ud)
{
    unused_noeval(ctl);
    return _cxuiGtk4Key(&((Gtk4Control*)ud)->base, ev);
}

static void onFocusIn(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Control*)ud)->base, true);
    _cxuiNativeLeave();
}

static void onFocusOut(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Control*)ud)->base, false);
    _cxuiNativeLeave();
}

// Keys to the core first, and focus changes
static void addInputControllers(Gtk4Control* c)
{
    GtkEventController* keys = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "event", G_CALLBACK(onKey), c);
    gtk_widget_add_controller(c->widget, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(onFocusIn), c);
    g_signal_connect(focus, "leave", G_CALLBACK(onFocusOut), c);
    gtk_widget_add_controller(c->widget, focus);
}

CxUiPeer* _cxuiGtk4ButtonCreate(CxUiButton* b, CxUiPeer* host)
{
    unused_noeval(b);
    GtkWidget* label  = gtk_label_new(NULL);
    GtkWidget* button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), label);
    Gtk4Control* c = controlNew(CXUI_PEER_Button, button, label, host);
    if (!c) {
        g_object_ref_sink(button);
        g_object_unref(button);
        return NULL;
    }
    g_signal_connect(button, "clicked", G_CALLBACK(onClicked), c);
    addInputControllers(c);
    return &c->base;
}

void _cxuiGtk4ButtonSetTitle(CxUiPeer* p, strref title)
{
    setMnemonicText((Gtk4Control*)p, title);
}

// GTK has no default button look of its own; the theme's suggested action is the nearest.
void _cxuiGtk4ButtonSetDefault(CxUiPeer* p, bool isDefault)
{
    GtkWidget* w = ((Gtk4Control*)p)->widget;
    if (isDefault)
        gtk_widget_add_css_class(w, "suggested-action");
    else
        gtk_widget_remove_css_class(w, "suggested-action");
}

// The widget's own measure, with its font and theme; DIPs are GTK's logical pixels.
CxUiSize _cxuiGtk4ButtonMeasure(CxUiButton* b, CxUiFont* f, strref title)
{
    unused_noeval(f);
    unused_noeval(title);
    CxUiPeer* p = CxUiWidget(b)->peer;
    if (!p)
        return (CxUiSize){ 0 };
    return widgetMeasure(((Gtk4Control*)p)->widget);
}

// ---- check boxes and radio buttons ----

static void onToggled(GtkCheckButton* b, gpointer ud)
{
    unused_noeval(b);
    Gtk4Control* c = ud;
    if (c->setting)
        return;
    _cxuiNativeEnter();
    _cxuiButtonClicked(&c->base);
    _cxuiNativeLeave();
}

CxUiPeer* _cxuiGtk4CheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio)
{
    unused_noeval(w);
    GtkWidget* label = gtk_label_new(NULL);
    GtkWidget* check = gtk_check_button_new();
    gtk_check_button_set_child(GTK_CHECK_BUTTON(check), label);
    Gtk4Control* c = controlNew(radio ? CXUI_PEER_Radio : CXUI_PEER_Check, check, label, host);
    if (!c) {
        g_object_ref_sink(check);
        g_object_unref(check);
        return NULL;
    }
    if (radio) {
        c->partner = g_object_ref_sink(gtk_check_button_new());
        gtk_check_button_set_group(GTK_CHECK_BUTTON(check), GTK_CHECK_BUTTON(c->partner));
    }
    g_signal_connect(check, "toggled", G_CALLBACK(onToggled), c);
    addInputControllers(c);
    return &c->base;
}

void _cxuiGtk4CheckSetState(CxUiPeer* p, uint32 state)
{
    Gtk4Control* c   = (Gtk4Control*)p;
    GtkCheckButton* b = GTK_CHECK_BUTTON(c->widget);
    c->setting        = true;
    gtk_check_button_set_inconsistent(b, state == CXUI_CHECK_Mixed);
    gtk_check_button_set_active(b, state == CXUI_CHECK_On);
    c->setting = false;
}

CxUiSize _cxuiGtk4CheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio)
{
    unused_noeval(f);
    unused_noeval(title);
    unused_noeval(radio);
    return w->peer ? widgetMeasure(((Gtk4Control*)w->peer)->widget) : (CxUiSize){ 0 };
}

// ---- lists and dropdowns ----

static void rowSetup(GtkSignalListItemFactory* f, GObject* item, gpointer ud)
{
    unused_noeval(f);
    unused_noeval(ud);
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_list_item_set_child(GTK_LIST_ITEM(item), label);
}

static void rowBind(GtkSignalListItemFactory* f, GObject* item, gpointer ud)
{
    unused_noeval(f);
    unused_noeval(ud);
    GtkStringObject* so = gtk_list_item_get_item(GTK_LIST_ITEM(item));
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(GTK_LIST_ITEM(item))), gtk_string_object_get_string(so));
}

static guint selectedOf(Gtk4Control* c)
{
    if (c->multi) {
        GtkBitset* set = gtk_selection_model_get_selection(c->select);
        guint first    = gtk_bitset_is_empty(set) ? GTK_INVALID_LIST_POSITION : gtk_bitset_get_minimum(set);
        gtk_bitset_unref(set);
        return first;
    }
    return c->base.kind == CXUI_PEER_List ? gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(c->select))
                                          : gtk_drop_down_get_selected(GTK_DROP_DOWN(c->widget));
}

static void onSelected(GObject* obj, GParamSpec* pspec, gpointer ud)
{
    unused_noeval(obj);
    unused_noeval(pspec);
    Gtk4Control* c = ud;
    if (c->setting)
        return;
    guint sel = selectedOf(c);
    _cxuiNativeEnter();
    _cxuiListChosen(&c->base, sel == GTK_INVALID_LIST_POSITION ? -1 : (int32)sel);
    _cxuiNativeLeave();
}

static void onMarks(GtkSelectionModel* m, guint position, guint n, gpointer ud)
{
    unused_noeval(m);
    unused_noeval(position);
    unused_noeval(n);
    Gtk4Control* c = ud;
    if (c->setting)
        return;
    _cxuiNativeEnter();
    _cxuiListMarksChanged(&c->base);
    _cxuiNativeLeave();
}

static void onRowActivate(GtkListView* v, guint position, gpointer ud)
{
    unused_noeval(v);
    _cxuiNativeEnter();
    _cxuiListActivated(&((Gtk4Control*)ud)->base, (int32)position);
    _cxuiNativeLeave();
}

CxUiPeer* _cxuiGtk4ListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown)
{
    CxUiList* l               = dropdown ? NULL : objDynCast(CxUiList, w);
    bool multi                = l && l->_select == CXUI_SELECT_Multi;
    GtkStringList* model      = gtk_string_list_new(NULL);
    GtkWidget* widget;
    GtkSelectionModel* select = NULL;
    GtkWidget* view           = NULL;
    if (dropdown) {
        widget = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(model)), NULL);
    } else if (multi) {
        select = GTK_SELECTION_MODEL(gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(model))));
    } else {
        GtkSingleSelection* single = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(model)));
        gtk_single_selection_set_autoselect(single, FALSE);
        gtk_single_selection_set_can_unselect(single, TRUE);
        select = GTK_SELECTION_MODEL(single);
    }
    if (!dropdown) {
        GtkListItemFactory* f = gtk_signal_list_item_factory_new();
        g_signal_connect(f, "setup", G_CALLBACK(rowSetup), NULL);
        g_signal_connect(f, "bind", G_CALLBACK(rowBind), NULL);
        view   = gtk_list_view_new(GTK_SELECTION_MODEL(select), f);
        widget = gtk_scrolled_window_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(widget), view);
        gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(widget), TRUE);
    }
    Gtk4Control* c = controlNew(dropdown ? CXUI_PEER_Dropdown : CXUI_PEER_List, widget, NULL, host);
    if (!c) {
        g_object_ref_sink(widget);
        g_object_unref(widget);
        g_object_unref(model);
        return NULL;
    }
    c->model      = model;
    c->select     = select;
    c->view       = view;
    c->multi      = multi;
    c->pendingTop = -1;
    if (multi)
        g_signal_connect(select, "selection-changed", G_CALLBACK(onMarks), c);
    else
        g_signal_connect(dropdown ? (gpointer)widget : (gpointer)select, "notify::selected", G_CALLBACK(onSelected), c);
    if (view)
        g_signal_connect(view, "activate", G_CALLBACK(onRowActivate), c);
    addInputControllers(c);
    return &c->base;
}

void _cxuiGtk4ListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added)
{
    Gtk4Control* c   = (Gtk4Control*)p;
    const char** add = xaAlloc((added + 1) * sizeof(char*));
    for (int32 i = 0; i < added; i++)
        add[i] = strEmpty(items[i]) ? "" : strC(items[i]);
    add[added] = NULL;
    c->setting = true;
    gtk_string_list_splice(c->model, at, removed, add);
    c->setting = false;
    xaFree(add);
}

void _cxuiGtk4ListSetValue(CxUiPeer* p, int32 value)
{
    Gtk4Control* c = (Gtk4Control*)p;
    guint pos      = value < 0 ? GTK_INVALID_LIST_POSITION : (guint)value;
    if (c->multi)
        return;
    c->setting = true;
    if (c->base.kind == CXUI_PEER_List)
        gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(c->select), pos);
    else
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c->widget), pos);
    c->setting = false;
}

void _cxuiGtk4ListSetMarks(CxUiPeer* p, const uint8* marks, int32 count)
{
    Gtk4Control* c = (Gtk4Control*)p;
    GtkBitset* on  = gtk_bitset_new_empty();
    for (int32 i = 0; i < count; i++) {
        if (marks[i])
            gtk_bitset_add(on, (guint)i);
    }
    GtkBitset* all = gtk_bitset_new_range(0, (guint)max(count, 0));
    c->setting     = true;
    gtk_selection_model_set_selection(c->select, on, all);
    c->setting = false;
    gtk_bitset_unref(on);
    gtk_bitset_unref(all);
}

void _cxuiGtk4ListGetMarks(CxUiPeer* p, uint8* marks, int32 count)
{
    GtkBitset* set = gtk_selection_model_get_selection(((Gtk4Control*)p)->select);
    for (int32 i = 0; i < count; i++)
        marks[i] = gtk_bitset_contains(set, (guint)i);
    gtk_bitset_unref(set);
}

// The rows are all as tall, so the scroll position over a row's height is the first one shown.
static GtkAdjustment* listScroll(Gtk4Control* c, double* row)
{
    GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(c->widget));
    guint n          = g_list_model_get_n_items(G_LIST_MODEL(c->model));
    *row             = n > 0 ? gtk_adjustment_get_upper(a) / n : 0;
    return a;
}

static void listTopApply(Gtk4Control* c)
{
    double row;
    GtkAdjustment* a = listScroll(c, &row);
    if (row <= 0 || gtk_adjustment_get_page_size(a) <= 0)
        return;
    gtk_adjustment_set_value(a, c->pendingTop * row);
    c->pendingTop = -1;
    if (c->topWatch) {
        g_signal_handler_disconnect(a, c->topWatch);
        c->topWatch = 0;
    }
}

static void onScrollSized(GtkAdjustment* a, gpointer ud)
{
    unused_noeval(a);
    listTopApply(ud);
}

int32 _cxuiGtk4ListTopItem(CxUiPeer* p)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (c->pendingTop >= 0)
        return c->pendingTop;
    double row;
    GtkAdjustment* a = listScroll(c, &row);
    return row > 0 ? (int32)floor(gtk_adjustment_get_value(a) / row + 0.5) : 0;
}

static gboolean onTopIdle(gpointer ud)
{
    Gtk4Control* c = ud;
    c->topIdle     = 0;
    listTopApply(c);
    if (c->pendingTop >= 0 && !c->topWatch)
        c->topWatch = g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(c->widget)), "changed",
                                       G_CALLBACK(onScrollSized), c);
    return G_SOURCE_REMOVE;
}

// The list view goes back to the top when its items are all replaced, as it is next laid out, so
// the scroll waits until after that; and until the list has its size, the row height isn't known.
void _cxuiGtk4ListSetTopItem(CxUiPeer* p, int32 index)
{
    Gtk4Control* c = (Gtk4Control*)p;
    c->pendingTop  = index;
    if (!c->topIdle)
        c->topIdle = g_idle_add(onTopIdle, c);
}

void _cxuiGtk4ListShow(CxUiPeer* p, int32 index)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (c->view)
        gtk_list_view_scroll_to(GTK_LIST_VIEW(c->view), index, GTK_LIST_SCROLL_NONE, NULL);
}

// A list's rows are as tall as its view's natural height shared among them, plus its frame. A
// dropdown is as wide as its widest item plus what the widget adds around the one it shows.
CxUiSize _cxuiGtk4ListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown)
{
    Gtk4Control* c = w->peer ? (Gtk4Control*)w->peer : NULL;
    if (!c)
        return (CxUiSize){ 0 };
    if (dropdown) {
        CxUiSize n  = widgetMeasure(c->widget);
        guint sel   = selectedOf(c);
        int32 shown = 0;
        if (sel != GTK_INVALID_LIST_POSITION && sel < g_list_model_get_n_items(G_LIST_MODEL(c->model))) {
            string text = 0;
            strCopy(&text, (strref)gtk_string_list_get_string(c->model, sel));
            shown = (int32)ceilf(cxuiFontAdvance(f, text) / cxuiFontScale(f));
            strDestroy(&text);
        }
        return (CxUiSize){ widest + max(n.w - shown, 0), n.h };
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(c->model));
    if (n == 0)
        return (CxUiSize){ 0 };
    int minH, natH, minW, natW;
    gtk_widget_measure(c->view, GTK_ORIENTATION_VERTICAL, -1, &minH, &natH, NULL, NULL);
    gtk_widget_measure(c->view, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &natW, NULL, NULL);
    int32 row = (int32)(natH / n);
    if (row <= 0)
        return (CxUiSize){ 0 };
    return (CxUiSize){ max(widest + 12, natW) + 2, rows * row + 2 };
}

void _cxuiGtk4ListSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor text)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (c->base.kind != CXUI_PEER_List)
        return;
    strCopy(&c->colorCss, _S"color: ");
    _cxuiGtk4CssColor(&c->colorCss, text);
    strAppend(&c->colorCss, _S";");
    strCopy(&c->bgCss, _S"background-color: ");
    _cxuiGtk4CssColor(&c->bgCss, bg);
    strAppend(&c->bgCss, _S";");
    cssRebuild();
}

// ---- sliders and progress bars ----

static int32 scalePos(Gtk4Control* c)
{
    return (int32)floor(gtk_range_get_value(GTK_RANGE(c->widget)) + 0.5);
}

static void onSlid(GtkRange* r, gpointer ud)
{
    unused_noeval(r);
    Gtk4Control* c = ud;
    if (c->setting)
        return;
    _cxuiNativeEnter();
    _cxuiSliderMoved(&c->base, scalePos(c), !c->dragging);
    _cxuiNativeLeave();
}

// A press starts a drag and a release ends it; neither is taken.
static gboolean onScaleEvent(GtkEventControllerLegacy* ctl, GdkEvent* ev, gpointer ud)
{
    unused_noeval(ctl);
    Gtk4Control* c    = ud;
    GdkEventType type = gdk_event_get_event_type(ev);
    if (type == GDK_BUTTON_PRESS) {
        c->dragging = true;
    } else if (type == GDK_BUTTON_RELEASE && c->dragging) {
        c->dragging = false;
        _cxuiNativeEnter();
        _cxuiSliderMoved(&c->base, scalePos(c), true);
        _cxuiNativeLeave();
    }
    return FALSE;
}

CxUiPeer* _cxuiGtk4SliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical)
{
    unused_noeval(w);
    GtkWidget* scale = gtk_scale_new(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, NULL);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_range_set_round_digits(GTK_RANGE(scale), 0);
    // The low end at the bottom
    gtk_range_set_inverted(GTK_RANGE(scale), vertical);
    Gtk4Control* c = controlNew(CXUI_PEER_Slider, scale, NULL, host);
    if (!c) {
        g_object_ref_sink(scale);
        g_object_unref(scale);
        return NULL;
    }
    g_signal_connect(scale, "value-changed", G_CALLBACK(onSlid), c);
    GtkEventController* drags = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(drags, GTK_PHASE_CAPTURE);
    g_signal_connect(drags, "event", G_CALLBACK(onScaleEvent), c);
    gtk_widget_add_controller(scale, drags);
    addInputControllers(c);
    return &c->base;
}

void _cxuiGtk4SliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery)
{
    Gtk4Control* c = (Gtk4Control*)p;
    GtkRange* r    = GTK_RANGE(c->widget);
    c->setting     = true;
    gtk_range_set_range(r, 0, range);
    gtk_range_set_increments(r, 1, pageStep);
    gtk_scale_clear_marks(GTK_SCALE(c->widget));
    for (int32 i = 0; tickEvery > 0 && i <= range; i += tickEvery)
        gtk_scale_add_mark(GTK_SCALE(c->widget), i, GTK_POS_BOTTOM, NULL);
    c->setting = false;
}

void _cxuiGtk4SliderSetPos(CxUiPeer* p, int32 pos)
{
    Gtk4Control* c = (Gtk4Control*)p;
    c->setting     = true;
    gtk_range_set_value(GTK_RANGE(c->widget), pos);
    c->setting = false;
}

// GTK's thickness, and the length asked for; before the slider exists, a scale of the same kind
// measures it, as one thinner than GTK's minimum would be laid out wrongly until measured again.
CxUiSize _cxuiGtk4SliderMeasure(CxUiWidget* w, int32 length, bool vertical, bool ticks)
{
    GtkWidget* scale = w->peer ? ((Gtk4Control*)w->peer)->widget : scaleProbes[vertical][ticks];
    if (!scale) {
        scale = scaleProbes[vertical][ticks] =
            g_object_ref_sink(gtk_scale_new(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, NULL));
        gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
        if (ticks)
            gtk_scale_add_mark(GTK_SCALE(scale), 0, GTK_POS_BOTTOM, NULL);
    }
    CxUiSize n = widgetMeasure(scale);
    return vertical ? (CxUiSize){ n.w, length } : (CxUiSize){ length, n.h };
}

static gboolean pulseTick(gpointer ud)
{
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(((Gtk4Control*)ud)->widget));
    return G_SOURCE_CONTINUE;
}

CxUiPeer* _cxuiGtk4ProgressCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    GtkWidget* bar = gtk_progress_bar_new();
    Gtk4Control* c = controlNew(CXUI_PEER_Progress, bar, NULL, host);
    if (!c) {
        g_object_ref_sink(bar);
        g_object_unref(bar);
        return NULL;
    }
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(bar), 0.1);
    return &c->base;
}

void _cxuiGtk4ProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (indeterminate && !c->pulse) {
        c->pulse = g_timeout_add(100, pulseTick, c);
    } else if (!indeterminate) {
        if (c->pulse)
            g_source_remove(c->pulse);
        c->pulse = 0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(c->widget), fraction);
    }
}

CxUiSize _cxuiGtk4ProgressMeasure(CxUiWidget* w, int32 length)
{
    if (!w->peer)
        return (CxUiSize){ 0 };
    return (CxUiSize){ length, widgetMeasure(((Gtk4Control*)w->peer)->widget).h };
}

// ---- tabs ----

// The notebook's pages are empty placeholders; the tabs' own pages are on a surface over the
// notebook's page area. A placeholder tells its tabs where that area is once it is allocated.
typedef struct CxUiGtkTabPage {
    GtkWidget parent_instance;
    Gtk4Control* tabs;   // NULL once the peer is gone
} CxUiGtkTabPage;

typedef struct CxUiGtkTabPageClass {
    GtkWidgetClass parent_class;
} CxUiGtkTabPageClass;

G_DEFINE_TYPE(CxUiGtkTabPage, cxui_gtk_tab_page, GTK_TYPE_WIDGET)

static gboolean tabsInsetsChanged(gpointer ud)
{
    Gtk4Control* c = ud;
    c->insetIdle   = 0;
    if (c->base.w) {
        _cxuiNativeEnter();
        _cxuiLayoutDirty(c->base.w);
        _cxuiNativeLeave();
    }
    return G_SOURCE_REMOVE;
}

// The room around the area, in logical pixels, which the core lays the pages out in
static void tabPageAllocate(GtkWidget* w, int width, int height, int baseline)
{
    unused_noeval(baseline);
    Gtk4Control* c = ((CxUiGtkTabPage*)w)->tabs;
    graphene_rect_t r;
    if (!c || !gtk_widget_compute_bounds(w, c->notebook, &r))
        return;
    int nw = gtk_widget_get_width(c->notebook), nh = gtk_widget_get_height(c->notebook);
    CxUiEdges in = { (int32)ceilf(r.origin.x), (int32)ceilf(r.origin.y), (int32)ceilf(nw - r.origin.x - width),
                     (int32)ceilf(nh - r.origin.y - height) };
    in           = (CxUiEdges){ max(in.left, 0), max(in.top, 0), max(in.right, 0), max(in.bottom, 0) };
    CxUiEdges k  = c->insets;
    if (c->insetsKnown && in.left == k.left && in.top == k.top && in.right == k.right && in.bottom == k.bottom)
        return;
    c->insets      = in;
    c->insetsKnown = true;
    if (!c->insetIdle)
        c->insetIdle = g_idle_add(tabsInsetsChanged, c);
}

static void cxui_gtk_tab_page_class_init(CxUiGtkTabPageClass* cls)
{
    GTK_WIDGET_CLASS(cls)->size_allocate = tabPageAllocate;
}

static void cxui_gtk_tab_page_init(CxUiGtkTabPage* p)
{
    unused_noeval(p);
}

static void tabsRender(Gtk4Control* c)
{
    bool marks   = showsMnemonics(c);
    string shown = 0;
    for (int32 i = 0; i < saSize(c->titles); i++) {
        GtkWidget* page  = gtk_notebook_get_nth_page(GTK_NOTEBOOK(c->notebook), i);
        GtkWidget* label = page ? gtk_notebook_get_tab_label(GTK_NOTEBOOK(c->notebook), page) : NULL;
        if (!label)
            continue;
        if (marks) {
            mnemonicMarkup(&shown, c->titles.a[i]);
            gtk_label_set_markup(GTK_LABEL(label), strEmpty(shown) ? "" : strC(shown));
        } else {
            _cxuiMnemonicStrip(&shown, c->titles.a[i]);
            gtk_label_set_text(GTK_LABEL(label), strEmpty(shown) ? "" : strC(shown));
        }
    }
    strDestroy(&shown);
}

static void onSwitchPage(GtkNotebook* nb, GtkWidget* page, guint num, gpointer ud)
{
    unused_noeval(nb);
    unused_noeval(page);
    Gtk4Control* c = ud;
    if (c->setting)
        return;
    _cxuiNativeEnter();
    _cxuiTabsPicked(&c->base, (int32)num);
    _cxuiNativeLeave();
}

CxUiPeer* _cxuiGtk4TabsCreate(CxUiTabs* t, CxUiPeer* host)
{
    unused_noeval(t);
    GtkWidget* outer = _cxuiGtk4RootNew(NULL, NULL);
    Gtk4Control* c   = controlNew(CXUI_PEER_Tabs, outer, NULL, host);
    if (!c) {
        g_object_ref_sink(outer);
        g_object_unref(outer);
        return NULL;
    }
    saInit(&c->titles, string, 4);
    c->notebook = g_object_ref_sink(gtk_notebook_new());
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(c->notebook), TRUE);
    c->page = g_object_ref_sink(_cxuiGtk4RootNew(NULL, NULL));
    _cxuiGtk4ChildAttach(c->notebook, NULL, &c->noteBounds, outer);
    _cxuiGtk4ChildAttach(c->page, NULL, &c->pageBounds, outer);
    g_signal_connect_after(c->notebook, "switch-page", G_CALLBACK(onSwitchPage), c);
    GtkEventController* keys = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "event", G_CALLBACK(onKey), c);
    gtk_widget_add_controller(c->notebook, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(onFocusIn), c);
    g_signal_connect(focus, "leave", G_CALLBACK(onFocusOut), c);
    gtk_widget_add_controller(c->notebook, focus);
    return &c->base;
}

void _cxuiGtk4TabsInsert(CxUiPeer* p, int32 at, strref title)
{
    Gtk4Control* c = (Gtk4Control*)p;
    saInsert(&c->titles, at, strref, title ? title : _S"");
    CxUiGtkTabPage* page = g_object_new(cxui_gtk_tab_page_get_type(), NULL);
    page->tabs           = c;
    c->setting           = true;
    gtk_notebook_insert_page(GTK_NOTEBOOK(c->notebook), GTK_WIDGET(page), gtk_label_new(NULL), at);
    c->setting = false;
    tabsRender(c);
}

void _cxuiGtk4TabsRemove(CxUiPeer* p, int32 at)
{
    Gtk4Control* c   = (Gtk4Control*)p;
    GtkWidget* page  = gtk_notebook_get_nth_page(GTK_NOTEBOOK(c->notebook), at);
    if (page)
        ((CxUiGtkTabPage*)page)->tabs = NULL;
    saRemove(&c->titles, at);
    c->setting = true;
    gtk_notebook_remove_page(GTK_NOTEBOOK(c->notebook), at);
    c->setting = false;
}

void _cxuiGtk4TabsSetTitle(CxUiPeer* p, int32 at, strref title)
{
    Gtk4Control* c = (Gtk4Control*)p;
    strDup(&c->titles.a[at], title);
    tabsRender(c);
}

void _cxuiGtk4TabsSetValue(CxUiPeer* p, int32 index)
{
    Gtk4Control* c = (Gtk4Control*)p;
    c->setting     = true;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(c->notebook), index);
    c->setting = false;
}

// The notebook fills the control, which is placed first.
void _cxuiGtk4TabsSetPage(CxUiPeer* p, CxUiRect px)
{
    Gtk4Control* c = (Gtk4Control*)p;
    c->noteBounds  = (CxUiRect){ 0, 0, c->bounds.w, c->bounds.h };
    c->pageBounds  = px;
    gtk_widget_queue_allocate(c->widget);
}

void _cxuiGtk4TabsSetColors(CxUiPeer* p, const CxUiTabsColors* colors)
{
    _cxuiGtk4RootSetColors(((Gtk4Control*)p)->page, colors->page, colors->border, 0);
}

// The room the notebook leaves around its page area: as it was allocated, or until then its
// measure with empty pages, the header at the top and a line of border around; before the
// notebook exists, an estimate from the font.
// GtkNotebook scrolls its tabs rather than wrapping them.
CxUiEdges _cxuiGtk4TabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth)
{
    unused_noeval(width);
    CxUiPeer* p = CxUiWidget(t)->peer;
    if (!p) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        float32 s = cxuiFontScale(f);
        *stripWidth = 0;
        for (int32 i = 0; i < saSize(t->_titles); i++)
            *stripWidth += (int32)ceilf(cxuiFontAdvance(f, t->_titles.a[i]) / s) + 24;
        return (CxUiEdges){ 1, (int32)ceilf(fm.height / s) + 16, 1, 1 };
    }
    Gtk4Control* c = (Gtk4Control*)p;
    int minW, natW, minH, natH;
    gtk_widget_measure(c->notebook, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &natW, NULL, NULL);
    gtk_widget_measure(c->notebook, GTK_ORIENTATION_VERTICAL, natW, &minH, &natH, NULL, NULL);
    *stripWidth = natW;
    if (c->insetsKnown)
        return c->insets;
    return (CxUiEdges){ 1, max(natH - 1, 1), 1, 1 };
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4TabsPage(CxUiPeer* p)
{
    return ((Gtk4Control*)p)->page;
}

static void tabsDestroy(Gtk4Control* c)
{
    if (c->insetIdle)
        g_source_remove(c->insetIdle);
    GtkNotebook* nb = GTK_NOTEBOOK(c->notebook);
    for (int32 i = 0, n = gtk_notebook_get_n_pages(nb); i < n; i++)
        ((CxUiGtkTabPage*)gtk_notebook_get_nth_page(nb, i))->tabs = NULL;
    g_signal_handlers_disconnect_by_data(c->notebook, c);
    _cxuiGtk4ControllersRelease(c->notebook, c);
    _cxuiGtk4ChildDetach(c->page);
    _cxuiGtk4ChildDetach(c->notebook);
    g_object_unref(c->page);
    g_object_unref(c->notebook);
    saDestroy(&c->titles);
}

// A tab picked as a click picks one
static bool tabsTestChoose(Gtk4Control* c, int32 index)
{
    if (gtk_widget_is_sensitive(c->widget) && gtk_widget_get_visible(c->widget))
        gtk_notebook_set_current_page(GTK_NOTEBOOK(c->notebook), index);
    return true;
}

static void tabsTestInfo(Gtk4Control* c, CxUiTestControlInfo* out)
{
    GtkNotebook* nb = GTK_NOTEBOOK(c->notebook);
    for (int32 i = 0, n = gtk_notebook_get_n_pages(nb); i < n; i++) {
        GtkWidget* label = gtk_notebook_get_tab_label(nb, gtk_notebook_get_nth_page(nb, i));
        strAppend(&out->items, label ? (strref)gtk_label_get_text(GTK_LABEL(label)) : NULL);
        strAppend(&out->items, _S"\n");
    }
    out->value = gtk_notebook_get_current_page(nb);
}

// ---- all of them ----

void _cxuiGtk4ControlSetBounds(CxUiPeer* p, CxUiRect px)
{
    Gtk4Control* c = (Gtk4Control*)p;
    c->bounds      = px;
    _cxuiGtk4ChildPlace(c->widget);
}

void _cxuiGtk4ControlSetFont(CxUiPeer* p, CxUiFont* f)
{
    _cxuiGtk4FontCss(&((Gtk4Control*)p)->fontCss, f);
    cssRebuild();
}

// Labels are never disabled natively: the core gives them the disabled text color.
void _cxuiGtk4PeerSetEnabled(CxUiPeer* p, bool enabled)
{
    if (_cxuiPeerIsControl(p->kind) && p->kind != CXUI_PEER_Label)
        gtk_widget_set_sensitive(((Gtk4Control*)p)->widget, enabled);
}

GtkWidget* _cxuiGtk4ControlWidget(CxUiPeer* p)
{
    return ((Gtk4Control*)p)->widget;
}

// A list's view, inside its scrolled window, takes the keys, and tabs' notebook.
GtkWidget* _cxuiGtk4ControlFocusWidget(CxUiPeer* p)
{
    Gtk4Control* c = (Gtk4Control*)p;
    return c->view ? c->view : c->notebook ? c->notebook : c->widget;
}

void _cxuiGtk4ControlDestroy(CxUiPeer* p)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (c->notebook)
        tabsDestroy(c);
    if (c->topWatch)
        g_signal_handler_disconnect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(c->widget)), c->topWatch);
    if (c->topIdle)
        g_source_remove(c->topIdle);
    if (c->window)
        g_signal_handlers_disconnect_by_func(c->window, onMnemonicsVisible, c);
    g_signal_handlers_disconnect_by_data(c->widget, c);
    _cxuiGtk4ControllersRelease(c->widget, c);
    _cxuiGtk4ChildDetach(c->widget);
    g_object_unref(c->widget);
    if (c->partner)
        g_object_unref(c->partner);
    if (c->model)
        g_object_unref(c->model);
    if (c->pulse)
        g_source_remove(c->pulse);
    int32 i = saFind(controls, ptr, c);
    if (i >= 0)
        saRemove(&controls, i);
    if (!strEmpty(c->fontCss) || !strEmpty(c->colorCss))
        cssRebuild();
    strDestroy(&c->fontCss);
    strDestroy(&c->colorCss);
    strDestroy(&c->bgCss);
    strDestroy(&c->text);
    xaFree(c);
}

void _cxuiGtk4ControlsShutdown(void)
{
    for (int32 i = 0; i < 4; i++) {
        GtkWidget** probe = &scaleProbes[i / 2][i % 2];
        if (*probe)
            g_object_unref(*probe);
        *probe = NULL;
    }
    if (css) {
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css));
        g_object_unref(css);
        css = NULL;
    }
    saDestroy(&controls);
}

// ---- tests ----

bool _cxuiGtk4TestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (c->label)
        strDup(&out->text, (strref)gtk_label_get_text(GTK_LABEL(c->label)));
    if (c->model) {
        guint n = g_list_model_get_n_items(G_LIST_MODEL(c->model));
        for (guint i = 0; i < n; i++) {
            strAppend(&out->items, (strref)gtk_string_list_get_string(c->model, i));
            strAppend(&out->items, _S"\n");
        }
        guint sel  = selectedOf(c);
        out->value = sel == GTK_INVALID_LIST_POSITION ? -1 : (int32)sel;
        if (c->view)
            out->top = _cxuiGtk4ListTopItem(p);
        if (c->multi) {
            string at = 0;
            for (guint i = 0; i < n; i++) {
                if (!gtk_selection_model_is_selected(c->select, i))
                    continue;
                strFormat(&at, _S"${int} ", stvar(int32, (int32)i));
                strAppend(&out->selection, at);
            }
            strDestroy(&at);
        }
    }
    if (p->kind == CXUI_PEER_Slider) {
        GtkAdjustment* a = gtk_range_get_adjustment(GTK_RANGE(c->widget));
        out->pos         = scalePos(c);
        out->range       = (int32)floor(gtk_adjustment_get_upper(a) + 0.5);
        out->vertical    = gtk_orientable_get_orientation(GTK_ORIENTABLE(c->widget)) == GTK_ORIENTATION_VERTICAL;
    }
    if (p->kind == CXUI_PEER_Progress) {
        out->range         = 10000;
        out->pos           = (int32)floor(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(c->widget)) * 10000 + 0.5);
        out->indeterminate = c->pulse != 0;
    }
    if (c->notebook)
        tabsTestInfo(c, out);
    out->isDefault = gtk_widget_has_css_class(c->widget, "suggested-action");
    out->enabled   = gtk_widget_get_sensitive(c->widget);
    out->visible   = gtk_widget_get_visible(c->widget);
    out->focused   = gtk_widget_has_focus(_cxuiGtk4ControlFocusWidget(p));
    out->bounds    = c->bounds;
    if (p->kind == CXUI_PEER_Check || p->kind == CXUI_PEER_Radio) {
        GtkCheckButton* b = GTK_CHECK_BUTTON(c->widget);
        out->checkState   = gtk_check_button_get_inconsistent(b) ? CXUI_CHECK_Mixed
                            : gtk_check_button_get_active(b)    ? CXUI_CHECK_On
                                                                : CXUI_CHECK_Off;
    }
    if (p->kind == CXUI_PEER_Label && gtk_label_get_wrap(GTK_LABEL(c->label))) {
        float32 s         = _cxuiGtk4WidgetScale(c->widget);
        out->neededHeight = (int32)ceilf(heightAt(c->widget, (int32)floorf(c->bounds.w / s)) * s);
    }
    return true;
}

// The way a click ends: a button's clicked signal, or a check button's activate, which a disabled
// one doesn't send.
bool _cxuiGtk4TestClick(CxUiPeer* p)
{
    if (!_cxuiPeerIsControl(p->kind) || p->kind == CXUI_PEER_Label)
        return false;
    GtkWidget* w = ((Gtk4Control*)p)->widget;
    if (gtk_widget_is_sensitive(w) && gtk_widget_get_visible(w))
        g_signal_emit_by_name(w, p->kind == CXUI_PEER_Button ? "clicked" : "activate");
    return true;
}

// A selection as the user makes one, and a double click's activate, which a disabled list doesn't
// take.
bool _cxuiGtk4TestChoose(CxUiPeer* p, int32 index, bool activate)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (p->kind == CXUI_PEER_Tabs)
        return tabsTestChoose(c, index);
    if (p->kind != CXUI_PEER_List && p->kind != CXUI_PEER_Dropdown)
        return false;
    if (!gtk_widget_is_sensitive(c->widget) || !gtk_widget_get_visible(c->widget))
        return true;
    guint pos = index < 0 ? GTK_INVALID_LIST_POSITION : (guint)index;
    if (c->multi) {
        if (index >= 0)
            gtk_selection_model_select_item(c->select, pos, TRUE);
    } else if (c->select) {
        gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(c->select), pos);
    }
    else
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c->widget), pos);
    if (activate && c->view && index >= 0)
        g_signal_emit_by_name(c->view, "activate", (guint)index);
    return true;
}

// A Ctrl-click: the item's selection changes, the others' don't
bool _cxuiGtk4TestToggle(CxUiPeer* p, int32 index)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (p->kind != CXUI_PEER_List || !c->multi)
        return false;
    if (!gtk_widget_is_sensitive(c->widget) || !gtk_widget_get_visible(c->widget))
        return true;
    if (gtk_selection_model_is_selected(c->select, (guint)index))
        gtk_selection_model_unselect_item(c->select, (guint)index);
    else
        gtk_selection_model_select_item(c->select, (guint)index, FALSE);
    return true;
}

// A drag to a position as a pointer gives one, a press then moves, letting go with release; a
// disabled slider takes none of it.
bool _cxuiGtk4TestSlide(CxUiPeer* p, int32 pos, bool release)
{
    Gtk4Control* c = (Gtk4Control*)p;
    if (p->kind != CXUI_PEER_Slider)
        return false;
    if (!gtk_widget_is_sensitive(c->widget) || !gtk_widget_get_visible(c->widget))
        return true;
    c->dragging = true;
    gtk_range_set_value(GTK_RANGE(c->widget), pos);
    if (release) {
        c->dragging = false;
        _cxuiSliderMoved(&c->base, scalePos(c), true);
    }
    return true;
}

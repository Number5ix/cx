#include "gtk4_ui.h"

#include <math.h>

#include <cx/format.h>

// A text field is a GtkEntry, or a bare GtkText inside a CxUiTextFrame, which draws the border and
// background; a text area a GtkTextView in a GtkScrolledWindow, framed by GTK or by the
// CxUiTextFrame. Fonts and a framed field's colors go through one CSS provider, with a class for each
// field. Keys go to the core first, from a capture-phase controller, so a handler can take any of
// them before GTK's own handling and focus navigation.

// An area's margins around its text, in logical pixels
#define AREA_MARGIN_X 6
#define AREA_MARGIN_Y 4

typedef struct Gtk4Text {
    CxUiPeer base;
    GtkWidget* widget;   // the GtkEntry, GtkText or an area's GtkScrolledWindow, held by a reference of our own
    GtkWidget* text;     // the GtkText: the widget itself, or the entry's; an area's GtkTextView
    GtkTextBuffer* buffer;   // an area's
    GtkTextMark* end;        // an area's end, which stays there as text is added
    int32 maxLength;         // an area's
    bool area;
    bool limiting;           // an area is inserting what fits under its limit
    // An area scrolled to its end stays there as GTK works out the height of its lines, until the
    // user scrolls away
    bool stickEnd;
    bool scrolling;          // the core is moving the area's scroll position
    CxUiRect bounds;     // physical pixels, within the parent
    uint32 id;           // its CSS class: cxui-t<id>
    string fontCss;      // declarations for the field's font
    string colorCss;     // rules for a framed field's colors
} Gtk4Text;

static GtkCssProvider* css;
static sa_ptr fields;   // Gtk4Text*, for rebuilding the CSS
static uint32 nextId;

static GtkEditable* editable(Gtk4Text* t)
{
    return GTK_EDITABLE(t->widget);
}

// An area's text, owned by the caller
static char* areaText(Gtk4Text* t)
{
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(t->buffer, &a, &b);
    return gtk_text_buffer_get_text(t->buffer, &a, &b, FALSE);
}

static void areaChanged(GtkTextBuffer* b, gpointer ud)
{
    unused_noeval(b);
    Gtk4Text* t = ud;
    char* text  = areaText(t);
    _cxuiNativeEnter();
    _cxuiTextChanged(&t->base, (strref)text);
    _cxuiNativeLeave();
    g_free(text);
}

static GtkAdjustment* areaAdjustment(Gtk4Text* t)
{
    return gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->widget));
}

static void areaToEnd(Gtk4Text* t)
{
    GtkAdjustment* v = areaAdjustment(t);
    t->scrolling     = true;
    gtk_adjustment_set_value(v, gtk_adjustment_get_upper(v) - gtk_adjustment_get_page_size(v));
    t->scrolling = false;
}

// The height of the lines, or of the view, changed.
static void areaResized(GtkAdjustment* v, gpointer ud)
{
    unused_noeval(v);
    Gtk4Text* t = ud;
    if (t->stickEnd)
        areaToEnd(t);
}

static void areaScrolled(GtkAdjustment* v, gpointer ud)
{
    Gtk4Text* t = ud;
    if (!t->scrolling && gtk_adjustment_get_value(v) + gtk_adjustment_get_page_size(v) < gtk_adjustment_get_upper(v) - 1)
        t->stickEnd = false;
}

// The length limit: an insertion that would pass it keeps as much as fits.
static void areaInsert(GtkTextBuffer* b, GtkTextIter* at, const char* text, int len, gpointer ud)
{
    Gtk4Text* t = ud;
    if (t->limiting || t->maxLength <= 0)
        return;
    int32 room  = max(t->maxLength - gtk_text_buffer_get_char_count(b), 0);
    glong chars = g_utf8_strlen(text, len);
    if (chars <= room)
        return;
    g_signal_stop_emission_by_name(b, "insert-text");
    if (room > 0) {
        const char* cut = g_utf8_offset_to_pointer(text, room);
        t->limiting     = true;
        gtk_text_buffer_insert(b, at, text, (int)(cut - text));
        t->limiting = false;
    }
}

static bool isEntry(Gtk4Text* t)
{
    return t->widget != t->text;
}

_Use_decl_annotations_
void _cxuiGtk4CssColor(string* out, CxUiColor c)
{
    string part = 0;
    strFormat(&part, _S"rgba(${int}, ${int}, ${int}, ${float})", stvar(int32, c.r), stvar(int32, c.g),
              stvar(int32, c.b), stvar(float64, c.a / 255.0));
    strAppend(out, part);
    strDestroy(&part);
}

static void cssRebuild(void)
{
    if (!css) {
        css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    string all = 0, rule = 0;
    for (int32 i = 0; i < saSize(fields); i++) {
        Gtk4Text* t = fields.a[i];
        if (!strEmpty(t->fontCss)) {
            strFormat(&rule, _S".cxui-t${uint} { ${string} }\n", stvar(uint32, t->id), stvar(strref, t->fontCss));
            strAppend(&all, rule);
        }
        strAppend(&all, t->colorCss);
    }
    gtk_css_provider_load_from_string(css, strEmpty(all) ? "" : strC(all));
    strDestroy(&all);
    strDestroy(&rule);
}

// Text set from code is reported too, and the core ignores it: the field has it already.
static void onChanged(GtkEditable* e, gpointer ud)
{
    unused_noeval(e);
    Gtk4Text* t = ud;
    _cxuiNativeEnter();
    _cxuiTextChanged(&t->base, (strref)gtk_editable_get_text(editable(t)));
    _cxuiNativeLeave();
}

static gboolean onKey(GtkEventControllerLegacy* ctl, GdkEvent* ev, gpointer ud)
{
    unused_noeval(ctl);
    return _cxuiGtk4Key(&((Gtk4Text*)ud)->base, ev);
}

static void onFocusIn(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Text*)ud)->base, true);
    _cxuiNativeLeave();
}

static void onFocusOut(GtkEventControllerFocus* ctl, gpointer ud)
{
    unused_noeval(ctl);
    _cxuiNativeEnter();
    _cxuiFocusChanged(&((Gtk4Text*)ud)->base, false);
    _cxuiNativeLeave();
}

CxUiPeer* _cxuiGtk4TextCreate(CxUiTextField* tf, CxUiPeer* host, bool framed)
{
    unused_noeval(tf);
    GtkWidget* parent = _cxuiGtk4HostWidget(host);
    if (!parent)
        return NULL;
    Gtk4Text* t  = xaAllocStruct(Gtk4Text, XA_Zero);
    t->base.kind = CXUI_PEER_Text;
    t->id        = ++nextId;
    CxUiTextArea* area = objDynCast(CxUiTextArea, tf);
    if (area) {
        bool wrap = cxuiTextAreaWrap(area);
        t->area   = true;
        t->widget = g_object_ref_sink(gtk_scrolled_window_new());
        t->text   = gtk_text_view_new();
        t->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->text));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(t->buffer, &end);
        t->end = gtk_text_buffer_create_mark(t->buffer, NULL, &end, FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(t->text), wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
        // Room around the text, as an entry has
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(t->text), AREA_MARGIN_X);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(t->text), AREA_MARGIN_X);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(t->text), AREA_MARGIN_Y);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(t->text), AREA_MARGIN_Y);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(t->widget), wrap ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(t->widget), !framed);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(t->widget), t->text);
    } else {
        t->widget = g_object_ref_sink(framed ? gtk_text_new() : gtk_entry_new());
        t->text   = framed ? t->widget : GTK_WIDGET(gtk_editable_get_delegate(GTK_EDITABLE(t->widget)));
    }

    char cls[32];
    snprintf(cls, sizeof(cls), "cxui-t%u", t->id);
    gtk_widget_add_css_class(t->widget, cls);
    if (!saValid(fields))
        saInit(&fields, ptr, 8);
    saPush(&fields, ptr, t);

    if (t->area) {
        g_signal_connect(t->buffer, "changed", G_CALLBACK(areaChanged), t);
        g_signal_connect(t->buffer, "insert-text", G_CALLBACK(areaInsert), t);
        g_signal_connect(areaAdjustment(t), "changed", G_CALLBACK(areaResized), t);
        g_signal_connect(areaAdjustment(t), "value-changed", G_CALLBACK(areaScrolled), t);
    } else {
        g_signal_connect(t->widget, "changed", G_CALLBACK(onChanged), t);
    }
    GtkEventController* keys = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "event", G_CALLBACK(onKey), t);
    gtk_widget_add_controller(t->widget, keys);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(onFocusIn), t);
    g_signal_connect(focus, "leave", G_CALLBACK(onFocusOut), t);
    gtk_widget_add_controller(t->widget, focus);
    _cxuiGtk4ChildAttach(t->widget, &t->base, &t->bounds, parent);
    return &t->base;
}

void _cxuiGtk4TextDestroy(CxUiPeer* p)
{
    Gtk4Text* t = (Gtk4Text*)p;
    g_signal_handlers_disconnect_by_data(t->widget, t);
    if (t->buffer) {
        g_signal_handlers_disconnect_by_data(t->buffer, t);
        g_signal_handlers_disconnect_by_data(areaAdjustment(t), t);
    }
    _cxuiGtk4ControllersRelease(t->widget, t);
    _cxuiGtk4ChildDetach(t->widget);
    g_object_unref(t->widget);
    int32 i = saFind(fields, ptr, t);
    if (i >= 0)
        saRemove(&fields, i);
    if (!strEmpty(t->fontCss) || !strEmpty(t->colorCss))
        cssRebuild();
    strDestroy(&t->fontCss);
    strDestroy(&t->colorCss);
    xaFree(t);
}

void _cxuiGtk4TextShutdown(void)
{
    if (css) {
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css));
        g_object_unref(css);
        css = NULL;
    }
    saDestroy(&fields);
}

_Use_decl_annotations_
GtkWidget* _cxuiGtk4TextWidget(CxUiPeer* p)
{
    return ((Gtk4Text*)p)->widget;
}

// An area's text view takes the keys, inside its scrolled window.
_Use_decl_annotations_
GtkWidget* _cxuiGtk4TextFocusWidget(CxUiPeer* p)
{
    Gtk4Text* t = (Gtk4Text*)p;
    return t->area ? t->text : t->widget;
}

void _cxuiGtk4TextSetBounds(CxUiPeer* p, CxUiRect px)
{
    Gtk4Text* t = (Gtk4Text*)p;
    t->bounds   = px;
    _cxuiGtk4ChildPlace(t->widget);
}

// The widget's own measure, with its font and theme; DIPs are GTK's logical pixels.
CxUiSize _cxuiGtk4TextMeasure(CxUiTextField* tf, CxUiFont* f, int32 columns, bool framed)
{
    unused_noeval(f);
    unused_noeval(framed);
    CxUiPeer* p = CxUiWidget(tf)->peer;
    if (!p)
        return (CxUiSize){ 0 };
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area)
        return (CxUiSize){ 0 };
    gtk_editable_set_width_chars(editable(t), columns);
    int minW, natW, minH, natH;
    gtk_widget_measure(t->widget, GTK_ORIENTATION_HORIZONTAL, -1, &minW, &natW, NULL, NULL);
    gtk_widget_measure(t->widget, GTK_ORIENTATION_VERTICAL, -1, &minH, &natH, NULL, NULL);
    return (CxUiSize){ natW, natH };
}

// Lines of the font, with the view's margins and the frame GTK draws; columns of its average
// character, beside room for a scroll bar.
CxUiSize _cxuiGtk4TextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed)
{
    unused_noeval(ta);
    CxUiFontMetrics m;
    cxuiFontMetrics(f, &m);
    float32 s    = cxuiFontScale(f);
    int32 chrome = framed ? 0 : 2;
    return (CxUiSize){ (int32)ceilf(columns * m.avgCharW / s) + 2 * AREA_MARGIN_X + 12 + chrome,
                       (int32)ceilf(lines * m.height / s) + 2 * AREA_MARGIN_Y + chrome };
}

void _cxuiGtk4TextSetText(CxUiPeer* p, strref text)
{
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area)
        gtk_text_buffer_set_text(t->buffer, strEmpty(text) ? "" : strC(text), -1);
    else
        gtk_editable_set_text(editable(t), strEmpty(text) ? "" : strC(text));
}

// The selection is kept, as appending moves neither of its ends.
void _cxuiGtk4TextAppend(CxUiPeer* p, strref text)
{
    Gtk4Text* t = (Gtk4Text*)p;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(t->buffer, &end);
    int32 limit  = t->maxLength;
    t->maxLength = 0;
    gtk_text_buffer_insert(t->buffer, &end, strC(text), strLen(text));
    t->maxLength = limit;
}

void _cxuiGtk4TextScrollToEnd(CxUiPeer* p)
{
    Gtk4Text* t = (Gtk4Text*)p;
    t->stickEnd = true;
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(t->text), t->end, 0, FALSE, 0, 0);
    areaToEnd(t);
}

void _cxuiGtk4TextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret)
{
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area) {
        GtkTextIter a, b, ins;
        gtk_text_buffer_get_iter_at_mark(t->buffer, &ins, gtk_text_buffer_get_insert(t->buffer));
        *caret = gtk_text_iter_get_offset(&ins);
        if (!gtk_text_buffer_get_selection_bounds(t->buffer, &a, &b))
            *start = *end = *caret;
        else
            *start = gtk_text_iter_get_offset(&a), *end = gtk_text_iter_get_offset(&b);
        return;
    }
    int s = 0, e = 0;
    *caret = gtk_editable_get_position(editable(t));
    if (!gtk_editable_get_selection_bounds(editable(t), &s, &e))
        s = e = *caret;
    *start = min(s, e);
    *end   = max(s, e);
}

void _cxuiGtk4TextSetSelection(CxUiPeer* p, int32 start, int32 end)
{
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area) {
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(t->buffer, &a, start);
        gtk_text_buffer_get_iter_at_offset(t->buffer, &b, end);
        gtk_text_buffer_select_range(t->buffer, &b, &a);
        t->stickEnd = false;
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(t->text), gtk_text_buffer_get_insert(t->buffer));
        return;
    }
    gtk_editable_select_region(editable(t), start, end);
}

// An area has none.
void _cxuiGtk4TextSetCue(CxUiPeer* p, strref cue)
{
    Gtk4Text* t    = (Gtk4Text*)p;
    if (t->area)
        return;
    const char* s  = strEmpty(cue) ? NULL : strC(cue);
    if (isEntry(t))
        gtk_entry_set_placeholder_text(GTK_ENTRY(t->widget), s);
    else
        gtk_text_set_placeholder_text(GTK_TEXT(t->widget), s);
}

void _cxuiGtk4TextSetMaxLength(CxUiPeer* p, int32 maxLength)
{
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area)
        t->maxLength = maxLength;
    else if (isEntry(t))
        gtk_entry_set_max_length(GTK_ENTRY(t->widget), maxLength);
    else
        gtk_text_set_max_length(GTK_TEXT(t->widget), maxLength);
}

// CSS sizes are in logical pixels; the font was made for the window's scale.
_Use_decl_annotations_
void _cxuiGtk4FontCss(string* out, CxUiFont* f)
{
    PangoFontDescription* d = _cxuiGtk4FontDescription(f);
    string families = 0, fam = 0;
    strref list     = (strref)pango_font_description_get_family(d);
    int32 len       = strLen(list);
    for (int32 start = 0; start < len;) {
        int32 end = strFindChar(list, start, ',');
        if (end < 0)
            end = len;
        strSubStr(&fam, list, start, end);
        strTrim(&fam, fam, 0);
        start = end + 1;
        if (strEmpty(fam))
            continue;
        strAppend(&families, strEmpty(families) ? _S"\"" : _S", \"");
        strAppend(&families, fam);
        strAppend(&families, _S"\"");
    }
    float64 px = pango_font_description_get_size(d) / (float64)PANGO_SCALE / cxuiFontScale(f);
    strFormat(out, _S"font-family: ${string}; font-size: ${float(0.2)}px; font-weight: ${int}; font-style: ${string};",
              stvar(strref, families), stvar(float64, px), stvar(int32, (int32)pango_font_description_get_weight(d)),
              stvar(strref, pango_font_description_get_style(d) == PANGO_STYLE_ITALIC ? _S"italic" : _S"normal"));
    pango_font_description_free(d);
    strDestroy(&families);
    strDestroy(&fam);
}

void _cxuiGtk4TextSetFont(CxUiPeer* p, CxUiFont* f)
{
    _cxuiGtk4FontCss(&((Gtk4Text*)p)->fontCss, f);
    cssRebuild();
}

void _cxuiGtk4TextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable)
{
    Gtk4Text* t = (Gtk4Text*)p;
    if (t->area)
        gtk_text_view_set_editable(GTK_TEXT_VIEW(t->text), !readOnly);
    else
        gtk_editable_set_editable(editable(t), !readOnly);
    gtk_widget_set_sensitive(t->widget, enabled);
    gtk_widget_set_focusable(t->text, focusable);
}

void _cxuiGtk4TextSetColors(CxUiPeer* p, const CxUiTextColors* c)
{
    Gtk4Text* t = (Gtk4Text*)p;
    strClear(&t->colorCss);
    if (c) {
        string sel = 0;
        strFormat(&sel, t->area ? _S".cxui-t${uint} textview, .cxui-t${uint} textview text" : _S".cxui-t${uint}",
                  stvar(uint32, t->id), stvar(uint32, t->id));
        strAppend(&t->colorCss, sel);
        strAppend(&t->colorCss, _S" { background: none; color: ");
        _cxuiGtk4CssColor(&t->colorCss, c->text);
        strAppend(&t->colorCss, _S"; caret-color: ");
        _cxuiGtk4CssColor(&t->colorCss, c->text);
        strAppend(&t->colorCss, _S"; }\n");
        strAppend(&t->colorCss, sel);
        strAppend(&t->colorCss, _S" > placeholder { color: ");
        _cxuiGtk4CssColor(&t->colorCss, c->cue);
        strAppend(&t->colorCss, _S"; }\n");
        strAppend(&t->colorCss, sel);
        strAppend(&t->colorCss, _S" > selection { background-color: ");
        _cxuiGtk4CssColor(&t->colorCss, c->selection);
        strAppend(&t->colorCss, _S"; color: ");
        _cxuiGtk4CssColor(&t->colorCss, c->selectionText);
        strAppend(&t->colorCss, _S"; }\n");
        strDestroy(&sel);
    }
    cssRebuild();
}

// ---- tests ----

bool _cxuiGtk4TestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out)
{
    Gtk4Text* t = (Gtk4Text*)p;
    _cxuiGtk4TextGetSelection(p, &out->selStart, &out->selEnd, &out->caret);
    if (t->area) {
        char* text = areaText(t);
        strDup(&out->text, (strref)text);
        g_free(text);
        GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->widget));
        out->multiline   = true;
        out->wrap        = gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(t->text)) != GTK_WRAP_NONE;
        out->atEnd       = gtk_adjustment_get_value(v) + gtk_adjustment_get_page_size(v) >= gtk_adjustment_get_upper(v) - 1;
        out->maxLength   = t->maxLength;
        out->readOnly    = !gtk_text_view_get_editable(GTK_TEXT_VIEW(t->text));
    } else {
        strDup(&out->text, (strref)gtk_editable_get_text(editable(t)));
        const char* cue = isEntry(t) ? gtk_entry_get_placeholder_text(GTK_ENTRY(t->widget))
                                     : gtk_text_get_placeholder_text(GTK_TEXT(t->widget));
        strDup(&out->cue, (strref)cue);
        out->maxLength = isEntry(t) ? gtk_entry_get_max_length(GTK_ENTRY(t->widget)) : gtk_text_get_max_length(GTK_TEXT(t->widget));
        out->readOnly  = !gtk_editable_get_editable(editable(t));
    }
    out->enabled   = gtk_widget_get_sensitive(t->widget);
    out->focusable = gtk_widget_get_focusable(t->text);
    out->focused   = gtk_widget_has_focus(t->text);
    out->framed    = t->area ? !gtk_scrolled_window_get_has_frame(GTK_SCROLLED_WINDOW(t->widget)) : !isEntry(t);
    out->visible   = gtk_widget_get_visible(t->widget);
    out->hasColors = !strEmpty(t->colorCss);
    out->bounds    = t->bounds;
    return true;
}

// The selection is replaced as a key press would replace it, which a read-only field refuses.
bool _cxuiGtk4TestType(CxUiPeer* p, strref s)
{
    Gtk4Text* t   = (Gtk4Text*)p;
    if (t->area) {
        bool can = gtk_text_view_get_editable(GTK_TEXT_VIEW(t->text)) && gtk_widget_get_sensitive(t->widget);
        if (can) {
            gtk_text_buffer_delete_selection(t->buffer, TRUE, TRUE);
            gtk_text_buffer_insert_interactive_at_cursor(t->buffer, strEmpty(s) ? "" : strC(s), strLen(s), TRUE);
        }
        return true;
    }
    GtkEditable* e = editable(t);
    if (!gtk_editable_get_editable(e) || !gtk_widget_get_sensitive(t->widget))
        return true;
    gtk_editable_delete_selection(e);
    int pos = gtk_editable_get_position(e);
    if (!strEmpty(s))
        gtk_editable_insert_text(e, strC(s), strLen(s), &pos);
    gtk_editable_set_position(e, pos);
    return true;
}

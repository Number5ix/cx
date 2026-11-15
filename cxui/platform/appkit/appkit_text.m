#include "appkit_ui.h"

// A text field is an NSTextField inside its host's view. While it has focus, the window's field
// editor (one NSTextView a window shares between its fields) does the editing, so the editor's
// colors are set each time a field begins. Keys reach cxui before the editor and the menu bar,
// through an event monitor; the field's delegate then keeps Enter, Tab and Esc from the editor,
// as cxui has acted on them already.
//
// A text area is an NSTextView in an NSScrollView, editing its own text, with a delegate of its
// own; each operation below hands an area to its own code.

typedef struct AppKitText AppKitText;

@interface CxUiTextFieldView : NSTextField <NSTextFieldDelegate> {
  @public
    AppKitText* peer;
    NSString* cue;
}
@end

@class CxUiTextAreaView;

struct AppKitText {
    CxUiPeer base;
    void* view;   // CxUiTextFieldView, or an area's NSScrollView, retained
    void* areaDelegate;   // an area's CxUiTextAreaDelegate, retained
    bool area;
    bool toEnd;           // an area to scroll to its end once it has a size
    CxUiRect bounds;
    int32 maxLength;
    // Code points; while the field is being edited, the field editor has the selection
    int32 selStart, selEnd;
    bool framed;
    bool hasColors;
    CxUiTextColors colors;
};

static CxUiTextFieldView* fieldView(CxUiPeer* p)
{
    return (__bridge CxUiTextFieldView*)((AppKitText*)p)->view;
}

// ---- code points ----

static bool lowHalf(NSString* s, NSUInteger i)
{
    return i > 0 && CFStringIsSurrogateLowCharacter([s characterAtIndex:i]) &&
           CFStringIsSurrogateHighCharacter([s characterAtIndex:i - 1]);
}

// Code points in the first units UTF-16 units of s
static int32 cpCount(NSString* s, NSUInteger units)
{
    int32 n = 0;
    for (NSUInteger i = 0; i < units && i < s.length; i++) {
        if (!lowHalf(s, i))
            n++;
    }
    return n;
}

// The UTF-16 offset of code point cp, clamped to the end
static NSUInteger cpUnit(NSString* s, int32 cp)
{
    NSUInteger i = 0;
    for (int32 n = 0; i < s.length; i++) {
        if (!lowHalf(s, i) && n++ == cp)
            return i;
    }
    return s.length;
}

// ---- the view ----

static NSTextView* editorOf(NSTextField* v)
{
    NSText* ed = v.currentEditor;
    return [ed isKindOfClass:NSTextView.class] ? (NSTextView*)ed : nil;
}

static bool editing(NSTextField* v)
{
    NSText* ed = v.currentEditor;
    return ed && v.window.firstResponder == ed;
}

static void styleEditor(CxUiTextFieldView* v)
{
    NSTextView* ed = editorOf(v);
    AppKitText* t  = v->peer;
    if (!ed || !t)
        return;
    ed.allowsUndo = YES;
    if (t->hasColors) {
        ed.insertionPointColor    = _cxuiAppKitColor(t->colors.text);
        ed.selectedTextAttributes = @{
            NSBackgroundColorAttributeName : _cxuiAppKitColor(t->colors.selection),
            NSForegroundColorAttributeName : _cxuiAppKitColor(t->colors.selectionText),
        };
    } else {
        ed.insertionPointColor    = NSColor.textColor;
        ed.selectedTextAttributes = @{
            NSBackgroundColorAttributeName : NSColor.selectedTextBackgroundColor,
            NSForegroundColorAttributeName : NSColor.selectedTextColor,
        };
    }
}

static void reportText(CxUiTextFieldView* v)
{
    if (!v->peer)
        return;
    _cxuiNativeEnter();
    _cxuiTextChanged(&v->peer->base, (strref)v.stringValue.UTF8String);
    _cxuiNativeLeave();
}

static void reportFocus(CxUiTextFieldView* v, bool gained)
{
    if (!v->peer)
        return;
    _cxuiNativeEnter();
    _cxuiFocusChanged(&v->peer->base, gained);
    _cxuiNativeLeave();
}

typedef BOOL (*ShouldChangeFn)(id, SEL, NSTextView*, NSRange, NSString*);

@implementation CxUiTextFieldView
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    styleEditor(self);
    reportFocus(self, true);
    return YES;
}
- (void)textDidEndEditing:(NSNotification*)n
{
    NSTextView* ed = n.object;
    if (peer && [ed isKindOfClass:NSTextView.class]) {
        NSRange r      = ed.selectedRange;
        peer->selStart = cpCount(ed.string, r.location);
        peer->selEnd   = cpCount(ed.string, NSMaxRange(r));
    }
    [super textDidEndEditing:n];
    reportFocus(self, false);
}
- (void)textDidChange:(NSNotification*)n
{
    [super textDidChange:n];
    reportText(self);
}
// The length limit: an edit that would pass it keeps as much as fits.
- (BOOL)textView:(NSTextView*)tv shouldChangeTextInRange:(NSRange)r replacementString:(NSString*)s
{
    if (s && peer && peer->maxLength > 0) {
        NSString* cur = tv.string;
        int32 kept    = cpCount(cur, cur.length) - (cpCount(cur, NSMaxRange(r)) - cpCount(cur, r.location));
        int32 room    = max(peer->maxLength - kept, 0);
        if (cpCount(s, s.length) > room) {
            NSString* cut = [s substringToIndex:cpUnit(s, room)];
            if (cut.length > 0) {
                [tv replaceCharactersInRange:r withString:cut];
                tv.selectedRange = NSMakeRange(r.location + cut.length, 0);
                [tv didChangeText];
            }
            return NO;
        }
    }
    IMP super_ = [NSTextField instanceMethodForSelector:_cmd];
    return super_ ? ((ShouldChangeFn)super_)(self, _cmd, tv, r, s) : YES;
}
// cxui has seen these keys already: Enter commits, Tab moves the focus, and a single-line field
// has no use for a line break, a tab or Esc's completion list.
- (BOOL)control:(NSControl*)c textView:(NSTextView*)tv doCommandBySelector:(SEL)sel
{
    return sel == @selector(insertNewline:) || sel == @selector(insertNewlineIgnoringFieldEditor:) ||
           sel == @selector(insertTab:) || sel == @selector(insertBacktab:) ||
           sel == @selector(insertTabIgnoringFieldEditor:) || sel == @selector(cancelOperation:) ||
           sel == @selector(complete:);
}
@end

static void configure(NSTextField* v, bool framed)
{
    v.bezeled         = !framed;
    v.bordered        = NO;
    v.drawsBackground = !framed;
    v.editable        = YES;
    v.selectable      = YES;
    v.usesSingleLineMode = YES;
    v.lineBreakMode      = NSLineBreakByClipping;
    v.cell.scrollable    = YES;
    v.cell.wraps         = NO;
    if (framed)
        v.focusRingType = NSFocusRingTypeNone;
}

static NSFont* fontFor(CxUiFont* f)
{
    return (__bridge_transfer NSFont*)_cxuiAppKitFontAtPoints(f);
}

// A framed field's cue takes the frame's cue color, which only an attributed placeholder can.
static void applyCue(AppKitText* t)
{
    CxUiTextFieldView* v = fieldView(&t->base);
    if (!v->cue.length) {
        v.placeholderString = nil;
        return;
    }
    if (!t->hasColors) {
        v.placeholderString = v->cue;
        return;
    }
    NSMutableDictionary* a        = [NSMutableDictionary dictionary];
    a[NSForegroundColorAttributeName] = _cxuiAppKitColor(t->colors.cue);
    if (v.font)
        a[NSFontAttributeName] = v.font;
    v.placeholderAttributedString = [[NSAttributedString alloc] initWithString:v->cue attributes:a];
}

// ---- text areas ----

@interface CxUiTextAreaView : NSTextView {
  @public
    AppKitText* peer;
    bool focusable;
}
@end

@interface CxUiTextAreaDelegate : NSObject <NSTextViewDelegate> {
  @public
    AppKitText* peer;
}
@end

static NSScrollView* areaScroll(CxUiPeer* p)
{
    return (__bridge NSScrollView*)((AppKitText*)p)->view;
}

static CxUiTextAreaView* areaView(CxUiPeer* p)
{
    return (CxUiTextAreaView*)areaScroll(p).documentView;
}

static void areaFocus(CxUiTextAreaView* v, bool gained)
{
    if (!v->peer)
        return;
    _cxuiNativeEnter();
    _cxuiFocusChanged(&v->peer->base, gained);
    _cxuiNativeLeave();
}

@implementation CxUiTextAreaView
- (BOOL)acceptsFirstResponder
{
    return focusable && [super acceptsFirstResponder];
}
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    areaFocus(self, true);
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    areaFocus(self, false);
    return YES;
}
@end

@implementation CxUiTextAreaDelegate
- (void)textDidChange:(NSNotification*)n
{
    if (!peer)
        return;
    _cxuiNativeEnter();
    _cxuiTextChanged(&peer->base, (strref)((NSTextView*)n.object).string.UTF8String);
    _cxuiNativeLeave();
}
// The length limit: an edit that would pass it keeps as much as fits.
- (BOOL)textView:(NSTextView*)tv shouldChangeTextInRange:(NSRange)r replacementString:(NSString*)s
{
    if (!s || !peer || peer->maxLength <= 0)
        return YES;
    NSString* cur = tv.string;
    int32 kept    = cpCount(cur, cur.length) - (cpCount(cur, NSMaxRange(r)) - cpCount(cur, r.location));
    int32 room    = max(peer->maxLength - kept, 0);
    if (cpCount(s, s.length) <= room)
        return YES;
    NSString* cut = [s substringToIndex:cpUnit(s, room)];
    if (cut.length > 0 && [tv shouldChangeTextInRange:r replacementString:nil]) {
        [tv.textStorage replaceCharactersInRange:r withString:cut];
        tv.selectedRange = NSMakeRange(r.location + cut.length, 0);
        [tv didChangeText];
    }
    return NO;
}
// cxui has seen Tab and Esc already: Tab moves the focus, and an area has no use for Esc's
// completion list.
- (BOOL)textView:(NSTextView*)tv doCommandBySelector:(SEL)sel
{
    return sel == @selector(insertTab:) || sel == @selector(insertBacktab:) ||
           sel == @selector(insertTabIgnoringFieldEditor:) || sel == @selector(cancelOperation:) ||
           sel == @selector(complete:);
}
@end

static CxUiPeer* areaCreate(AppKitText* t, NSView* parent, bool wrap, bool framed)
{
    NSSize big             = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    NSScrollView* sv       = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sv.hasVerticalScroller = YES;
    sv.hasHorizontalScroller = !wrap;
    sv.autohidesScrollers  = YES;
    sv.borderType          = framed ? NSNoBorder : NSBezelBorder;
    sv.drawsBackground     = !framed;
    CxUiTextAreaView* tv   = [[CxUiTextAreaView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    tv->peer               = t;
    tv->focusable          = true;
    tv.richText            = NO;
    tv.importsGraphics     = NO;
    tv.allowsUndo          = YES;
    tv.automaticQuoteSubstitutionEnabled  = NO;
    tv.automaticDashSubstitutionEnabled   = NO;
    tv.automaticTextReplacementEnabled    = NO;
    tv.automaticSpellingCorrectionEnabled = NO;
    tv.minSize             = NSZeroSize;
    tv.maxSize             = big;
    tv.verticallyResizable = YES;
    tv.autoresizingMask    = NSViewWidthSizable;
    tv.drawsBackground     = !framed;
    if (!wrap) {
        tv.horizontallyResizable              = YES;
        tv.textContainer.widthTracksTextView  = NO;
        tv.textContainer.containerSize        = big;
    }
    CxUiTextAreaDelegate* d = [CxUiTextAreaDelegate new];
    d->peer                 = t;
    tv.delegate             = d;
    sv.documentView         = tv;
    t->area                 = true;
    t->view                 = (__bridge_retained void*)sv;
    t->areaDelegate         = (__bridge_retained void*)d;
    [parent addSubview:sv];
    return &t->base;
}

static void areaDestroy(AppKitText* t)
{
    NSScrollView* sv        = CFBridgingRelease(t->view);
    CxUiTextAreaDelegate* d = CFBridgingRelease(t->areaDelegate);
    CxUiTextAreaView* tv    = (CxUiTextAreaView*)sv.documentView;
    tv->peer                = NULL;
    d->peer                 = NULL;
    if (tv.window.firstResponder == tv)
        [tv.window makeFirstResponder:nil];
    tv.delegate = nil;
    [sv removeFromSuperview];
    // AppKit may still be calling into them from further up the stack.
    __autoreleasing id keep  = sv;
    __autoreleasing id keepD = d;
    (void)keep;
    (void)keepD;
    xaFree(t);
}

static void areaScrollEnd(AppKitText* t)
{
    CxUiTextAreaView* tv = areaView(&t->base);
    [tv scrollRangeToVisible:NSMakeRange(tv.string.length, 0)];
}

// The view fills the scroll view's width when it wraps, and at least its height.
static void areaPlaced(AppKitText* t)
{
    NSScrollView* sv     = areaScroll(&t->base);
    CxUiTextAreaView* tv = areaView(&t->base);
    NSSize cs            = sv.contentSize;
    tv.minSize           = NSMakeSize(tv.textContainer.widthTracksTextView ? 0 : cs.width, cs.height);
    if (tv.textContainer.widthTracksTextView)
        [tv setFrameSize:NSMakeSize(cs.width, max(tv.frame.size.height, cs.height))];
    if (t->toEnd && cs.height > 0) {
        t->toEnd = false;
        areaScrollEnd(t);
    }
}

// ---- operations ----

CxUiPeer* _cxuiAppKitTextCreate(CxUiTextField* tf, CxUiPeer* host, bool framed)
{
    NSView* parent = _cxuiAppKitPeerContent(host);
    if (!parent)
        return NULL;
    AppKitText* t        = xaAllocStruct(AppKitText, XA_Zero);
    t->base.kind         = CXUI_PEER_Text;
    t->framed            = framed;
    CxUiTextArea* area   = objDynCast(CxUiTextArea, tf);
    if (area)
        return areaCreate(t, parent, cxuiTextAreaWrap(area), framed);
    CxUiTextFieldView* v = [[CxUiTextFieldView alloc] initWithFrame:NSZeroRect];
    v->peer              = t;
    v.delegate           = v;
    configure(v, framed);
    t->view = (__bridge_retained void*)v;
    [parent addSubview:v];
    return &t->base;
}

void _cxuiAppKitTextDestroy(CxUiPeer* p)
{
    if (((AppKitText*)p)->area) {
        areaDestroy((AppKitText*)p);
        return;
    }
    CxUiTextFieldView* v = CFBridgingRelease(((AppKitText*)p)->view);
    v->peer              = NULL;
    if (v.currentEditor)
        [v.window makeFirstResponder:nil];
    v.delegate = nil;
    [v removeFromSuperview];
    // AppKit may still be calling into the view from further up the stack.
    __autoreleasing id keep = v;
    (void)keep;
    xaFree(p);
}

NSView* _cxuiAppKitTextView(CxUiPeer* p)
{
    return ((AppKitText*)p)->area ? (NSView*)areaScroll(p) : fieldView(p);
}

NSView* _cxuiAppKitTextFocusView(CxUiPeer* p)
{
    return ((AppKitText*)p)->area ? (NSView*)areaView(p) : fieldView(p);
}

_Use_decl_annotations_
CxUiPeer* _cxuiAppKitTextPeerFor(id responder)
{
    if ([responder isKindOfClass:CxUiTextAreaView.class]) {
        AppKitText* t = ((CxUiTextAreaView*)responder)->peer;
        return t ? &t->base : NULL;
    }
    if ([responder isKindOfClass:NSTextView.class] && ((NSTextView*)responder).fieldEditor)
        responder = ((NSTextView*)responder).delegate;
    if (![responder isKindOfClass:CxUiTextFieldView.class])
        return NULL;
    AppKitText* t = ((CxUiTextFieldView*)responder)->peer;
    return t ? &t->base : NULL;
}

void _cxuiAppKitTextSetBounds(CxUiPeer* p, CxUiRect px)
{
    AppKitText* t = (AppKitText*)p;
    t->bounds     = px;
    _cxuiAppKitPlaceView(_cxuiAppKitTextView(p), px);
    if (t->area)
        areaPlaced(t);
}

// Lines of the font, with the text's padding, a scroll bar, and the bezel of an area of its own
CxUiSize _cxuiAppKitTextAreaMeasure(CxUiTextArea* ta, CxUiFont* f, int32 columns, int32 lines, bool framed)
{
    unused_noeval(ta);
    static NSLayoutManager* lm;
    if (!lm)
        lm = [NSLayoutManager new];
    NSFont* font  = fontFor(f);
    CGFloat line  = [lm defaultLineHeightForFont:font];
    CGFloat digit = [@"0" sizeWithAttributes:@{ NSFontAttributeName : font }].width;
    CGFloat bar   = [NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy];
    CGFloat edge  = framed ? 0 : 2;
    return (CxUiSize){ (int32)ceil(digit * columns + 10 + bar + edge), (int32)ceil(line * lines + edge) };
}

void _cxuiAppKitTextAppend(CxUiPeer* p, strref text)
{
    CxUiTextAreaView* tv = areaView(p);
    NSAttributedString* a =
        [[NSAttributedString alloc] initWithString:_cxuiAppKitString(text) attributes:tv.typingAttributes];
    NSRange sel = tv.selectedRange;
    [tv.textStorage appendAttributedString:a];
    tv.selectedRange = sel;
}

void _cxuiAppKitTextScrollToEnd(CxUiPeer* p)
{
    AppKitText* t = (AppKitText*)p;
    if (areaScroll(p).contentSize.height > 0)
        areaScrollEnd(t);
    else
        t->toEnd = true;
}

// A line of the font with the field's own insets, and columns of its digit width
CxUiSize _cxuiAppKitTextMeasure(CxUiTextField* tf, CxUiFont* f, int32 columns, bool framed)
{
    unused_noeval(tf);
    static NSTextField* probes[2];
    NSTextField* m = probes[framed];
    if (!m) {
        m = probes[framed] = [[NSTextField alloc] initWithFrame:NSZeroRect];
        configure(m, framed);
    }
    NSFont* font  = fontFor(f);
    m.font        = font;
    m.stringValue = @"";
    NSSize cell   = [m.cell cellSizeForBounds:NSMakeRect(0, 0, 100000, 100000)];
    CGFloat digit = [@"0" sizeWithAttributes:@{ NSFontAttributeName : font }].width;
    return (CxUiSize){ (int32)ceil(cell.width + digit * columns), (int32)ceil(cell.height) };
}

void _cxuiAppKitTextSetText(CxUiPeer* p, strref text)
{
    if (((AppKitText*)p)->area)
        areaView(p).string = _cxuiAppKitString(text);
    else
        fieldView(p).stringValue = _cxuiAppKitString(text);
}

void _cxuiAppKitTextGetSelection(CxUiPeer* p, int32* start, int32* end, int32* caret)
{
    AppKitText* t        = (AppKitText*)p;
    if (t->area) {
        CxUiTextAreaView* tv = areaView(p);
        NSRange r            = tv.selectedRange;
        *start               = cpCount(tv.string, r.location);
        *end = *caret = cpCount(tv.string, NSMaxRange(r));
        return;
    }
    CxUiTextFieldView* v = fieldView(p);
    NSTextView* ed       = editing(v) ? editorOf(v) : nil;
    if (ed) {
        NSRange r = ed.selectedRange;
        *start    = cpCount(ed.string, r.location);
        *end      = cpCount(ed.string, NSMaxRange(r));
    } else {
        *start = t->selStart;
        *end   = t->selEnd;
    }
    // AppKit doesn't say which end of a selection the caret is at.
    *caret = *end;
}

void _cxuiAppKitTextSetSelection(CxUiPeer* p, int32 start, int32 end)
{
    AppKitText* t        = (AppKitText*)p;
    if (t->area) {
        CxUiTextAreaView* tv = areaView(p);
        NSString* str        = tv.string;
        NSUInteger a         = cpUnit(str, start);
        NSUInteger b         = cpUnit(str, max(end, start));
        tv.selectedRange     = NSMakeRange(a, b - a);
        [tv scrollRangeToVisible:tv.selectedRange];
        return;
    }
    CxUiTextFieldView* v = fieldView(p);
    NSString* s          = v.stringValue;
    int32 n              = cpCount(s, s.length);
    t->selStart          = clamp(start, 0, n);
    t->selEnd            = clamp(end, t->selStart, n);
    NSTextView* ed       = editing(v) ? editorOf(v) : nil;
    if (ed) {
        NSUInteger a     = cpUnit(ed.string, t->selStart);
        ed.selectedRange = NSMakeRange(a, cpUnit(ed.string, t->selEnd) - a);
    }
}

// An area has none.
void _cxuiAppKitTextSetCue(CxUiPeer* p, strref cue)
{
    if (((AppKitText*)p)->area)
        return;
    fieldView(p)->cue = strEmpty(cue) ? nil : _cxuiAppKitString(cue);
    applyCue((AppKitText*)p);
}

void _cxuiAppKitTextSetMaxLength(CxUiPeer* p, int32 maxLength)
{
    ((AppKitText*)p)->maxLength = maxLength;
}

void _cxuiAppKitTextSetFont(CxUiPeer* p, CxUiFont* f)
{
    if (((AppKitText*)p)->area) {
        areaView(p).font = fontFor(f);
        return;
    }
    fieldView(p).font = fontFor(f);
    applyCue((AppKitText*)p);
}

void _cxuiAppKitTextSetState(CxUiPeer* p, bool readOnly, bool enabled, bool focusable)
{
    AppKitText* t = (AppKitText*)p;
    if (t->area) {
        CxUiTextAreaView* tv = areaView(p);
        if ((!enabled || !focusable) && tv.window.firstResponder == tv)
            [tv.window makeFirstResponder:nil];
        tv.editable   = !readOnly && enabled;
        tv.selectable = enabled;
        tv->focusable = enabled && focusable;
        if (!t->hasColors)
            tv.textColor = enabled ? NSColor.textColor : NSColor.disabledControlTextColor;
        return;
    }
    CxUiTextFieldView* v = fieldView(p);
    if ((!enabled || !focusable) && v.currentEditor)
        [v.window makeFirstResponder:nil];
    v.editable              = !readOnly;
    v.enabled               = enabled;
    v.refusesFirstResponder = !focusable;
}

void _cxuiAppKitTextSetColors(CxUiPeer* p, const CxUiTextColors* c)
{
    AppKitText* t        = (AppKitText*)p;
    if (t->area) {
        CxUiTextAreaView* tv = areaView(p);
        t->hasColors         = c != NULL;
        if (c)
            t->colors = *c;
        tv.textColor           = c ? _cxuiAppKitColor(c->text) : NSColor.textColor;
        tv.insertionPointColor = c ? _cxuiAppKitColor(c->text) : NSColor.textColor;
        tv.backgroundColor     = c ? _cxuiAppKitColor(c->bg) : NSColor.textBackgroundColor;
        tv.drawsBackground     = c != NULL || !t->framed;
        tv.selectedTextAttributes = @{
            NSBackgroundColorAttributeName : c ? _cxuiAppKitColor(c->selection) : NSColor.selectedTextBackgroundColor,
            NSForegroundColorAttributeName : c ? _cxuiAppKitColor(c->selectionText) : NSColor.selectedTextColor,
        };
        return;
    }
    CxUiTextFieldView* v = fieldView(p);
    t->hasColors         = c != NULL;
    if (c) {
        t->colors         = *c;
        v.textColor       = _cxuiAppKitColor(c->text);
        v.backgroundColor = _cxuiAppKitColor(c->bg);
    } else {
        v.textColor       = NSColor.controlTextColor;
        v.backgroundColor = NSColor.textBackgroundColor;
    }
    v.drawsBackground = c != NULL || !t->framed;
    applyCue(t);
    if (editing(v)) {
        NSTextView* ed = editorOf(v);
        ed.textColor       = v.textColor;
        ed.backgroundColor = v.backgroundColor;
        styleEditor(v);
    }
}

// ---- keys ----

// Keys for a field, or a focused button, go to cxui before AppKit dispatches them anywhere, which
// is the only point ahead of both the menu bar's shortcuts and the control's own handling. So do
// all of a canvas's that captures keys, including the key-ups AppKit drops while Command is held.
static id keyMonitor;

static NSEvent* routeKey(NSEvent* ev)
{
    NSWindow* w = ev.window;
    if (w && _cxuiAppKitCanvasTakesKeys(w.firstResponder))
        return _cxuiAppKitCanvasCaptureKey(w.firstResponder, ev) ? nil : ev;
    CxUiPeer* p = w ? _cxuiAppKitTextPeerFor(w.firstResponder) : NULL;
    if (w && !p)
        p = _cxuiAppKitControlPeerFor(w.firstResponder);
    return p && _cxuiAppKitKey(p, ev) ? nil : ev;
}

void _cxuiAppKitTextInit(void)
{
    if (keyMonitor)
        return;
    NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged;
    keyMonitor       = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* ev) {
        return routeKey(ev);
    }];
}

void _cxuiAppKitTextShutdown(void)
{
    if (keyMonitor)
        [NSEvent removeMonitor:keyMonitor];
    keyMonitor = nil;
}

// ---- tests ----

bool _cxuiAppKitTestTextInfo(CxUiPeer* p, CxUiTestTextInfo* out)
{
    AppKitText* t        = (AppKitText*)p;
    if (t->area) {
        NSScrollView* sv     = areaScroll(p);
        CxUiTextAreaView* tv = areaView(p);
        strDup(&out->text, (strref)tv.string.UTF8String);
        _cxuiAppKitTextGetSelection(p, &out->selStart, &out->selEnd, &out->caret);
        out->multiline = true;
        out->wrap      = tv.textContainer.widthTracksTextView;
        out->atEnd     = NSMaxY(sv.contentView.documentVisibleRect) >= NSHeight(tv.frame) - 1;
        out->maxLength = t->maxLength;
        out->readOnly  = !tv.editable;
        out->enabled   = tv.selectable;
        out->focusable = tv->focusable;
        out->focused   = tv.window.firstResponder == tv;
        out->framed    = t->framed;
        out->visible   = !sv.hidden;
        out->hasColors = t->hasColors;
        out->bounds    = t->bounds;
        return true;
    }
    CxUiTextFieldView* v = fieldView(p);
    strDup(&out->text, (strref)v.stringValue.UTF8String);
    if (v->cue)
        strDup(&out->cue, (strref)v->cue.UTF8String);
    _cxuiAppKitTextGetSelection(p, &out->selStart, &out->selEnd, &out->caret);
    out->maxLength = t->maxLength;
    out->readOnly  = !v.editable;
    out->enabled   = v.enabled;
    out->focusable = !v.refusesFirstResponder;
    out->focused   = editing(v);
    out->framed    = t->framed;
    out->visible   = !v.hidden;
    out->hasColors = t->hasColors;
    out->bounds    = t->bounds;
    return true;
}

// The selection is replaced as typing would replace it, which a read-only field refuses.
bool _cxuiAppKitTestType(CxUiPeer* p, strref s)
{
    AppKitText* t        = (AppKitText*)p;
    if (t->area) {
        CxUiTextAreaView* tv = areaView(p);
        if (tv.editable)
            [tv insertText:_cxuiAppKitString(s) replacementRange:tv.selectedRange];
        return true;
    }
    CxUiTextFieldView* v = fieldView(p);
    if (!v.editable || !v.enabled)
        return true;
    NSString* ins  = _cxuiAppKitString(s);
    NSTextView* ed = editing(v) ? editorOf(v) : nil;
    if (ed) {
        [ed insertText:ins replacementRange:ed.selectedRange];
        return true;
    }
    NSString* cur = v.stringValue;
    NSUInteger a = cpUnit(cur, t->selStart), b = cpUnit(cur, t->selEnd);
    if (t->maxLength > 0) {
        int32 room = max(t->maxLength - (cpCount(cur, cur.length) - (t->selEnd - t->selStart)), 0);
        ins        = [ins substringToIndex:cpUnit(ins, room)];
    }
    v.stringValue = [cur stringByReplacingCharactersInRange:NSMakeRange(a, b - a) withString:ins];
    t->selStart = t->selEnd = t->selStart + cpCount(ins, ins.length);
    reportText(v);
    return true;
}

#include "appkit_ui.h"

#include <cx/format.h>

#include "core/cxuicontrols.h"

// A label is a non-editable NSTextField, a button an NSButton with the push bezel, and a check box or
// radio button an NSButton of the switch or radio type. macOS has no mnemonics, so the `&` is left
// out of what they show. The default button takes Return as its key
// equivalent, which is what gives it the default look, but never acts on it: Enter reaches the
// core, which presses the default itself, so a key handler can take it first. A focused button's
// keys reach the core through the text fields' event monitor.
//
// A list is a single-column NSTableView in an NSScrollView, and a dropdown an NSPopUpButton whose
// items go straight into its menu, which, unlike its own calls, keeps items with the same title.
//
// A slider is an NSSlider over whole positions, sending its action as it is dragged; a move is
// done unless it came from a press or a drag of the mouse. A progress bar is an
// NSProgressIndicator showing ten thousandths, animated while it shows activity.
//
// Tabs are an NSTabView whose items have empty views, inside a flipped view that also holds the
// page surface over the tab view's content area, where the pages' peers go. The page surface is
// clear, so the tab view's own box shows behind the pages. NSTabView has no disabled state; picks
// are refused instead. It shortens titles that don't fit, and can't scroll them, so tabs on
// several lines are rows of segmented controls, which is what NSTabView draws its tabs as, wrapped
// to the width over a tab view without tabs: the last row across its top edge, which is left out
// under the row, as NSTabView's is under its tabs.
//
// AppKit groups radio buttons in the same view that share an action, turning the others off when
// one is turned on, even from code. cxui groups its own, so each radio button gets an action of its
// own, and every action a button sends ends up in clicked:.

typedef struct AppKitControl AppKitControl;

@interface CxUiButtonView : NSButton {
  @public
    AppKitControl* peer;
}
@end

@class CxUiListSource;

@interface CxUiListView : NSTableView {
  @public
    AppKitControl* peer;
}
@end

@interface CxUiPopUpView : NSPopUpButton {
  @public
    AppKitControl* peer;
}
@end

@interface CxUiSliderView : NSSlider {
  @public
    AppKitControl* peer;
}
- (void)moved:(bool)done;
@end

@interface CxUiTabView : NSTabView <NSTabViewDelegate> {
  @public
    AppKitControl* peer;
}
@end

@interface CxUiTabsBox : NSView
@end

@interface CxUiTabSegments : NSSegmentedControl {
  @public
    AppKitControl* peer;
}
@end

@class CxUiTabRows;

// A tab view without tabs that leaves out a gap in what it draws
@interface CxUiTabBox : NSTabView {
  @public
    NSRect gap;
}
@end

struct AppKitControl {
    CxUiPeer base;
    void* view;   // NSTextField, CxUiButtonView, CxUiPopUpView or a list's NSScrollView, retained
    CxUiRect bounds;
    void* source;        // a list's CxUiListSource, or wrapped tabs' CxUiTabRows, retained
    bool setting;        // the core is changing a list's or tabs' items or selection
    bool multi;          // a list where any number can be selected
    bool disabled;       // tabs, which take no picks
    CxUiPeer* page;      // the tabs' page surface
};

static NSView* controlView(CxUiPeer* p)
{
    return (__bridge NSView*)((AppKitControl*)p)->view;
}

static NSString* shownText(strref text)
{
    string shown = 0;
    _cxuiMnemonicStrip(&shown, text);
    NSString* s = _cxuiAppKitString(shown);
    strDestroy(&shown);
    return s;
}

static NSFont* fontFor(CxUiFont* f)
{
    return (__bridge_transfer NSFont*)_cxuiAppKitFontAtPoints(f);
}

@implementation CxUiButtonView
- (BOOL)performKeyEquivalent:(NSEvent*)ev
{
    return NO;
}
- (void)clicked:(id)sender
{
    if (!peer)
        return;
    _cxuiNativeEnter();
    _cxuiButtonClicked(&peer->base);
    _cxuiNativeLeave();
}
- (BOOL)sendAction:(SEL)action to:(id)target
{
    [self clicked:self];
    return YES;
}
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

static AppKitControl* controlNew(CxUiPeerKind kind, NSView* v, CxUiPeer* host)
{
    NSView* parent = _cxuiAppKitPeerContent(host);
    if (!parent)
        return NULL;
    AppKitControl* c = xaAllocStruct(AppKitControl, XA_Zero);
    c->base.kind     = kind;
    c->view          = (__bridge_retained void*)v;
    [parent addSubview:v];
    return c;
}

// ---- labels ----

static void configureLabel(NSTextField* l)
{
    l.editable           = NO;
    l.selectable         = NO;
    l.bezeled            = NO;
    l.bordered           = NO;
    l.drawsBackground    = NO;
    l.usesSingleLineMode = NO;
    l.maximumNumberOfLines = 0;
    l.cell.wraps         = NO;
    l.lineBreakMode      = NSLineBreakByClipping;
}

CxUiPeer* _cxuiAppKitLabelCreate(CxUiLabel* lbl, CxUiPeer* host)
{
    unused_noeval(lbl);
    NSTextField* l = [NSTextField labelWithString:@""];
    configureLabel(l);
    AppKitControl* c = controlNew(CXUI_PEER_Label, l, host);
    return c ? &c->base : NULL;
}

void _cxuiAppKitLabelSetText(CxUiPeer* p, strref text)
{
    ((NSTextField*)controlView(p)).stringValue = shownText(text);
}

void _cxuiAppKitLabelSetStyle(CxUiPeer* p, uint32 align, uint32 ellipsis, bool wrap)
{
    NSTextField* l = (NSTextField*)controlView(p);
    l.cell.wraps   = wrap;
    l.alignment    = (align & CXUI_TEXT_Right) ? NSTextAlignmentRight
                     : (align & CXUI_TEXT_Center) ? NSTextAlignmentCenter
                                                  : NSTextAlignmentLeft;
    l.lineBreakMode = wrap                            ? NSLineBreakByWordWrapping
                      : ellipsis == CXUI_ELLIPSIS_None  ? NSLineBreakByClipping
                      : ellipsis == CXUI_ELLIPSIS_End ? NSLineBreakByTruncatingTail
                                                      : NSLineBreakByTruncatingMiddle;
}

// The cell's height for the text broken into lines width points across
static int32 cellHeightFor(NSTextField* l, int32 width)
{
    NSSize sz = [l.cell cellSizeForBounds:NSMakeRect(0, 0, max(width, 1), 100000)];
    return (int32)ceil(sz.height);
}

int32 _cxuiAppKitLabelHeightFor(CxUiLabel* lbl, CxUiFont* f, strref text, int32 width)
{
    unused_noeval(lbl);
    static NSTextField* probe;
    if (!probe) {
        probe = [NSTextField labelWithString:@""];
        configureLabel(probe);
        probe.cell.wraps   = YES;
        probe.lineBreakMode = NSLineBreakByWordWrapping;
    }
    probe.font        = fontFor(f);
    probe.stringValue = shownText(text);
    return cellHeightFor(probe, width);
}

// Check boxes and radio buttons keep the appearance's text color.
void _cxuiAppKitControlSetColor(CxUiPeer* p, CxUiColor color)
{
    if (p->kind == CXUI_PEER_Label)
        ((NSTextField*)controlView(p)).textColor = _cxuiAppKitColor(color);
}

// The label's own size for the text, with the padding its cell adds
CxUiSize _cxuiAppKitLabelMeasure(CxUiLabel* lbl, CxUiFont* f, strref text)
{
    unused_noeval(lbl);
    static NSTextField* probe;
    if (!probe) {
        probe = [NSTextField labelWithString:@""];
        configureLabel(probe);
    }
    probe.font        = fontFor(f);
    probe.stringValue = shownText(text);
    NSSize sz         = [probe.cell cellSizeForBounds:NSMakeRect(0, 0, 100000, 100000)];
    return (CxUiSize){ (int32)ceil(sz.width), (int32)ceil(sz.height) };
}

// ---- buttons ----

static void configureButton(NSButton* b)
{
    b.bezelStyle = NSBezelStylePush;
    [b setButtonType:NSButtonTypeMomentaryPushIn];
}

CxUiPeer* _cxuiAppKitButtonCreate(CxUiButton* btn, CxUiPeer* host)
{
    unused_noeval(btn);
    CxUiButtonView* b = [[CxUiButtonView alloc] initWithFrame:NSZeroRect];
    configureButton(b);
    b.title           = @"";
    b.target          = b;
    b.action          = @selector(clicked:);
    AppKitControl* c  = controlNew(CXUI_PEER_Button, b, host);
    if (c)
        b->peer = c;
    return c ? &c->base : NULL;
}

void _cxuiAppKitButtonSetTitle(CxUiPeer* p, strref title)
{
    ((NSButton*)controlView(p)).title = shownText(title);
}

void _cxuiAppKitButtonSetDefault(CxUiPeer* p, bool isDefault)
{
    ((NSButton*)controlView(p)).keyEquivalent = isDefault ? @"\r" : @"";
}

CxUiSize _cxuiAppKitButtonMeasure(CxUiButton* btn, CxUiFont* f, strref title)
{
    unused_noeval(btn);
    static NSButton* probe;
    if (!probe) {
        probe = [[NSButton alloc] initWithFrame:NSZeroRect];
        configureButton(probe);
    }
    probe.font  = fontFor(f);
    probe.title = shownText(title);
    NSSize sz   = probe.fittingSize;
    return (CxUiSize){ (int32)ceil(sz.width), (int32)ceil(sz.height) };
}

// Buttons take focus as in other Mac programs: with the system's Keyboard navigation turned on.
bool _cxuiAppKitButtonsFocus(void)
{
    return NSApp.fullKeyboardAccessEnabled;
}

// ---- check boxes and radio buttons ----

static NSControlStateValue nsState(uint32 state)
{
    return state == CXUI_CHECK_On ? NSControlStateValueOn : state == CXUI_CHECK_Mixed ? NSControlStateValueMixed
                                                                                     : NSControlStateValueOff;
}

static void configureCheck(NSButton* b, bool radio)
{
    [b setButtonType:radio ? NSButtonTypeRadio : NSButtonTypeSwitch];
    b.allowsMixedState = !radio;
}

CxUiPeer* _cxuiAppKitCheckCreate(CxUiWidget* w, CxUiPeer* host, bool radio)
{
    unused_noeval(w);
    CxUiButtonView* b = [[CxUiButtonView alloc] initWithFrame:NSZeroRect];
    configureCheck(b, radio);
    b.title          = @"";
    b.target         = b;
    b.action         = radio ? NSSelectorFromString([NSString stringWithFormat:@"cxuiRadio%p:", (__bridge void*)b])
                             : @selector(clicked:);
    AppKitControl* c = controlNew(radio ? CXUI_PEER_Radio : CXUI_PEER_Check, b, host);
    if (c)
        b->peer = c;
    return c ? &c->base : NULL;
}

void _cxuiAppKitCheckSetState(CxUiPeer* p, uint32 state)
{
    ((NSButton*)controlView(p)).state = nsState(state);
}

CxUiSize _cxuiAppKitCheckMeasure(CxUiWidget* w, CxUiFont* f, strref title, bool radio)
{
    unused_noeval(w);
    static NSButton* probe[2];
    if (!probe[radio]) {
        probe[radio] = [[NSButton alloc] initWithFrame:NSZeroRect];
        configureCheck(probe[radio], radio);
    }
    NSButton* b = probe[radio];
    b.font      = fontFor(f);
    b.title     = shownText(title);
    NSSize sz   = b.fittingSize;
    return (CxUiSize){ (int32)ceil(sz.width), (int32)ceil(sz.height) };
}

// ---- lists and dropdowns ----

@interface CxUiListSource : NSObject <NSTableViewDataSource, NSTableViewDelegate> {
  @public
    NSMutableArray<NSString*>* items;
    AppKitControl* peer;
}
@end

@implementation CxUiListSource
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tv
{
    return (NSInteger)items.count;
}
- (id)tableView:(NSTableView*)tv objectValueForTableColumn:(NSTableColumn*)col row:(NSInteger)row
{
    return row >= 0 && row < (NSInteger)items.count ? items[(NSUInteger)row] : @"";
}
- (void)tableViewSelectionDidChange:(NSNotification*)n
{
    if (!peer || peer->setting)
        return;
    _cxuiNativeEnter();
    if (peer->multi)
        _cxuiListMarksChanged(&peer->base);
    else
        _cxuiListChosen(&peer->base, (int32)((NSTableView*)n.object).selectedRow);
    _cxuiNativeLeave();
}
- (void)activateRow:(NSInteger)row
{
    if (!peer || row < 0)
        return;
    _cxuiNativeEnter();
    _cxuiListActivated(&peer->base, (int32)row);
    _cxuiNativeLeave();
}
- (void)doubleClicked:(id)sender
{
    [self activateRow:((NSTableView*)sender).clickedRow];
}
@end

@implementation CxUiListView
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

@implementation CxUiPopUpView
- (void)chose:(id)sender
{
    if (!peer)
        return;
    _cxuiNativeEnter();
    _cxuiListChosen(&peer->base, (int32)self.indexOfSelectedItem);
    _cxuiNativeLeave();
}
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

static CxUiListView* tableOf(CxUiPeer* p)
{
    return (CxUiListView*)((NSScrollView*)controlView(p)).documentView;
}

static CxUiListSource* sourceOf(CxUiPeer* p)
{
    return (__bridge CxUiListSource*)((AppKitControl*)p)->source;
}

// A row as tall as a line of the font, plus a little room
static CGFloat rowHeightFor(NSFont* font)
{
    return ceil(font.ascender - font.descender + font.leading) + 2;
}

CxUiPeer* _cxuiAppKitListCreate(CxUiWidget* w, CxUiPeer* host, bool dropdown)
{
    CxUiList* l = dropdown ? NULL : objDynCast(CxUiList, w);
    bool multi  = l && l->_select == CXUI_SELECT_Multi;
    if (dropdown) {
        CxUiPopUpView* b = [[CxUiPopUpView alloc] initWithFrame:NSZeroRect pullsDown:NO];
        [b removeAllItems];
        b.target         = b;
        b.action         = @selector(chose:);
        AppKitControl* c = controlNew(CXUI_PEER_Dropdown, b, host);
        if (c)
            b->peer = c;
        return c ? &c->base : NULL;
    }
    NSScrollView* sv        = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    sv.hasVerticalScroller  = YES;
    sv.autohidesScrollers   = YES;
    sv.borderType           = NSBezelBorder;
    CxUiListView* tv        = [[CxUiListView alloc] initWithFrame:NSZeroRect];
    NSTableColumn* col      = [[NSTableColumn alloc] initWithIdentifier:@"items"];
    col.resizingMask        = NSTableColumnAutoresizingMask;
    col.editable            = NO;
    [tv addTableColumn:col];
    tv.headerView              = nil;
    tv.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    tv.allowsEmptySelection    = YES;
    tv.allowsMultipleSelection = multi;
    tv.style                   = NSTableViewStylePlain;
    CxUiListSource* src = [CxUiListSource new];
    src->items          = [NSMutableArray new];
    tv.dataSource       = src;
    tv.delegate         = src;
    tv.target           = src;
    tv.doubleAction     = @selector(doubleClicked:);
    sv.documentView     = tv;
    AppKitControl* c    = controlNew(CXUI_PEER_List, sv, host);
    if (!c)
        return NULL;
    c->source = (__bridge_retained void*)src;
    c->multi  = multi;
    src->peer = c;
    tv->peer  = c;
    return &c->base;
}

void _cxuiAppKitListSplice(CxUiPeer* p, int32 at, int32 removed, const string* items, int32 added)
{
    AppKitControl* c = (AppKitControl*)p;
    c->setting       = true;
    if (p->kind == CXUI_PEER_Dropdown) {
        NSPopUpButton* b = (NSPopUpButton*)controlView(p);
        for (int32 i = 0; i < removed; i++)
            [b removeItemAtIndex:at];
        for (int32 i = 0; i < added; i++)
            [b.menu insertItem:[[NSMenuItem alloc] initWithTitle:_cxuiAppKitString(items[i]) action:nil keyEquivalent:@""]
                       atIndex:at + i];
    } else {
        NSMutableArray<NSString*>* list = sourceOf(p)->items;
        [list removeObjectsInRange:NSMakeRange((NSUInteger)at, (NSUInteger)removed)];
        for (int32 i = 0; i < added; i++)
            [list insertObject:_cxuiAppKitString(items[i]) atIndex:(NSUInteger)(at + i)];
        [tableOf(p) reloadData];
    }
    c->setting = false;
}

void _cxuiAppKitListSetValue(CxUiPeer* p, int32 value)
{
    AppKitControl* c = (AppKitControl*)p;
    c->setting       = true;
    if (p->kind == CXUI_PEER_Dropdown) {
        NSPopUpButton* b = (NSPopUpButton*)controlView(p);
        if (value >= 0)
            [b selectItemAtIndex:value];
        else
            [b selectItem:nil];
    } else if (value >= 0) {
        [tableOf(p) selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)value] byExtendingSelection:NO];
    } else {
        [tableOf(p) deselectAll:nil];
    }
    c->setting = false;
}

void _cxuiAppKitListSetMarks(CxUiPeer* p, const uint8* marks, int32 count)
{
    AppKitControl* c      = (AppKitControl*)p;
    NSMutableIndexSet* on = [NSMutableIndexSet indexSet];
    for (int32 i = 0; i < count; i++) {
        if (marks[i])
            [on addIndex:(NSUInteger)i];
    }
    c->setting = true;
    [tableOf(p) selectRowIndexes:on byExtendingSelection:NO];
    c->setting = false;
}

void _cxuiAppKitListGetMarks(CxUiPeer* p, uint8* marks, int32 count)
{
    memset(marks, 0, count);
    NSIndexSet* on = tableOf(p).selectedRowIndexes;
    for (NSUInteger i = on.firstIndex; i != NSNotFound; i = [on indexGreaterThanIndex:i]) {
        if (i < (NSUInteger)count)
            marks[i] = 1;
    }
}

// The row at the top of the visible part; the table view is flipped.
int32 _cxuiAppKitListTopItem(CxUiPeer* p)
{
    NSTableView* tv = tableOf(p);
    NSInteger row   = [tv rowAtPoint:NSMakePoint(0, NSMinY(tv.visibleRect) + 1)];
    return row < 0 ? 0 : (int32)row;
}

void _cxuiAppKitListSetTopItem(CxUiPeer* p, int32 index)
{
    NSTableView* tv  = tableOf(p);
    NSScrollView* sv = (NSScrollView*)controlView(p);
    NSClipView* clip = sv.contentView;
    if (index < 0 || index >= tv.numberOfRows)
        return;
    CGFloat limit = max(NSHeight(tv.frame) - NSHeight(clip.bounds), 0);
    [clip scrollToPoint:NSMakePoint(0, min([tv rectOfRow:index].origin.y, limit))];
    [sv reflectScrolledClipView:clip];
}

void _cxuiAppKitListShow(CxUiPeer* p, int32 index)
{
    if (p->kind == CXUI_PEER_List)
        [tableOf(p) scrollRowToVisible:index];
}

// A list's rows inside its bezel, beside a scroller; a dropdown as wide as its widest item plus
// what an empty one needs around it.
CxUiSize _cxuiAppKitListMeasure(CxUiWidget* w, CxUiFont* f, int32 widest, int32 rows, bool dropdown)
{
    unused_noeval(w);
    NSFont* font = fontFor(f);
    if (dropdown) {
        static NSPopUpButton* probe;
        if (!probe)
            probe = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        probe.font = font;
        [probe removeAllItems];
        [probe addItemWithTitle:@""];
        NSSize sz = probe.fittingSize;
        return (CxUiSize){ widest + (int32)ceil(sz.width), (int32)ceil(sz.height) };
    }
    CGFloat row = rowHeightFor(font) + 2;   // the table's spacing between rows
    CGFloat bar = [NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy];
    return (CxUiSize){ widest + 12 + (int32)ceil(bar), (int32)ceil(rows * row) + 4 };
}

// ---- sliders and progress bars ----

@implementation CxUiSliderView
- (void)moved:(bool)done
{
    if (!peer || peer->setting)
        return;
    _cxuiNativeEnter();
    _cxuiSliderMoved(&peer->base, (int32)floor(self.doubleValue + 0.5), done);
    _cxuiNativeLeave();
}
- (void)slid:(id)sender
{
    NSEventType t = NSApp.currentEvent.type;
    [self moved:t != NSEventTypeLeftMouseDown && t != NSEventTypeLeftMouseDragged];
}
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

CxUiPeer* _cxuiAppKitSliderCreate(CxUiWidget* w, CxUiPeer* host, bool vertical)
{
    unused_noeval(w);
    CxUiSliderView* sl   = [[CxUiSliderView alloc] initWithFrame:NSZeroRect];
    sl.vertical          = vertical;
    sl.minValue          = 0;
    sl.continuous        = YES;
    sl.tickMarkPosition  = vertical ? NSTickMarkPositionTrailing : NSTickMarkPositionBelow;
    sl.target            = sl;
    sl.action            = @selector(slid:);
    AppKitControl* c     = controlNew(CXUI_PEER_Slider, sl, host);
    if (c)
        sl->peer = c;
    return c ? &c->base : NULL;
}

void _cxuiAppKitSliderSetRange(CxUiPeer* p, int32 range, int32 pageStep, int32 tickEvery)
{
    AppKitControl* c      = (AppKitControl*)p;
    NSSlider* sl          = (NSSlider*)controlView(p);
    c->setting            = true;
    sl.maxValue           = range;
    sl.altIncrementValue  = pageStep;
    sl.numberOfTickMarks  = tickEvery > 0 ? range / tickEvery + 1 : 0;
    c->setting            = false;
}

void _cxuiAppKitSliderSetPos(CxUiPeer* p, int32 pos)
{
    AppKitControl* c = (AppKitControl*)p;
    c->setting       = true;
    ((NSSlider*)controlView(p)).doubleValue = pos;
    c->setting = false;
}

// AppKit's thickness, and the length asked for
CxUiSize _cxuiAppKitSliderMeasure(CxUiWidget* w, int32 length, bool vertical, bool ticks)
{
    unused_noeval(w);
    static NSSlider* probes[2][2];
    NSSlider* sl = probes[vertical][ticks];
    if (!sl) {
        sl = probes[vertical][ticks] = [[NSSlider alloc] initWithFrame:NSZeroRect];
        sl.vertical                  = vertical;
        sl.numberOfTickMarks         = ticks ? 2 : 0;
    }
    NSSize sz = sl.fittingSize;
    return vertical ? (CxUiSize){ (int32)ceil(sz.width), length } : (CxUiSize){ length, (int32)ceil(sz.height) };
}

CxUiPeer* _cxuiAppKitProgressCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    NSProgressIndicator* pi = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    pi.style                = NSProgressIndicatorStyleBar;
    pi.minValue             = 0;
    pi.maxValue             = 10000;
    pi.indeterminate        = NO;
    AppKitControl* c        = controlNew(CXUI_PEER_Progress, pi, host);
    return c ? &c->base : NULL;
}

void _cxuiAppKitProgressSet(CxUiPeer* p, float64 fraction, bool indeterminate)
{
    NSProgressIndicator* pi = (NSProgressIndicator*)controlView(p);
    if (indeterminate != pi.indeterminate) {
        pi.indeterminate = indeterminate;
        if (indeterminate)
            [pi startAnimation:nil];
        else
            [pi stopAnimation:nil];
    }
    if (!indeterminate)
        pi.doubleValue = floor(fraction * 10000 + 0.5);
}

// ---- tabs ----

@implementation CxUiTabsBox
- (BOOL)isFlipped
{
    return YES;
}
@end

@implementation CxUiTabView
- (BOOL)tabView:(NSTabView*)tv shouldSelectTabViewItem:(NSTabViewItem*)item
{
    return !peer || peer->setting || !peer->disabled;
}
- (void)tabView:(NSTabView*)tv didSelectTabViewItem:(NSTabViewItem*)item
{
    if (!peer || peer->setting)
        return;
    _cxuiNativeEnter();
    _cxuiTabsPicked(&peer->base, (int32)[tv indexOfTabViewItem:item]);
    _cxuiNativeLeave();
}
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

static CxUiTabView* tabViewOf(CxUiPeer* p)
{
    return (CxUiTabView*)controlView(p).subviews.firstObject;
}

// ---- tabs on several lines ----

// Room between the rows and the box's sides, and between rows, in points
#define ROW_MARGIN 8
#define ROW_GAP    2
// Room between the last row and the pages
#define ROW_CLEAR  4

@implementation CxUiTabBox
- (void)drawRect:(NSRect)dirty
{
    if (NSIsEmptyRect(gap)) {
        [super drawRect:dirty];
        return;
    }
    [NSGraphicsContext saveGraphicsState];
    NSBezierPath* clip = [NSBezierPath bezierPathWithRect:self.bounds];
    [clip appendBezierPathWithRect:gap];
    clip.windingRule = NSWindingRuleEvenOdd;
    [clip addClip];
    [super drawRect:dirty];
    [NSGraphicsContext restoreGraphicsState];
}
@end

@implementation CxUiTabSegments
- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->base, false);
        _cxuiNativeLeave();
    }
    return YES;
}
@end

static NSSegmentedControl* segmentProbe(NSFont* font)
{
    static NSSegmentedControl* probe;
    if (!probe)
        probe = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    probe.font = font;
    return probe;
}

static void segmentsFill(NSSegmentedControl* sc, NSArray<NSString*>* labels)
{
    sc.segmentCount = (NSInteger)labels.count;
    for (NSUInteger i = 0; i < labels.count; i++) {
        [sc setLabel:labels[i] forSegment:(NSInteger)i];
        [sc setWidth:0 forSegment:(NSInteger)i];
    }
}

static NSSize segmentsSize(NSArray<NSString*>* labels, NSFont* font)
{
    NSSegmentedControl* sc = segmentProbe(font);
    segmentsFill(sc, labels.count ? labels : @[ @"Ag" ]);
    return sc.fittingSize;
}

// Where each row starts: as many tabs as fit across width points, or all on one with width 0
static NSArray<NSNumber*>* rowStarts(NSArray<NSString*>* titles, NSFont* font, CGFloat width)
{
    NSMutableArray<NSNumber*>* starts = [NSMutableArray arrayWithObject:@0];
    NSUInteger start                  = 0;
    for (NSUInteger i = 1; width > 0 && i < titles.count; i++) {
        if (segmentsSize([titles subarrayWithRange:NSMakeRange(start, i - start + 1)], font).width > width) {
            [starts addObject:@(i)];
            start = i;
        }
    }
    return starts;
}

typedef struct TabRowsGeometry {
    CGFloat rowH;     // a row's height
    CGFloat boxTop;   // where the box's frame starts, under all but half of the last row
    CxUiEdges in;     // the pages' room inside the control, in points
} TabRowsGeometry;

static TabRowsGeometry rowsGeometry(NSUInteger rows, NSFont* font)
{
    static NSTabView* probe;
    if (!probe) {
        probe             = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 1000, 1000)];
        probe.tabViewType = NSNoTabsBezelBorder;
    }
    TabRowsGeometry g = { .rowH = ceil(segmentsSize(@[], font).height) };
    rows              = max(rows, 1);
    g.boxTop          = (rows - 1) * (g.rowH + ROW_GAP) + floor(g.rowH / 2);
    NSRect in         = probe.contentRect;
    CGFloat below     = (rows - 1) * (g.rowH + ROW_GAP) + g.rowH + ROW_CLEAR;
    g.in = (CxUiEdges){ (int32)ceil(NSMinX(in)), (int32)ceil(fmax(g.boxTop + NSMinY(in), below)),
                        (int32)ceil(1000 - NSMaxX(in)), (int32)ceil(1000 - NSMaxY(in)) };
    return g;
}

@interface CxUiTabRows : NSObject {
  @public
    AppKitControl* peer;
    NSMutableArray<NSString*>* titles;
    NSMutableArray<CxUiTabSegments*>* rows;
    NSArray<NSNumber*>* starts;
    CxUiTabBox* box;
    NSFont* font;
    NSInteger value;
}
- (void)layout;
@end

@implementation CxUiTabRows
- (void)layout
{
    NSView* outer = box.superview;
    CGFloat width = outer.bounds.size.width;
    starts        = rowStarts(titles, font, width - 2 * ROW_MARGIN);
    while (rows.count < starts.count) {
        CxUiTabSegments* sc = [[CxUiTabSegments alloc] initWithFrame:NSZeroRect];
        sc.trackingMode     = NSSegmentSwitchTrackingSelectOne;
        sc.target           = self;
        sc.action           = @selector(picked:);
        sc->peer            = peer;
        // In front of the box, behind the page surface
        [outer addSubview:sc positioned:NSWindowAbove relativeTo:box];
        [rows addObject:sc];
    }
    while (rows.count > starts.count) {
        CxUiTabSegments* sc = rows.lastObject;
        sc->peer            = NULL;
        sc.target           = nil;
        [sc removeFromSuperview];
        [rows removeLastObject];
    }
    TabRowsGeometry g = rowsGeometry(starts.count, font);
    for (NSUInteger r = 0; r < rows.count; r++) {
        NSUInteger from     = starts[r].unsignedIntegerValue;
        NSUInteger to       = r + 1 < starts.count ? starts[r + 1].unsignedIntegerValue : titles.count;
        CxUiTabSegments* sc = rows[r];
        if (font)
            sc.font = font;
        segmentsFill(sc, [titles subarrayWithRange:NSMakeRange(from, to - from)]);
        sc.selectedSegment = value >= (NSInteger)from && value < (NSInteger)to ? value - (NSInteger)from : -1;
        sc.enabled         = !peer || !peer->disabled;
        NSSize sz          = sc.fittingSize;
        sc.frame           = NSMakeRect(floor((width - sz.width) / 2), r * (g.rowH + ROW_GAP), sz.width, g.rowH);
        sc.hidden          = to == from;
    }
    box.frame = NSMakeRect(0, g.boxTop, width, fmax(outer.bounds.size.height - g.boxTop, 0));
    // The segments are translucent; the box's edge is left out under the last row.
    CxUiTabSegments* last = rows.lastObject;
    box->gap = last && !last.hidden ? [box convertRect:last.frame fromView:outer] : NSZeroRect;
    box.needsDisplay = YES;
}
- (void)picked:(CxUiTabSegments*)sc
{
    NSUInteger r = [rows indexOfObjectIdenticalTo:sc];
    if (!peer || r == NSNotFound || sc.selectedSegment < 0)
        return;
    value = starts[r].integerValue + sc.selectedSegment;
    [self layout];
    _cxuiNativeEnter();
    _cxuiTabsPicked(&peer->base, (int32)value);
    _cxuiNativeLeave();
}
@end

static CxUiTabRows* rowsOf(CxUiPeer* p)
{
    return p->kind == CXUI_PEER_Tabs ? (__bridge CxUiTabRows*)((AppKitControl*)p)->source : nil;
}

static CxUiPeer* tabRowsCreate(NSView* box, CxUiPeer* host)
{
    CxUiTabRows* r = [CxUiTabRows new];
    r->titles      = [NSMutableArray new];
    r->rows        = [NSMutableArray new];
    r->value       = -1;
    r->box         = [[CxUiTabBox alloc] initWithFrame:NSZeroRect];
    r->box.tabViewType = NSNoTabsBezelBorder;
    [box addSubview:r->box];
    AppKitControl* c = controlNew(CXUI_PEER_Tabs, box, host);
    if (!c)
        return NULL;
    c->page   = _cxuiAppKitSurfaceCreate(box);
    c->source = (__bridge_retained void*)r;
    r->peer   = c;
    return &c->base;
}

CxUiPeer* _cxuiAppKitTabsCreate(CxUiTabs* t, CxUiPeer* host)
{
    CxUiTabsBox* box  = [[CxUiTabsBox alloc] initWithFrame:NSZeroRect];
    // From macOS 14 a view draws outside its frame unless told not to.
    box.clipsToBounds = YES;
    if (CxUiZBox(t)->_flags & CXUI_TABS_Multiline)
        return tabRowsCreate(box, host);
    CxUiTabView* tv      = [[CxUiTabView alloc] initWithFrame:NSZeroRect];
    tv.autoresizingMask  = NSViewWidthSizable | NSViewHeightSizable;
    tv.delegate          = tv;
    [box addSubview:tv];
    AppKitControl* c = controlNew(CXUI_PEER_Tabs, box, host);
    if (!c)
        return NULL;
    c->page = _cxuiAppKitSurfaceCreate(box);
    tv->peer = c;
    return &c->base;
}

void _cxuiAppKitTabsInsert(CxUiPeer* p, int32 at, strref title)
{
    CxUiTabRows* r = rowsOf(p);
    if (r) {
        [r->titles insertObject:_cxuiAppKitString(title) atIndex:(NSUInteger)at];
        if (r->value >= at)
            r->value++;
        [r layout];
        return;
    }
    AppKitControl* c     = (AppKitControl*)p;
    NSTabViewItem* item  = [[NSTabViewItem alloc] initWithIdentifier:nil];
    item.label           = _cxuiAppKitString(title);
    item.view            = [[NSView alloc] initWithFrame:NSZeroRect];
    c->setting           = true;
    [tabViewOf(p) insertTabViewItem:item atIndex:at];
    c->setting = false;
}

void _cxuiAppKitTabsRemove(CxUiPeer* p, int32 at)
{
    CxUiTabRows* r = rowsOf(p);
    if (r) {
        [r->titles removeObjectAtIndex:(NSUInteger)at];
        if (r->value == at)
            r->value = -1;
        else if (r->value > at)
            r->value--;
        [r layout];
        return;
    }
    AppKitControl* c = (AppKitControl*)p;
    CxUiTabView* tv  = tabViewOf(p);
    c->setting       = true;
    [tv removeTabViewItem:[tv tabViewItemAtIndex:at]];
    c->setting = false;
}

void _cxuiAppKitTabsSetTitle(CxUiPeer* p, int32 at, strref title)
{
    CxUiTabRows* r = rowsOf(p);
    if (r) {
        r->titles[(NSUInteger)at] = _cxuiAppKitString(title);
        [r layout];
        return;
    }
    [tabViewOf(p) tabViewItemAtIndex:at].label = _cxuiAppKitString(title);
}

// NSTabView always shows one tab picked.
void _cxuiAppKitTabsSetValue(CxUiPeer* p, int32 index)
{
    CxUiTabRows* r = rowsOf(p);
    if (r) {
        r->value = index;
        [r layout];
        return;
    }
    AppKitControl* c = (AppKitControl*)p;
    CxUiTabView* tv  = tabViewOf(p);
    if (index < 0 || index >= tv.numberOfTabViewItems)
        return;
    c->setting = true;
    [tv selectTabViewItemAtIndex:index];
    c->setting = false;
}

void _cxuiAppKitTabsSetPage(CxUiPeer* p, CxUiRect px)
{
    _cxuiAppKitPanelSetBounds(((AppKitControl*)p)->page, px);
}

NSView* _cxuiAppKitTabsPage(CxUiPeer* p)
{
    return _cxuiAppKitPanelView(((AppKitControl*)p)->page);
}

// Measured on controls of its own with the same titles and font, never the ones shown
CxUiEdges _cxuiAppKitTabsMeasure(CxUiTabs* t, CxUiFont* f, int32 width, int32* stripWidth)
{
    if (CxUiZBox(t)->_flags & CXUI_TABS_Multiline) {
        NSFont* font                     = fontFor(f);
        NSMutableArray<NSString*>* names = [NSMutableArray new];
        string shown                     = 0;
        for (int32 i = 0; i < saSize(t->_titles); i++) {
            _cxuiMnemonicStrip(&shown, t->_titles.a[i]);
            [names addObject:_cxuiAppKitString(shown)];
        }
        strDestroy(&shown);
        NSUInteger rows = rowStarts(names, font, width > 0 ? width - 2 * ROW_MARGIN : 0).count;
        *stripWidth     = (int32)ceil(segmentsSize(names, font).width) + 2 * ROW_MARGIN;
        return rowsGeometry(rows, font).in;
    }
    static NSTabView* probe;
    if (!probe)
        probe = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 1000, 1000)];
    probe.font = fontFor(f);
    while (probe.numberOfTabViewItems > 0)
        [probe removeTabViewItem:[probe tabViewItemAtIndex:0]];
    string shown = 0;
    for (int32 i = 0; i < saSize(t->_titles); i++) {
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:nil];
        _cxuiMnemonicStrip(&shown, t->_titles.a[i]);
        item.label = _cxuiAppKitString(shown);
        [probe addTabViewItem:item];
    }
    strDestroy(&shown);
    NSRect in   = probe.contentRect;
    *stripWidth = (int32)ceil(probe.minimumSize.width);
    return (CxUiEdges){ (int32)ceil(NSMinX(in)), (int32)ceil(NSMinY(in)), (int32)ceil(1000 - NSMaxX(in)),
                        (int32)ceil(1000 - NSMaxY(in)) };
}

// ---- all of them ----

void _cxuiAppKitControlSetBounds(CxUiPeer* p, CxUiRect px)
{
    ((AppKitControl*)p)->bounds = px;
    _cxuiAppKitPlaceView(controlView(p), px);
    if (rowsOf(p))
        [rowsOf(p) layout];
    else if (p->kind == CXUI_PEER_Tabs)
        tabViewOf(p).frame = controlView(p).bounds;
}

void _cxuiAppKitControlSetFont(CxUiPeer* p, CxUiFont* f)
{
    if (p->kind == CXUI_PEER_List) {
        NSFont* font                                   = fontFor(f);
        NSTableView* tv                                = tableOf(p);
        ((NSCell*)tv.tableColumns.firstObject.dataCell).font = font;
        tv.rowHeight                                   = rowHeightFor(font);
        return;
    }
    if (rowsOf(p)) {
        rowsOf(p)->font = fontFor(f);
        [rowsOf(p) layout];
        return;
    }
    if (p->kind == CXUI_PEER_Tabs) {
        tabViewOf(p).font = fontFor(f);
        return;
    }
    ((NSControl*)controlView(p)).font = fontFor(f);
}

// Labels are never disabled natively: the core gives them the disabled text color.
void _cxuiAppKitPeerSetEnabled(CxUiPeer* p, bool enabled)
{
    if (p->kind == CXUI_PEER_List)
        tableOf(p).enabled = enabled;
    else if (p->kind == CXUI_PEER_Tabs) {
        ((AppKitControl*)p)->disabled = !enabled;
        [rowsOf(p) layout];
    }
    else if (_cxuiPeerIsControl(p->kind) && p->kind != CXUI_PEER_Label)
        ((NSControl*)controlView(p)).enabled = enabled;
}

NSView* _cxuiAppKitControlView(CxUiPeer* p)
{
    return controlView(p);
}

NSView* _cxuiAppKitControlFocusView(CxUiPeer* p)
{
    if (rowsOf(p))
        return rowsOf(p)->rows.firstObject ? rowsOf(p)->rows.firstObject : controlView(p);
    return p->kind == CXUI_PEER_List ? tableOf(p) : p->kind == CXUI_PEER_Tabs ? tabViewOf(p) : controlView(p);
}

_Use_decl_annotations_
CxUiPeer* _cxuiAppKitControlPeerFor(id responder)
{
    AppKitControl* c = [responder isKindOfClass:CxUiButtonView.class]  ? ((CxUiButtonView*)responder)->peer
                       : [responder isKindOfClass:CxUiListView.class]  ? ((CxUiListView*)responder)->peer
                       : [responder isKindOfClass:CxUiPopUpView.class] ? ((CxUiPopUpView*)responder)->peer
                       : [responder isKindOfClass:CxUiSliderView.class] ? ((CxUiSliderView*)responder)->peer
                       : [responder isKindOfClass:CxUiTabView.class]    ? ((CxUiTabView*)responder)->peer
                       : [responder isKindOfClass:CxUiTabSegments.class] ? ((CxUiTabSegments*)responder)->peer
                                                                         : NULL;
    return c ? &c->base : NULL;
}

void _cxuiAppKitControlDestroy(CxUiPeer* p)
{
    AppKitControl* c = (AppKitControl*)p;
    NSView* focus    = _cxuiAppKitControlFocusView(p);
    NSView* v        = CFBridgingRelease(c->view);
    if ([v isKindOfClass:CxUiButtonView.class]) {
        ((CxUiButtonView*)v)->peer = NULL;
        ((CxUiButtonView*)v).target = nil;
    }
    if ([v isKindOfClass:CxUiPopUpView.class]) {
        ((CxUiPopUpView*)v)->peer = NULL;
        ((CxUiPopUpView*)v).target = nil;
    }
    if ([v isKindOfClass:CxUiSliderView.class]) {
        ((CxUiSliderView*)v)->peer = NULL;
        ((CxUiSliderView*)v).target = nil;
    }
    if ([focus isKindOfClass:CxUiTabView.class]) {
        ((CxUiTabView*)focus)->peer   = NULL;
        ((CxUiTabView*)focus).delegate = nil;
    }
    if (c->page)
        _cxuiAppKitPanelDestroy(c->page);
    if ([focus isKindOfClass:CxUiListView.class]) {
        CxUiListView* tv = (CxUiListView*)focus;
        tv->peer         = NULL;
        tv.dataSource    = nil;
        tv.delegate      = nil;
        tv.target        = nil;
    }
    CxUiTabRows* rows = rowsOf(p);
    for (CxUiTabSegments* sc in rows ? rows->rows : @[]) {
        sc->peer  = NULL;
        sc.target = nil;
        if (sc.window.firstResponder == sc)
            [sc.window makeFirstResponder:nil];
    }
    if (rows)
        rows->peer = NULL;
    id src = c->source ? CFBridgingRelease(c->source) : nil;
    if ([src isKindOfClass:CxUiListSource.class])
        ((CxUiListSource*)src)->peer = NULL;
    if (focus.window.firstResponder == focus)
        [focus.window makeFirstResponder:nil];
    [v removeFromSuperview];
    // AppKit may still be calling into the view from further up the stack.
    __autoreleasing id keep    = v;
    __autoreleasing id keepSrc = src;
    (void)keep;
    (void)keepSrc;
    xaFree(p);
}

// ---- tests ----

bool _cxuiAppKitTestControlInfo(CxUiPeer* p, CxUiTestControlInfo* out)
{
    NSView* v    = controlView(p);
    bool label   = p->kind == CXUI_PEER_Label;
    if (rowsOf(p)) {
        AppKitControl* c = (AppKitControl*)p;
        CxUiTabRows* r   = rowsOf(p);
        out->value       = -1;
        int32 at         = 0;
        for (CxUiTabSegments* sc in r->rows) {
            for (NSInteger i = 0; i < sc.segmentCount; i++, at++) {
                strAppend(&out->items, (strref)[sc labelForSegment:i].UTF8String);
                strAppend(&out->items, _S"\n");
                if (sc.selectedSegment == i)
                    out->value = at;
            }
        }
        CGFloat s    = v.window ? v.window.backingScaleFactor : 1;
        // The box's content area, below the last row, which overlaps the box
        NSRect in    = NSOffsetRect(r->box.contentRect, NSMinX(r->box.frame), NSMinY(r->box.frame));
        CGFloat top  = r->rows.lastObject.hidden ? 0 : NSMaxY(r->rows.lastObject.frame) + ROW_CLEAR;
        if (top > NSMinY(in))
            in = NSMakeRect(NSMinX(in), top, NSWidth(in), NSMaxY(in) - top);
        NSRect pg    = _cxuiAppKitPanelView(c->page).frame;
        out->display = (CxUiRect){ (int32)lround(NSMinX(in) * s), (int32)lround(NSMinY(in) * s),
                                   (int32)lround(NSWidth(in) * s), (int32)lround(NSHeight(in) * s) };
        out->page    = (CxUiRect){ (int32)lround(NSMinX(pg) * s), (int32)lround(NSMinY(pg) * s),
                                   (int32)lround(NSWidth(pg) * s), (int32)lround(NSHeight(pg) * s) };
        out->enabled = !c->disabled;
        out->visible = !v.hidden;
        out->focused = [r->rows containsObject:(id)v.window.firstResponder];
        out->bounds  = c->bounds;
        return true;
    }
    if (p->kind == CXUI_PEER_Tabs) {
        AppKitControl* c = (AppKitControl*)p;
        CxUiTabView* tv  = tabViewOf(p);
        for (NSTabViewItem* item in tv.tabViewItems) {
            strAppend(&out->items, (strref)item.label.UTF8String);
            strAppend(&out->items, _S"\n");
        }
        out->value     = tv.selectedTabViewItem ? (int32)[tv indexOfTabViewItem:tv.selectedTabViewItem] : -1;
        CGFloat s      = v.window ? v.window.backingScaleFactor : 1;
        NSRect in      = tv.contentRect;
        NSRect pg      = _cxuiAppKitPanelView(c->page).frame;
        out->display   = (CxUiRect){ (int32)lround(NSMinX(in) * s), (int32)lround(NSMinY(in) * s),
                                     (int32)lround(NSWidth(in) * s), (int32)lround(NSHeight(in) * s) };
        out->page      = (CxUiRect){ (int32)lround(NSMinX(pg) * s), (int32)lround(NSMinY(pg) * s),
                                     (int32)lround(NSWidth(pg) * s), (int32)lround(NSHeight(pg) * s) };
        out->enabled   = !c->disabled;
        out->visible   = !v.hidden;
        out->focused   = v.window.firstResponder == tv;
        out->bounds    = c->bounds;
        return true;
    }
    if (p->kind == CXUI_PEER_List) {
        NSTableView* tv = tableOf(p);
        for (NSString* item in sourceOf(p)->items) {
            strAppend(&out->items, (strref)item.UTF8String);
            strAppend(&out->items, _S"\n");
        }
        out->value   = (int32)tv.selectedRow;
        out->top     = _cxuiAppKitListTopItem(p);
        if (((AppKitControl*)p)->multi) {
            NSIndexSet* on = tv.selectedRowIndexes;
            string at      = 0;
            out->value     = on.count > 0 ? (int32)on.firstIndex : -1;
            for (NSUInteger i = on.firstIndex; i != NSNotFound; i = [on indexGreaterThanIndex:i]) {
                strFormat(&at, _S"${int} ", stvar(int32, (int32)i));
                strAppend(&out->selection, at);
            }
            strDestroy(&at);
        }
        out->enabled = tv.enabled;
        out->visible = !v.hidden;
        out->focused = v.window.firstResponder == tv;
        out->bounds  = ((AppKitControl*)p)->bounds;
        return true;
    }
    if (p->kind == CXUI_PEER_Slider || p->kind == CXUI_PEER_Progress) {
        bool slider = p->kind == CXUI_PEER_Slider;
        out->pos    = (int32)floor(((NSControl*)v).doubleValue + 0.5);
        out->range  = slider ? (int32)((NSSlider*)v).maxValue : 10000;
        if (slider) {
            out->vertical = ((NSSlider*)v).vertical;
            out->enabled  = ((NSSlider*)v).enabled;
        } else {
            NSProgressIndicator* pi = (NSProgressIndicator*)v;
            out->pos                = (int32)floor(pi.doubleValue + 0.5);
            out->indeterminate      = pi.indeterminate;
            out->enabled            = true;
        }
        out->visible = !v.hidden;
        out->focused = v.window.firstResponder == v;
        out->bounds  = ((AppKitControl*)p)->bounds;
        return true;
    }
    if (p->kind == CXUI_PEER_Dropdown) {
        NSPopUpButton* b = (NSPopUpButton*)v;
        for (NSMenuItem* item in b.itemArray) {
            strAppend(&out->items, (strref)item.title.UTF8String);
            strAppend(&out->items, _S"\n");
        }
        out->value = (int32)b.indexOfSelectedItem;
    }
    NSString* s = label ? ((NSTextField*)v).stringValue : p->kind == CXUI_PEER_Dropdown ? @"" : ((NSButton*)v).title;
    strDup(&out->text, (strref)s.UTF8String);
    out->isDefault = p->kind == CXUI_PEER_Button && [((NSButton*)v).keyEquivalent isEqualToString:@"\r"];
    out->enabled   = label || ((NSControl*)v).enabled;
    if (p->kind == CXUI_PEER_Check || p->kind == CXUI_PEER_Radio) {
        NSControlStateValue st = ((NSButton*)v).state;
        out->checkState        = st == NSControlStateValueOn ? CXUI_CHECK_On
                                 : st == NSControlStateValueMixed ? CXUI_CHECK_Mixed
                                                                  : CXUI_CHECK_Off;
    }
    out->visible   = !v.hidden;
    out->focused   = v.window.firstResponder == v;
    out->bounds    = ((AppKitControl*)p)->bounds;
    if (p->kind == CXUI_PEER_Label && ((NSTextField*)v).cell.wraps) {
        CGFloat s         = v.window ? v.window.backingScaleFactor : 1;
        out->neededHeight = (int32)ceil(cellHeightFor((NSTextField*)v, (int32)floor(v.frame.size.width)) * s);
    }
    return true;
}

// The click a mouse gives, which a disabled control doesn't take
bool _cxuiAppKitTestClick(CxUiPeer* p)
{
    if (!_cxuiPeerIsControl(p->kind) || p->kind == CXUI_PEER_Label)
        return false;
    NSButton* b = (NSButton*)controlView(p);
    if (b.enabled && !b.hiddenOrHasHiddenAncestor)
        [b performClick:nil];
    return true;
}

// A selection as the user makes one, with the action or notification that follows, and a double
// click's; a disabled control takes neither.
bool _cxuiAppKitTestChoose(CxUiPeer* p, int32 index, bool activate)
{
    if (p->kind == CXUI_PEER_Dropdown) {
        CxUiPopUpView* b = (CxUiPopUpView*)controlView(p);
        if (b.enabled && !b.hiddenOrHasHiddenAncestor && index >= 0 && index < b.numberOfItems) {
            [b selectItemAtIndex:index];
            [b chose:b];
        }
        return true;
    }
    CxUiTabRows* rows = rowsOf(p);
    if (rows) {
        if (((AppKitControl*)p)->disabled || controlView(p).hiddenOrHasHiddenAncestor)
            return true;
        for (NSUInteger r = rows->starts.count; r-- > 0;) {
            NSInteger from = rows->starts[r].integerValue;
            if (index >= from && index - from < rows->rows[r].segmentCount) {
                rows->rows[r].selectedSegment = index - from;
                [rows picked:rows->rows[r]];
                break;
            }
        }
        return true;
    }
    if (p->kind == CXUI_PEER_Tabs) {
        CxUiTabView* tv = tabViewOf(p);
        if (!tv.hiddenOrHasHiddenAncestor && index >= 0 && index < tv.numberOfTabViewItems)
            [tv selectTabViewItemAtIndex:index];
        return true;
    }
    if (p->kind != CXUI_PEER_List)
        return false;
    NSTableView* tv = tableOf(p);
    if (!tv.enabled || tv.hiddenOrHasHiddenAncestor)
        return true;
    if (index >= 0)
        [tv selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)index] byExtendingSelection:NO];
    else
        [tv deselectAll:nil];
    if (activate)
        [sourceOf(p) activateRow:index];
    return true;
}

// A Command-click: the row's selection changes, with the notification that follows
bool _cxuiAppKitTestToggle(CxUiPeer* p, int32 index)
{
    if (p->kind != CXUI_PEER_List || !((AppKitControl*)p)->multi)
        return false;
    NSTableView* tv = tableOf(p);
    if (!tv.enabled || tv.hiddenOrHasHiddenAncestor || index < 0 || index >= tv.numberOfRows)
        return true;
    if ([tv isRowSelected:index])
        [tv deselectRow:index];
    else
        [tv selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)index] byExtendingSelection:YES];
    return true;
}

// A drag to a position, letting go with release; a disabled slider takes none of it.
bool _cxuiAppKitTestSlide(CxUiPeer* p, int32 pos, bool release)
{
    if (p->kind != CXUI_PEER_Slider)
        return false;
    CxUiSliderView* sl = (CxUiSliderView*)controlView(p);
    if (!sl.enabled || sl.hiddenOrHasHiddenAncestor)
        return true;
    sl.doubleValue = pos;
    [sl moved:false];
    if (release)
        [sl moved:true];
    return true;
}

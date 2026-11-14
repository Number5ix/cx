#include "appkit_ui.h"

// A menu an item opens is an NSMenu whose items stand for cxui's by position. A menu bar is a list
// of items that open menus; macOS has one menu bar for the program, so the bar shown there is the
// key window's, or that of the nearest window it belongs to, between the application menu and
// Edit, which every program has. Items in a bar that open no menu aren't shown.
//
// Hints are shown as key equivalents, which the menus never act on: as on the other platforms,
// the keys go to the window's key handler.

typedef struct AppKitMenu AppKitMenu;

@interface CxUiNSMenu : NSMenu {
  @public
    AppKitMenu* peer;   // NULL once the peer is destroyed
}
@end

@implementation CxUiNSMenu
@end

struct AppKitMenu {
    CxUiPeer base;
    // CxUiNSMenu, retained; a bar's holds its items while they aren't in the menu bar
    void* menu;
    // A bar's items, in order; NULL for other menus
    void* items;   // NSMutableArray<NSMenuItem*>, retained
};

// The program's menu bar and the parts of it that are always there (main thread only)
static NSMenu* mainBar;
static NSMenuItem* appItem;
static NSMenuItem* editItem;
static AppKitMenu* shownBar;

static CxUiNSMenu* nsMenu(AppKitMenu* p)
{
    return (__bridge CxUiNSMenu*)p->menu;
}

static NSMutableArray<NSMenuItem*>* barItems(AppKitMenu* p)
{
    return (__bridge NSMutableArray<NSMenuItem*>*)p->items;
}

// ---- hints as key equivalents ----

static unichar keyChar(CxUiKey key)
{
    if (key >= CXUI_KEY_A && key <= CXUI_KEY_Z)
        return 'a' + (key - CXUI_KEY_A);
    if (key >= CXUI_KEY_0 && key <= CXUI_KEY_9)
        return '0' + (key - CXUI_KEY_0);
    if (key >= CXUI_KEY_F1 && key <= CXUI_KEY_F20)
        return NSF1FunctionKey + (key - CXUI_KEY_F1);
    switch (key) {
    case CXUI_KEY_Tab:       return '\t';
    case CXUI_KEY_Enter:     return '\r';
    case CXUI_KEY_Escape:    return 0x1b;
    case CXUI_KEY_Space:     return ' ';
    case CXUI_KEY_Backspace: return NSBackspaceCharacter;
    case CXUI_KEY_Insert:    return NSInsertFunctionKey;
    case CXUI_KEY_Delete:    return NSDeleteFunctionKey;
    case CXUI_KEY_Home:      return NSHomeFunctionKey;
    case CXUI_KEY_End:       return NSEndFunctionKey;
    case CXUI_KEY_PageUp:    return NSPageUpFunctionKey;
    case CXUI_KEY_PageDown:  return NSPageDownFunctionKey;
    case CXUI_KEY_Left:      return NSLeftArrowFunctionKey;
    case CXUI_KEY_Right:     return NSRightArrowFunctionKey;
    case CXUI_KEY_Up:        return NSUpArrowFunctionKey;
    case CXUI_KEY_Down:      return NSDownArrowFunctionKey;
    default:                 return 0;
    }
}

// Apple's modifier symbols, as cxuiKeyLabel() writes them on macOS
static const struct {
    const char* symbol;
    uint32 mod;
} symbolMods[] = {
    { "\xE2\x8C\x83", CXUI_MOD_Ctrl },
    { "\xE2\x8C\xA5", CXUI_MOD_Alt },
    { "\xE2\x87\xA7", CXUI_MOD_Shift },
    { "\xE2\x8C\x98", CXUI_MOD_Meta },
};

// A hint written with cxui's key names, or the way cxuiKeyLabel() writes one here
static bool hintKey(strref hint, CxUiKey* key, uint32* mods)
{
    *mods = 0;
    if (strEmpty(hint))
        return false;
    if (cxuiKeyParse(key, mods, hint))
        return true;
    uint32 sym = 0;
    int32 at   = 0;
    for (bool more = true; more;) {
        more = false;
        for (int i = 0; i < (int)(sizeof(symbolMods) / sizeof(symbolMods[0])); i++) {
            int32 n = (int32)strlen(symbolMods[i].symbol);
            string part = 0;
            strSubStr(&part, hint, at, at + n);
            if (strEq(part, (strref)symbolMods[i].symbol)) {
                sym |= symbolMods[i].mod;
                at += n;
                more = true;
            }
            strDestroy(&part);
        }
    }
    string rest = 0;
    strSubStr(&rest, hint, at, strEnd);
    bool ok = sym && cxuiKeyParse(key, mods, rest);
    strDestroy(&rest);
    if (ok)
        *mods |= sym;
    return ok;
}

static void setHint(NSMenuItem* mi, strref hint)
{
    CxUiKey key;
    uint32 mods;
    unichar c = hintKey(hint, &key, &mods) ? keyChar(key) : 0;
    if (!c) {
        mi.keyEquivalent = @"";
        return;
    }
    mi.keyEquivalent = [NSString stringWithCharacters:&c length:1];
    mi.keyEquivalentModifierMask = ((mods & CXUI_MOD_Ctrl) ? NSEventModifierFlagControl : 0) |
                                   ((mods & CXUI_MOD_Alt) ? NSEventModifierFlagOption : 0) |
                                   ((mods & CXUI_MOD_Shift) ? NSEventModifierFlagShift : 0) |
                                   ((mods & (CXUI_MOD_Meta | CXUI_MOD_Primary)) ? NSEventModifierFlagCommand : 0);
}

static void readHint(string* out, NSMenuItem* mi)
{
    strClear(out);
    if (mi.keyEquivalent.length != 1)
        return;
    unichar c = [mi.keyEquivalent characterAtIndex:0];
    for (int32 k = CXUI_KEY_None + 1; k < CXUI_KEY_Count; k++) {
        if (keyChar((CxUiKey)k) != c)
            continue;
        NSEventModifierFlags m = mi.keyEquivalentModifierMask;
        cxuiKeyFormat(out, (CxUiKey)k,
                      ((m & NSEventModifierFlagControl) ? CXUI_MOD_Ctrl : 0) | ((m & NSEventModifierFlagOption) ? CXUI_MOD_Alt : 0) |
                          ((m & NSEventModifierFlagShift) ? CXUI_MOD_Shift : 0) |
                          ((m & NSEventModifierFlagCommand) ? CXUI_MOD_Meta : 0));
        return;
    }
}

// ---- choosing ----

static bool windowLocked(AppKitMenu* p)
{
    CxUiWindow* win = p->base.w ? _cxuiWindowOf(p->base.w) : NULL;
    return win && win->_modalLocks > 0;
}

static bool itemActive(CxUiMenuItem* item)
{
    return !(CxUiWidget(item)->state & CXUI_WS_Inactive);
}

@interface CxUiMenuTarget : NSObject <NSMenuDelegate>
@end

@implementation CxUiMenuTarget
- (void)chose:(NSMenuItem*)sender
{
    CxUiNSMenu* m = (CxUiNSMenu*)sender.menu;
    if (![m isKindOfClass:CxUiNSMenu.class] || !m->peer || !m->peer->base.w || windowLocked(m->peer))
        return;
    CxUiWidget* it     = _cxuiContainerChild(m->peer->base.w, (int32)[m indexOfItem:sender]);
    CxUiMenuItem* item = it ? objDynCast(CxUiMenuItem, it) : NULL;
    if (item)
        _cxuiMenuChosen(item);
}
- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu forEvent:(NSEvent*)event target:(id*)target action:(SEL*)action
{
    return NO;
}
// Grayed while a modal belongs to the window
- (void)menuNeedsUpdate:(NSMenu*)menu
{
    CxUiNSMenu* m = (CxUiNSMenu*)menu;
    if (![m isKindOfClass:CxUiNSMenu.class] || !m->peer || !m->peer->base.w)
        return;
    bool locked = windowLocked(m->peer);
    for (NSInteger i = 0; i < m.numberOfItems; i++) {
        CxUiWidget* it     = _cxuiContainerChild(m->peer->base.w, (int32)i);
        CxUiMenuItem* item = it ? objDynCast(CxUiMenuItem, it) : NULL;
        if (item)
            [m itemAtIndex:i].enabled = !locked && itemActive(item);
    }
}
@end

static CxUiMenuTarget* menuTarget;

// ---- items ----

static void itemSet(NSMenuItem* mi, CxUiMenuItem* item, bool inBar)
{
    if (mi.separatorItem)
        return;
    string shown = 0;
    _cxuiMnemonicStrip(&shown, item->_title);
    NSString* title = _cxuiAppKitString(shown);
    strDestroy(&shown);
    mi.title   = title;
    mi.tag     = item->_kind;
    mi.state   = item->_checked ? NSControlStateValueOn : NSControlStateValueOff;
    mi.enabled = itemActive(item);
    CxUiPeer* sub     = item->_submenu ? CxUiWidget(item->_submenu)->peer : NULL;
    CxUiNSMenu* subNs = sub ? nsMenu((AppKitMenu*)sub) : nil;
    if (mi.submenu != subNs) {
        mi.submenu = nil;
        if (subNs) {
            subNs.title = title;
            mi.submenu  = subNs;
        }
    } else if (subNs) {
        subNs.title = title;
    }
    mi.hidden = inBar && !subNs;
    mi.action = subNs ? nil : @selector(chose:);
    mi.target = subNs ? nil : menuTarget;
    setHint(mi, item->_hint);
}

static NSMenuItem* itemNew(CxUiMenuItem* item, bool inBar)
{
    if (item->_kind == CXUI_MENU_Separator)
        return [NSMenuItem separatorItem];
    NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    itemSet(mi, item, inBar);
    return mi;
}

// ---- the program's menu bar ----

// The application menu every Mac program has, and Edit, whose shortcuts are the only way text
// fields get Command+C, V, X, A and Z. Quit goes to applicationShouldTerminate:, so it raises
// cxuiQuitRequestNotify like the Dock's.
static void barParts(void)
{
    NSString* name = NSProcessInfo.processInfo.processName;
    NSMenu* app    = [[NSMenu alloc] initWithTitle:name];
    [app addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
    appItem         = [[NSMenuItem alloc] init];
    appItem.submenu = app;

    NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem         = [[NSMenuItem alloc] init];
    editItem.submenu = edit;
}

// Edit goes after the bar's first menu, as File, Edit, View... go. A new menu each time, as the
// menu bar doesn't always show items changed in the one it has.
static void barFill(void)
{
    if (!mainBar)
        return;
    [mainBar removeAllItems];
    mainBar = [[NSMenu alloc] init];
    [mainBar addItem:appItem];
    bool edit = false;
    for (NSMenuItem* mi in shownBar ? barItems(shownBar) : @[]) {
        [mainBar addItem:mi];
        if (!edit && !mi.hidden) {
            [mainBar addItem:editItem];
            edit = true;
        }
    }
    if (!edit)
        [mainBar addItem:editItem];
    NSApp.mainMenu = mainBar;
}

void _cxuiAppKitMenuInstall(void)
{
    if (!menuTarget)
        menuTarget = [CxUiMenuTarget new];
    if (!appItem)
        barParts();
    // Emptied from one session to the next, as its items can't be in two menus
    if (!mainBar)
        mainBar = [[NSMenu alloc] init];
    barFill();
}

void _cxuiAppKitMenuShowBar(CxUiPeer* bar)
{
    if (shownBar == (AppKitMenu*)bar)
        return;
    shownBar = (AppKitMenu*)bar;
    barFill();
}

CxUiPeer* _cxuiAppKitMenuShownBar(void)
{
    return shownBar ? &shownBar->base : NULL;
}

// ---- backend ----

CxUiPeer* _cxuiAppKitMenuCreate(CxUiMenu* m, bool bar)
{
    unused_noeval(m);
    if (!menuTarget)
        menuTarget = [CxUiMenuTarget new];
    AppKitMenu* p     = xaAllocStruct(AppKitMenu, XA_Zero);
    p->base.kind      = CXUI_PEER_Menu;
    CxUiNSMenu* menu  = [[CxUiNSMenu alloc] initWithTitle:@""];
    menu->peer        = p;
    menu.autoenablesItems = NO;
    menu.delegate     = menuTarget;
    p->menu           = (__bridge_retained void*)menu;
    if (bar)
        p->items = (__bridge_retained void*)[NSMutableArray new];
    return &p->base;
}

void _cxuiAppKitMenuInsert(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    AppKitMenu* p = (AppKitMenu*)peer;
    if (p->items) {
        [barItems(p) insertObject:itemNew(item, true) atIndex:(NSUInteger)at];
        if (shownBar == p)
            barFill();
        return;
    }
    [nsMenu(p) insertItem:itemNew(item, false) atIndex:at];
}

// The item lets go of its submenu, which belongs to its own peer.
void _cxuiAppKitMenuRemove(CxUiPeer* peer, int32 at)
{
    AppKitMenu* p = (AppKitMenu*)peer;
    if (p->items) {
        NSMenuItem* mi = barItems(p)[(NSUInteger)at];
        [mi.menu removeItem:mi];
        mi.submenu = nil;
        [barItems(p) removeObjectAtIndex:(NSUInteger)at];
        if (shownBar == p)
            barFill();
        return;
    }
    [nsMenu(p) itemAtIndex:at].submenu = nil;
    [nsMenu(p) removeItemAtIndex:at];
}

void _cxuiAppKitMenuUpdate(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    AppKitMenu* p = (AppKitMenu*)peer;
    if (p->items) {
        bool was = barItems(p)[(NSUInteger)at].hidden;
        itemSet(barItems(p)[(NSUInteger)at], item, true);
        if (shownBar == p && was != barItems(p)[(NSUInteger)at].hidden)
            barFill();
        return;
    }
    itemSet([nsMenu(p) itemAtIndex:at], item, false);
}

void _cxuiAppKitMenuDestroy(CxUiPeer* peer)
{
    AppKitMenu* p    = (AppKitMenu*)peer;
    CxUiNSMenu* menu = CFBridgingRelease(p->menu);
    NSMutableArray<NSMenuItem*>* items = p->items ? CFBridgingRelease(p->items) : nil;
    if (shownBar == p)
        _cxuiAppKitMenuShowBar(NULL);
    for (NSMenuItem* mi in items ? items : menu.itemArray)
        mi.submenu = nil;
    [menu removeAllItems];
    // A menu open now, or the menu bar, may still be calling into it.
    menu->peer = NULL;
    menu.delegate = nil;
    __autoreleasing id keep      = menu;
    __autoreleasing id keepItems = items;
    (void)keep;
    (void)keepItems;
    xaFree(p);
}

// ---- tests ----

static NSMenuItem* itemAt(AppKitMenu* p, int32 at)
{
    NSInteger n = p->items ? (NSInteger)barItems(p).count : nsMenu(p).numberOfItems;
    if (at < 0 || at >= n)
        return nil;
    return p->items ? barItems(p)[(NSUInteger)at] : [nsMenu(p) itemAtIndex:at];
}

int32 _cxuiAppKitTestMenuCount(CxUiPeer* peer)
{
    AppKitMenu* p = (AppKitMenu*)peer;
    return p->items ? (int32)barItems(p).count : (int32)nsMenu(p).numberOfItems;
}

bool _cxuiAppKitTestMenuItem(CxUiPeer* peer, int32 at, CxUiTestMenuItemInfo* out)
{
    NSMenuItem* mi = itemAt((AppKitMenu*)peer, at);
    if (!mi)
        return false;
    out->enabled = mi.enabled;
    if (mi.separatorItem) {
        out->kind = CXUI_MENU_Separator;
        return true;
    }
    strCopy(&out->title, (strref)mi.title.UTF8String);
    readHint(&out->hint, mi);
    out->kind    = (uint32)mi.tag;
    out->checked = mi.state == NSControlStateValueOn;
    out->submenu = mi.hasSubmenu;
    return true;
}

// As a click on it does: nothing for a grayed item, a separator, or one that opens a menu
bool _cxuiAppKitTestMenuChoose(CxUiPeer* peer, int32 at)
{
    AppKitMenu* p  = (AppKitMenu*)peer;
    NSMenuItem* mi = itemAt(p, at);
    if (!mi)
        return false;
    if (!p->items && mi.enabled && !mi.separatorItem && !mi.hasSubmenu)
        [nsMenu(p) performActionForItemAtIndex:at];
    return true;
}

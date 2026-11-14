#include "appkit_ui.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// NSAlert's runModal is modal to the whole program, so a parent only matters to the core, which
// keeps its close box from working. The queue runs inside runModal through the common modes.

static NSMutableArray<NSAlert*>* openAlerts;   // innermost last; main thread only

int32 _cxuiAppKitMessageBox(const CxUiMessageBoxDesc* desc, CxUiPeer* parent)
{
    unused_noeval(parent);

    NSArray<NSString*>* labels;
    switch (desc->buttons) {
    case CXUI_MB_Custom: {
        // macOS has no mnemonics, so the `&` is left out.
        NSMutableArray<NSString*>* custom = [NSMutableArray new];
        string shown                      = 0;
        for (int32 i = 0; i < 3 && !strEmpty(desc->custom[i]); i++) {
            _cxuiMnemonicStrip(&shown, desc->custom[i]);
            [custom addObject:_cxuiAppKitString(shown)];
        }
        strDestroy(&shown);
        labels = custom;
        break;
    }
    case CXUI_MB_OkCancel:
        labels = @[ @"OK", @"Cancel" ];
        break;
    case CXUI_MB_YesNo:
        labels = @[ @"Yes", @"No" ];
        break;
    case CXUI_MB_YesNoCancel:
        labels = @[ @"Yes", @"No", @"Cancel" ];
        break;
    default:
        labels = @[ @"OK" ];
        break;
    }

    NSAlert* alert = [NSAlert new];
    if (strEmpty(desc->title)) {
        alert.messageText = _cxuiAppKitString(desc->text);
    } else {
        alert.messageText     = _cxuiAppKitString(desc->title);
        alert.informativeText = _cxuiAppKitString(desc->text);
    }
    alert.alertStyle = desc->kind == CXUI_MB_Error     ? NSAlertStyleCritical
                       : desc->kind == CXUI_MB_Warning ? NSAlertStyleWarning
                                                       : NSAlertStyleInformational;

    // AppKit gives Return to the first button and Escape to one titled Cancel; a custom box's
    // cancel button takes Escape itself.
    for (NSString* label in labels)
        [alert addButtonWithTitle:label];
    NSInteger def = desc->defaultButton > 0 && desc->defaultButton <= (int32)labels.count ? desc->defaultButton - 1 : 0;
    if (def > 0) {
        alert.buttons[0].keyEquivalent   = @"";
        alert.buttons[def].keyEquivalent = @"\r";
    }
    if (desc->buttons == CXUI_MB_Custom && desc->cancelButton > 0 && desc->cancelButton - 1 != def)
        alert.buttons[desc->cancelButton - 1].keyEquivalent = @"\033";

    _cxuiAppKitBecomeRegular();
    if (!openAlerts)
        openAlerts = [NSMutableArray new];
    [openAlerts addObject:alert];
    _cxuiAppKitLoopDepth++;
    NSModalResponse r = [alert runModal];
    _cxuiAppKitLoopDepth--;
    [openAlerts removeObjectIdenticalTo:alert];
    _cxuiAppKitLoopLeft();

    NSInteger index = r - NSAlertFirstButtonReturn;
    return index >= 0 && index < (NSInteger)labels.count ? (int32)index + 1 : 0;
}

bool _cxuiAppKitTestAnswerMessageBox(int32 button)
{
    NSAlert* alert = openAlerts.lastObject;
    if (!alert || button < 1 || button > (int32)alert.buttons.count)
        return false;
    [alert.buttons[button - 1] performClick:nil];
    return true;
}

// ---- file dialogs ----

// Panels run modal to the whole program, like alerts. A test's answer cancels the panel and stands
// in for its result, as a panel's selection can't be set from code.
typedef struct AppKitFileBox {
    void* panel;   // the NSSavePanel or NSOpenPanel, unretained
    bool answered;
    bool chosen;
    string path;
    struct AppKitFileBox* outer;
} AppKitFileBox;

static AppKitFileBox* openFileBoxes;   // main thread only

// The filter's extensions as content types; nil where it lists every file
static NSArray<UTType*>* typesFor(const CxUiFileFilter* filter)
{
    NSMutableArray<UTType*>* types = [NSMutableArray new];
    for (NSString* p in [_cxuiAppKitString(filter->patterns) componentsSeparatedByString:@";"]) {
        NSString* pat = [p stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if ([pat isEqualToString:@"*"] || [pat isEqualToString:@"*.*"])
            return nil;
        if (![pat hasPrefix:@"*."])
            continue;
        UTType* t = [UTType typeWithFilenameExtension:[pat substringFromIndex:2]];
        if (t)
            [types addObject:t];
    }
    return types.count ? types : nil;
}

// macOS always asks before saving over a file, and lists the files of the filter chosen at first.
bool _cxuiAppKitFileDialog(const CxUiFileDialogDesc* desc, CxUiPeer* parent, string* out)
{
    unused_noeval(parent);
    NSSavePanel* panel;
    if (desc->mode == CXUI_FILE_Save) {
        panel = [NSSavePanel savePanel];
        if (!strEmpty(desc->initialFile))
            panel.nameFieldStringValue = _cxuiAppKitString(desc->initialFile);
    } else {
        NSOpenPanel* op           = [NSOpenPanel openPanel];
        op.canChooseFiles         = desc->mode != CXUI_FILE_Folder;
        op.canChooseDirectories   = desc->mode == CXUI_FILE_Folder;
        op.allowsMultipleSelection = NO;
        panel                     = op;
    }
    // Panels have no title bar; the title is shown as their message.
    if (!strEmpty(desc->title)) {
        panel.title   = _cxuiAppKitString(desc->title);
        panel.message = _cxuiAppKitString(desc->title);
    }
    if (!strEmpty(desc->initialDir))
        panel.directoryURL = [NSURL fileURLWithPath:_cxuiAppKitString(desc->initialDir) isDirectory:YES];
    if (desc->nfilters > 0) {
        NSArray<UTType*>* types = typesFor(&desc->filters[desc->filterIndex]);
        if (types)
            panel.allowedContentTypes = types;
    }

    AppKitFileBox box = { .panel = (__bridge void*)panel, .outer = openFileBoxes };
    openFileBoxes     = &box;
    _cxuiAppKitBecomeRegular();
    _cxuiAppKitLoopDepth++;
    NSModalResponse r = [panel runModal];
    _cxuiAppKitLoopDepth--;
    openFileBoxes = box.outer;
    _cxuiAppKitLoopLeft();

    bool chosen = false;
    if (box.answered) {
        chosen = box.chosen;
        if (chosen)
            strDup(out, box.path);
    } else if (r == NSModalResponseOK && panel.URL.path) {
        strDup(out, (strref)panel.URL.path.UTF8String);
        chosen = true;
    }
    strDestroy(&box.path);
    return chosen;
}

bool _cxuiAppKitTestAnswerFileDialog(strref path)
{
    AppKitFileBox* box = openFileBoxes;
    if (!box || box->answered)
        return false;
    box->answered = true;
    box->chosen   = path != NULL;
    strDup(&box->path, path);
    [(__bridge NSSavePanel*)box->panel cancel:nil];
    return true;
}

// ---- the color dialog ----

// The shared color panel runs modal, with OK and Cancel added below its own controls; closing it
// cancels. Its delegate is put back afterwards.
@interface CxUiColorAsk : NSObject <NSWindowDelegate>
@end

@implementation CxUiColorAsk
- (void)ok:(id)sender
{
    [NSApp stopModalWithCode:NSModalResponseOK];
}
- (void)cancel:(id)sender
{
    [NSApp stopModalWithCode:NSModalResponseCancel];
}
- (BOOL)windowShouldClose:(NSWindow*)w
{
    [NSApp stopModalWithCode:NSModalResponseCancel];
    return YES;
}
@end

static CxUiColorAsk* openColorAsk;   // main thread only

static NSButton* colorButton(NSString* title, SEL action, NSString* key, CxUiColorAsk* ask)
{
    NSButton* b      = [NSButton buttonWithTitle:title target:ask action:action];
    b.keyEquivalent  = key;
    b.autoresizingMask = NSViewMinXMargin;
    return b;
}

static NSView* colorButtons(CxUiColorAsk* ask)
{
    NSView* v        = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 220, 40)];
    NSButton* ok     = colorButton(@"OK", @selector(ok:), @"\r", ask);
    NSButton* cancel = colorButton(@"Cancel", @selector(cancel:), @"\033", ask);
    CGFloat w        = fmax(ok.fittingSize.width, cancel.fittingSize.width) + 12;
    CGFloat h        = ok.fittingSize.height;
    ok.frame         = NSMakeRect(220 - 8 - w, 8, w, h);
    cancel.frame     = NSMakeRect(220 - 16 - 2 * w, 8, w, h);
    [v addSubview:cancel];
    [v addSubview:ok];
    return v;
}

bool _cxuiAppKitColorDialog(const CxUiColorDialogDesc* desc, CxUiPeer* parent, CxUiColor* color)
{
    if (openColorAsk)
        return false;
    NSColorPanel* panel = NSColorPanel.sharedColorPanel;
    CxUiColorAsk* ask   = [CxUiColorAsk new];
    id<NSWindowDelegate> oldDelegate = panel.delegate;
    NSString* oldTitle  = panel.title;
    panel.showsAlpha    = NO;
    panel.color         = [NSColor colorWithSRGBRed:color->r / 255.0 green:color->g / 255.0 blue:color->b / 255.0 alpha:1.0];
    panel.accessoryView = colorButtons(ask);
    panel.delegate      = ask;
    if (!strEmpty(desc->title))
        panel.title = _cxuiAppKitString(desc->title);
    NSWindow* over = parent ? _cxuiAppKitPeerContent(parent).window : nil;
    if (over) {
        NSRect of = over.frame, pf = panel.frame;
        [panel setFrameOrigin:NSMakePoint(NSMidX(of) - pf.size.width / 2, NSMidY(of) - pf.size.height / 2)];
    } else {
        [panel center];
    }

    openColorAsk = ask;
    _cxuiAppKitBecomeRegular();
    _cxuiAppKitLoopDepth++;
    NSModalResponse r = [NSApp runModalForWindow:panel];
    _cxuiAppKitLoopDepth--;
    openColorAsk = nil;
    [panel orderOut:nil];
    panel.accessoryView = nil;
    panel.delegate      = oldDelegate;
    panel.title         = oldTitle;
    _cxuiAppKitLoopLeft();

    if (r != NSModalResponseOK)
        return false;
    NSColor* c = [panel.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!c)
        return false;
    color->r = (uint8)lround(fmin(fmax(c.redComponent, 0), 1) * 255);
    color->g = (uint8)lround(fmin(fmax(c.greenComponent, 0), 1) * 255);
    color->b = (uint8)lround(fmin(fmax(c.blueComponent, 0), 1) * 255);
    return true;
}

bool _cxuiAppKitTestAnswerColorDialog(const CxUiColor* color)
{
    if (!openColorAsk)
        return false;
    if (color) {
        NSColorPanel.sharedColorPanel.color = [NSColor colorWithSRGBRed:color->r / 255.0
                                                                  green:color->g / 255.0
                                                                   blue:color->b / 255.0
                                                                  alpha:1.0];
        [openColorAsk ok:nil];
    } else {
        [openColorAsk cancel:nil];
    }
    return true;
}

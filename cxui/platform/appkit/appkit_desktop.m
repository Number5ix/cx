#include "appkit_ui.h"

// ---- monitors ----

// Screens are in points with the origin at the bottom left of the primary one; cxui's rectangles
// have it at the top left, as window positions do.
static CxUiRect topLeftRect(NSRect r, CGFloat primaryH)
{
    return (CxUiRect){ (int32)lround(r.origin.x), (int32)lround(primaryH - (r.origin.y + r.size.height)),
                       (int32)lround(r.size.width), (int32)lround(r.size.height) };
}

// The first screen is the one with the menu bar, which is the primary one.
void _cxuiAppKitMonitors(sa_CxUiMonitor* out)
{
    NSArray<NSScreen*>* screens = NSScreen.screens;
    if (!screens.count)
        return;
    CGFloat primaryH = screens.firstObject.frame.size.height;
    for (NSUInteger i = 0; i < screens.count; i++) {
        NSScreen* s   = screens[i];
        CxUiMonitor m = {
            .bounds   = topLeftRect(s.frame, primaryH),
            .workArea = topLeftRect(s.visibleFrame, primaryH),
            .scale    = (float32)s.backingScaleFactor,
            .flags    = CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown | (i == 0 ? CXUI_MON_Primary : 0),
        };
        saPush(out, opaque, m);
    }
}

// ---- clipboard ----

// macOS lines end in \n already, so the text goes as it is.
bool _cxuiAppKitClipboardSetText(strref text)
{
    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    return [pb setString:_cxuiAppKitString(text) forType:NSPasteboardTypeString];
}

bool _cxuiAppKitClipboardGetText(string* out)
{
    NSString* s = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    if (!s)
        return false;
    strDup(out, (strref)s.UTF8String);
    return true;
}

#include "appkit_ui.h"

#include <ColorSync/ColorSync.h>

// ---- monitors ----

// Screens are in points with the origin at the bottom left of the primary one; cxui's rectangles
// have it at the top left, as window positions do.
static CxUiRect topLeftRect(NSRect r, CGFloat primaryH)
{
    return (CxUiRect){ (int32)lround(r.origin.x), (int32)lround(primaryH - (r.origin.y + r.size.height)),
                       (int32)lround(r.size.width), (int32)lround(r.size.height) };
}

// A display's UUID, which stays with the monitor from one run to the next
static void displayId(string* out, NSScreen* s)
{
    CGDirectDisplayID d = [s.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    CFUUIDRef uuid      = CGDisplayCreateUUIDFromDisplayID(d);
    if (!uuid)
        return;
    CFStringRef text = CFUUIDCreateString(NULL, uuid);
    strDup(out, (strref)((__bridge NSString*)text).UTF8String);
    CFRelease(text);
    CFRelease(uuid);
}

// The first screen is the one with the menu bar, which is the primary one.
void _cxuiAppKitMonitors(sa_CxUiMonitor* out)
{
    NSArray<NSScreen*>* screens = NSScreen.screens;
    if (!screens.count)
        return;
    CGFloat primaryH = screens.firstObject.frame.size.height;
    for (NSUInteger i = 0; i < screens.count; i++) {
        NSScreen* s    = screens[i];
        NSEdgeInsets safe = s.safeAreaInsets;
        CxUiMonitor m  = {
             .bounds    = topLeftRect(s.frame, primaryH),
             .workArea  = topLeftRect(s.visibleFrame, primaryH),
             .scale     = (float32)s.backingScaleFactor,
             .flags     = CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown | (i == 0 ? CXUI_MON_Primary : 0),
             .refreshHz = (float32)s.maximumFramesPerSecond,
             .safeArea  = { (int32)lround(safe.left), (int32)lround(safe.top), (int32)lround(safe.right),
                            (int32)lround(safe.bottom) },
        };
        displayId(&m.id, s);
        strDup(&m.name, (strref)s.localizedName.UTF8String);
        saPush(out, CxUiMonitor, m);
        cxuiMonitorDestroy(&m);
    }
}

int32 _cxuiAppKitWindowMonitor(NSWindow* w)
{
    NSUInteger i = w.screen ? [NSScreen.screens indexOfObject:w.screen] : NSNotFound;
    return i == NSNotFound ? -1 : (int32)i;
}

// For the life of the process, as NSApp's; a change between sessions has no one to tell.
void _cxuiAppKitDesktopInit(void)
{
    static id observer;
    if (observer)
        return;
    observer = [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidChangeScreenParametersNotification
                                                               object:nil
                                                                queue:nil
                                                           usingBlock:^(NSNotification* n) {
                                                             if (!_cxui.backend)
                                                                 return;
                                                             _cxuiAppKitMonitorsReframe();
                                                             _cxuiNativeEnter();
                                                             _cxuiMonitorsChanged();
                                                             _cxuiNativeLeave();
                                                           }];
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

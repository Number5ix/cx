#include "appkit_ui.h"

#include <Carbon/Carbon.h>   // kVK_* key codes
#import <QuartzCore/CAMetalLayer.h>

// A canvas is a flipped view inside the window's root view. Paint handlers run from a run loop
// observer just before the loop waits, in the common modes, never from drawRect:, which only
// copies the back buffer; so a handler can do anything, including destroy the canvas.
//
// A render view is a canvas view with no back buffer, backed by a CAMetalLayer that cxui never
// draws into: AppKit asking to show it again, as when it is resized, queues a paint, which tells
// the program to present a frame.

typedef struct AppKitCanvas AppKitCanvas;

// With text input on, the view is an input method's client: what it commits becomes text events,
// and what it is composing is kept here but not drawn.
@interface CxUiCanvasView : NSView <NSTextInputClient> {
  @public
    AppKitCanvas* peer;   // NULL once the peer is destroyed
    NSMutableAttributedString* marked;
    NSRange markedSel;
}
- (void)typeKey:(NSEvent*)ev;
@end

struct AppKitCanvas {
    CxUiCanvasPeer cp;
    void* view;          // CxUiCanvasView, retained
    CxUiRect bounds;     // physical pixels within the window's content
    CxUiRect painted;    // painted since the last drawRect:, to show
    bool paintPending;   // on the list for the observer
    uint32 cursor;
    // A cursor made from an image, while one is set, and the scale it was made for
    CxUiCursor* imageCursor;   // not a reference: the core holds it while it is set
    void* nsCursor;            // NSCursor, retained
    CGFloat nsCursorScale;
    CxUiPoint lastPointer;   // physical pixels
    flags_t textInput;
    flags_t keyCapture;
    CxUiRect caret;          // physical pixels, while hasCaret
    bool hasCaret;
    // CxUiPointerLocks in effect, where the lock began in the view and on the screen, and whether
    // this canvas hid the pointer
    uint32 lockMode;
    CxUiPoint lockAt;
    CGPoint lockGlobal;
    bool hidCursor;
    // Where the last locked move posted by another process put the pointer, while remoteValid
    CGPoint remoteLast;
    bool remoteValid;
};

static CxUiCanvasView* canvasView(AppKitCanvas* c)
{
    return (__bridge CxUiCanvasView*)c->view;
}

static CGFloat viewScale(NSView* v)
{
    return v.window ? v.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
}

// A press is being handed to cxui, which focuses the canvas if it should.
static bool pressing;

// ---- painting ----

// Canvases waiting for a paint, main thread only. A peer is taken off when destroyed.
static NSMutableArray<NSValue*>* pending;
static CFRunLoopObserverRef paintObserver;

static void paintPending(CFRunLoopObserverRef obs, CFRunLoopActivity activity, void* info)
{
    unused_noeval(obs);
    unused_noeval(activity);
    unused_noeval(info);
    while (pending.count > 0) {
        AppKitCanvas* c = pending.firstObject.pointerValue;
        [pending removeObjectAtIndex:0];
        c->paintPending = false;
        CxUiRect box;
        _cxuiNativeEnter();
        // Layout still queued runs first, so a canvas it moves or grows paints its new area.
        _cxuiLayoutNow(&c->cp.base);
        if (_cxuiCanvasPaint(&c->cp, &box) && c->cp.ops) {
            CxUiCanvasView* v = canvasView(c);
            CGFloat s         = viewScale(v);
            [v setNeedsDisplayInRect:NSMakeRect(box.x / s, box.y / s, box.w / s, box.h / s)];
        }
        _cxuiNativeLeave();   // c may be gone after this
    }
}

void _cxuiAppKitCanvasInit(void)
{
    if (paintObserver)
        return;
    pending = [NSMutableArray new];
    // Before Core Animation commits what the run loop's pass changed
    paintObserver = CFRunLoopObserverCreate(NULL, kCFRunLoopBeforeWaiting, true, 1000000, paintPending, NULL);
    CFRunLoopAddObserver(CFRunLoopGetMain(), paintObserver, kCFRunLoopCommonModes);
}

static void queuePaint(AppKitCanvas* c)
{
    if (c->paintPending)
        return;
    c->paintPending = true;
    [pending addObject:[NSValue valueWithPointer:c]];
    CFRunLoopWakeUp(CFRunLoopGetMain());
}

// ---- input ----

static uint32 eventMods(NSEventModifierFlags f)
{
    uint32 mods = 0;
    if (f & NSEventModifierFlagShift)
        mods |= CXUI_MOD_Shift;
    if (f & NSEventModifierFlagControl)
        mods |= CXUI_MOD_Ctrl;
    if (f & NSEventModifierFlagOption)
        mods |= CXUI_MOD_Alt;
    if (f & NSEventModifierFlagCommand)
        mods |= CXUI_MOD_Meta;
    return mods;
}

static uint32 heldButtons(void)
{
    // bit 0 left, 1 right, 2 middle, 3 and 4 the side buttons
    NSUInteger m = NSEvent.pressedMouseButtons;
    return ((m & 1) ? CXUI_BTN_Left : 0) | ((m & 2) ? CXUI_BTN_Right : 0) | ((m & 4) ? CXUI_BTN_Middle : 0) |
           ((m & 8) ? CXUI_BTN_X1 : 0) | ((m & 16) ? CXUI_BTN_X2 : 0);
}

static uint32 eventButton(NSEvent* ev)
{
    switch (ev.buttonNumber) {
    case 0: return CXUI_BTN_Left;
    case 1: return CXUI_BTN_Right;
    case 2: return CXUI_BTN_Middle;
    case 3: return CXUI_BTN_X1;
    case 4: return CXUI_BTN_X2;
    }
    return 0;
}

static CxUiKey keyCodeToKey(unsigned short code)
{
    switch (code) {
    case kVK_UpArrow:        return CXUI_KEY_Up;
    case kVK_DownArrow:      return CXUI_KEY_Down;
    case kVK_LeftArrow:      return CXUI_KEY_Left;
    case kVK_RightArrow:     return CXUI_KEY_Right;
    case kVK_Home:           return CXUI_KEY_Home;
    case kVK_End:            return CXUI_KEY_End;
    case kVK_PageUp:         return CXUI_KEY_PageUp;
    case kVK_PageDown:       return CXUI_KEY_PageDown;
    case kVK_Help:           return CXUI_KEY_Insert;
    case kVK_ForwardDelete:  return CXUI_KEY_Delete;
    case kVK_Delete:         return CXUI_KEY_Backspace;
    case kVK_Tab:            return CXUI_KEY_Tab;
    case kVK_Return:         return CXUI_KEY_Enter;
    case kVK_ANSI_KeypadEnter: return CXUI_KEY_PadEnter;
    case kVK_Escape:         return CXUI_KEY_Escape;
    case kVK_Space:          return CXUI_KEY_Space;
    case kVK_ANSI_Keypad0:   return CXUI_KEY_Pad0;
    case kVK_ANSI_Keypad1:   return CXUI_KEY_Pad1;
    case kVK_ANSI_Keypad2:   return CXUI_KEY_Pad2;
    case kVK_ANSI_Keypad3:   return CXUI_KEY_Pad3;
    case kVK_ANSI_Keypad4:   return CXUI_KEY_Pad4;
    case kVK_ANSI_Keypad5:   return CXUI_KEY_Pad5;
    case kVK_ANSI_Keypad6:   return CXUI_KEY_Pad6;
    case kVK_ANSI_Keypad7:   return CXUI_KEY_Pad7;
    case kVK_ANSI_Keypad8:   return CXUI_KEY_Pad8;
    case kVK_ANSI_Keypad9:   return CXUI_KEY_Pad9;
    case kVK_ANSI_KeypadDecimal:  return CXUI_KEY_PadDecimal;
    case kVK_ANSI_KeypadPlus:     return CXUI_KEY_PadAdd;
    case kVK_ANSI_KeypadMinus:    return CXUI_KEY_PadSubtract;
    case kVK_ANSI_KeypadMultiply: return CXUI_KEY_PadMultiply;
    case kVK_ANSI_KeypadDivide:   return CXUI_KEY_PadDivide;
    case kVK_Shift: case kVK_RightShift:     return CXUI_KEY_Shift;
    case kVK_Control: case kVK_RightControl: return CXUI_KEY_Ctrl;
    case kVK_Option: case kVK_RightOption:   return CXUI_KEY_Alt;
    case kVK_Command: case kVK_RightCommand: return CXUI_KEY_Meta;
    case kVK_CapsLock:       return CXUI_KEY_CapsLock;
    case kVK_F1: return CXUI_KEY_F1;   case kVK_F2: return CXUI_KEY_F2;   case kVK_F3: return CXUI_KEY_F3;
    case kVK_F4: return CXUI_KEY_F4;   case kVK_F5: return CXUI_KEY_F5;   case kVK_F6: return CXUI_KEY_F6;
    case kVK_F7: return CXUI_KEY_F7;   case kVK_F8: return CXUI_KEY_F8;   case kVK_F9: return CXUI_KEY_F9;
    case kVK_F10: return CXUI_KEY_F10; case kVK_F11: return CXUI_KEY_F11; case kVK_F12: return CXUI_KEY_F12;
    case kVK_F13: return CXUI_KEY_F13; case kVK_F14: return CXUI_KEY_F14; case kVK_F15: return CXUI_KEY_F15;
    case kVK_F16: return CXUI_KEY_F16; case kVK_F17: return CXUI_KEY_F17; case kVK_F18: return CXUI_KEY_F18;
    case kVK_F19: return CXUI_KEY_F19; case kVK_F20: return CXUI_KEY_F20;
    }
    return CXUI_KEY_None;
}

// The digit a key types with Shift, as on a French keyboard; CXUI_KEY_None if it isn't one
static CxUiKey shiftedDigit(NSEvent* ev)
{
    NSString* s = [ev charactersByApplyingModifiers:NSEventModifierFlagShift];
    unichar ch  = s.length == 1 ? [s characterAtIndex:0] : 0;
    return ch >= '0' && ch <= '9' ? (CxUiKey)(CXUI_KEY_0 + (ch - '0')) : CXUI_KEY_None;
}

// Letters, digits and punctuation by what the layout puts on the key, without Shift, or a digit
// typed with Shift; the rest by the key's position.
static CxUiKey eventKey(NSEvent* ev)
{
    CxUiKey key = keyCodeToKey(ev.keyCode);
    if (key != CXUI_KEY_None || ev.type == NSEventTypeFlagsChanged)
        return key;
    NSString* s = ev.charactersIgnoringModifiers.lowercaseString;
    if (s.length != 1)
        return shiftedDigit(ev);
    unichar ch = [s characterAtIndex:0];
    if (ch >= 'a' && ch <= 'z')
        return (CxUiKey)(CXUI_KEY_A + (ch - 'a'));
    if (ch >= '0' && ch <= '9')
        return (CxUiKey)(CXUI_KEY_0 + (ch - '0'));
    switch (ch) {
    case '.':  return CXUI_KEY_Period;
    case ',':  return CXUI_KEY_Comma;
    case '/':  return CXUI_KEY_Slash;
    case '-':  return CXUI_KEY_Minus;
    case '=':  return CXUI_KEY_Equals;
    case ';':  return CXUI_KEY_Semicolon;
    case '\'': return CXUI_KEY_Quote;
    case '[':  return CXUI_KEY_BracketLeft;
    case ']':  return CXUI_KEY_BracketRight;
    case '\\': return CXUI_KEY_Backslash;
    case '`':  return CXUI_KEY_Grave;
    }
    return CXUI_KEY_None;
}

// Whether a modifier key went down or up, from the flags after the change
static bool modifierDown(NSEvent* ev, CxUiKey key)
{
    NSEventModifierFlags f = ev.modifierFlags;
    switch (key) {
    case CXUI_KEY_Shift:    return (f & NSEventModifierFlagShift) != 0;
    case CXUI_KEY_Ctrl:     return (f & NSEventModifierFlagControl) != 0;
    case CXUI_KEY_Alt:      return (f & NSEventModifierFlagOption) != 0;
    case CXUI_KEY_Meta:     return (f & NSEventModifierFlagCommand) != 0;
    case CXUI_KEY_CapsLock: return (f & NSEventModifierFlagCapsLock) != 0;
    default:                return false;
    }
}

// A point of the view, in physical pixels, in Quartz's global display space: points from the top
// left of the primary screen
static CGPoint globalPoint(NSView* v, CxUiPoint px)
{
    CGFloat s  = viewScale(v);
    NSPoint at = [v.window convertPointToScreen:[v convertPoint:NSMakePoint((px.x + 0.5) / s, (px.y + 0.5) / s)
                                                         toView:nil]];
    return CGPointMake(at.x, NSMaxY(NSScreen.screens.firstObject.frame) - at.y);
}

// Puts the pointer back where the lock began. Associating again ends the moment after a warp
// when Quartz ignores the mouse.
static void lockWarpBack(AppKitCanvas* c)
{
    CGWarpMouseCursorPosition(c->lockGlobal);
    CGAssociateMouseAndMouseCursorPosition(c->lockMode != CXUI_POINTERLOCK_Raw);
}

_Use_decl_annotations_
bool _cxuiAppKitKey(CxUiPeer* p, NSEvent* ev)
{
    CxUiKey key = eventKey(ev);
    // Whether a modifier cxui has no code for went down or up can't be told.
    if (key == CXUI_KEY_None && ev.type == NSEventTypeFlagsChanged)
        return false;
    bool down = ev.type == NSEventTypeKeyDown || (ev.type == NSEventTypeFlagsChanged && modifierDown(ev, key));
    _cxuiNativeEnter();
    CxUiResult r = _cxuiKey(p, key, eventMods(ev.modifierFlags), ev.keyCode, down);
    _cxuiNativeLeave();
    return r == CXUI_Handled;
}

static NSCursor* cursorFor(uint32 cursor)
{
    switch (cursor) {
    case CXUI_CURSOR_IBeam:      return NSCursor.IBeamCursor;
    case CXUI_CURSOR_Hand:       return NSCursor.pointingHandCursor;
    case CXUI_CURSOR_Cross:      return NSCursor.crosshairCursor;
    case CXUI_CURSOR_SizeWE:     return NSCursor.resizeLeftRightCursor;
    case CXUI_CURSOR_SizeNS:     return NSCursor.resizeUpDownCursor;
    case CXUI_CURSOR_Move:       return NSCursor.openHandCursor;
    case CXUI_CURSOR_NotAllowed: return NSCursor.operationNotAllowedCursor;
    case CXUI_CURSOR_SizeNWSE:
    case CXUI_CURSOR_SizeNESW:
        // The window corners' own cursors, public since macOS 15
        if (@available(macOS 15.0, *))
            return [NSCursor frameResizeCursorFromPosition:cursor == CXUI_CURSOR_SizeNWSE
                                                               ? NSCursorFrameResizePositionTopLeft
                                                               : NSCursorFrameResizePositionTopRight
                                              inDirections:NSCursorFrameResizeDirectionsAll];
        break;
    case CXUI_CURSOR_None: {
        static NSCursor* none;
        if (!none)
            none = [[NSCursor alloc] initWithImage:[[NSImage alloc] initWithSize:NSMakeSize(1, 1)] hotSpot:NSZeroPoint];
        return none;
    }
    }
    // No public busy cursor (macOS shows its own when a program stops answering), and no diagonal
    // ones before 15
    return NSCursor.arrowCursor;
}

// An image's pixels as a cursor, shown at the image's size in physical pixels
static NSCursor* imageCursorFor(CxUiCursor* cur, CGFloat scale)
{
    CxUiImage* img = cur->_image;
    CxUiSize sz    = cxuiImageSize(img);
    int32 stride;
    const uint8* rows      = _cxuiImageRows(img, &stride);
    CGColorSpaceRef space  = CGColorSpaceCreateDeviceRGB();
    CFDataRef data         = CFDataCreate(NULL, rows, (CFIndex)stride * (sz.h - 1) + sz.w * 4);
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    // Premultiplied B, G, R, A in memory
    CGImageRef cg = CGImageCreate(sz.w, sz.h, 8, 32, stride, space,
                                  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, prov, NULL, false,
                                  kCGRenderingIntentDefault);
    NSImage* ns   = [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(sz.w / scale, sz.h / scale)];
    CGImageRelease(cg);
    CGDataProviderRelease(prov);
    CFRelease(data);
    CGColorSpaceRelease(space);
    return [[NSCursor alloc] initWithImage:ns hotSpot:NSMakePoint(cur->_hot.x / scale, cur->_hot.y / scale)];
}

// The canvas's cursor: an image's, made again when the scale changes, or a stock one
static NSCursor* canvasCursor(AppKitCanvas* c, CGFloat scale)
{
    if (!c->imageCursor)
        return cursorFor(c->cursor);
    if (!c->nsCursor || c->nsCursorScale != scale) {
        if (c->nsCursor)
            CFBridgingRelease(c->nsCursor);
        c->nsCursor      = (__bridge_retained void*)imageCursorFor(c->imageCursor, scale);
        c->nsCursorScale = scale;
    }
    return (__bridge NSCursor*)c->nsCursor;
}

@implementation CxUiCanvasView
- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)isOpaque
{
    return YES;
}
// AppKit focuses the view a left click lands on before the view sees the click; the press decides
// instead, so a canvas that can't take focus doesn't, and one that can isn't scrolled into view.
- (BOOL)acceptsFirstResponder
{
    return pressing || NSApp.currentEvent.type != NSEventTypeLeftMouseDown;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)ev
{
    return YES;
}

// Copies what was drawn; painting happened before, in the observer.
- (void)drawRect:(NSRect)dirty
{
    if (!peer || !peer->cp.ops)
        return;
    CGImageRef img = _cxuiAppKitTargetImage(peer->cp.target);
    if (!img)
        return;
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    CGFloat s        = viewScale(self);
    CxUiSize alloc   = _cxuiAppKitTargetAlloc(peer->cp.target);
    CGContextSaveGState(ctx);
    // The back buffer is bigger than the view; only the view's part of it is shown.
    CGContextClipToRect(ctx, NSIntersectionRect(dirty, self.bounds));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    // The view is flipped; the image draws bottom up, so flip it back around its own height.
    CGContextTranslateCTM(ctx, 0, alloc.h / s);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(0, 0, alloc.w / s, alloc.h / s), img);
    CGContextRestoreGState(ctx);
    CGImageRelease(img);
}

- (void)updateTrackingAreas
{
    for (NSTrackingArea* a in self.trackingAreas)
        [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc]
                              initWithRect:NSZeroRect
                                   options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                           NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil]];
    [super updateTrackingAreas];
}

// An image cursor is made again for the new scale.
- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self.window invalidateCursorRectsForView:self];
}

- (void)resetCursorRects
{
    if (peer)
        [self addCursorRect:self.visibleRect cursor:canvasCursor(peer, viewScale(self))];
}

- (CxUiPoint)px:(NSEvent*)ev
{
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    CGFloat s = viewScale(self);
    return (CxUiPoint){ (int32)floor(p.x * s), (int32)floor(p.y * s) };
}

// Whether another process posted the event, as remote control does. Those set the pointer's
// position, put it back after a warp, and can't be trusted for distances.
static bool postedEvent(CGEventRef cg)
{
    return cg && CGEventGetIntegerValueField(cg, kCGEventSourceStateID) != kCGEventSourceStateHIDSystemState &&
           CGEventGetIntegerValueField(cg, kCGEventSourceUnixProcessID) != 0;
}

// While locked, motion goes to cxui as distances: the event's own from the mouse, and the change
// in position between posted events, which leave the pointer where they put it.
- (void)lockedMotion:(NSEvent*)ev
{
    AppKitCanvas* c = peer;
    CGFloat s       = viewScale(self);
    CGEventRef cg   = ev.CGEvent;
    float32 dx, dy;
    if (postedEvent(cg)) {
        CGPoint at = CGEventGetLocation(cg);
        dx = c->remoteValid ? (float32)((at.x - c->remoteLast.x) * s) : 0;
        dy = c->remoteValid ? (float32)((at.y - c->remoteLast.y) * s) : 0;
        c->remoteLast  = at;
        c->remoteValid = true;
    } else {
        dx             = (float32)(ev.deltaX * s);
        dy             = (float32)(ev.deltaY * s);
        c->remoteValid = false;
        if (c->lockMode == CXUI_POINTERLOCK_Warp)
            lockWarpBack(c);
    }
    _cxuiNativeEnter();
    _cxuiCanvasPointerMotion(&c->cp.base, dx, dy, eventMods(ev.modifierFlags));
    _cxuiNativeLeave();
}

- (void)pointer:(CxUiPointerKind)kind event:(NSEvent*)ev
{
    if (!peer)
        return;
    if (peer->lockMode && (kind == CXUI_PTR_Move || kind == CXUI_PTR_Left)) {
        if (kind == CXUI_PTR_Move)
            [self lockedMotion:ev];
        return;
    }
    CxUiPoint at = [self px:ev];
    peer->lastPointer = at;
    uint32 button     = (kind == CXUI_PTR_Press || kind == CXUI_PTR_Release) ? eventButton(ev) : 0;
    _cxuiNativeEnter();
    bool was = pressing;
    pressing = kind == CXUI_PTR_Press;
    _cxuiCanvasPointer(&peer->cp.base, kind, button, heldButtons(), eventMods(ev.modifierFlags), at);
    pressing = was;
    _cxuiNativeLeave();
}

- (void)mouseDown:(NSEvent*)ev { [self pointer:CXUI_PTR_Press event:ev]; }
- (void)mouseUp:(NSEvent*)ev { [self pointer:CXUI_PTR_Release event:ev]; }
- (void)rightMouseDown:(NSEvent*)ev { [self pointer:CXUI_PTR_Press event:ev]; }
- (void)rightMouseUp:(NSEvent*)ev { [self pointer:CXUI_PTR_Release event:ev]; }
- (void)otherMouseDown:(NSEvent*)ev { [self pointer:CXUI_PTR_Press event:ev]; }
- (void)otherMouseUp:(NSEvent*)ev { [self pointer:CXUI_PTR_Release event:ev]; }
- (void)mouseMoved:(NSEvent*)ev { [self pointer:CXUI_PTR_Move event:ev]; }
- (void)mouseDragged:(NSEvent*)ev { [self pointer:CXUI_PTR_Move event:ev]; }
- (void)rightMouseDragged:(NSEvent*)ev { [self pointer:CXUI_PTR_Move event:ev]; }
- (void)otherMouseDragged:(NSEvent*)ev { [self pointer:CXUI_PTR_Move event:ev]; }
- (void)mouseEntered:(NSEvent*)ev { [self pointer:CXUI_PTR_Move event:ev]; }
- (void)mouseExited:(NSEvent*)ev { [self pointer:CXUI_PTR_Left event:ev]; }

- (void)scrollWheel:(NSEvent*)ev
{
    if (!peer)
        return;
    // Trackpads report points; a wheel, lines. macOS counts up and left as positive.
    CGFloat dx = ev.scrollingDeltaX, dy = ev.scrollingDeltaY;
    if (ev.hasPreciseScrollingDeltas) {
        dx /= 20;
        dy /= 20;
    }
    CxUiPoint at = [self px:ev];
    _cxuiNativeEnter();
    CxUiResult r = _cxuiCanvasWheel(&peer->cp.base, -dx, -dy, eventMods(ev.modifierFlags), at);
    _cxuiNativeLeave();
    if (r != CXUI_Handled)
        [super scrollWheel:ev];
}

- (void)keyDown:(NSEvent*)ev
{
    if (peer && peer->textInput)
        [self typeKey:ev];
    else if (!peer || !_cxuiAppKitKey(&peer->cp.base, ev))
        [super keyDown:ev];
}

// With text input on, the key goes to cxui and then to the input method, which raises text through
// insertText:. While it is composing, the keys are its own.
- (void)typeKey:(NSEvent*)ev
{
    if (marked.length == 0)
        _cxuiAppKitKey(&peer->cp.base, ev);
    // The handler may have destroyed the canvas, or turned text input off.
    if (peer && peer->textInput)
        [self interpretKeyEvents:@[ ev ]];
}
- (void)keyUp:(NSEvent*)ev
{
    if (!peer || !_cxuiAppKitKey(&peer->cp.base, ev))
        [super keyUp:ev];
}
- (void)flagsChanged:(NSEvent*)ev
{
    if (!peer || !_cxuiAppKitKey(&peer->cp.base, ev))
        [super flagsChanged:ev];
}

- (BOOL)becomeFirstResponder
{
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->cp.base, true);
        _cxuiNativeLeave();
    }
    return YES;
}
- (BOOL)resignFirstResponder
{
    if (peer) {
        _cxuiNativeEnter();
        _cxuiFocusChanged(&peer->cp.base, false);
        _cxuiNativeLeave();
    }
    return YES;
}

// ---- NSTextInputClient ----

// Without text input there is no input context, so input methods leave the keys alone.
- (NSTextInputContext*)inputContext
{
    return peer && peer->textInput ? [super inputContext] : nil;
}

- (void)insertText:(id)string replacementRange:(NSRange)range
{
    marked       = nil;
    NSString* in = [string isKindOfClass:NSAttributedString.class] ? [string string] : string;
    if (!peer || in.length == 0)
        return;
    // Control characters are keys, and the function keys' private characters aren't text.
    NSMutableString* text = [NSMutableString stringWithCapacity:in.length];
    for (NSUInteger i = 0; i < in.length; i++) {
        unichar ch = [in characterAtIndex:i];
        if (ch >= 0x20 && ch != 0x7F && !(ch >= 0xF700 && ch <= 0xF8FF))
            [text appendFormat:@"%C", ch];
    }
    if (text.length == 0)
        return;
    _cxuiNativeEnter();
    _cxuiCanvasText(&peer->cp.base, (strref)text.UTF8String);
    _cxuiNativeLeave();
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)sel replacementRange:(NSRange)range
{
    marked = [string isKindOfClass:NSAttributedString.class]
                 ? [[NSMutableAttributedString alloc] initWithAttributedString:string]
                 : [[NSMutableAttributedString alloc] initWithString:string];
    markedSel = sel;
}

- (void)unmarkText
{
    marked = nil;
}

- (BOOL)hasMarkedText
{
    return marked.length > 0;
}

- (NSRange)markedRange
{
    return marked.length > 0 ? NSMakeRange(0, marked.length) : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
    return marked.length > 0 ? markedSel : NSMakeRange(0, 0);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return NSNotFound;
}

// Where the input method's windows go: by the caret, or by the canvas without one
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    if (actual)
        *actual = range;
    NSRect r = self.bounds;
    if (peer && peer->hasCaret) {
        CGFloat s  = viewScale(self);
        CxUiRect c = peer->caret;
        r          = NSMakeRect(c.x / s, c.y / s, max(c.w, 1) / s, c.h / s);
    }
    return self.window ? [self.window convertRectToScreen:[self convertRect:r toView:nil]] : NSZeroRect;
}

// The key has already gone to cxui as a key event.
- (void)doCommandBySelector:(SEL)sel
{
}
@end

@interface CxUiRenderNSView : CxUiCanvasView
@end

@implementation CxUiRenderNSView
- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer
{
    return YES;
}
- (void)updateLayer
{
    if (peer)
        queuePaint(peer);
}
// The drawable follows the view's size in pixels.
- (void)fitDrawable
{
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    CGFloat s           = viewScale(self);
    NSSize size         = self.bounds.size;
    layer.contentsScale = s;
    layer.drawableSize  = CGSizeMake(fmax(round(size.width * s), 1), fmax(round(size.height * s), 1));
}
- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self fitDrawable];
}
- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self fitDrawable];
    self.needsDisplay = YES;
}
@end

// ---- operations ----

_Use_decl_annotations_
NSView* _cxuiAppKitCanvasView(CxUiPeer* p)
{
    return canvasView((AppKitCanvas*)p);
}

bool _cxuiAppKitIsCanvasView(id responder)
{
    return [responder isKindOfClass:CxUiCanvasView.class];
}

CxUiPeer* _cxuiAppKitCanvasCreate(CxUiCanvas* cv, CxUiPeer* parent)
{
    NSView* root = _cxuiAppKitPeerContent(parent);
    if (!root)
        return NULL;
    _cxuiAppKitCanvasInit();
    AppKitCanvas* c   = xaAllocStruct(AppKitCanvas, XA_Zero);
    c->cp.base.kind   = CXUI_PEER_Canvas;
    CxUiCanvasView* v = nil;
    if (objDynCast(CxUiRenderView, cv)) {
        v                           = [[CxUiRenderNSView alloc] initWithFrame:NSZeroRect];
        v.wantsLayer                = YES;
        v.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        [(CxUiRenderNSView*)v fitDrawable];
    } else {
        c->cp.ops    = &_cxuiAppKitCgOps;
        c->cp.target = c->cp.ops->targetCreate(&c->cp);
        v            = [[CxUiCanvasView alloc] initWithFrame:NSZeroRect];
    }
    // From macOS 14 a view draws outside its frame unless told not to.
    v.clipsToBounds   = YES;
    v->peer           = c;
    c->view           = (__bridge_retained void*)v;
    [root addSubview:v];
    return &c->cp.base;
}

void _cxuiAppKitCanvasSetBounds(CxUiPeer* p, CxUiRect px)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    c->bounds       = px;
    if (c->cp.ops)
        c->cp.ops->targetResize(c->cp.target, (CxUiSize){ px.w, px.h });
    _cxuiAppKitPlaceView(canvasView(c), px);
}

void _cxuiAppKitCanvasInvalidate(CxUiPeer* p, CxUiRect px)
{
    (void)px;
    queuePaint((AppKitCanvas*)p);
}

// AppKit shows the result as the run loop's pass ends; this paints now, and asks for it at once.
void _cxuiAppKitCanvasPaintNow(CxUiPeer* p)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    CxUiRect box;
    if (_cxuiCanvasPaint(&c->cp, &box) && c->cp.ops) {
        CxUiCanvasView* v = canvasView(c);
        CGFloat s         = viewScale(v);
        [v setNeedsDisplayInRect:NSMakeRect(box.x / s, box.y / s, box.w / s, box.h / s)];
        [v displayIfNeeded];
    }
}

// AppKit keeps the mouse with the view a button went down on, so there is nothing to do.
void _cxuiAppKitCanvasCapture(CxUiPeer* p, bool on)
{
    unused_noeval(p);
    unused_noeval(on);
}

void _cxuiAppKitCanvasSetCursor(CxUiPeer* p, uint32 cursor)
{
    AppKitCanvas* c   = (AppKitCanvas*)p;
    c->cursor         = cursor;
    CxUiCanvasView* v = canvasView(c);
    [v.window invalidateCursorRectsForView:v];
}

void _cxuiAppKitCanvasSetCursorImage(CxUiPeer* p, CxUiCursor* cursor)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    if (c->nsCursor)
        CFBridgingRelease(c->nsCursor);
    c->nsCursor       = NULL;
    c->imageCursor    = cursor;
    CxUiCanvasView* v = canvasView(c);
    [v.window invalidateCursorRectsForView:v];
}

bool _cxuiAppKitCanvasWarpPointer(CxUiPeer* p, CxUiPoint px)
{
    CxUiCanvasView* v = canvasView((AppKitCanvas*)p);
    if (!v.window)
        return false;
    CGWarpMouseCursorPosition(globalPoint(v, px));
    CGAssociateMouseAndMouseCursorPosition(true);
    return true;
}

bool _cxuiAppKitCanvasSetTextInput(CxUiPeer* p, flags_t flags)
{
    AppKitCanvas* c   = (AppKitCanvas*)p;
    CxUiCanvasView* v = canvasView(c);
    if (!flags && c->textInput) {
        [v.inputContext discardMarkedText];
        v->marked = nil;
    }
    c->textInput = flags;
    return true;
}

void _cxuiAppKitCanvasSetTextCaret(CxUiPeer* p, CxUiRect px)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    c->caret        = px;
    c->hasCaret     = true;
    [canvasView(c).inputContext invalidateCharacterCoordinates];
}

// Command already reaches a canvas, so there is nothing to do for the system key.
bool _cxuiAppKitCanvasSetKeyCapture(CxUiPeer* p, flags_t flags)
{
    ((AppKitCanvas*)p)->keyCapture = flags;
    return true;
}

_Use_decl_annotations_
bool _cxuiAppKitCanvasTakesKeys(id responder)
{
    return [responder isKindOfClass:CxUiCanvasView.class] && ((CxUiCanvasView*)responder)->peer &&
           (((CxUiCanvasView*)responder)->peer->keyCapture & CXUI_KEYCAPTURE_On);
}

_Use_decl_annotations_
bool _cxuiAppKitCanvasCaptureKey(id responder, NSEvent* ev)
{
    CxUiCanvasView* v = responder;
    // Command+Q still quits, through the menu.
    NSEventModifierFlags mods = ev.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask &
                                ~(NSEventModifierFlagCapsLock | NSEventModifierFlagNumericPad | NSEventModifierFlagFunction);
    if (ev.type != NSEventTypeFlagsChanged && mods == NSEventModifierFlagCommand &&
        [ev.charactersIgnoringModifiers.lowercaseString isEqualToString:@"q"])
        return false;
    if (ev.type == NSEventTypeKeyDown && v->peer->textInput)
        [v typeKey:ev];
    else
        _cxuiAppKitKey(&v->peer->cp.base, ev);
    return true;
}

// The pointer is hidden while locked, and in Raw mode stops moving while the mouse goes on
// reporting motion.
uint32 _cxuiAppKitCanvasPointerLock(CxUiPeer* p, uint32 mode, CxUiPoint px)
{
    AppKitCanvas* c   = (AppKitCanvas*)p;
    CxUiCanvasView* v = canvasView(c);
    if (mode == CXUI_POINTERLOCK_Off || !v.window) {
        if (c->lockMode) {
            c->lockMode   = CXUI_POINTERLOCK_Off;
            c->lockGlobal = globalPoint(v, px);
            lockWarpBack(c);
        }
        if (c->hidCursor) {
            [NSCursor unhide];
            c->hidCursor = false;
        }
        return CXUI_POINTERLOCK_Off;
    }
    c->lockMode    = mode;
    c->lockAt      = px;
    c->lockGlobal  = globalPoint(v, px);
    c->remoteValid = false;
    lockWarpBack(c);
    if (!c->hidCursor) {
        [NSCursor hide];
        c->hidCursor = true;
    }
    return mode;
}

void _cxuiAppKitDoubleClick(int64* time, int32* distance)
{
    *time     = (int64)(NSEvent.doubleClickInterval * 1e6);
    *distance = (int32)(4 * NSScreen.mainScreen.backingScaleFactor);
}

bool _cxuiAppKitTestExpose(CxUiPeer* p)
{
    CxUiCanvasView* v = canvasView((AppKitCanvas*)p);
    [v setNeedsDisplay:YES];
    [v displayIfNeeded];
    return true;
}

bool _cxuiAppKitTestScreenOrigin(CxUiPeer* p, CxUiPoint* out)
{
    CxUiCanvasView* v = canvasView((AppKitCanvas*)p);
    NSWindow* win     = v.window;
    if (!win)
        return false;
    // in physical pixels, from the top of the primary screen
    NSRect r    = [win convertRectToScreen:[v convertRect:v.bounds toView:nil]];
    CGFloat top = NSScreen.screens.firstObject.frame.size.height, s = win.backingScaleFactor;
    *out        = (CxUiPoint){ (int32)lround(r.origin.x * s), (int32)lround((top - (r.origin.y + r.size.height)) * s) };
    return true;
}

void _cxuiAppKitCanvasDestroy(CxUiPeer* p)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    if (c->paintPending)
        [pending removeObject:[NSValue valueWithPointer:c]];
    if (c->lockMode || c->hidCursor)
        _cxuiAppKitCanvasPointerLock(p, CXUI_POINTERLOCK_Off, c->lockAt);
    if (c->nsCursor)
        CFBridgingRelease(c->nsCursor);
    CxUiCanvasView* v = CFBridgingRelease(c->view);
    v->peer           = NULL;
    [v removeFromSuperview];
    // AppKit may still be calling into the view from further up the stack.
    __autoreleasing id keep = v;
    (void)keep;
    if (c->cp.ops)
        c->cp.ops->targetDestroy(c->cp.target);
    xaFree(c);
}

bool _cxuiAppKitSurfaceGet(CxUiPeer* p, CxUiNativeSurface* out)
{
    AppKitCanvas* c = (AppKitCanvas*)p;
    CALayer* layer  = c->cp.ops ? nil : canvasView(c).layer;
    if (![layer isKindOfClass:CAMetalLayer.class])
        return false;
    *out = (CxUiNativeSurface){ .kind = CXUI_SURFACE_Metal, .native = (__bridge void*)layer };
    return true;
}

// Where the view is in its window's content, in pixels. Metal's drawables can't be read back.
bool _cxuiAppKitTestSurfaceInfo(CxUiPeer* p, CxUiTestSurfaceInfo* out)
{
    AppKitCanvas* c   = (AppKitCanvas*)p;
    CxUiCanvasView* v = canvasView(c);
    NSView* content   = v.window.contentView;
    if (c->cp.ops || !content)
        return false;
    NSRect r  = [v convertRect:v.bounds toView:content];
    CGFloat s = viewScale(v);
    *out      = (CxUiTestSurfaceInfo){ .bounds = { (int32)lround(NSMinX(r) * s), (int32)lround(NSMinY(r) * s),
                                                   (int32)lround(NSWidth(r) * s), (int32)lround(NSHeight(r) * s) },
                                       .shown  = !v.hiddenOrHasHiddenAncestor };
    return true;
}

// ---- typing, for tests ----

static const unsigned short letterCodes[26] = {
    kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
    kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
    kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
    kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z,
};
static const unsigned short digitCodes[10] = {
    kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
    kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9,
};

// The character a key types without modifiers, as AppKit reports it; 0 for none
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
    case CXUI_KEY_Enter:
    case CXUI_KEY_PadEnter:  return '\r';
    case CXUI_KEY_Escape:    return 0x1B;
    case CXUI_KEY_Space:     return ' ';
    case CXUI_KEY_Backspace: return 0x7F;
    case CXUI_KEY_Delete:    return NSDeleteFunctionKey;
    case CXUI_KEY_Up:        return NSUpArrowFunctionKey;
    case CXUI_KEY_Down:      return NSDownArrowFunctionKey;
    case CXUI_KEY_Left:      return NSLeftArrowFunctionKey;
    case CXUI_KEY_Right:     return NSRightArrowFunctionKey;
    case CXUI_KEY_Home:      return NSHomeFunctionKey;
    case CXUI_KEY_End:       return NSEndFunctionKey;
    case CXUI_KEY_PageUp:    return NSPageUpFunctionKey;
    case CXUI_KEY_PageDown:  return NSPageDownFunctionKey;
    default:                 return 0;
    }
}

static bool keyCodeFor(CxUiKey key, unsigned short* code)
{
    if (key >= CXUI_KEY_A && key <= CXUI_KEY_Z) {
        *code = letterCodes[key - CXUI_KEY_A];
        return true;
    }
    if (key >= CXUI_KEY_0 && key <= CXUI_KEY_9) {
        *code = digitCodes[key - CXUI_KEY_0];
        return true;
    }
    for (unsigned short c = 0; c < 128; c++) {
        if (keyCodeToKey(c) == key) {
            *code = c;
            return true;
        }
    }
    return false;
}

// Posts the key's down and up to the window's queue, so they go through everything a real key
// does, the event monitor and the menu bar's shortcuts included; they arrive asynchronously.
bool _cxuiAppKitTestNativeKey(CxUiPeer* p, CxUiKey key, uint32 mods)
{
    NSWindow* win = _cxuiAppKitPeerView(p).window;
    unsigned short code;
    unichar plain = keyChar(key);
    if (!win || !plain || !keyCodeFor(key, &code))
        return false;
    unichar typed = plain;
    if ((mods & CXUI_MOD_Shift) && plain >= 'a' && plain <= 'z')
        typed = plain - 'a' + 'A';
    if ((mods & CXUI_MOD_Ctrl) && plain >= 'a' && plain <= 'z')
        typed = plain - 'a' + 1;
    NSEventModifierFlags f = ((mods & CXUI_MOD_Shift) ? NSEventModifierFlagShift : 0) |
                             ((mods & CXUI_MOD_Ctrl) ? NSEventModifierFlagControl : 0) |
                             ((mods & CXUI_MOD_Alt) ? NSEventModifierFlagOption : 0) |
                             ((mods & CXUI_MOD_Meta) ? NSEventModifierFlagCommand : 0);
    if (plain >= NSUpArrowFunctionKey)
        f |= NSEventModifierFlagFunction;
    NSString* chars = [NSString stringWithCharacters:&typed length:1];
    NSString* bare  = [NSString stringWithCharacters:&plain length:1];
    for (int i = 0; i < 2; i++) {
        NSEvent* ev = [NSEvent keyEventWithType:i == 0 ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                       location:NSZeroPoint
                                  modifierFlags:f
                                      timestamp:NSProcessInfo.processInfo.systemUptime
                                   windowNumber:win.windowNumber
                                        context:nil
                                     characters:chars
                    charactersIgnoringModifiers:bare
                                      isARepeat:NO
                                        keyCode:code];
        [NSApp postEvent:ev atStart:NO];
    }
    return true;
}

// Posts a left click to the window's queue, so it goes through the window as a real one does; it
// arrives asynchronously.
bool _cxuiAppKitTestNativeClick(CxUiPeer* p, CxUiPoint px)
{
    CxUiCanvasView* v = canvasView((AppKitCanvas*)p);
    NSWindow* win     = v.window;
    if (!win)
        return false;
    CGFloat s  = viewScale(v);
    NSPoint at = [v convertPoint:NSMakePoint((px.x + 0.5) / s, (px.y + 0.5) / s) toView:nil];
    for (int i = 0; i < 2; i++) {
        NSEvent* ev = [NSEvent mouseEventWithType:i == 0 ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp
                                         location:at
                                    modifierFlags:0
                                        timestamp:NSProcessInfo.processInfo.systemUptime
                                     windowNumber:win.windowNumber
                                          context:nil
                                      eventNumber:0
                                       clickCount:1
                                         pressure:i == 0 ? 1 : 0];
        [NSApp postEvent:ev atStart:NO];
    }
    return true;
}

// A wheel's lines, handed to the canvas as the window would; what cxui passes on goes up the
// responder chain to a scroll view. Lines up are positive to Quartz.
bool _cxuiAppKitTestNativeWheel(CxUiPeer* p, CxUiPoint px, int32 notches)
{
    CxUiCanvasView* v = canvasView((AppKitCanvas*)p);
    NSWindow* win     = v.window;
    if (!win)
        return false;
    CGFloat s     = viewScale(v);
    NSPoint at    = [win convertPointToScreen:[v convertPoint:NSMakePoint((px.x + 0.5) / s, (px.y + 0.5) / s)
                                                       toView:nil]];
    CGFloat top   = NSMaxY(NSScreen.screens.firstObject.frame);
    CGEventRef ce = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, -notches);
    if (!ce)
        return false;
    CGEventSetLocation(ce, CGPointMake(at.x, top - at.y));
    NSEvent* ev = [NSEvent eventWithCGEvent:ce];
    CFRelease(ce);
    [v scrollWheel:ev];
    return true;
}

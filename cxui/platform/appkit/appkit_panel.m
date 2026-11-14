#include "appkit_ui.h"

// A panel is a flipped view inside its host's view (the window's root view or another panel's);
// the peers of the widgets inside it are views inside it. Bounds are physical pixels in the
// host, which the frame gives in points. A scroll box's panel view is the document of an
// NSScrollView, as large as the content.

typedef struct AppKitPanel AppKitPanel;

@interface CxUiPanelView : NSView {
  @public
    NSColor* bg;
    NSColor* border;
    CGFloat borderPts;
    NSAttributedString* title;   // a frame's, or nil
    CGFloat titleIndent, titleGap;   // points
}
@end

@implementation CxUiPanelView
- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)isOpaque
{
    return bg != nil;
}
// A title moves the top edge down to the middle of its line, with a gap for the text.
- (void)drawRect:(NSRect)dirty
{
    NSRect r = self.bounds;
    dirty    = NSIntersectionRect(dirty, r);
    if (bg) {
        [bg setFill];
        NSRectFill(dirty);
    }
    NSSize text = title ? title.size : NSZeroSize;
    CGFloat top = title ? fmax((ceil(text.height) - borderPts) / 2, 0) : 0;
    CGFloat gap = title ? titleIndent + 2 * titleGap + ceil(text.width) : 0;
    if (border && borderPts > 0) {
        CGFloat w = r.size.width, h = r.size.height, b = borderPts;
        NSRect edges[5] = { NSMakeRect(0, top, title ? titleIndent : w, b), NSMakeRect(gap, top, title ? w - gap : 0, b),
                            NSMakeRect(0, h - b, w, b), NSMakeRect(0, top + b, b, h - top - 2 * b),
                            NSMakeRect(w - b, top + b, b, h - top - 2 * b) };
        [border setFill];
        for (int i = 0; i < 5; i++) {
            if (edges[i].size.width > 0 && edges[i].size.height > 0)
                NSRectFill(NSIntersectionRect(edges[i], dirty));
        }
    }
    if (title)
        [title drawAtPoint:NSMakePoint(titleIndent + titleGap, 0)];
}
@end

struct AppKitPanel {
    CxUiPeer base;
    void* view;     // CxUiPanelView, retained
    void* scroll;   // a scroll box's NSScrollView, retained
};

static CxUiPanelView* panelView(CxUiPeer* p)
{
    return (__bridge CxUiPanelView*)((AppKitPanel*)p)->view;
}

static NSScrollView* panelScroll(CxUiPeer* p)
{
    return (__bridge NSScrollView*)((AppKitPanel*)p)->scroll;
}

// The panel's own view in its host
static NSView* panelOuter(CxUiPeer* p)
{
    return ((AppKitPanel*)p)->scroll ? (NSView*)panelScroll(p) : panelView(p);
}

NSColor* _cxuiAppKitColor(CxUiColor c)
{
    return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:c.a / 255.0];
}

static CGFloat windowScale(NSView* v)
{
    return v.window ? v.window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
}

_Use_decl_annotations_
void _cxuiAppKitPlaceView(NSView* v, CxUiRect px)
{
    CGFloat s = windowScale(v);
    v.frame   = NSMakeRect(px.x / s, px.y / s, px.w / s, px.h / s);
}

NSView* _cxuiAppKitPanelView(CxUiPeer* p)
{
    return panelView(p);
}

CxUiPeer* _cxuiAppKitPanelCreate(CxUiWidget* w, CxUiPeer* host)
{
    unused_noeval(w);
    NSView* parent = _cxuiAppKitPeerContent(host);
    if (!parent)
        return NULL;
    AppKitPanel* p   = xaAllocStruct(AppKitPanel, XA_Zero);
    p->base.kind     = CXUI_PEER_Panel;
    CxUiPanelView* v = [[CxUiPanelView alloc] initWithFrame:NSZeroRect];
    // From macOS 14 a view draws outside its frame unless told not to.
    v.clipsToBounds  = YES;
    p->view          = (__bridge_retained void*)v;
    CxUiScrollBox* sb = objDynCast(CxUiScrollBox, w);
    if (sb) {
        NSScrollView* sv          = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        sv.hasHorizontalScroller  = (sb->_axes & CXUI_EXPAND_H) != 0;
        sv.hasVerticalScroller    = (sb->_axes & CXUI_EXPAND_V) != 0;
        sv.autohidesScrollers     = YES;
        sv.drawsBackground        = NO;
        sv.documentView           = v;
        p->scroll                 = (__bridge_retained void*)sv;
    }
    [parent addSubview:panelOuter(&p->base)];
    return &p->base;
}

_Use_decl_annotations_
CxUiPeer* _cxuiAppKitSurfaceCreate(NSView* parent)
{
    AppKitPanel* p   = xaAllocStruct(AppKitPanel, XA_Zero);
    p->base.kind     = CXUI_PEER_Panel;
    CxUiPanelView* v = [[CxUiPanelView alloc] initWithFrame:NSZeroRect];
    v.clipsToBounds  = YES;
    p->view          = (__bridge_retained void*)v;
    [parent addSubview:v];
    return &p->base;
}

void _cxuiAppKitPanelSetBounds(CxUiPeer* p, CxUiRect px)
{
    _cxuiAppKitPlaceView(panelOuter(p), px);
}

// The document takes the content's size, in points, and at least the view's.
void _cxuiAppKitPanelSetContent(CxUiPeer* p, CxUiSize px)
{
    if (!((AppKitPanel*)p)->scroll)
        return;
    CxUiPanelView* v = panelView(p);
    CGFloat s        = windowScale(v);
    NSSize view      = panelScroll(p).contentSize;
    [v setFrameSize:NSMakeSize(max(px.w / s, view.width), max(px.h / s, view.height))];
}

void _cxuiAppKitPanelScrollTo(CxUiPeer* p, CxUiRect px)
{
    if (!((AppKitPanel*)p)->scroll)
        return;
    CxUiPanelView* v = panelView(p);
    CGFloat s        = windowScale(v);
    [v scrollRectToVisible:NSMakeRect(px.x / s, px.y / s, px.w / s, px.h / s)];
}

// Overlay scroll bars float over the content.
int32 _cxuiAppKitScrollBarSize(void)
{
    if (NSScroller.preferredScrollerStyle == NSScrollerStyleOverlay)
        return 0;
    return (int32)ceil([NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy]);
}

bool _cxuiAppKitTestScrollPos(CxUiPeer* p, CxUiPoint* out)
{
    if (p->kind != CXUI_PEER_Panel || !((AppKitPanel*)p)->scroll)
        return false;
    NSPoint at = panelScroll(p).contentView.bounds.origin;
    CGFloat s  = windowScale(panelView(p));
    *out       = (CxUiPoint){ (int32)floor(at.x * s + 0.5), (int32)floor(at.y * s + 0.5) };
    return true;
}

// A notch moves the view by 40 points, kept within the document.
bool _cxuiAppKitTestScrollWheel(CxUiPeer* p, int32 notches)
{
    if (p->kind != CXUI_PEER_Panel || !((AppKitPanel*)p)->scroll)
        return false;
    NSScrollView* sv = panelScroll(p);
    NSClipView* clip = sv.contentView;
    NSPoint at       = clip.bounds.origin;
    CGFloat limit    = max(NSHeight(panelView(p).frame) - NSHeight(clip.bounds), 0);
    at.y             = min(max(at.y + notches * 40, 0), limit);
    [clip scrollToPoint:at];
    [sv reflectScrolledClipView:clip];
    return true;
}

void _cxuiAppKitPanelSetColors(CxUiPeer* p, CxUiColor bg, CxUiColor border, int32 borderPx)
{
    CxUiPanelView* v = panelView(p);
    v->bg            = _cxuiAppKitColor(bg);
    v->border        = borderPx > 0 ? _cxuiAppKitColor(border) : nil;
    v->borderPts     = borderPx / windowScale(v);
    [v setNeedsDisplay:YES];
}

void _cxuiAppKitPanelSetTitle(CxUiPeer* p, strref title, CxUiFont* f, CxUiColor color, int32 indent, int32 gap)
{
    CxUiPanelView* v = panelView(p);
    CGFloat s        = windowScale(v);
    v->title         = nil;
    if (!strEmpty(title) && f) {
        NSFont* font = (__bridge_transfer NSFont*)_cxuiAppKitFontAtPoints(f);
        v->title     = [[NSAttributedString alloc]
            initWithString:_cxuiAppKitString(title)
                attributes:@{ NSFontAttributeName : font, NSForegroundColorAttributeName : _cxuiAppKitColor(color) }];
    }
    v->titleIndent = indent / s;
    v->titleGap    = gap / s;
    [v setNeedsDisplay:YES];
}

void _cxuiAppKitPanelDestroy(CxUiPeer* p)
{
    CxUiPanelView* v = CFBridgingRelease(((AppKitPanel*)p)->view);
    NSScrollView* sv = ((AppKitPanel*)p)->scroll ? CFBridgingRelease(((AppKitPanel*)p)->scroll) : nil;
    [(sv ? (NSView*)sv : v) removeFromSuperview];
    // AppKit may still be calling into the views from further up the stack.
    __autoreleasing id keep   = v;
    __autoreleasing id keepSv = sv;
    (void)keep;
    (void)keepSv;
    xaFree(p);
}

// ---- any child view ----

static NSView* peerView(CxUiPeer* p)
{
    switch (p->kind) {
    case CXUI_PEER_Canvas:
        return _cxuiAppKitCanvasView(p);
    case CXUI_PEER_Panel:
        return panelOuter(p);
    case CXUI_PEER_Text:
        return _cxuiAppKitTextView(p);
    case CXUI_PEER_Window:
    case CXUI_PEER_Other:
        return nil;
    default:
        return _cxuiAppKitControlView(p);
    }
}

NSView* _cxuiAppKitPeerView(CxUiPeer* p)
{
    return p->kind == CXUI_PEER_Window ? _cxuiAppKitPeerContent(p) : peerView(p);
}

// A text field that already has the field editor keeps it, and its selection.
void _cxuiAppKitPeerFocus(CxUiPeer* p)
{
    NSView* v = _cxuiPeerIsControl(p->kind)  ? _cxuiAppKitControlFocusView(p)
                : p->kind == CXUI_PEER_Text ? _cxuiAppKitTextFocusView(p)
                                            : peerView(p);
    if (!v || (p->kind == CXUI_PEER_Text && _cxuiAppKitTextPeerFor(v.window.firstResponder) == p))
        return;
    [v.window makeFirstResponder:v];
}

// On the peer's own view, and on the one inside that takes focus, as AppKit asks the view under
// the mouse
void _cxuiAppKitPeerSetTip(CxUiPeer* p, strref tip)
{
    NSString* t = strEmpty(tip) ? nil : _cxuiAppKitString(tip);
    NSView* v   = peerView(p);
    v.toolTip   = t;
    NSView* in  = _cxuiPeerIsControl(p->kind) ? _cxuiAppKitControlFocusView(p)
                  : p->kind == CXUI_PEER_Text ? _cxuiAppKitTextFocusView(p)
                                              : nil;
    if (in && in != v)
        in.toolTip = t;
}

bool _cxuiAppKitTestTip(CxUiPeer* p, string* out)
{
    NSView* v = peerView(p);
    strCopy(out, v.toolTip ? (strref)v.toolTip.UTF8String : NULL);
    return v != nil;
}

void _cxuiAppKitPeerSetVisible(CxUiPeer* p, bool visible)
{
    NSView* v = peerView(p);
    if (v)
        v.hidden = !visible;
}

void _cxuiAppKitPeerRaise(CxUiPeer* p)
{
    // The local reference keeps the view alive while it moves.
    NSView* v      = peerView(p);
    NSView* parent = v.superview;
    if (parent && parent.subviews.lastObject != v)
        [parent addSubview:v positioned:NSWindowAbove relativeTo:nil];
}

bool _cxuiAppKitTestFrontmost(CxUiPeer* win, CxUiPeer* p, CxUiPoint px, bool* front)
{
    NSView* content = _cxuiAppKitPeerContent(win);
    NSView* v       = peerView(p);
    if (!content || !v || !content.window)
        return false;
    // The content view is flipped, so client pixels are its points times the window's scale.
    CGFloat s     = content.window.backingScaleFactor;
    NSPoint at    = NSMakePoint(px.x / s, px.y / s);
    NSView* above = content.superview ? content.superview : content;
    NSView* hit   = [content hitTest:[content convertPoint:at toView:above]];
    *front        = hit && (hit == v || [hit isDescendantOf:v]);
    return true;
}

bool _cxuiAppKitTestPeerShown(CxUiPeer* p, bool* shown)
{
    if (p->kind == CXUI_PEER_Window) {
        *shown = _cxuiAppKitPeerContent(p).window.visible;
        return true;
    }
    NSView* v = peerView(p);
    *shown    = v && !v.hiddenOrHasHiddenAncestor;
    return v != nil;
}

#include "cxui_private.h"

#include <cxui/desktop.h>

_Use_decl_annotations_
bool cxuiClipboardSetText(strref text)
{
    return _cxui.backend && _cxui.backend->clipboardSetText && _cxui.backend->clipboardSetText(text);
}

_Use_decl_annotations_
bool cxuiClipboardGetText(string* out)
{
    strClear(out);
    return _cxui.backend && _cxui.backend->clipboardGetText && _cxui.backend->clipboardGetText(out);
}

_Use_decl_annotations_
int32 cxuiMonitors(sa_CxUiMonitor* out)
{
    if (!out->a)
        saInit(out, opaque(CxUiMonitor), 4);
    saClear(out);
    if (_cxui.backend && _cxui.backend->monitors)
        _cxui.backend->monitors(out);
    return saSize(*out);
}

// Whether enough of the top edge is on the monitor's work area to take hold of with the mouse
static bool reachable(CxUiRect r, const CxUiMonitor* m)
{
    float32 s     = m->scale > 0 ? m->scale : 1.0f;
    CxUiRect a    = m->workArea;
    int32 overlap = min(r.x + r.w, a.x + a.w) - max(r.x, a.x);
    return overlap >= min(r.w, (int32)(100 * s)) && r.y >= a.y - (int32)(16 * s) && r.y + (int32)(24 * s) <= a.y + a.h;
}

static int64 distanceSq(CxUiRect r, CxUiRect a)
{
    int64 cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int64 dx = cx < a.x ? a.x - cx : cx > a.x + a.w ? cx - (a.x + a.w) : 0;
    int64 dy = cy < a.y ? a.y - cy : cy > a.y + a.h ? cy - (a.y + a.h) : 0;
    return dx * dx + dy * dy;
}

// The work area of the monitor nearest r's center; false if there are no monitors.
static bool workAreaNear(CxUiRect r, CxUiRect* out)
{
    sa_CxUiMonitor mons = { 0 };
    int32 n             = cxuiMonitors(&mons);
    int32 best          = -1;
    int64 bestSq        = -1;
    for (int32 i = 0; i < n; i++) {
        int64 d = distanceSq((CxUiRect){ r.x + r.w / 2, r.y + r.h / 2, 0, 0 }, mons.a[i].bounds);
        if (bestSq < 0 || d < bestSq) {
            best   = i;
            bestSq = d;
        }
    }
    if (best >= 0)
        *out = mons.a[best].workArea;
    saDestroy(&mons);
    return best >= 0;
}

_Use_decl_annotations_
CxUiPoint _cxuiNearPoint(CxUiRect win, CxUiRect other, uint32 where)
{
    CxUiPoint p = { other.x + (other.w - win.w) / 2, other.y + (other.h - win.h) / 2 };
    CxUiRect a;
    if (!workAreaNear(other, &a))
        a = (CxUiRect){ p.x, p.y, win.w, win.h };
    if (where == CXUI_NEAR_Right || where == CXUI_NEAR_Left) {
        int32 right = other.x + other.w, left = other.x - win.w;
        bool rightFits = right + win.w <= a.x + a.w, leftFits = left >= a.x;
        if (where == CXUI_NEAR_Right)
            p.x = rightFits ? right : left;
        else
            p.x = leftFits ? left : right;
        // Neither fits: the side with more room
        if (!rightFits && !leftFits)
            p.x = (a.x + a.w - right) >= (other.x - a.x) ? right : left;
    }
    p.x = max(min(p.x, a.x + a.w - win.w), a.x);
    p.y = max(min(p.y, a.y + a.h - win.h), a.y);
    return p;
}

_Use_decl_annotations_
void _cxuiPlacementClamp(CxUiPlacement* pl)
{
    if (!(pl->valid & (CXUI_PLACE_Position | CXUI_PLACE_Size)))
        return;
    sa_CxUiMonitor mons = { 0 };
    if (cxuiMonitors(&mons) == 0) {
        saDestroy(&mons);
        return;
    }

    CxUiRect* r = &pl->normal;
    if (!(pl->valid & CXUI_PLACE_Position)) {
        // Only the size means anything: no bigger than the biggest monitor
        int32 w = 0, h = 0;
        for (int32 i = 0; i < saSize(mons); i++) {
            w = max(w, mons.a[i].workArea.w);
            h = max(h, mons.a[i].workArea.h);
        }
        r->w = min(r->w, w);
        r->h = min(r->h, h);
        saDestroy(&mons);
        return;
    }

    int32 best   = 0;
    int64 bestSq = -1;
    for (int32 i = 0; i < saSize(mons); i++) {
        if (reachable(*r, &mons.a[i])) {
            saDestroy(&mons);
            return;
        }
        int64 d = distanceSq(*r, mons.a[i].workArea);
        if (bestSq < 0 || d < bestSq) {
            best   = i;
            bestSq = d;
        }
    }

    CxUiRect a = mons.a[best].workArea;
    r->w       = min(r->w, a.w);
    r->h       = min(r->h, a.h);
    r->x       = clamp(r->x, a.x, a.x + a.w - r->w);
    r->y       = clamp(r->y, a.y, a.y + a.h - r->h);
    saDestroy(&mons);
}

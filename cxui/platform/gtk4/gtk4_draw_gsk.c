#include "gtk4_ui.h"

#include <cx/utils/lazyinit.h>
#include <pango/pangocairo.h>

// GTK snapshots a whole widget whenever it is drawn, so a canvas keeps what was drawn as render
// nodes: a grid of tiles, each holding a clip of the pass that last painted it. A pass covers
// whole tiles, so each tile's node replaces the old one outright, and GSK's own damage tracking
// then redraws only the tiles that changed. Coordinates are physical pixels; the canvas widget
// scales the nodes to GTK's logical units when it appends them.

#define TILE 128

// ---- fonts ----

// One Pango context for every font, with fixed options, so measuring and drawing shape alike on
// every display. Used on one thread at a time: the UI thread, or a test's.
static PangoContext* pangoCtx;
static LazyInitState pangoInit;

static void pangoInitOnce(void* unused)
{
    unused_noeval(unused);
    PangoFontMap* fm          = pango_cairo_font_map_new();
    pangoCtx                  = pango_font_map_create_context(fm);
    g_object_unref(fm);
    cairo_font_options_t* opt = cairo_font_options_create();
    cairo_font_options_set_antialias(opt, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(opt, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(opt, CAIRO_HINT_METRICS_ON);
    pango_cairo_context_set_font_options(pangoCtx, opt);
    cairo_font_options_destroy(opt);
    pango_context_set_round_glyph_positions(pangoCtx, TRUE);
}

// What one codepoint shapes to on its own: the font from the fallback list that has it, its glyph,
// and its advance rounded to pixels. A NULL font means it needs shaping in context.
typedef struct PgGlyph {
    PangoFont* font;
    PangoGlyph glyph;
    int32 advance;
    bool known;
} PgGlyph;

#define PG_FONTS 8

typedef struct PgFont {
    PangoAttrList* attrs;   // the font description, for itemizing
    CxUiFontMetrics m;
    PgGlyph* pages[256];    // codepoints below 0x10000, 256 to a page, made on first use
    PangoFont* fonts[PG_FONTS];   // references for the fonts the pages name
    int32 nfonts;
} PgFont;

// A run shaped into items, each with its own font from the fallback list, every glyph's advance
// rounded to whole pixels.
typedef struct Shaped {
    GList* items;
    PangoGlyphString** glyphs;
    int32 n;
    int32 advance;
    const char* text;
    int32 len;
} Shaped;

static void shape(PgFont* f, strref s, Shaped* out)
{
    memset(out, 0, sizeof(*out));
    out->text  = strC(s);
    out->len   = strLen(s);
    out->items = pango_itemize(pangoCtx, out->text, 0, out->len, f->attrs, NULL);
    out->n     = (int32)g_list_length(out->items);
    out->glyphs = xaAlloc(sizeof(PangoGlyphString*) * max(out->n, 1));
    int32 i     = 0;
    for (GList* l = out->items; l; l = l->next, i++) {
        PangoItem* item  = l->data;
        PangoGlyphString* g = pango_glyph_string_new();
        pango_shape_with_flags(out->text + item->offset, item->length, out->text, out->len, &item->analysis, g,
                               PANGO_SHAPE_ROUND_POSITIONS);
        for (int32 j = 0; j < g->num_glyphs; j++) {
            g->glyphs[j].geometry.width = PANGO_UNITS_ROUND(g->glyphs[j].geometry.width);
            out->advance += g->glyphs[j].geometry.width / PANGO_SCALE;
        }
        out->glyphs[i] = g;
    }
}

static void shapedFree(Shaped* sh)
{
    for (int32 i = 0; i < sh->n; i++)
        pango_glyph_string_free(sh->glyphs[i]);
    xaFree(sh->glyphs);
    g_list_free_full(sh->items, (GDestroyNotify)pango_item_free);
}


// ---- simple runs ----

// Most text needs no shaping in context, so it is drawn from glyphs looked up one codepoint at a
// time and cached, without itemizing and shaping the run. These need context, and go through
// Pango in full.
static bool needsShaping(uint32 cp)
{
    return (cp >= 0x0300 && cp < 0x0370) || (cp >= 0x0590 && cp < 0x1E00) || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0xFE00 && cp < 0xFE10) || cp >= 0x10000;
}

static PangoFont* keepFont(PgFont* f, PangoFont* font)
{
    for (int32 i = 0; i < f->nfonts; i++) {
        if (f->fonts[i] == font)
            return font;
    }
    if (f->nfonts == PG_FONTS)
        return NULL;
    f->fonts[f->nfonts++] = g_object_ref(font);
    return font;
}

static PgGlyph lookupGlyph(PgFont* f, uint32 cp)
{
    PgGlyph none = { 0 };
    if (needsShaping(cp))
        return none;
    PgGlyph** page = &f->pages[cp >> 8];
    if (!*page)
        *page = xaAlloc(sizeof(PgGlyph) * 256, XA_Zero);
    PgGlyph* g = &(*page)[cp & 0xFF];
    if (g->known)
        return *g;

    char utf8[4] = { 0 };
    if (cp < 0x80) {
        utf8[0] = (char)cp;
    } else if (cp < 0x800) {
        utf8[0] = (char)(0xC0 | (cp >> 6));
        utf8[1] = (char)(0x80 | (cp & 0x3F));
    } else {
        utf8[0] = (char)(0xE0 | (cp >> 12));
        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (cp & 0x3F));
    }
    Shaped sh;
    shape(f, (strref)utf8, &sh);
    if (sh.n == 1 && sh.glyphs[0]->num_glyphs == 1) {
        g->font    = keepFont(f, ((PangoItem*)sh.items->data)->analysis.font);
        g->glyph   = sh.glyphs[0]->glyphs[0].glyph;
        g->advance = sh.advance;
    }
    g->known = true;
    shapedFree(&sh);
    return *g;
}

// A run's glyphs, if every codepoint has one that needs no context
typedef struct Simple {
    PgGlyph* g;
    int32 n;
    PgGlyph buf[128];
} Simple;

static bool simpleInit(PgFont* f, strref s, Simple* r)
{
    int32 count = _cxuiCodepoints(s);
    r->n        = 0;
    r->g        = count <= 128 ? r->buf : xaAlloc(sizeof(PgGlyph) * count);
    striter it;
    int32 cp;
    striBorrow(&it, s);
    while (r->n < count && striU8Char(&it, &cp)) {
        PgGlyph g = lookupGlyph(f, (uint32)cp);
        if (!g.font)
            break;
        r->g[r->n++] = g;
    }
    striFinish(&it);
    if (r->n == count)
        return true;
    if (r->g != r->buf)
        xaFree(r->g);
    return false;
}

static void simpleFree(Simple* r)
{
    if (r->g != r->buf)
        xaFree(r->g);
}

// Appends glyphs [from, to) of a simple run in one font, each advance given, and returns the width.
typedef void (*AppendGlyphs)(void* ctx, PangoFont* font, PangoGlyphString* g, float32 x);

static int32 simpleSegments(Simple* r, const int32* widths, float32 x, AppendGlyphs fn, void* ctx)
{
    // gsk_text_node_new copies the glyphs, so one scratch string serves every run.
    static PangoGlyphString* scratch;
    if (!scratch)
        scratch = pango_glyph_string_new();
    int32 total = 0;
    for (int32 i = 0; i < r->n;) {
        int32 j = i;
        while (j < r->n && r->g[j].font == r->g[i].font)
            j++;
        pango_glyph_string_set_size(scratch, j - i);
        int32 seg = 0;
        for (int32 k = i; k < j; k++) {
            // Cleared whole: set_size doesn't, and a stale is_color bit has GSK draw the glyph in its
            // own colors instead of the text's, which can be invisible.
            PangoGlyphInfo* gi        = &scratch->glyphs[k - i];
            int32 w                   = widths ? widths[k] : r->g[k].advance;
            memset(gi, 0, sizeof(*gi));
            gi->glyph                 = r->g[k].glyph;
            gi->geometry.width        = w * PANGO_SCALE;
            gi->attr.is_cluster_start = 1;
            scratch->log_clusters[k - i] = k - i;
            seg += w;
        }
        if (fn)
            fn(ctx, r->g[i].font, scratch, x + total);
        total += seg;
        i = j;
    }
    return total;
}

static int32 pgAdvance(PgFont* f, strref s)
{
    Simple r;
    if (simpleInit(f, s, &r)) {
        int32 adv = 0;
        for (int32 i = 0; i < r.n; i++)
            adv += r.g[i].advance;
        simpleFree(&r);
        return adv;
    }
    Shaped sh;
    shape(f, s, &sh);
    int32 adv = sh.advance;
    shapedFree(&sh);
    return adv;
}

// The family and size of GTK's interface font, where there is a display to ask.
static void uiFont(string* family, float32* points)
{
    strDup(family, _S"Sans");
    *points = 10;
    if (!gdk_display_get_default())
        return;
    char* name = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-font-name", &name, NULL);
    if (!name)
        return;
    PangoFontDescription* d = pango_font_description_from_string(name);
    if (pango_font_description_get_family(d))
        strCopy(family, (strref)pango_font_description_get_family(d));
    if (pango_font_description_get_size(d) > 0 && !pango_font_description_get_size_is_absolute(d))
        *points = (float32)pango_font_description_get_size(d) / PANGO_SCALE;
    pango_font_description_free(d);
    g_free(name);
}

static void* pgFontCreate(strref faces, float32 px, float32 scale, uint32 flags)
{
    lazyInit(&pangoInit, pangoInitOnce, NULL);
    string uiFamily = 0, families = 0, face = 0;
    float32 uiPoints;
    uiFont(&uiFamily, &uiPoints);
    if (px <= 0)
        px = uiPoints * scale * 96.0f / 72.0f;

    // Pango tries each family of a comma-separated list in turn.
    int32 len = strLen(faces);
    for (int32 start = 0; start <= len;) {
        int32 end = strFindChar(faces, start, ';');
        if (end < 0)
            end = len;
        strSubStr(&face, faces, start, end);
        strTrim(&face, face, 0);
        start = end + 1;
        if (strEmpty(face))
            continue;
        strref fam = strEqi(face, _S"ui")          ? (strref)uiFamily
                     : strEqi(face, _S"sans")      ? _S"Sans"
                     : strEqi(face, _S"serif")     ? _S"Serif"
                     : strEqi(face, _S"monospace") ? _S"Monospace"
                                                   : (strref)face;
        if (!strEmpty(families))
            strAppend(&families, _S",");
        strAppend(&families, fam);
    }
    if (strEmpty(families))
        strDup(&families, uiFamily);

    PangoFontDescription* d = pango_font_description_new();
    pango_font_description_set_family(d, strC(families));
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_font_description_set_weight(d, (flags & CXUI_FONT_Bold) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_style(d, (flags & CXUI_FONT_Italic) ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);

    PgFont* f = xaAllocStruct(PgFont, XA_Zero);
    f->attrs  = pango_attr_list_new();
    pango_attr_list_insert(f->attrs, pango_attr_font_desc_new(d));
    PangoFontMetrics* pm = pango_context_get_metrics(pangoCtx, d, NULL);
    f->m.ascent          = PANGO_PIXELS_CEIL(pango_font_metrics_get_ascent(pm));
    f->m.descent         = PANGO_PIXELS_CEIL(pango_font_metrics_get_descent(pm));
    f->m.height          = f->m.ascent + f->m.descent;
    f->m.avgCharW        = max(PANGO_PIXELS(pango_font_metrics_get_approximate_char_width(pm)), 1);
    f->m.cellW           = max(pgAdvance(f, _S"0"), 1);
    f->m.cellH           = f->m.height;
    pango_font_metrics_unref(pm);
    pango_font_description_free(d);
    strDestroy(&uiFamily);
    strDestroy(&families);
    strDestroy(&face);
    return f;
}

_Use_decl_annotations_
PangoFontDescription* _cxuiGtk4FontDescription(CxUiFont* f)
{
    PgFont* pf                = _cxuiFontNative(f, &_cxuiGtk4GskOps);
    PangoAttrIterator* it     = pango_attr_list_get_iterator(pf->attrs);
    PangoAttrFontDesc* a      = (PangoAttrFontDesc*)pango_attr_iterator_get(it, PANGO_ATTR_FONT_DESC);
    PangoFontDescription* d   = pango_font_description_copy(a->desc);
    pango_attr_iterator_destroy(it);
    return d;
}

static void pgFontDestroy(void* fp)
{
    PgFont* f = fp;
    for (int32 i = 0; i < 256; i++)
        xaFree(f->pages[i]);
    for (int32 i = 0; i < f->nfonts; i++)
        g_object_unref(f->fonts[i]);
    pango_attr_list_unref(f->attrs);
    xaFree(f);
}

static void pgFontMetrics(void* fp, CxUiFontMetrics* out)
{
    *out = ((PgFont*)fp)->m;
}

static int32 pgFontAdvance(void* fp, strref s)
{
    return pgAdvance(fp, s);
}

typedef struct GskTarget {
    CxUiSize size;
    int32 cols;
    int32 rows;
    GskRenderNode** tiles;   // cols * rows; NULL where nothing has been drawn
    // During a pass
    GtkSnapshot* snap;
    CxUiRect dirty;
    bool innerClip;   // a clip pushed inside the dirty one
} GskTarget;

static void dropTiles(GskTarget* t)
{
    for (int32 i = 0; i < t->cols * t->rows; i++) {
        if (t->tiles[i])
            gsk_render_node_unref(t->tiles[i]);
    }
    xaDestroy(&t->tiles);
    t->cols = t->rows = 0;
}

static void* gskTargetCreate(CxUiCanvasPeer* p)
{
    unused_noeval(p);
    return xaAllocStruct(GskTarget, XA_Zero);
}

static void gskTargetResize(void* tp, CxUiSize px)
{
    GskTarget* t = tp;
    int32 cols = (px.w + TILE - 1) / TILE, rows = (px.h + TILE - 1) / TILE;
    t->size    = px;
    if (cols == t->cols && rows == t->rows)
        return;
    // A new size repaints everything, so nothing needs keeping.
    dropTiles(t);
    t->cols = cols;
    t->rows = rows;
    if (cols * rows > 0)
        t->tiles = xaAlloc(sizeof(GskRenderNode*) * cols * rows, XA_Zero);
}

static void gskTargetDestroy(void* tp)
{
    GskTarget* t = tp;
    if (t->snap)
        g_object_unref(t->snap);
    dropTiles(t);
    xaFree(t);
}

static CxUiRect gskExpandDirty(void* tp, CxUiRect r)
{
    GskTarget* t = tp;
    int32 x0 = r.x / TILE * TILE, y0 = r.y / TILE * TILE;
    int32 x1 = min((r.x + r.w + TILE - 1) / TILE * TILE, t->size.w);
    int32 y1 = min((r.y + r.h + TILE - 1) / TILE * TILE, t->size.h);
    return (CxUiRect){ x0, y0, x1 - x0, y1 - y0 };
}

static graphene_rect_t grect(CxUiRect r)
{
    return GRAPHENE_RECT_INIT((float)r.x, (float)r.y, (float)r.w, (float)r.h);
}

static GdkRGBA rgba(CxUiColor c)
{
    return (GdkRGBA){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
}

static void gskBegin(void* tp, CxUiRect dirty)
{
    GskTarget* t    = tp;
    t->snap         = gtk_snapshot_new();
    t->dirty        = dirty;
    t->innerClip    = false;
    graphene_rect_t r = grect(dirty);
    gtk_snapshot_push_clip(t->snap, &r);
}

static void gskEnd(void* tp)
{
    GskTarget* t = tp;
    if (t->innerClip)
        gtk_snapshot_pop(t->snap);
    gtk_snapshot_pop(t->snap);
    GskRenderNode* node = gtk_snapshot_free_to_node(t->snap);
    t->snap             = NULL;

    // The pass covered these tiles completely.
    int32 c0 = t->dirty.x / TILE, c1 = (t->dirty.x + t->dirty.w + TILE - 1) / TILE;
    int32 r0 = t->dirty.y / TILE, r1 = (t->dirty.y + t->dirty.h + TILE - 1) / TILE;
    for (int32 row = r0; row < min(r1, t->rows); row++) {
        for (int32 col = c0; col < min(c1, t->cols); col++) {
            GskRenderNode** tile = &t->tiles[row * t->cols + col];
            if (*tile)
                gsk_render_node_unref(*tile);
            graphene_rect_t tr = GRAPHENE_RECT_INIT(col * TILE, row * TILE, TILE, TILE);
            *tile              = node ? gsk_clip_node_new(node, &tr) : NULL;
        }
    }
    if (node)
        gsk_render_node_unref(node);
}

static void gskFill(void* tp, CxUiRect r, CxUiColor c)
{
    GdkRGBA color     = rgba(c);
    graphene_rect_t b = grect(r);
    gtk_snapshot_append_color(((GskTarget*)tp)->snap, &color, &b);
}

static void gskLine(void* tp, CxUiPoint a, CxUiPoint b, int32 width, CxUiColor c)
{
    graphene_rect_t bounds = GRAPHENE_RECT_INIT((float)(min(a.x, b.x) - width), (float)(min(a.y, b.y) - width),
                                                (float)(abs(b.x - a.x) + 2 * width + 1),
                                                (float)(abs(b.y - a.y) + 2 * width + 1));
    cairo_t* cr = gtk_snapshot_append_cairo(((GskTarget*)tp)->snap, &bounds);
    cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    cairo_set_line_width(cr, width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    cairo_move_to(cr, a.x + 0.5, a.y + 0.5);
    cairo_line_to(cr, b.x + 0.5, b.y + 0.5);
    cairo_stroke(cr);
    cairo_destroy(cr);
}

static void appendRun(GtkSnapshot* snap, PangoFont* font, PangoGlyphString* g, CxUiColor c, float32 x, float32 y)
{
    GdkRGBA color         = rgba(c);
    graphene_point_t base = GRAPHENE_POINT_INIT(x, y);
    // NULL for a run with nothing to draw, such as spaces
    GskRenderNode* node = gsk_text_node_new(font, g, &color, &base);
    if (node) {
        gtk_snapshot_append_node(snap, node);
        gsk_render_node_unref(node);
    }
}

typedef struct RunCtx {
    GtkSnapshot* snap;
    CxUiColor c;
    float32 y;
} RunCtx;

static void appendSegment(void* ctx, PangoFont* font, PangoGlyphString* g, float32 x)
{
    RunCtx* rc = ctx;
    appendRun(rc->snap, font, g, rc->c, x, rc->y);
}

static int32 gskText(void* tp, void* fp, CxUiPoint at, strref s, CxUiColor c)
{
    PgFont* f = fp;
    Simple r;
    if (simpleInit(f, s, &r)) {
        RunCtx rc = { ((GskTarget*)tp)->snap, c, (float32)(at.y + f->m.ascent) };
        int32 adv = simpleSegments(&r, NULL, (float32)at.x, appendSegment, &rc);
        simpleFree(&r);
        return adv;
    }
    Shaped sh;
    shape(f, s, &sh);
    int32 x = at.x, i = 0;
    for (GList* l = sh.items; l; l = l->next, i++) {
        appendRun(((GskTarget*)tp)->snap, ((PangoItem*)l->data)->analysis.font, sh.glyphs[i], c, (float32)x,
                  (float32)(at.y + f->m.ascent));
        x += pango_glyph_string_get_width(sh.glyphs[i]) / PANGO_SCALE;
    }
    int32 adv = sh.advance;
    shapedFree(&sh);
    return adv;
}

// Each glyph's width is rewritten so that codepoint n starts at cell n, as terminals do.
static void gskTextCells(void* tp, void* fp, CxUiPoint at, strref s, const int32* cellw, CxUiColor c)
{
    PgFont* f = fp;
    Simple r;
    if (simpleInit(f, s, &r)) {
        RunCtx rc = { ((GskTarget*)tp)->snap, c, (float32)(at.y + f->m.ascent) };
        simpleSegments(&r, cellw, (float32)at.x, appendSegment, &rc);
        simpleFree(&r);
        return;
    }
    Shaped sh;
    shape(f, s, &sh);

    // The codepoint each byte belongs to, and where each codepoint's cell starts
    int32* cpAt = xaAlloc(sizeof(int32) * (sh.len + 1));
    int32 cp    = -1;
    for (int32 b = 0; b < sh.len; b++) {
        if (((uint8)sh.text[b] & 0xC0) != 0x80)
            cp++;
        cpAt[b] = cp;
    }
    cpAt[sh.len]  = cp + 1;
    int32* cellX  = xaAlloc(sizeof(int32) * (cp + 2));
    cellX[0]      = 0;
    for (int32 i = 0; i <= cp; i++)
        cellX[i + 1] = cellX[i] + cellw[i];

    int32 i = 0;
    for (GList* l = sh.items; l; l = l->next, i++) {
        PangoItem* item     = l->data;
        PangoGlyphString* g = sh.glyphs[i];
        for (int32 j = 0; j < g->num_glyphs; j++) {
            int32 start = item->offset + g->log_clusters[j];
            bool first  = j == 0 || g->log_clusters[j - 1] != g->log_clusters[j];
            int32 next  = item->offset + item->length;
            for (int32 k = j + 1; k < g->num_glyphs; k++) {
                if (g->log_clusters[k] != g->log_clusters[j]) {
                    next = item->offset + g->log_clusters[k];
                    break;
                }
            }
            g->glyphs[j].geometry.x_offset = 0;
            g->glyphs[j].geometry.width    = first ? (cellX[cpAt[next]] - cellX[cpAt[start]]) * PANGO_SCALE : 0;
        }
        appendRun(((GskTarget*)tp)->snap, item->analysis.font, g, c, (float32)(at.x + cellX[cpAt[item->offset]]),
                  (float32)(at.y + f->m.ascent));
    }
    xaFree(cellX);
    xaFree(cpAt);
    shapedFree(&sh);
}

// ---- images ----

// The texture keeps its own copy: tiles may still hold nodes using it after the image is gone.
static void* gskImageUpload(const void* px, int32 w, int32 h)
{
    GBytes* bytes   = g_bytes_new(px, (gsize)w * h * 4);
    GdkTexture* tex = gdk_memory_texture_new(w, h, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize)w * 4);
    g_bytes_unref(bytes);
    return tex;
}

static void gskImageFree(void* img)
{
    g_object_unref(img);
}

// The whole texture is scaled so src lands on dst, then clipped to dst.
static void gskImage(void* tp, void* img, CxUiRect src, CxUiRect dst)
{
    GskTarget* t      = tp;
    GdkTexture* tex   = img;
    float32 sx        = (float32)dst.w / src.w, sy = (float32)dst.h / src.h;
    graphene_rect_t b = GRAPHENE_RECT_INIT(dst.x - src.x * sx, dst.y - src.y * sy, gdk_texture_get_width(tex) * sx,
                                           gdk_texture_get_height(tex) * sy);
    graphene_rect_t c = grect(dst);
    gtk_snapshot_push_clip(t->snap, &c);
    gtk_snapshot_append_scaled_texture(t->snap, tex, GSK_SCALING_FILTER_LINEAR, &b);
    gtk_snapshot_pop(t->snap);
}

static void gskClipSet(void* tp, CxUiRect r)
{
    GskTarget* t = tp;
    if (t->innerClip)
        gtk_snapshot_pop(t->snap);
    graphene_rect_t b = grect(r);
    gtk_snapshot_push_clip(t->snap, &b);
    t->innerClip = true;
}

_Use_decl_annotations_
void _cxuiGtk4TargetAppend(void* tp, GtkSnapshot* snap)
{
    GskTarget* t = tp;
    for (int32 i = 0; i < t->cols * t->rows; i++) {
        if (t->tiles[i])
            gtk_snapshot_append_node(snap, t->tiles[i]);
    }
}

// Tests render the tiles with the cairo renderer, which needs no display.
static void gskTestRead(void* tp, uint8* rgba)
{
    static GskRenderer* renderer;
    GskTarget* t = tp;
    if (!renderer) {
        renderer = gsk_cairo_renderer_new();
        if (!gsk_renderer_realize(renderer, NULL, NULL)) {
            g_clear_object(&renderer);
            memset(rgba, 0, (size_t)t->size.w * t->size.h * 4);
            return;
        }
    }

    GtkSnapshot* snap = gtk_snapshot_new();
    _cxuiGtk4TargetAppend(t, snap);
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);
    if (!node)
        node = gsk_container_node_new(NULL, 0);
    graphene_rect_t view = GRAPHENE_RECT_INIT(0, 0, (float)t->size.w, (float)t->size.h);
    GdkTexture* tex      = gsk_renderer_render_texture(renderer, node, &view);
    gsk_render_node_unref(node);

    // GDK_MEMORY_DEFAULT: B, G, R, A bytes, premultiplied
    size_t stride = (size_t)t->size.w * 4;
    gdk_texture_download(tex, rgba, stride);
    g_object_unref(tex);
    for (size_t i = 0; i < (size_t)t->size.w * t->size.h; i++) {
        uint8 b         = rgba[i * 4];
        rgba[i * 4]     = rgba[i * 4 + 2];
        rgba[i * 4 + 2] = b;
    }
}

const CxUiDrawOps _cxuiGtk4GskOps = {
    .name          = _S"gsk",
    .targetCreate  = gskTargetCreate,
    .targetResize  = gskTargetResize,
    .targetDestroy = gskTargetDestroy,
    .expandDirty   = gskExpandDirty,
    .begin         = gskBegin,
    .end           = gskEnd,
    .fill          = gskFill,
    .line          = gskLine,
    .clipSet       = gskClipSet,
    .fontCreate    = pgFontCreate,
    .fontDestroy   = pgFontDestroy,
    .fontMetrics   = pgFontMetrics,
    .fontAdvance   = pgFontAdvance,
    .text          = gskText,
    .textCells     = gskTextCells,
    .imageUpload   = gskImageUpload,
    .imageFree     = gskImageFree,
    .image         = gskImage,
    .testRead      = gskTestRead,
};

const CxUiDrawOps* _cxuiPlatformDrawOps(uint32 which)
{
    unused_noeval(which);
    return &_cxuiGtk4GskOps;
}

// Grids: a list store of many rows with columns of each kind of alignment, a fitted, a fixed and
// a stretching column, stripes, and rows dimmed, bold or colored, sorted by clicking a header,
// selected many at a time, renamed in place, following its end as rows are added; a small one
// with lines and no headers whose values are edited; a tree with guide lines and a folder whose
// ten thousand children are added as it first opens.

#include "gallery.h"

typedef struct GridPage {
    CxUiListStore* files;
    CxUiGrid* grid;
    int32 next;   // the number of the next row added
    CxUiTreeStore* folders;
    CxUiGrid* tree;
    int32 made;   // folders made with the button
} GridPage;

static GridPage* page;

static const strref kinds[] = { _S"Text", _S"Image", _S"Model", _S"Sound", _S"Script" };

static void pageSay(strref what)
{
    galleryStatus(_S"grid", what);
}

static void addRows(int32 n)
{
    string num = 0, name = 0, size = 0;
    for (int32 i = 0; i < n; i++, page->next++) {
        int32 k = page->next;
        strFormat(&num, _S"${int}", stvar(int32, k));
        strFormat(&name, _S"file_${int}.${string}", stvar(int32, k), stvar(strref, kinds[k % 5]));
        strFormat(&size, _S"${int} KB", stvar(int64, ((int64)k * 7919) % 100000));
        int64 row = cxuiListStoreAppend(page->files, num, name, kinds[k % 5], size);
        if (k % 7 == 3)
            cxuiListStoreSetFlags(page->files, row, CXUI_CELL_Dim);
        else if (k % 13 == 5)
            cxuiListStoreSetFlags(page->files, row, CXUI_CELL_Bold);
        if (k % 50 == 10)
            cxuiListStoreSetColors(page->files, row, cxuiHex(0x202020), cxuiHex(0xF0C040));
    }
    strDestroy(&num);
    strDestroy(&name);
    strDestroy(&size);
}

static void sayRows(void)
{
    string what = 0;
    strFormat(&what, _S"${int} rows", stvar(int64, cxuiGridRowCount(page->grid)));
    pageSay(what);
    strDestroy(&what);
}

static CxUiResult action(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    int32 which = 0;
    stvlNext(cvars, int32, &which);
    switch (which) {
    case 0:
        addRows(1000);
        break;
    case 1:
        cxuiListStoreRemove(page->files, 0, 1);
        break;
    case 2:
        cxuiListStoreClear(page->files);
        page->next = 0;
        break;
    case 3:
        addRows(200000);
        break;
    case 4:
        cxuiGridShowRow(page->grid, cxuiGridRowCount(page->grid) - 1);
        break;
    }
    sayRows();
    return CXUI_Handled;
}

static CxUiResult sorted(stvlist* cvars, CxUiSortEvent* ev)
{
    unused_noeval(cvars);
    cxuiListStoreSort(page->files, ev->column, ev->descending);
    string what = 0;
    strFormat(&what, _S"sorted by column ${int}${string}", stvar(int32, ev->column),
              stvar(strref, ev->descending ? _S", largest first" : _S""));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

static CxUiResult selChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string what = 0;
    strFormat(&what, _S"${int} selected, the first row ${int}", stvar(int64, cxuiGridSelectedCount(page->grid)),
              stvar(int64, cxuiGridSelected(page->grid)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

static CxUiResult opened(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    string what = 0;
    strFormat(&what, _S"opened ${string}", stvar(strref, cxuiListStoreText(page->files, (int64)ev->item, 1)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

static CxUiResult menu(stvlist* cvars, CxUiContextEvent* ev)
{
    unused_noeval(cvars);
    string what = 0;
    if (ev->onItem)
        strFormat(&what, _S"menu for row ${int}, column ${int}, at ${int},${int}", stvar(int64, (int64)ev->item),
                  stvar(int32, ev->column), stvar(int32, ev->x), stvar(int32, ev->y));
    else
        strFormat(&what, _S"menu for ${string} column ${int}", stvar(strref, ev->header ? _S"the header of" : _S"no row,"),
                  stvar(int32, ev->column));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

// Renaming: an empty name is refused, and the cell keeps its text.
static CxUiResult edited(stvlist* cvars, CxUiEditEvent* ev)
{
    CxUiTreeStore* tree = stvlNextPtr(cvars);
    string what = 0;
    bool refuse = false;
    switch (ev->stage) {
    case CXUI_EDIT_Start:
        break;
    case CXUI_EDIT_Commit:
        refuse = strEmpty(ev->text);
        if (refuse)
            strCopy(&what, _S"a name can't be empty");
        else if (tree)
            strFormat(&what, _S"tree: renamed ${string} to ${string}",
                      stvar(strref, cxuiTreeStoreText(tree, (CxUiNodeId)ev->item, ev->column)), stvar(strref, ev->text));
        else
            strFormat(&what, _S"row ${int}, column ${int} is now ${string}", stvar(int64, (int64)ev->item),
                      stvar(int32, ev->column), stvar(strref, ev->text));
        break;
    case CXUI_EDIT_Cancel:
        strFormat(&what, _S"dropped ${string}", stvar(strref, ev->text));
        break;
    }
    if (what)
        pageSay(what);
    strDestroy(&what);
    return refuse ? CXUI_Handled : CXUI_Default;
}

// ---- the tree ----

// Data on a folder whose children are added when it first opens
#define LAZY 1

static CxUiResult treeExpand(stvlist* cvars, CxUiExpandEvent* ev)
{
    unused_noeval(cvars);
    CxUiNodeId node = (CxUiNodeId)ev->item;
    stvar data      = cxuiTreeStoreData(page->folders, node);
    if (ev->expanded && stvarIs(&data, int32) && data.data.st_int32 == LAZY) {
        cxuiTreeStoreSetData(page->folders, node, stvNone);
        cxuiTreeStoreRemove(page->folders, cxuiTreeStoreFirstChild(page->folders, node));
        string name = 0, size = 0;
        for (int32 i = 0; i < 10000; i++) {
            strFormat(&name, _S"item_${int}.dat", stvar(int32, i));
            strFormat(&size, _S"${int} KB", stvar(int32, (i * 31) % 997));
            cxuiTreeStoreAppend(page->folders, node, name, size);
        }
        strDestroy(&name);
        strDestroy(&size);
        pageSay(_S"tree: added 10,000 children");
        return CXUI_Handled;
    }
    string what = 0;
    strFormat(&what, _S"tree: ${string} ${string}", stvar(strref, ev->expanded ? _S"opened" : _S"closed"),
              stvar(strref, cxuiTreeStoreText(page->folders, node, 0)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

// Opening a file says so; a folder opens or closes, as by default.
static CxUiResult treeActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    CxUiNodeId node = (CxUiNodeId)ev->item;
    if (cxuiTreeStoreHasChildren(page->folders, node))
        return CXUI_Default;
    string what = 0;
    strFormat(&what, _S"tree: opened ${string}", stvar(strref, cxuiTreeStoreText(page->folders, node, 0)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

// A node checked or cleared checks or clears the nodes under it, and those above it follow.
static CxUiResult treeCheck(stvlist* cvars, CxUiCheckEvent* ev)
{
    unused_noeval(cvars);
    CxUiNodeId node = (CxUiNodeId)ev->item;
    cxuiTreeStoreSetCheck(page->folders, node, ev->column, ev->state);
    cxuiTreeStorePropagateCheck(page->folders, node, ev->column);
    return CXUI_Handled;
}

static CxUiResult treeChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    string what = 0;
    strFormat(&what, _S"tree: ${int} selected, the first ${string}", stvar(int64, cxuiGridSelectedCount(page->tree)),
              stvar(strref, cxuiTreeStoreText(page->folders, cxuiGridSelectedNode(page->tree), 0)));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Handled;
}

static CxUiResult treeAction(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    int32 which = 0;
    stvlNext(cvars, int32, &which);
    CxUiNodeId focus = cxuiGridFocusNode(page->tree);
    switch (which) {
    case 0:
        cxuiGridExpandAll(page->tree, 0);
        break;
    case 1:
        cxuiGridCollapseAll(page->tree, 0);
        break;
    case 2: {
        string name = 0;
        strFormat(&name, _S"New folder ${int}", stvar(int32, ++page->made));
        CxUiNodeId made = cxuiTreeStoreAppend(page->folders, focus, name);
        strDestroy(&name);
        cxuiGridSetFocusNode(page->tree, made);
        cxuiGridSetNodeSelected(page->tree, made, true);
        cxuiGridEdit(page->tree, made, 0);
        break;
    }
    case 3:
        cxuiTreeStoreRemove(page->folders, focus);
        break;
    }
    return CXUI_Handled;
}

static void buildTree(CxUiBox* box)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(2);
    page->folders    = t;
    CxUiNodeId docs  = cxuiTreeStoreAppend(t, 0, _S"Documents");
    cxuiTreeStoreAppend(t, docs, _S"letter.txt", _S"3 KB");
    cxuiTreeStoreAppend(t, docs, _S"budget.csv", _S"12 KB");
    cxuiTreeStoreAppend(t, docs, _S"notes.md", _S"1 KB");
    CxUiNodeId pics = cxuiTreeStoreAppend(t, 0, _S"Pictures");
    CxUiNodeId hols = cxuiTreeStoreAppend(t, pics, _S"Holidays");
    cxuiTreeStoreAppend(t, hols, _S"beach.png", _S"2,048 KB");
    cxuiTreeStoreAppend(t, hols, _S"hills.png", _S"1,812 KB");
    CxUiNodeId fam = cxuiTreeStoreAppend(t, pics, _S"Family");
    cxuiTreeStoreAppend(t, fam, _S"party.jpg", _S"960 KB");
    CxUiNodeId big = cxuiTreeStoreAppend(t, 0, _S"Big folder");
    cxuiTreeStoreSetData(t, big, stvar(int32, LAZY));
    // Shows an expander until the folder first opens
    cxuiTreeStoreSetFlags(t, cxuiTreeStoreAppend(t, big, _S"loading"), CXUI_CELL_Dim | CXUI_CELL_ReadOnly);
    cxuiTreeStoreAppend(t, 0, _S"Empty folder");
    // Not part of what is checked
    cxuiTreeStoreSetFlags(t, cxuiTreeStoreAppend(t, 0, _S"readme.txt", _S"4 KB"), CXUI_CELL_CheckHidden);

    page->tree = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .kind = CXUI_COL_Tree,
                                          .flags = CXUI_COL_Stretch | CXUI_COL_CheckBox | CXUI_COL_Editable },
                                        { .title = _S"Size", .width = cxuiLen(90), .align = CXUI_ALIGN_End } },
        .ncolumns = 2,
        .flags    = CXUI_GRID_TreeLines,
        .select   = CXUI_SELECT_Multi });
    cxuiGridSetSource(page->tree, t);
    cxuiGridSetExpanded(page->tree, pics, true);
    cxuiOnExpand(page->tree, treeExpand, stvNone);
    cxuiOnActivate(page->tree, treeActivate, stvNone);
    cxuiOnChange(page->tree, treeChanged, stvNone);
    cxuiOnCheck(page->tree, treeCheck, stvNone);
    cxuiOnContext(page->tree, menu, stvNone);
    cxuiOnEdit(page->tree, edited, stvar(ptr, t));
    cxuiWidgetSetNaturalSize(page->tree, cxuiLen(0), cxuiLen(200));
    cxuiContainerAppend(box, page->tree);

    CxUiBox* row                 = galleryRow(box, 6);
    static const strref titles[] = { _S"E&xpand all", _S"C&ollapse all", _S"&New folder", _S"Remo&ve" };
    for (int32 i = 0; i < 4; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = titles[i] });
        cxuiOnActivate(b, treeAction, stvar(int32, i));
        galleryAdd(row, b);
    }
    objRelease(&row);
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->files);
    objRelease(&page->grid);
    objRelease(&page->folders);
    objRelease(&page->tree);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page = xaAllocStruct(GridPage, XA_Zero);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);

    galleryHeading(box, _S"Files");
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"A list store shown in a grid. Every 7th row is dim, every 13th bold and every 50th "
                                "colored. Click a header to sort; Ctrl and Shift select many rows, and Ctrl+arrows "
                                "move without selecting, for Ctrl+Space to add a row; type a number to go to its row; "
                                "drag a header's edge to resize, or double-click it to fit. F2 (Return on the Mac), "
                                "or a click on the selected row, renames a file or changes its kind; Tab goes on to the next. Scrolled "
                                "to the end, the rows added stay in sight.",
                        .wrap = true }));
    page->files = cxuiListStoreCreate(4);
    addRows(10000);
    page->grid = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"#", .width = cxuiChars(9), .align = CXUI_ALIGN_End,
                                          .flags = CXUI_COL_Sortable },
                                        { .title = _S"Name",
                                          .flags = CXUI_COL_Stretch | CXUI_COL_Sortable | CXUI_COL_CheckBox | CXUI_COL_Editable,
                                          .minWidth = cxuiLen(120) },
                                        { .title = _S"Kind", .width = cxuiLen(70), .align = CXUI_ALIGN_Center,
                                          .flags = CXUI_COL_Sortable | CXUI_COL_Editable },
                                        { .title = _S"Size", .width = cxuiLen(90), .align = CXUI_ALIGN_End,
                                          .flags = CXUI_COL_Sortable | CXUI_COL_FixedWidth } },
        .ncolumns = 4,
        .flags    = CXUI_GRID_Stripes | CXUI_GRID_FollowTail,
        .select   = CXUI_SELECT_Multi });
    cxuiGridSetSource(page->grid, page->files);
    // The rows start in order of their numbers.
    cxuiGridSetSort(page->grid, 0, false);
    cxuiOnSort(page->grid, sorted, stvNone);
    cxuiOnChange(page->grid, selChanged, stvNone);
    cxuiOnActivate(page->grid, opened, stvNone);
    cxuiOnContext(page->grid, menu, stvNone);
    cxuiOnEdit(page->grid, edited, stvNone);
    cxuiWidgetSetNaturalSize(page->grid, cxuiLen(0), cxuiLen(220));
    cxuiContainerAppend(box, page->grid);

    CxUiBox* row                 = galleryRow(box, 6);
    static const strref titles[] = { _S"&Add 1,000", _S"&Remove first", _S"&Clear", _S"Add &200,000", _S"Show &last" };
    for (int32 i = 0; i < 5; i++) {
        CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = titles[i] });
        cxuiOnActivate(b, action, stvar(int32, i));
        galleryAdd(row, b);
    }
    objRelease(&row);

    // The small grid and the tree, side by side
    CxUiBox* bottom = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 16 });
    cxuiContainerAppend(box, bottom);
    CxUiBox* left  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 8 });
    CxUiBox* right = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 8 });
    cxuiContainerAppend(bottom, left);
    cxuiContainerAppend(bottom, right);
    objRelease(&bottom);

    galleryHeading(left, _S"Lines, no headers");
    galleryAdd(left, cxuiLabelCreate(&(CxUiLabelDesc){
                         .text = _S"The values can be edited.", .wrap = true }));
    CxUiListStore* small = cxuiListStoreCreate(2);
    static const strref keys[] = { _S"Server", _S"Port", _S"User", _S"A key whose name is too long to fit" };
    static const strref vals[] = { _S"localhost", _S"7000", _S"guest", _S"A value that is also far too long to fit" };
    for (int32 i = 0; i < 4; i++)
        cxuiListStoreAppend(small, keys[i], vals[i]);
    CxUiGrid* plain = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .width = cxuiLen(140) }, { .flags = CXUI_COL_Stretch | CXUI_COL_Editable } },
        .ncolumns = 2,
        .flags    = CXUI_GRID_Lines | CXUI_GRID_NoHeader });
    cxuiGridSetSource(plain, small);
    cxuiOnEdit(plain, edited, stvNone);
    objRelease(&small);
    cxuiWidgetSetNaturalSize(plain, cxuiLen(300), cxuiLen(90));
    cxuiWidgetSetExpand(plain, CXUI_EXPAND_None);
    galleryAdd(left, plain);
    objRelease(&left);

    galleryHeading(right, _S"Tree");
    galleryAdd(right, cxuiLabelCreate(&(CxUiLabelDesc){
                          .text = _S"Big folder adds its ten thousand children as it first opens; Right and Left open, "
                                  "close and move between parent and child. Checking a folder checks what is in it. "
                                  "F2 (Return on the Mac) renames.",
                          .wrap = true }));
    buildTree(right);
    objRelease(&right);
}

static void shown(void)
{
    sayRows();
}

const GalleryPage galleryGrid = {
    .name  = _S"grid",
    .title = _S"Grid",
    .build = build,
    .shown = shown,
};

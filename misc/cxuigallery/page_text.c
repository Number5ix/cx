// Text fields: plain, read-only, framed, limited and monospace ones; a find box that turns red on
// no match; and a command line with Tab completion and a history.

#include "gallery.h"

// What the find box searches, and the commands the command line completes
static const char sample[]          = "The quick brown fox jumps over the lazy dog";
static const strref commands[]      = { _S"clear", _S"help", _S"level", _S"load", _S"theme", _S"tool" };
#define NCOMMANDS ((int32)(sizeof(commands) / sizeof(commands[0])))
#define OUTPUT_LINES 4

// Holds a reference to each widget it uses after building, released when the page goes
typedef struct TextPage {
    CxUiTextFrame* findFrame;
    CxUiTextField* find;
    CxUiTextField* cmd;
    CxUiDLabel* output;
    sa_string history;
    int32 histPos;        // 0 for the line being typed, else how far back
    int32 completeAt;     // the next completion to offer, or -1 when none is in progress
    string completeBase;  // what was typed before completing
} TextPage;

static TextPage* page;

static void pageSay(strref what)
{
    galleryStatus(_S"text", what);
}

// Live search: the box turns red when the sample doesn't contain its text.
static CxUiResult findChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    strref text = cxuiTextFieldText(page->find);
    bool hit    = strEmpty(text) || strFind((strref)sample, 0, text) >= 0;
    cxuiTextFrameSetBad(page->findFrame, !hit);
    string what = 0;
    strFormat(&what, _SL("find \"${string}\": ${string}"), stvar(strref, text), stvar(strref, hit ? _S"found" : _S"no match"));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

// Enter or F3 for the next match, with Shift for the previous; Esc clears the box.
static CxUiResult findKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    if (!ev->down)
        return CXUI_Default;
    if (ev->key == CXUI_KEY_Enter || ev->key == CXUI_KEY_F3) {
        pageSay((ev->mods & CXUI_MOD_Shift) ? _S"find: previous match" : _S"find: next match");
        return CXUI_Handled;
    }
    if (ev->key == CXUI_KEY_Escape) {
        cxuiTextFieldSetText(page->find, NULL);
        cxuiTextFrameSetBad(page->findFrame, false);
        pageSay(_S"find cleared");
        return CXUI_Handled;
    }
    return CXUI_Default;
}

// Ctrl+F, or Command+F on macOS, anywhere on the page
static CxUiResult pageKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    if (!ev->down || !cxuiKeyMatches(CXUI_KEY_F, CXUI_MOD_Primary, ev->key, ev->mods))
        return CXUI_Default;
    cxuiFocus(page->find);
    cxuiTextFieldSelectAll(page->find);
    string label = 0;
    cxuiKeyLabel(&label, CXUI_KEY_F, CXUI_MOD_Primary);
    pageSay(label);
    strDestroy(&label);
    return CXUI_Handled;
}

// Tab offers each command that starts with what was typed, in turn; Shift+Tab goes back.
static void complete(bool back)
{
    if (page->completeAt < 0) {
        strDup(&page->completeBase, cxuiTextFieldText(page->cmd));
        page->completeAt = back ? NCOMMANDS - 1 : 0;
    }
    for (int32 tries = 0; tries < NCOMMANDS; tries++) {
        int32 i          = page->completeAt;
        page->completeAt = (page->completeAt + (back ? NCOMMANDS - 1 : 1)) % NCOMMANDS;
        if (strBeginsWith(commands[i], page->completeBase)) {
            cxuiTextFieldSetText(page->cmd, commands[i]);
            return;
        }
    }
    pageSay(_S"nothing to complete");
}

// Enter runs the line: it goes in the history and the output.
static void submit(void)
{
    strref line = cxuiTextFieldText(page->cmd);
    if (strEmpty(line))
        return;
    string out = 0, ran = 0;
    strFormat(&ran, _SL("ran \"${string}\""), stvar(strref, line));
    pageSay(ran);
    saPush(&page->history, strref, line);
    page->histPos = 0;
    // The last few lines run
    int32 n = saSize(page->history);
    for (int32 i = max(n - OUTPUT_LINES, 0); i < n; i++) {
        strAppend(&out, strEmpty(out) ? _S"> " : _S"\n> ");
        strAppend(&out, page->history.a[i]);
    }
    cxuiDLabelSetText(page->output, out);
    cxuiTextFieldSetText(page->cmd, NULL);
    strDestroy(&ran);
    strDestroy(&out);
}

static CxUiResult cmdKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    if (!ev->down)
        return CXUI_Default;
    switch (ev->key) {
    case CXUI_KEY_Tab:
        if (ev->mods & ~CXUI_MOD_Shift)
            return CXUI_Default;
        complete((ev->mods & CXUI_MOD_Shift) != 0);
        return CXUI_Handled;
    case CXUI_KEY_Up:
    case CXUI_KEY_Down: {
        int32 n = saSize(page->history);
        int32 p = clamp(page->histPos + (ev->key == CXUI_KEY_Up ? 1 : -1), 0, n);
        if (p != page->histPos) {
            page->histPos = p;
            cxuiTextFieldSetText(page->cmd, p ? (strref)page->history.a[n - p] : NULL);
        }
        return CXUI_Handled;
    }
    case CXUI_KEY_Enter:
    case CXUI_KEY_PadEnter:
        submit();
        return CXUI_Handled;
    case CXUI_KEY_Escape:
        cxuiTextFieldSetText(page->cmd, NULL);
        return CXUI_Handled;
    default:
        return CXUI_Default;
    }
}

// Typing over a completion or a line from the history starts afresh.
static CxUiResult cmdChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(cvars);
    unused_noeval(ev);
    page->completeAt = -1;
    page->histPos    = 0;
    return CXUI_Default;
}

static CxUiResult committed(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(cvars);
    CxUiTextField* f = objDynCast(CxUiTextField, ev->base.target);
    string what      = 0;
    strFormat(&what, _SL("committed \"${string}\""), stvar(strref, f ? cxuiTextFieldText(f) : NULL));
    pageSay(what);
    strDestroy(&what);
    return CXUI_Default;
}

static void labelled(void* box, strref label, void* w)
{
    CxUiBox* row = galleryRow(box, 6);
    CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = label });
    cxuiWidgetSetNaturalSize(l, cxuiLen(90), cxuiLen(0));
    galleryAdd(row, l);
    cxuiWidgetSetExpand((CxUiWidget*)w, CXUI_EXPAND_H);
    galleryAdd(row, w);
    objRelease(&row);
}

static CxUiTextField* plainField(strref text, strref cue)
{
    CxUiTextField* f = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = text, .cue = cue });
    cxuiOnCommit(f, committed, stvNone);
    return f;
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    objRelease(&page->findFrame);
    objRelease(&page->find);
    objRelease(&page->cmd);
    objRelease(&page->output);
    saDestroy(&page->history);
    strDestroy(&page->completeBase);
    xaDestroy(&page);
    return true;
}

static void build(CxUiBox* box)
{
    page             = xaAllocStruct(TextPage, XA_Zero);
    page->completeAt = -1;
    saInit(&page->history, string, 8);
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);
    cxuiOnKey(box, pageKey, stvNone);

    galleryHeading(box, _S"Fields");
    labelled(box, _S"&Plain:", plainField(NULL, _S"A cue while empty"));
    CxUiTextField* ro = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"Read-only, selectable", .readOnly = true });
    labelled(box, _S"&Read-only:", ro);
    labelled(box, _S"&Limited:", cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"Up to 8 characters", .maxLength = 8 }));
    CxUiTextField* mono = plainField(_S"Monospace 0123456789", NULL);
    cxuiWidgetSetFont(mono, &(CxUiFontDesc){ .faces = _S"monospace" });
    labelled(box, _S"&Monospace:", mono);
    CxUiTextField* framed = plainField(NULL, _S"In the palette's colors");
    labelled(box, _S"F&ramed:", cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = framed }));
    objRelease(&framed);

    galleryHeading(box, _S"Find");
    string findLabel = 0, findHint = 0;
    cxuiKeyLabel(&findLabel, CXUI_KEY_F, CXUI_MOD_Primary);
    strFormat(&findHint, _SL("Searches \"${string}\"; red on no match. Enter or F3 for the next, Shift for the "
                             "previous, Esc clears; ${string} comes here from anywhere on the page."),
              stvar(strref, (strref)sample), stvar(string, findLabel));
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){ .text = findHint, .wrap = true }));
    page->find      = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = findLabel, .columns = 18 });
    page->findFrame = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = page->find, .padding = { 4, 3 } });
    strDestroy(&findLabel);
    strDestroy(&findHint);
    cxuiOnChange(page->find, findChanged, stvNone);
    cxuiOnKey(page->find, findKey, stvNone);
    cxuiContainerAppend(box, page->findFrame);

    galleryHeading(box, _S"Command line");
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Tab completes (clear, help, level, load, theme, tool), Shift+Tab goes back; Up and "
                                "Down go through the history; Enter runs the line.",
                        .wrap = true }));
    // Output over a prompt, in a frame; the field has no border of its own.
    CxUiFrame* console = cxuiFrameCreate(&(CxUiPanelDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .margin = { 6, 6, 6, 6 } });
    page->output       = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"", .padding = { 4, 2 } });
    cxuiWidgetSetFont(page->output, &(CxUiFontDesc){ .faces = _S"Consolas;monospace" });
    cxuiWidgetSetNaturalSize(page->output, cxuiLen(0), cxuiLen(OUTPUT_LINES * 17));
    cxuiWidgetSetAlign(page->output, CXUI_ALIGN_Fill, CXUI_ALIGN_Default);
    cxuiContainerAppend(console, page->output);
    CxUiBox* prompt  = galleryRow(console, 0);
    CxUiDLabel* mark = cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S">", .padding = { 4, 0 } });
    cxuiWidgetSetFont(mark, &(CxUiFontDesc){ .faces = _S"Consolas;monospace" });
    galleryAdd(prompt, mark);
    page->cmd = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"Type a command" });
    cxuiWidgetSetFont(page->cmd, &(CxUiFontDesc){ .faces = _S"Consolas;monospace" });
    cxuiOnKey(page->cmd, cmdKey, stvNone);
    cxuiOnChange(page->cmd, cmdChanged, stvNone);
    CxUiTextFrame* cmdFrame = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = page->cmd, .noBorder = true, .padding = { 2, 3 } });
    cxuiWidgetSetExpand(cmdFrame, CXUI_EXPAND_H);
    galleryAdd(prompt, cmdFrame);
    objRelease(&prompt);
    galleryAdd(box, console);
}

const GalleryPage galleryText = {
    .name  = _S"text",
    .title = _S"Text input",
    .build = build,
};

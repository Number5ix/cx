// Modal windows: message boxes, asked from the UI thread or another one, and popups that end
// themselves, nest, or stay up while another thread floods the UI thread with tasks.

#include "gallery.h"

static void pageSay(strref what)
{
    galleryStatus(_S"modals", what);
}

static const strref answers[] = { _S"closed", _S"Yes", _S"No", _S"Cancel" };

static strref answerOf(int32 r)
{
    return answers[r >= 0 && r <= 3 ? r : 0];
}

static CxUiResult endPopupAfter(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

static CxUiTimer* endAfter(int64 delay, CxUiWindow* popup, int32 result)
{
    CxUiTimer* t = cxuiTimerCreate(delay, CXUI_Timer_OneShot, endPopupAfter, stvar(object, popup), stvar(int32, result));
    cxuiTimerStart(t);
    return t;
}

// A popup saying what it is, in the gallery's theme; returns the caller's reference.
static CxUiWindow* popupCreate(strref title, strref text, int32 w, int32 h)
{
    CxUiWindow* p = cxuiWindowCreate(&(CxUiWindowDesc){ .title = title, .size = { w, h } });
    galleryFollowTheme(p);
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .margin = { 14, 14, 14, 14 } });
    galleryAdd(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = text, .wrap = true }));
    galleryAdd(p, col);
    return p;
}

static void message(void)
{
    int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = galleryWindow(),
                                                    .title  = _S"cxui gallery",
                                                    .text   = _S"A message box belonging to the gallery.\nIt has two lines.",
                                                    .kind   = CXUI_MB_Info });
    string line = 0;
    strFormat(&line, _SL("the message box returned ${int}"), stvar(int32, r));
    pageSay(line);
    strDestroy(&line);
}

static void question(void)
{
    int32 r     = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent        = galleryWindow(),
                                                        .title         = _S"cxui gallery",
                                                        .text          = _S"Answer Yes, No or Cancel. Enter answers No.",
                                                        .kind          = CXUI_MB_Question,
                                                        .buttons       = CXUI_MB_YesNoCancel,
                                                        .defaultButton = 2 });
    string line = 0;
    strFormat(&line, _SL("answered ${string}"), stvar(strref, answerOf(r)));
    pageSay(line);
    strDestroy(&line);
}

// Pretend work, a step every 100 ms, until it is done or cancelled. The dialog is modal, so only
// one runs at a time.
static CxUiTimer* stepper;

static CxUiResult progressStep(stvlist* cvars, CxUiTimerEvent* ev)
{
    CxUiProgressDialog* d = stvlAtObj(cvars, 0, CxUiProgressDialog);
    int32* done           = stvlAtPtr(cvars, 1);
    bool cancelled        = cxuiProgressDialogCancelled(d);
    if (!cancelled && *done < 50) {
        (*done)++;
        string line = 0;
        strFormat(&line, _SL("Copying file ${int} of 50"), stvar(int32, *done));
        cxuiProgressDialogSetText(d, line);
        cxuiProgressDialogSetCount(d, *done);
        strDestroy(&line);
        return CXUI_Default;
    }
    pageSay(cancelled ? _S"the progress dialog was cancelled" : _S"the progress dialog finished");
    cxuiTimerStop(ev->timer);
    cxuiDestroy(d);
    xaFree(done);
    objRelease(&stepper);   // the timer holds itself while its handler runs
    return CXUI_Default;
}

static void progress(void)
{
    CxUiProgressDialog* d = cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){
        .title = _S"cxui gallery", .text = _S"Starting", .total = 50, .cancel = true });
    cxuiWindowShowModal(d, galleryWindow(), NULL);
    stepper = cxuiTimerCreate(timeMS(100), 0, progressStep, stvar(object, d), stvar(ptr, xaAllocStruct(int32, XA_Zero)));
    cxuiTimerStart(stepper);
    objRelease(&d);
}

static void file(uint32 mode)
{
    static const CxUiFileFilter filters[] = { { _S"Text files", _S"*.txt;*.md" }, { _S"All files", _S"*" } };
    static const strref titles[]          = { _S"Open a text file", _S"Save as", _S"Choose a folder" };
    string path                           = 0;
    uint32 r = cxuiFileDialog(&(CxUiFileDialogDesc){ .parent      = galleryWindow(),
                                                     .title       = titles[mode],
                                                     .mode        = mode,
                                                     .filters     = filters,
                                                     .nfilters    = 2,
                                                     .defaultExt  = _S"txt",
                                                     .initialFile = _S"notes.txt" },
                              &path);
    static const strref status[] = { _S"cancelled", _S"existing", _S"new" };
    string line                  = 0;
    strFormat(&line, _SL("${string}: ${string}"), stvar(strref, status[r < 3 ? r : 0]), stvar(string, path));
    pageSay(line);
    strDestroy(&line);
    strDestroy(&path);
}

static void color(void)
{
    static CxUiColor c = { .r = 255, .g = 160, .b = 0, .a = 255 };
    if (!cxuiColorDialog(&(CxUiColorDialogDesc){ .parent = galleryWindow() }, &c)) {
        pageSay(_S"cancelled");
        return;
    }
    string line = 0;
    strFormat(&line, _SL("chose ${int}, ${int}, ${int}"), stvar(int32, c.r), stvar(int32, c.g), stvar(int32, c.b));
    pageSay(line);
    strDestroy(&line);
}

static void custom(void)
{
    int32 r     = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent        = galleryWindow(),
                                                        .title         = _S"cxui gallery",
                                                        .text          = _S"A new version is ready. Install it now?",
                                                        .kind          = CXUI_MB_Question,
                                                        .buttons       = CXUI_MB_Custom,
                                                        .custom        = { _S"&Install", _S"Remind me &later", _S"&Skip" },
                                                        .defaultButton = 1,
                                                        .cancelButton  = 2 });
    string line = 0;
    strFormat(&line, _SL("the custom box returned ${int}"), stvar(int32, r));
    pageSay(line);
    strDestroy(&line);
}

// The box belongs to no window, so every window waits for it; the thread waits for the answer.
static int askThread(Thread* self)
{
    unused_noeval(self);
    int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .title         = _S"cxui gallery",
                                                    .text          = _S"Asked from another thread, which waits for the answer.",
                                                    .kind          = CXUI_MB_Question,
                                                    .buttons       = CXUI_MB_YesNoCancel,
                                                    .defaultButton = 2 });
    say(_SL("modals: the other thread was answered ${string}\n"), stvar(strref, answerOf(r)));
    return 0;
}

static void popup(void)
{
    CxUiWindow* p = popupCreate(_S"Popup", _S"This popup ends itself in 5 seconds, returning 1.", 360, 120);
    CxUiTimer* t  = endAfter(timeS(5), p, 1);
    int32 r       = cxuiWindowPopup(p, galleryWindow());
    string line   = 0;
    strFormat(&line, _SL("the popup returned ${int}"), stvar(int32, r));
    pageSay(line);
    strDestroy(&line);
    cxuiTimerStop(t);
    objRelease(&t);
    cxuiDestroy(p);
    objRelease(&p);
}

static CxUiResult openInner(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* outer   = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* inner   = popupCreate(_S"Inner popup", _S"Ends in 4 seconds, returning 2. The outer popup is ended in "
                                                       "1 second, but returns after this one.", 320, 140);
    CxUiTimer* endOuter = endAfter(timeS(1), outer, 1);
    CxUiTimer* endInner = endAfter(timeS(4), inner, 2);
    int32 r             = cxuiWindowPopup(inner, outer);
    say(_SL("modals: the inner popup returned ${int}\n"), stvar(int32, r));
    objRelease(&endOuter);
    objRelease(&endInner);
    cxuiDestroy(inner);
    objRelease(&inner);
    return CXUI_Default;
}

static void nested(void)
{
    CxUiWindow* outer = popupCreate(_S"Outer popup", _S"Another popup opens over this one in a second.", 420, 220);
    CxUiTimer* t      = cxuiTimerCreate(timeS(1), CXUI_Timer_OneShot, openInner, stvar(object, outer));
    cxuiTimerStart(t);
    int32 r     = cxuiWindowPopup(outer, galleryWindow());
    string line = 0;
    strFormat(&line, _SL("the outer popup returned ${int}"), stvar(int32, r));
    pageSay(line);
    strDestroy(&line);
    cxuiTimerStop(t);
    objRelease(&t);
    cxuiDestroy(outer);
    objRelease(&outer);
}

typedef struct Flood {
    int32 ran;   // UI thread only
} Flood;

static bool floodTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Flood* f = stvlAtPtr(cvars, 0);
    if (++f->ran % 100 == 0)
        say(_SL("modals: ${int} tasks run\n"), stvar(int32, f->ran));
    return true;
}

static int floodThread(Thread* self)
{
    Flood* f = stvlAtPtr(&self->args, 0);
    for (int32 i = 0; i < 1000; i++) {
        cxuiPost(closureCreate(floodTask, stvar(ptr, f)));
        osSleep(timeMS(2));
    }
    return 0;
}

static void flood(void)
{
    Flood* f    = xaAllocStruct(Flood, XA_Zero);
    Thread* thr = thrCreate(floodThread, _S"Flood", stvar(ptr, f));
    cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = galleryWindow(),
                                          .title  = _S"cxui gallery",
                                          .text   = _S"Another thread is posting 1000 tasks; the console counts them as they run." });
    string line = 0;
    strFormat(&line, _SL("the message box closed after ${int} tasks"), stvar(int32, f->ran));
    pageSay(line);
    strDestroy(&line);
    // Tasks still queued refer to f, so it stays until the thread is done and they have run.
    thrWait(thr, timeForever);
    thrRelease(&thr);
    cxuiFlush();
    xaFree(f);
}

static CxUiResult action(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    switch (stvlAt(cvars, 0, int32)) {
    case 0: message(); break;
    case 1: question(); break;
    case 2:
        thrRun(askThread, _S"Ask", stvNone);
        pageSay(_S"another thread is asking");
        break;
    case 3: popup(); break;
    case 4: nested(); break;
    case 5: flood(); break;
    case 6: custom(); break;
    case 7: progress(); break;
    case 8: file(CXUI_FILE_Open); break;
    case 9: file(CXUI_FILE_Save); break;
    case 10: file(CXUI_FILE_Folder); break;
    case 11: color(); break;
    }
    return CXUI_Default;
}

static void row(void* box, strref title, int32 what, strref text)
{
    CxUiBox* r    = galleryRow(box, 10);
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = title });
    cxuiWidgetSetNaturalSize(b, cxuiLen(170), cxuiLen(0));
    cxuiOnActivate(b, action, stvar(int32, what));
    galleryAdd(r, b);
    CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = text, .wrap = true });
    galleryAdd(r, l);
    objRelease(&r);
}

static void build(CxUiBox* box)
{
    galleryHeading(box, _S"Message boxes");
    row(box, _S"&Message", 0, _S"Information with an OK button.");
    row(box, _S"&Question", 1, _S"Yes, No and Cancel, with No pressed by Enter.");
    row(box, _S"&Custom buttons", 6, _S"Install, Remind me later and Skip; Escape and the close box answer Remind me later.");
    row(box, _S"From &another thread", 2,
        _S"Asked by a thread other than the UI thread, which waits for the answer; it belongs to no window, so "
          "every window waits for it.");

    galleryHeading(box, _S"Files");
    row(box, _S"&Open a file", 8, _S"Text files, or every file; says which was chosen and whether it exists.");
    row(box, _S"&Save a file", 9, _S"Starts with notes.txt; a name without an extension gets .txt.");
    row(box, _S"Choose a f&older", 10, _S"Any folder.");

    galleryHeading(box, _S"Colors");
    row(box, _S"Co&lor", 11, _S"The platform's color dialog, starting at the last color chosen.");

    galleryHeading(box, _S"Popups");
    row(box, _S"&Popup", 3, _S"Ends itself after 5 seconds with 1.");
    row(box, _S"&Nested popups", 4, _S"Two popups; the outer one is ended first, but returns last.");
    row(box, _S"P&rogress", 7, _S"A progress dialog counting to 50 over five seconds; Cancel, Escape and its close box "
                                "stop it.");
    row(box, _S"&Flood", 5, _S"A message box while another thread posts 1000 tasks to the UI thread; they keep "
                            "running while it is up.");
}

const GalleryPage galleryModals = {
    .name  = _S"modals",
    .title = _S"Modal windows",
    .build = build,
};

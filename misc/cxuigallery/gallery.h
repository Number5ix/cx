#pragma once

// The cxui gallery: a manual check of cxui on a real desktop. One window lists the pages; each
// page shows a part of cxui and says what it does in the status line and on the console.

#include <cx/console.h>
#include <cx/format.h>
#include <cx/thread.h>
#include <cxui.h>

// Writes a line to the console at once, even when it isn't a terminal.
#define say(fmt, ...)                         \
    do {                                      \
        conFmt(conOut(), fmt, __VA_ARGS__);   \
        conFlush(conOut());                   \
    } while (0)

// A page. The main window has one handler for each of its events, so a page hears of the ones
// it needs through these instead; each page is built once, so its state can be static.
typedef struct GalleryPage {
    strref name;    // for the console's "page" command
    strref title;   // in the list
    // Fills box with the page, the first time it is shown. The page holds what it keeps and lets
    // go of it when box is destroyed (its destroyNotify).
    void (*build)(CxUiBox* box);
    // The page is now the one shown; the window's default buttons were cleared for it. Optional.
    void (*shown)(void);
    // The main window's theme or scale changed. Optional.
    void (*themed)(void);
    void (*scaled)(void);
} GalleryPage;

extern const GalleryPage galleryNative;
extern const GalleryPage galleryValues;
extern const GalleryPage galleryDrawn;
extern const GalleryPage galleryText;
extern const GalleryPage galleryLayout;
extern const GalleryPage galleryTabs;
extern const GalleryPage galleryCanvas;
extern const GalleryPage galleryInput;
extern const GalleryPage galleryDesktop;
extern const GalleryPage galleryModals;
extern const GalleryPage galleryWindows;
extern const GalleryPage galleryGrid;
extern const GalleryPage galleryLua;

// The main window, which popups and message boxes belong to; borrowed. UI thread only.
CxUiWindow* galleryWindow(void);

// Shows what happened in the status line and on the console, as "page: text". UI thread only.
void galleryStatus(strref page, strref text);

// A window a page opens: it takes the theme chosen in the main window, and follows it. UI
// thread only.
void galleryFollowTheme(CxUiWindow* w);

// Logs a window's resizes, moves, state changes, activation and closing. UI thread only.
void galleryLogWindow(CxUiWindow* w);

// Appends w to box and drops the caller's reference.
void galleryAdd(void* box, void* w);

// A row of widgets, appended to box; returns the caller's reference.
CxUiBox* galleryRow(void* box, int32 gap);

// A heading over a part of a page
void galleryHeading(void* box, strref text);

#if defined(_PLATFORM_MACOS)
// Presents a frame to a render view's CAMetalLayer, banded by the frame number (render_metal.m)
void galleryMetalFrame(void* layer, int32 frame);
#endif

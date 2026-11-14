// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuiprogress.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

// A label above a bar, and the Cancel button below at the right. Cancel, like Escape and the close
// box, asks the dialog to close; unless the close handler takes that, the dialog is cancelled and
// stays shown, for the program to end the work and close it.

static CxUiResult cancelPressed(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(cvars);
    CxUiWindow* w = ev->base.window;
    if (w && w->peer)
        _cxuiWindowCloseRequested(w->peer, CXUI_CLOSE_User);
    return CXUI_Handled;
}

static void showCount(CxUiProgressDialog* self)
{
    cxuiProgressSetIndeterminate(self->_bar, self->_total <= 0);
    if (self->_total > 0)
        cxuiProgressSetValue(self->_bar, (float64)self->_count / (float64)self->_total * 100.0);
}

_objfactory_guaranteed CxUiProgressDialog* CxUiProgressDialog_createWith(_In_opt_ const CxUiProgressDialogDesc* desc)
{
    CxUiProgressDialog* self;
    self = objInstCreate(CxUiProgressDialog);
    CxUiProgressDialogDesc d = desc ? *desc : (CxUiProgressDialogDesc){ 0 };
    _cxuiWindowSetup(CxUiWindow(self), &(CxUiWindowDesc){ .title       = d.title,
                                                          .name        = d.name,
                                                          .sizing      = CXUI_SIZE_Fixed,
                                                          .closeAction = CXUI_OnClose_None });
    self->_total = max(d.total, 0);
    objInstInit(self);

    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 10, .margin = { 16, 16, 16, 16 } });
    self->_label = cxuiLabelCreate(&(CxUiLabelDesc){ .text = d.text, .ellipsis = CXUI_ELLIPSIS_End });
    cxuiWidgetSetNaturalSize(self->_label, cxuiLen(360), cxuiLen(0));
    self->_bar = cxuiProgressCreate(&(CxUiProgressDesc){ .length = 360 });
    cxuiWidgetSetExpand(self->_bar, CXUI_EXPAND_H);
    cxuiContainerAppend(col, self->_label);
    cxuiContainerAppend(col, self->_bar);
    if (d.cancel) {
        self->_cancel = cxuiButtonCreate(&(CxUiButtonDesc){ .title = strEmpty(d.cancelTitle) ? _S"Cancel" : d.cancelTitle });
        cxuiWidgetSetLayout(self->_cancel, &(CxUiLayoutParams){ .minSize = { cxuiLen(80), cxuiLen(0) },
                                                                .align   = { CXUI_ALIGN_End } });
        cxuiOnActivate(self->_cancel, cancelPressed, stvNone);
        cxuiContainerAppend(col, self->_cancel);
        cxuiWindowSetDefaultEsc(self, self->_cancel);
    }
    cxuiContainerAppend(self, col);
    objRelease(&col);
    showCount(self);
    return self;
}

void CxUiProgressDialog_setText(_In_ CxUiProgressDialog* self, _In_ strref text)
{
    cxuiLabelSetText(self->_label, text);
}

void CxUiProgressDialog_setCount(_In_ CxUiProgressDialog* self, int64 count)
{
    self->_count = count;
    showCount(self);
}

void CxUiProgressDialog_setTotal(_In_ CxUiProgressDialog* self, int64 total)
{
    self->_total = max(total, 0);
    showCount(self);
}

bool CxUiProgressDialog_cancelled(_In_ CxUiProgressDialog* self)
{
    return self->_cancelled;
}

extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiProgressDialog_handleEvent(_In_ CxUiProgressDialog* self, _Inout_ CxUiEvent* ev)
{
    if (ev->kind == CXUI_EV_Close) {
        self->_cancelled = true;
        if (self->_cancel)
            cxuiWidgetSetEnabled(self->_cancel, false);
    }
    return parent_handleEvent(ev);
}

void CxUiProgressDialog_destroy(_In_ CxUiProgressDialog* self)
{
    // Autogen begins -----
    objRelease(&self->_label);
    objRelease(&self->_bar);
    objRelease(&self->_cancel);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuiprogress.auto.inc"
// clang-format on
// Autogen ends -------

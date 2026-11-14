// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuipalette.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <cx/utils/lazyinit.h>

// clang-format off
static const CxUiPaletteColors lightColors = { {
    [CXUI_PAL_Content]        = CXUI_HEX(0xffffff),
    [CXUI_PAL_Panel]          = CXUI_HEX(0xf0f0f1),
    [CXUI_PAL_Border]         = CXUI_HEX(0xc8c8cc),
    [CXUI_PAL_Text]           = CXUI_HEX(0x1f1f1f),
    [CXUI_PAL_TextDim]        = CXUI_HEX(0x70757c),
    [CXUI_PAL_TextDisabled]   = CXUI_HEX(0xa0a3a8),
    [CXUI_PAL_Accent]         = CXUI_HEX(0x1a73d9),
    [CXUI_PAL_Selection]      = CXUI_HEX(0xb5d5fb),
    [CXUI_PAL_SelectionText]  = CXUI_HEX(0x1f1f1f),
    [CXUI_PAL_Hover]          = CXUI_HEX(0xe3e5e8),
    [CXUI_PAL_Pressed]        = CXUI_HEX(0xcfd2d6),
    [CXUI_PAL_Input]          = CXUI_HEX(0xffffff),
    [CXUI_PAL_InputText]      = CXUI_HEX(0x1f1f1f),
    [CXUI_PAL_InputBorder]    = CXUI_HEX(0xa8a8ad),
    [CXUI_PAL_InputBad]       = CXUI_HEX(0xffd9d9),
    [CXUI_PAL_InputCue]       = CXUI_HEX(0x8a8f96),
    [CXUI_PAL_Focus]          = CXUI_HEX(0x1a73d9),
    [CXUI_PAL_ScrollTrack]    = CXUI_HEX(0xf0f0f1),
    [CXUI_PAL_ScrollThumb]    = CXUI_HEX(0xc4c4c8),
    [CXUI_PAL_ScrollThumbHot] = CXUI_HEX(0x9d9da3),
    [CXUI_PAL_TooltipBg]      = CXUI_HEX(0xf7f7f7),
    [CXUI_PAL_TooltipText]    = CXUI_HEX(0x1f1f1f),
    [CXUI_PAL_Link]           = CXUI_HEX(0x0b5cad),
    [CXUI_PAL_LinkHover]      = CXUI_HEX(0x1a73d9),
    [CXUI_PAL_GridLine]       = CXUI_HEX(0xe1e1e4),
    [CXUI_PAL_GridHeader]     = CXUI_HEX(0xf0f0f1),
    [CXUI_PAL_GridHeaderText] = CXUI_HEX(0x1f1f1f),
} };

static const CxUiPaletteColors darkColors = { {
    [CXUI_PAL_Content]        = CXUI_HEX(0x1e1e1e),
    [CXUI_PAL_Panel]          = CXUI_HEX(0x2b2b2d),
    [CXUI_PAL_Border]         = CXUI_HEX(0x3f3f42),
    [CXUI_PAL_Text]           = CXUI_HEX(0xd4d4d4),
    [CXUI_PAL_TextDim]        = CXUI_HEX(0x7f8388),
    [CXUI_PAL_TextDisabled]   = CXUI_HEX(0x65686c),
    [CXUI_PAL_Accent]         = CXUI_HEX(0x4aa3ff),
    [CXUI_PAL_Selection]      = CXUI_HEX(0x264f78),
    [CXUI_PAL_SelectionText]  = CXUI_HEX(0xffffff),
    [CXUI_PAL_Hover]          = CXUI_HEX(0x3a3d41),
    [CXUI_PAL_Pressed]        = CXUI_HEX(0x4b4b4f),
    [CXUI_PAL_Input]          = CXUI_HEX(0x3a3a3d),
    [CXUI_PAL_InputText]      = CXUI_HEX(0xd4d4d4),
    [CXUI_PAL_InputBorder]    = CXUI_HEX(0x55555a),
    [CXUI_PAL_InputBad]       = CXUI_HEX(0x6b2a2a),
    [CXUI_PAL_InputCue]       = CXUI_HEX(0x7f8388),
    [CXUI_PAL_Focus]          = CXUI_HEX(0x4aa3ff),
    [CXUI_PAL_ScrollTrack]    = CXUI_HEX(0x252526),
    [CXUI_PAL_ScrollThumb]    = CXUI_HEX(0x4b4b4f),
    [CXUI_PAL_ScrollThumbHot] = CXUI_HEX(0x6e6e73),
    [CXUI_PAL_TooltipBg]      = CXUI_HEX(0x252526),
    [CXUI_PAL_TooltipText]    = CXUI_HEX(0xd4d4d4),
    [CXUI_PAL_Link]           = CXUI_HEX(0x4aa3ff),
    [CXUI_PAL_LinkHover]      = CXUI_HEX(0x79b8ff),
    [CXUI_PAL_GridLine]       = CXUI_HEX(0x333336),
    [CXUI_PAL_GridHeader]     = CXUI_HEX(0x2b2b2d),
    [CXUI_PAL_GridHeaderText] = CXUI_HEX(0xd4d4d4),
} };
// clang-format on

// The built-ins live for the whole process, shared by every session.
static CxUiPalette* builtin[2];
static LazyInitState builtinInit;

static void builtinInitOnce(void* unused)
{
    unused_noeval(unused);
    for (int i = 0; i < 2; i++) {
        builtin[i]          = objInstCreate(CxUiPalette);
        builtin[i]->_colors = i ? darkColors : lightColors;
        builtin[i]->_frozen = true;
        objInstInit(builtin[i]);
    }
    // Windows and panels look like the platform's dialogs.
    if (_cxui.backend && _cxui.backend->systemPanelColor)
        builtin[0]->_colors.c[CXUI_PAL_Panel] = _cxui.backend->systemPanelColor();
}

_Use_decl_annotations_
CxUiPalette* _cxuiPaletteBuiltin(bool dark)
{
    lazyInit(&builtinInit, builtinInitOnce, NULL);
    return builtin[dark ? 1 : 0];
}

CxUiPalette* cxuiPaletteLight(void)
{
    return objAcquire(_cxuiPaletteBuiltin(false));
}

CxUiPalette* cxuiPaletteDark(void)
{
    return objAcquire(_cxuiPaletteBuiltin(true));
}

_objfactory_guaranteed CxUiPalette* CxUiPalette_create()
{
    CxUiPalette* self;
    self          = objInstCreate(CxUiPalette);
    self->_colors = lightColors;

    objInstInit(self);
    return self;
}

_objfactory_guaranteed CxUiPalette* CxUiPalette_clone(_In_ CxUiPalette* src)
{
    CxUiPalette* self;
    self          = objInstCreate(CxUiPalette);
    self->_colors = src->_colors;

    objInstInit(self);
    return self;
}

CxUiColor CxUiPalette_color(_In_ CxUiPalette* self, uint32 role)
{
    return role < CXUI_PAL_Count ? self->_colors.c[role] : (CxUiColor){ 0 };
}

void CxUiPalette_setColor(_In_ CxUiPalette* self, uint32 role, CxUiColor c)
{
    devAssertMsg(!self->_frozen, "a palette can't change once a widget uses it");
    if (role < CXUI_PAL_Count && !self->_frozen)
        self->_colors.c[role] = c;
}

// Autogen begins -----
// clang-format off
#include "core/cxuipalette.auto.inc"
// clang-format on
// Autogen ends -------

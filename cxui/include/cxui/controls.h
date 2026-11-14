#pragma once

/// @file controls.h
/// @brief Labels, buttons, check boxes, radio buttons, lists, dropdowns, sliders and progress bars

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuicontrols.h>

CX_C_BEGIN

/// @defgroup cxui_controls Native controls
/// @ingroup cxui
/// @{
/// Labels, buttons, check boxes, radio buttons, lists, dropdowns, sliders and progress bars are the
/// platform's own, and look like any other on it.
///
/// - **Mnemonics.** An `&` before a letter in a label's text or a button's title underlines it,
///   and Alt with that letter presses the button, or gives focus to the first widget after the
///   label that can take it (put a label before the field it names). `&&` shows one `&`. macOS
///   has no mnemonics: the `&` is left out there, and Alt does nothing.
/// - **Clicking a button raises an activate event** (cxuiOnActivate()), and so does Space while
///   it has focus, Enter while it has focus, and cxuiButtonPress().
/// - **A window's default buttons** are pressed by Enter and Esc when nothing with focus takes
///   them; see cxuiWindowSetDefaultEnter() and cxuiWindowSetDefaultEsc().
/// - **Check boxes and radio buttons raise change events** when the user changes them: a click,
///   Space while one has focus, its mnemonic, or cxuiCheckPress(). A check box raises its own; a
///   radio button raises one on its CxUiRadioGroup. Setting a value from code raises none.
/// - **A radio group** is a row or column (as cxuiBoxCreate() makes) that keeps at most one radio
///   button inside it selected, however deeply it is nested in other containers there; a group
///   inside a group has radios of its own. Tab stops at the selected radio button, and the arrow
///   keys choose the next or previous one.
/// - **Lists and dropdowns** hold text items, which can be set before or after they are shown.
///   Picking an item raises a change event; read cxuiListValue() or cxuiDropdownValue().
///   Double-clicking a list's item, or Enter on it, raises an activate event whose `item` is its
///   position. Their colors are a text field's.
/// - **A slider** raises a change event as its value changes while the user drags it, and a commit
///   event once they let go, or move it with a key, if its value is different from the last commit.
///   Its values are a whole number of steps from its lowest.
/// - **A label's text takes the palette's text color** unless it is given its own, and its
///   disabled text color while it is disabled.
/// - **A wrapping label** (`wrap`) breaks its text into lines at the width it gets. It starts as
///   wide as its text up to `columns` average characters, expands across to take any width its
///   container offers, and is as tall as its lines need at that width; the window's height follows
///   when its width changes.
/// - On macOS, buttons take keyboard focus only when the system's Keyboard navigation setting
///   is on, as in other Mac programs.
///
/// @code
///   CxUiLabel* nameLabel = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Name:" });
///   CxUiTextField* name  = cxuiTextFieldCreate(NULL);
///   CxUiButton* ok       = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"OK" });
///   CxUiButton* cancel   = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Cancel" });
///   cxuiOnActivate(ok, onOk, stvar(ptr, dlg));
///   cxuiOnActivate(cancel, onCancel, stvar(ptr, dlg));
///   cxuiWindowSetDefaultEnter(win, ok);
///   cxuiWindowSetDefaultEsc(win, cancel);
///   cxuiWindowSetStartFocus(win, name);
/// @endcode

/// CxUiLabel* cxuiLabelCreate(const CxUiLabelDesc* desc)
///
/// Creates a label
///
/// @param ... (desc) Initial settings, or NULL for an empty label
/// @return The label, with a reference the caller owns
#define cxuiLabelCreate(...) CxUiLabel_createWith(__VA_ARGS__)

/// CxUiButton* cxuiButtonCreate(const CxUiButtonDesc* desc)
///
/// Creates a push button
///
/// @param ... (desc) Initial settings, or NULL for a button with no title
/// @return The button, with a reference the caller owns
#define cxuiButtonCreate(...) CxUiButton_createWith(__VA_ARGS__)

/// CxUiCheck* cxuiCheckCreate(const CxUiCheckDesc* desc)
///
/// Creates a check box
///
/// @param ... (desc) Initial settings, or NULL for a clear check box with no title
/// @return The check box, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiCheck* wrap = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Wrap lines", .state = CXUI_CHECK_On });
///   cxuiOnChange(wrap, onWrap, stvar(ptr, view));
/// @endcode
#define cxuiCheckCreate(...) CxUiCheck_createWith(__VA_ARGS__)

/// CxUiRadio* cxuiRadioCreate(const CxUiRadioDesc* desc)
///
/// Creates a radio button, to put in a CxUiRadioGroup
///
/// @param ... (desc) Initial settings, or NULL for one with no title
/// @return The radio button, with a reference the caller owns
#define cxuiRadioCreate(...) CxUiRadio_createWith(__VA_ARGS__)

/// CxUiRadioGroup* cxuiRadioGroupCreate(const CxUiBoxDesc* desc)
///
/// Creates a radio group: a row or column whose radio buttons are one choice
///
/// @param ... (desc) Initial settings as for cxuiBoxCreate(), or NULL for an empty row
/// @return The group, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiRadioGroup* size = cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
///   CxUiRadio* small     = cxuiRadioCreate(&(CxUiRadioDesc){ .title = _S"&Small" });
///   CxUiRadio* large     = cxuiRadioCreate(&(CxUiRadioDesc){ .title = _S"&Large" });
///   cxuiContainerAppend(size, small);
///   cxuiContainerAppend(size, large);
///   cxuiRadioGroupSetValue(size, small);
///   cxuiOnChange(size, onSize, stvar(ptr, dlg));   // reads cxuiRadioGroupIndex(size)
/// @endcode
#define cxuiRadioGroupCreate(...) CxUiRadioGroup_createWith(__VA_ARGS__)

/// CxUiList* cxuiListCreate(const CxUiListDesc* desc)
///
/// Creates a list
///
/// @param ... (desc) Initial settings, or NULL for an empty list five items high
/// @return The list, with a reference the caller owns
///
/// Example:
/// @code
///   static const strref servers[] = { _S"Alpha", _S"Beta", _S"Gamma", NULL };
///   CxUiList* list = cxuiListCreate(&(CxUiListDesc){ .items = servers });
///   cxuiOnActivate(list, onConnect, stvar(ptr, dlg));   // ev->item is the server's position
/// @endcode
#define cxuiListCreate(...) CxUiList_createWith(__VA_ARGS__)

/// CxUiDropdown* cxuiDropdownCreate(const CxUiDropdownDesc* desc)
///
/// Creates a dropdown
///
/// @param ... (desc) Initial settings, or NULL for an empty one
/// @return The dropdown, with a reference the caller owns
///
/// Example:
/// @code
///   static const strref sizes[] = { _S"Small", _S"Medium", _S"Large", NULL };
///   CxUiDropdown* size = cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = sizes, .value = 1 });
///   cxuiOnChange(size, onSize, stvar(ptr, dlg));
/// @endcode
#define cxuiDropdownCreate(...) CxUiDropdown_createWith(__VA_ARGS__)

/// CxUiSlider* cxuiSliderCreate(const CxUiSliderDesc* desc)
///
/// Creates a slider
///
/// @param ... (desc) Initial settings, or NULL for one from 0 to 100 in steps of 1
/// @return The slider, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiSlider* volume = cxuiSliderCreate(&(CxUiSliderDesc){ .min = 0, .max = 1, .step = 0.05, .value = 0.8 });
///   cxuiOnChange(volume, previewVolume, stvar(ptr, mixer));   // while dragging
///   cxuiOnCommit(volume, saveVolume, stvar(ptr, mixer));      // once let go
/// @endcode
#define cxuiSliderCreate(...) CxUiSlider_createWith(__VA_ARGS__)

/// CxUiProgress* cxuiProgressCreate(const CxUiProgressDesc* desc)
///
/// Creates a progress bar
///
/// @param ... (desc) Initial settings, or NULL for an empty one that is full at 100
/// @return The progress bar, with a reference the caller owns
#define cxuiProgressCreate(...) CxUiProgress_createWith(__VA_ARGS__)

/// @}  // end of cxui_controls group

CX_C_END

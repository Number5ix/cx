#pragma once

/// @file text.h
/// @brief Text fields and text areas

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuitext.h>

CX_C_BEGIN

/// @defgroup cxui_text Text fields and text areas
/// @ingroup cxui
/// @{
/// A text field is the platform's own single-line edit, with its caret, selection, clipboard and
/// undo keys. On its own it looks like any other edit on the platform. Inside a CxUiTextFrame it
/// has no border of its own and takes its colors from the palette instead, for a window drawn in
/// its own colors.
///
/// - **Every edit raises a change event** (cxuiOnChange()); setting the text from code doesn't.
/// - **Enter raises a commit event** (cxuiOnCommit()), and so does leaving the field after editing
///   it. Enter then goes on to the field's containers, as an unhandled key does.
/// - **Keys reach the field's key handler before the field acts on them**, Tab included: a handler
///   that returns CXUI_Handled keeps the key from the field and from moving focus.
/// - **Positions count characters** (Unicode code points), from 0. A new field's caret is at the
///   start.
/// - **A text area** (CxUiTextArea) is a text field of several lines, with scroll bars, and is used
///   through the same calls. Enter starts a new line, so it doesn't commit, and doesn't reach the
///   window's default button; Tab still moves focus. It has no cue. It can go in a CxUiTextFrame too.
///
/// @code
///   CxUiTextField* find  = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"Find", .columns = 18 });
///   CxUiTextFrame* frame = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = find, .noBorder = true });
///   cxuiOnChange(find, findChanged, stvar(ptr, lv));
///   cxuiContainerAppend(toolbar, frame);
/// @endcode

/// CxUiTextField* cxuiTextFieldCreate(const CxUiTextFieldDesc* desc)
///
/// Creates a single-line text field
///
/// @param ... (desc) Initial settings, or NULL for an empty field
/// @return The field, with a reference the caller owns
#define cxuiTextFieldCreate(...) CxUiTextField_createWith(__VA_ARGS__)

/// CxUiTextArea* cxuiTextAreaCreate(const CxUiTextAreaDesc* desc)
///
/// Creates a text area of several lines
///
/// It expands in both directions to fill the room its container gives it.
///
/// @param ... (desc) Initial settings, or NULL for an empty area 40 characters across and 4 lines down
/// @return The area, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiTextArea* log = cxuiTextAreaCreate(&(CxUiTextAreaDesc){ .readOnly = true, .wrap = true, .lines = 10 });
///   cxuiTextAreaAppend(log, _S"Connected.\n");
///   cxuiTextAreaScrollToEnd(log);
/// @endcode
#define cxuiTextAreaCreate(...) CxUiTextArea_createWith(__VA_ARGS__)

/// CxUiTextFrame* cxuiTextFrameCreate(const CxUiTextFrameDesc* desc)
///
/// Puts a text field in a frame that draws its border and background in the palette's colors
///
/// Add the frame to a container in place of the field.
///
/// @param ... (desc) Settings, with the field
/// @return The frame, with a reference the caller owns
#define cxuiTextFrameCreate(...) CxUiTextFrame_createWith(__VA_ARGS__)

/// @}  // end of cxui_text group

CX_C_END

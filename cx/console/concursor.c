#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "console_private.h"

#include <cx/utils/compare.h>

// Worst case: "\x1b[" + 5-digit row + ";" + 5-digit col + "H" == 2 + 5 + 1 + 5 + 1 = 14, or
// two relative-move sequences back to back ("\x1b[65535A\x1b[65535D" == 16). 32 leaves margin.
#define CURSOR_BUF_MAX 32

// Appends a single relative-move CSI sequence (CUU/CUD/CUF/CUB) for one axis. delta == 0
// appends nothing. negDir/posDir are the VT final byte for the negative/positive direction
// (e.g. 'A'/'B' for rows, 'D'/'C' for columns).
static uint32 appendMove(char* buf, uint32 pos, int16 delta, char negDir, char posDir)
{
    if (delta == 0)
        return pos;

    uint32 n   = (uint32)(delta < 0 ? -(int32)delta : delta);
    buf[pos++] = '\x1b';
    buf[pos++] = '[';
    pos        = _conAppendDec(buf, pos, n);
    buf[pos++] = delta < 0 ? negDir : posDir;
    return pos;
}

_Use_decl_annotations_
bool conSetCursor(ConStream* con, uint16 row, uint16 col)
{
    conLock(con);
    bool ok;

    if (con->caps.vt) {
        char buf[CURSOR_BUF_MAX];
        uint32 pos = 0;
        buf[pos++] = '\x1b';
        buf[pos++] = '[';
        pos        = _conAppendDec(buf, pos, (uint32)row + 1);
        pos        = _conAppendCode(buf, pos, (uint32)col + 1);
        buf[pos++] = 'H';
        ok         = _conWriteLocked(con, (const uint8*)buf, pos);
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatCursorSet(con, row, col);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conMoveCursor(ConStream* con, int16 drow, int16 dcol)
{
    if (drow == 0 && dcol == 0)
        return true;

    conLock(con);
    bool ok;

    if (con->caps.vt) {
        char buf[CURSOR_BUF_MAX];
        uint32 pos = 0;
        pos        = appendMove(buf, pos, drow, 'A', 'B');
        pos        = appendMove(buf, pos, dcol, 'D', 'C');
        ok         = _conWriteLocked(con, (const uint8*)buf, pos);
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        // The legacy console API has no relative-move call -- query the position and set it back.
        uint16 row, col;
        if (!_conPlatCursorGet(con, &row, &col)) {
            ok = false;
        } else {
            int32 newRow = clamp((int32)row + drow, 0, 0xFFFF);
            int32 newCol = clamp((int32)col + dcol, 0, 0xFFFF);
            ok           = _conPlatCursorSet(con, (uint16)newRow, (uint16)newCol);
        }
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conGetCursor(ConStream* con, uint16* row, uint16* col)
{
    conLock(con);
    bool ok = con->caps.cursorquery && con->kind != CON_Kind_Mem ?
        _conPlatCursorGet(con, row, col) :
        false;
    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conShowCursor(ConStream* con, bool show)
{
    static const uint8 showSeq[] = { '\x1b', '[', '?', '2', '5', 'h' };
    static const uint8 hideSeq[] = { '\x1b', '[', '?', '2', '5', 'l' };

    conLock(con);
    bool ok;

    if (con->caps.vt) {
        ok = show ? _conWriteLocked(con, showSeq, sizeof(showSeq)) :
                    _conWriteLocked(con, hideSeq, sizeof(hideSeq));
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatCursorShow(con, show);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conSaveCursor(ConStream* con)
{
    // DECSC -- ESC 7. Written as two literals ("\x1b" "7") rather than "\x1b7": '7' is a
    // valid hex digit, so a single literal would be parsed as the (out of range) escape
    // \x1B7 instead of ESC followed by the character '7'.
    static const uint8 seq[] = { '\x1b', '7' };

    conLock(con);
    bool ok;

    if (con->caps.vt) {
        ok = _conWriteLocked(con, seq, sizeof(seq));
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatCursorSave(con);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conRestoreCursor(ConStream* con)
{
    // DECRC -- ESC 8. See conSaveCursor() for why this is two literals, not "\x1b8".
    static const uint8 seq[] = { '\x1b', '8' };

    conLock(con);
    bool ok;

    if (con->caps.vt) {
        ok = _conWriteLocked(con, seq, sizeof(seq));
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatCursorRestore(con);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conEraseLine(ConStream* con, ConEraseMode mode)
{
    conLock(con);
    bool ok;

    if (con->caps.vt) {
        char buf[CURSOR_BUF_MAX];
        uint32 pos = 0;
        buf[pos++] = '\x1b';
        buf[pos++] = '[';
        pos        = _conAppendDec(buf, pos, (uint32)mode);
        buf[pos++] = 'K';
        ok         = _conWriteLocked(con, (const uint8*)buf, pos);
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatEraseLine(con, mode);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conEraseScreen(ConStream* con, ConEraseMode mode)
{
    conLock(con);
    bool ok;

    if (con->caps.vt) {
        char buf[CURSOR_BUF_MAX];
        uint32 pos = 0;
        buf[pos++] = '\x1b';
        buf[pos++] = '[';
        pos        = _conAppendDec(buf, pos, (uint32)mode);
        buf[pos++] = 'J';
        ok         = _conWriteLocked(con, (const uint8*)buf, pos);
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatEraseScreen(con, mode);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conScroll(ConStream* con, int16 lines)
{
    if (lines == 0)
        return true;

    conLock(con);
    bool ok;

    if (con->caps.vt) {
        char buf[CURSOR_BUF_MAX];
        uint32 pos = 0;
        buf[pos++] = '\x1b';
        buf[pos++] = '[';
        uint32 n   = (uint32)(lines < 0 ? -(int32)lines : lines);
        pos        = _conAppendDec(buf, pos, n);
        buf[pos++] = lines > 0 ? 'S' : 'T';
        ok         = _conWriteLocked(con, (const uint8*)buf, pos);
    } else if (con->caps.cursor && con->kind != CON_Kind_Mem) {
        ok = _conPlatScroll(con, lines);
    } else {
        ok = false;
    }

    conUnlock(con);
    return ok;
}

_Use_decl_annotations_
bool conAltScreen(ConStream* con, bool enable)
{
    // No legacy fallback: alt screen only works with vt
    static const uint8 onSeq[]  = { '\x1b', '[', '?', '1', '0', '4', '9', 'h' };
    static const uint8 offSeq[] = { '\x1b', '[', '?', '1', '0', '4', '9', 'l' };

    conLock(con);
    bool ok = con->caps.altscreen ?
        (enable ? _conWriteLocked(con, onSeq, sizeof(onSeq)) :
                  _conWriteLocked(con, offSeq, sizeof(offSeq))) :
        false;
    conUnlock(con);
    return ok;
}

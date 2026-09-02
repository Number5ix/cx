// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/struct/struct.h>
#include <cx/string.h>
FSFileWin_ClassIf FSFileWin_ClassIf_tmpl = {
    ._size = sizeof(FSFileWin_ClassIf),
    ._parent = (ObjIface*)&File_ClassIf_tmpl,
};

static FSFileWin_ClassIf _impl_FSFileWin_FSFileWin_ClassIf = {
    ._size = sizeof(FSFileWin_ClassIf),
    ._implements = (ObjIface*)&FSFileWin_ClassIf_tmpl,
    .close = (bool (*)(void*))FSFileWin_close,
    .read = (bool (*)(void*, void*, size_t, size_t*))FSFileWin_read,
    .write = (bool (*)(void*, const void*, size_t, size_t*))FSFileWin_write,
    .tell = (int64 (*)(void*))FSFileWin_tell,
    .seek = (int64 (*)(void*, int64, FSSeekType))FSFileWin_seek,
    .flush = (bool (*)(void*))FSFileWin_flush,
};

static ObjIface* _ifimpl_FSFileWin[] = {
    (ObjIface*)&_impl_FSFileWin_FSFileWin_ClassIf,
    NULL
};

ObjClassInfo FSFileWin_clsinfo = {
    .instsize = sizeof(FSFileWin),
    .classif = (ObjIface*)&FSFileWin_ClassIf_tmpl,
    .parent = &File_clsinfo,
    .destroy = (void(*)(void*))FSFileWin_destroy,
    .ifimpl = _ifimpl_FSFileWin,
};

// ==================== Auto-generated section ends ======================

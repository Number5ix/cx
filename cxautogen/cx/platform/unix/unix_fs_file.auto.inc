// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/struct/struct.h>
#include <cx/string.h>
FSFileUnix_ClassIf FSFileUnix_ClassIf_tmpl = {
    ._size = sizeof(FSFileUnix_ClassIf),
    ._parent = (ObjIface*)&File_ClassIf_tmpl,
};

static FSFileUnix_ClassIf _impl_FSFileUnix_FSFileUnix_ClassIf = {
    ._size = sizeof(FSFileUnix_ClassIf),
    ._implements = (ObjIface*)&FSFileUnix_ClassIf_tmpl,
    .close = (bool (*)(void*))FSFileUnix_close,
    .read = (bool (*)(void*, void*, size_t, size_t*))FSFileUnix_read,
    .write = (bool (*)(void*, const void*, size_t, size_t*))FSFileUnix_write,
    .tell = (int64 (*)(void*))FSFileUnix_tell,
    .seek = (int64 (*)(void*, int64, FSSeekType))FSFileUnix_seek,
    .flush = (bool (*)(void*))FSFileUnix_flush,
};

static ObjIface* _ifimpl_FSFileUnix[] = {
    (ObjIface*)&_impl_FSFileUnix_FSFileUnix_ClassIf,
    NULL
};

ObjClassInfo FSFileUnix_clsinfo = {
    .instsize = sizeof(FSFileUnix),
    .classif = (ObjIface*)&FSFileUnix_ClassIf_tmpl,
    .parent = &File_clsinfo,
    .destroy = (void(*)(void*))FSFileUnix_destroy,
    .ifimpl = _ifimpl_FSFileUnix,
};

// ==================== Auto-generated section ends ======================

// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/struct/struct.h>
#include <cx/string.h>
File_ClassIf File_ClassIf_tmpl = {
    ._size = sizeof(File_ClassIf),
};

static File_ClassIf _impl_File_File_ClassIf = {
    ._size = sizeof(File_ClassIf),
    ._implements = (ObjIface*)&File_ClassIf_tmpl,
};

static ObjIface* _ifimpl_File[] = {
    (ObjIface*)&_impl_File_File_ClassIf,
    NULL
};

ObjClassInfo File_clsinfo = {
    .instsize = sizeof(File),
    .classif = (ObjIface*)&File_ClassIf_tmpl,
    ._abstract = true,
    .ifimpl = _ifimpl_File,
};

// ==================== Auto-generated section ends ======================

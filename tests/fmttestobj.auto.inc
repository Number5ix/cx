// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
FmtTestClass_ClassIf FmtTestClass_ClassIf_tmpl = {
    ._size = sizeof(FmtTestClass_ClassIf),
};

static Formattable _impl_FmtTestClass_Formattable = {
    ._size = sizeof(Formattable),
    ._implements = (ObjIface*)&Formattable_tmpl,
    .format = (bool (*)(void*, FMTVar*, string*))FmtTestClass_format,
};

static FmtTestClass_ClassIf _impl_FmtTestClass_FmtTestClass_ClassIf = {
    ._size = sizeof(FmtTestClass_ClassIf),
    ._implements = (ObjIface*)&FmtTestClass_ClassIf_tmpl,
    .format = (bool (*)(void*, FMTVar*, string*))FmtTestClass_format,
};

static ObjIface *_ifimpl_FmtTestClass[] = {
    (ObjIface*)&_impl_FmtTestClass_Formattable,
    (ObjIface*)&_impl_FmtTestClass_FmtTestClass_ClassIf,
    NULL
};

ObjClassInfo FmtTestClass_clsinfo = {
    .instsize = sizeof(FmtTestClass),
    .classif = (ObjIface*)&FmtTestClass_ClassIf_tmpl,
    .destroy = (void(*)(void*))FmtTestClass_destroy,
    .ifimpl = _ifimpl_FmtTestClass,
};

// ==================== Auto-generated section ends ======================

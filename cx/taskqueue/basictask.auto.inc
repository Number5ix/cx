// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
BasicTask_ClassIf BasicTask_ClassIf_tmpl = {
    ._size = sizeof(BasicTask_ClassIf),
};

static BasicTask_ClassIf _impl_BasicTask_BasicTask_ClassIf = {
    ._size = sizeof(BasicTask_ClassIf),
    ._implements = (ObjIface*)&BasicTask_ClassIf_tmpl,
};

static ObjIface *_ifimpl_BasicTask[] = {
    (ObjIface*)&_impl_BasicTask_BasicTask_ClassIf,
    NULL
};

ObjClassInfo BasicTask_clsinfo = {
    .instsize = sizeof(BasicTask),
    .classif = (ObjIface*)&BasicTask_ClassIf_tmpl,
    ._abstract = true,
    .ifimpl = _ifimpl_BasicTask,
};

// ==================== Auto-generated section ends ======================
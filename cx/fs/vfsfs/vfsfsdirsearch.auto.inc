// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
VFSFSDirSearch_ClassIf VFSFSDirSearch_ClassIf_tmpl = {
    ._size = sizeof(VFSFSDirSearch_ClassIf),
};

static VFSDirSearchProvider _impl_VFSFSDirSearch_VFSDirSearchProvider = {
    ._size = sizeof(VFSDirSearchProvider),
    ._implements = (ObjIface*)&VFSDirSearchProvider_tmpl,
    .close = (bool (*)(void*))VFSFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSFSDirSearch_next,
};

static VFSFSDirSearch_ClassIf _impl_VFSFSDirSearch_VFSFSDirSearch_ClassIf = {
    ._size = sizeof(VFSFSDirSearch_ClassIf),
    ._implements = (ObjIface*)&VFSFSDirSearch_ClassIf_tmpl,
    .close = (bool (*)(void*))VFSFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSFSDirSearch_next,
};

static ObjIface *_ifimpl_VFSFSDirSearch[] = {
    (ObjIface*)&_impl_VFSFSDirSearch_VFSDirSearchProvider,
    (ObjIface*)&_impl_VFSFSDirSearch_VFSFSDirSearch_ClassIf,
    NULL
};

ObjClassInfo VFSFSDirSearch_clsinfo = {
    .instsize = sizeof(VFSFSDirSearch),
    .classif = (ObjIface*)&VFSFSDirSearch_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSFSDirSearch_destroy,
    .ifimpl = _ifimpl_VFSFSDirSearch,
};

// ==================== Auto-generated section ends ======================

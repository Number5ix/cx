// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
VFSVFSDirSearch_ClassIf VFSVFSDirSearch_ClassIf_tmpl = {
    ._size = sizeof(VFSVFSDirSearch_ClassIf),
};

static VFSDirSearchProvider _impl_VFSVFSDirSearch_VFSDirSearchProvider = {
    ._size = sizeof(VFSDirSearchProvider),
    ._implements = (ObjIface*)&VFSDirSearchProvider_tmpl,
    .close = (bool (*)(void*))VFSVFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSVFSDirSearch_next,
};

static VFSVFSDirSearch_ClassIf _impl_VFSVFSDirSearch_VFSVFSDirSearch_ClassIf = {
    ._size = sizeof(VFSVFSDirSearch_ClassIf),
    ._implements = (ObjIface*)&VFSVFSDirSearch_ClassIf_tmpl,
    .close = (bool (*)(void*))VFSVFSDirSearch_close,
    .next = (FSDirEnt *(*)(void*))VFSVFSDirSearch_next,
};

static ObjIface *_ifimpl_VFSVFSDirSearch[] = {
    (ObjIface*)&_impl_VFSVFSDirSearch_VFSDirSearchProvider,
    (ObjIface*)&_impl_VFSVFSDirSearch_VFSVFSDirSearch_ClassIf,
    NULL
};

ObjClassInfo VFSVFSDirSearch_clsinfo = {
    .instsize = sizeof(VFSVFSDirSearch),
    .classif = (ObjIface*)&VFSVFSDirSearch_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSVFSDirSearch_destroy,
    .ifimpl = _ifimpl_VFSVFSDirSearch,
};

// ==================== Auto-generated section ends ======================

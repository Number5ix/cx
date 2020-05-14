// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
VFSVFS_ClassIf VFSVFS_ClassIf_tmpl = {
    ._size = sizeof(VFSVFS_ClassIf),
};

static VFSProvider _impl_VFSVFS_VFSProvider = {
    ._size = sizeof(VFSProvider),
    ._implements = (ObjIface*)&VFSProvider_tmpl,
    .flags = (uint32 (*)(void*))VFSVFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSVFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSVFS_stat,
    .createDir = (bool (*)(void*, string))VFSVFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSVFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSVFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSVFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSVFS_searchDir,
};

static VFSVFS_ClassIf _impl_VFSVFS_VFSVFS_ClassIf = {
    ._size = sizeof(VFSVFS_ClassIf),
    ._implements = (ObjIface*)&VFSVFS_ClassIf_tmpl,
    .flags = (uint32 (*)(void*))VFSVFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSVFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSVFS_stat,
    .createDir = (bool (*)(void*, string))VFSVFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSVFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSVFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSVFS_rename,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSVFS_searchDir,
};

static ObjIface *_ifimpl_VFSVFS[] = {
    (ObjIface*)&_impl_VFSVFS_VFSProvider,
    (ObjIface*)&_impl_VFSVFS_VFSVFS_ClassIf,
    NULL
};

ObjClassInfo VFSVFS_clsinfo = {
    .instsize = sizeof(VFSVFS),
    .classif = (ObjIface*)&VFSVFS_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSVFS_destroy,
    .ifimpl = _ifimpl_VFSVFS,
};

// ==================== Auto-generated section ends ======================

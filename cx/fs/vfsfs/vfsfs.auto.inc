// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
VFSFS_ClassIf VFSFS_ClassIf_tmpl = {
    ._size = sizeof(VFSFS_ClassIf),
};

static VFSProvider _impl_VFSFS_VFSProvider = {
    ._size = sizeof(VFSProvider),
    ._implements = (ObjIface*)&VFSProvider_tmpl,
    .flags = (uint32 (*)(void*))VFSFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSFS_stat,
    .setTimes = (bool (*)(void*, string, int64, int64))VFSFS_setTimes,
    .createDir = (bool (*)(void*, string))VFSFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSFS_rename,
    .getFSPath = (bool (*)(void*, string*, string))VFSFS_getFSPath,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSFS_searchDir,
};

static VFSFS_ClassIf _impl_VFSFS_VFSFS_ClassIf = {
    ._size = sizeof(VFSFS_ClassIf),
    ._implements = (ObjIface*)&VFSFS_ClassIf_tmpl,
    .flags = (uint32 (*)(void*))VFSFS_flags,
    .open = (ObjInst *(*)(void*, string, int))VFSFS_open,
    .stat = (int (*)(void*, string, FSStat*))VFSFS_stat,
    .setTimes = (bool (*)(void*, string, int64, int64))VFSFS_setTimes,
    .createDir = (bool (*)(void*, string))VFSFS_createDir,
    .removeDir = (bool (*)(void*, string))VFSFS_removeDir,
    .deleteFile = (bool (*)(void*, string))VFSFS_deleteFile,
    .rename = (bool (*)(void*, string, string))VFSFS_rename,
    .getFSPath = (bool (*)(void*, string*, string))VFSFS_getFSPath,
    .searchDir = (ObjInst *(*)(void*, string, string, bool))VFSFS_searchDir,
};

static ObjIface *_ifimpl_VFSFS[] = {
    (ObjIface*)&_impl_VFSFS_VFSProvider,
    (ObjIface*)&_impl_VFSFS_VFSFS_ClassIf,
    NULL
};

ObjClassInfo VFSFS_clsinfo = {
    .instsize = sizeof(VFSFS),
    .classif = (ObjIface*)&VFSFS_ClassIf_tmpl,
    .destroy = (void(*)(void*))VFSFS_destroy,
    .ifimpl = _ifimpl_VFSFS,
};

// ==================== Auto-generated section ends ======================

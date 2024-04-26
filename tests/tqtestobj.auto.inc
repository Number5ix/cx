// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
TQTest1_ClassIf TQTest1_ClassIf_tmpl = {
    ._size = sizeof(TQTest1_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

TQTestFail_ClassIf TQTestFail_ClassIf_tmpl = {
    ._size = sizeof(TQTestFail_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

TQTestCC1_ClassIf TQTestCC1_ClassIf_tmpl = {
    ._size = sizeof(TQTestCC1_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

TQTestCC2_ClassIf TQTestCC2_ClassIf_tmpl = {
    ._size = sizeof(TQTestCC2_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

static TQTest1_ClassIf _impl_TQTest1_TQTest1_ClassIf = {
    ._size = sizeof(TQTest1_ClassIf),
    ._implements = (ObjIface*)&TQTest1_ClassIf_tmpl,
    .run = (bool (*)(void*, TaskQueue*, TaskControl*))TQTest1_run,
};

static ObjIface *_ifimpl_TQTest1[] = {
    (ObjIface*)&_impl_TQTest1_TQTest1_ClassIf,
    NULL
};

ObjClassInfo TQTest1_clsinfo = {
    .instsize = sizeof(TQTest1),
    .classif = (ObjIface*)&TQTest1_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQTest1,
};

static TQTestFail_ClassIf _impl_TQTestFail_TQTestFail_ClassIf = {
    ._size = sizeof(TQTestFail_ClassIf),
    ._implements = (ObjIface*)&TQTestFail_ClassIf_tmpl,
    .run = (bool (*)(void*, TaskQueue*, TaskControl*))TQTestFail_run,
};

static ObjIface *_ifimpl_TQTestFail[] = {
    (ObjIface*)&_impl_TQTestFail_TQTestFail_ClassIf,
    NULL
};

ObjClassInfo TQTestFail_clsinfo = {
    .instsize = sizeof(TQTestFail),
    .classif = (ObjIface*)&TQTestFail_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQTestFail,
};

static TQTestCC1_ClassIf _impl_TQTestCC1_TQTestCC1_ClassIf = {
    ._size = sizeof(TQTestCC1_ClassIf),
    ._implements = (ObjIface*)&TQTestCC1_ClassIf_tmpl,
    .run = (bool (*)(void*, TaskQueue*, TaskControl*))TQTestCC1_run,
};

static ObjIface *_ifimpl_TQTestCC1[] = {
    (ObjIface*)&_impl_TQTestCC1_TQTestCC1_ClassIf,
    NULL
};

ObjClassInfo TQTestCC1_clsinfo = {
    .instsize = sizeof(TQTestCC1),
    .classif = (ObjIface*)&TQTestCC1_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQTestCC1,
};

static TQTestCC2_ClassIf _impl_TQTestCC2_TQTestCC2_ClassIf = {
    ._size = sizeof(TQTestCC2_ClassIf),
    ._implements = (ObjIface*)&TQTestCC2_ClassIf_tmpl,
    .run = (bool (*)(void*, TaskQueue*, TaskControl*))TQTestCC2_run,
};

static ObjIface *_ifimpl_TQTestCC2[] = {
    (ObjIface*)&_impl_TQTestCC2_TQTestCC2_ClassIf,
    NULL
};

ObjClassInfo TQTestCC2_clsinfo = {
    .instsize = sizeof(TQTestCC2),
    .classif = (ObjIface*)&TQTestCC2_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQTestCC2,
};

// ==================== Auto-generated section ends ======================

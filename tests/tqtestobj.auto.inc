// ==================== Auto-generated section begins ====================
// clang-format off
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

TQTestSched_ClassIf TQTestSched_ClassIf_tmpl = {
    ._size = sizeof(TQTestSched_ClassIf),
    ._parent = (ObjIface*)&ComplexTask_ClassIf_tmpl,
};

TQTestS1_ClassIf TQTestS1_ClassIf_tmpl = {
    ._size = sizeof(TQTestS1_ClassIf),
    ._parent = (ObjIface*)&TQTestSched_ClassIf_tmpl,
};

TQTestS2_ClassIf TQTestS2_ClassIf_tmpl = {
    ._size = sizeof(TQTestS2_ClassIf),
    ._parent = (ObjIface*)&TQTestSched_ClassIf_tmpl,
};

TQDelayTest_ClassIf TQDelayTest_ClassIf_tmpl = {
    ._size = sizeof(TQDelayTest_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

static TQTest1_ClassIf _impl_TQTest1_TQTest1_ClassIf = {
    ._size = sizeof(TQTest1_ClassIf),
    ._implements = (ObjIface*)&TQTest1_ClassIf_tmpl,
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTest1_run,
};

static ObjIface* _ifimpl_TQTest1[] = {
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
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTestFail_run,
};

static ObjIface* _ifimpl_TQTestFail[] = {
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
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTestCC1_run,
};

static ObjIface* _ifimpl_TQTestCC1[] = {
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
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTestCC2_run,
};

static ObjIface* _ifimpl_TQTestCC2[] = {
    (ObjIface*)&_impl_TQTestCC2_TQTestCC2_ClassIf,
    NULL
};

ObjClassInfo TQTestCC2_clsinfo = {
    .instsize = sizeof(TQTestCC2),
    .classif = (ObjIface*)&TQTestCC2_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQTestCC2,
};

static Sortable _impl_TQTestSched_Sortable = {
    ._size = sizeof(Sortable),
    ._implements = (ObjIface*)&Sortable_tmpl,
};

static TQTestSched_ClassIf _impl_TQTestSched_TQTestSched_ClassIf = {
    ._size = sizeof(TQTestSched_ClassIf),
    ._implements = (ObjIface*)&TQTestSched_ClassIf_tmpl,
};

static ObjIface* _ifimpl_TQTestSched[] = {
    (ObjIface*)&_impl_TQTestSched_Sortable,
    (ObjIface*)&_impl_TQTestSched_TQTestSched_ClassIf,
    NULL
};

ObjClassInfo TQTestSched_clsinfo = {
    .instsize = sizeof(TQTestSched),
    .classif = (ObjIface*)&TQTestSched_ClassIf_tmpl,
    .parent = &ComplexTask_clsinfo,
    ._abstract = true,
    .ifimpl = _ifimpl_TQTestSched,
};

static TQTestS1_ClassIf _impl_TQTestS1_TQTestS1_ClassIf = {
    ._size = sizeof(TQTestS1_ClassIf),
    ._implements = (ObjIface*)&TQTestS1_ClassIf_tmpl,
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTestS1_run,
};

static ObjIface* _ifimpl_TQTestS1[] = {
    (ObjIface*)&_impl_TQTestS1_TQTestS1_ClassIf,
    NULL
};

ObjClassInfo TQTestS1_clsinfo = {
    .instsize = sizeof(TQTestS1),
    .classif = (ObjIface*)&TQTestS1_ClassIf_tmpl,
    .parent = &TQTestSched_clsinfo,
    .ifimpl = _ifimpl_TQTestS1,
};

static TQTestS2_ClassIf _impl_TQTestS2_TQTestS2_ClassIf = {
    ._size = sizeof(TQTestS2_ClassIf),
    ._implements = (ObjIface*)&TQTestS2_ClassIf_tmpl,
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQTestS2_run,
};

static ObjIface* _ifimpl_TQTestS2[] = {
    (ObjIface*)&_impl_TQTestS2_TQTestS2_ClassIf,
    NULL
};

ObjClassInfo TQTestS2_clsinfo = {
    .instsize = sizeof(TQTestS2),
    .classif = (ObjIface*)&TQTestS2_ClassIf_tmpl,
    .parent = &TQTestSched_clsinfo,
    .destroy = (void(*)(void*))TQTestS2_destroy,
    .ifimpl = _ifimpl_TQTestS2,
};

static TQDelayTest_ClassIf _impl_TQDelayTest_TQDelayTest_ClassIf = {
    ._size = sizeof(TQDelayTest_ClassIf),
    ._implements = (ObjIface*)&TQDelayTest_ClassIf_tmpl,
    .run = (uint32 (*)(void*, TaskQueue*, TQWorker*, TaskControl*))TQDelayTest_run,
};

static ObjIface* _ifimpl_TQDelayTest[] = {
    (ObjIface*)&_impl_TQDelayTest_TQDelayTest_ClassIf,
    NULL
};

ObjClassInfo TQDelayTest_clsinfo = {
    .instsize = sizeof(TQDelayTest),
    .classif = (ObjIface*)&TQDelayTest_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .ifimpl = _ifimpl_TQDelayTest,
};

// ==================== Auto-generated section ends ======================

// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
MTask_ClassIf MTask_ClassIf_tmpl = {
    ._size = sizeof(MTask_ClassIf),
    ._parent = (ObjIface*)&Task_ClassIf_tmpl,
};

static MTask_ClassIf _impl_MTask_MTask_ClassIf = {
    ._size = sizeof(MTask_ClassIf),
    ._implements = (ObjIface*)&MTask_ClassIf_tmpl,
    .add = (void (*)(void*, Task*))MTask_add,
    ._cycle = (void (*)(void*, int64*))MTask__cycle,
    .run = (bool (*)(void*, TaskQueue*, TaskControl*))MTask_run,
};

static ObjIface *_ifimpl_MTask[] = {
    (ObjIface*)&_impl_MTask_MTask_ClassIf,
    NULL
};

ObjClassInfo MTask_clsinfo = {
    .instsize = sizeof(MTask),
    .classif = (ObjIface*)&MTask_ClassIf_tmpl,
    .parent = &Task_clsinfo,
    .init = (bool(*)(void*))MTask_init,
    .destroy = (void(*)(void*))MTask_destroy,
    .ifimpl = _ifimpl_MTask,
};

// ==================== Auto-generated section ends ======================

// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
TaskQueue_ClassIf TaskQueue_ClassIf_tmpl = {
    ._size = sizeof(TaskQueue_ClassIf),
};

static TaskQueue_ClassIf _impl_TaskQueue_TaskQueue_ClassIf = {
    ._size = sizeof(TaskQueue_ClassIf),
    ._implements = (ObjIface*)&TaskQueue_ClassIf_tmpl,
    .start = (bool (*)(void*, Event*))TaskQueue_start,
};

static ObjIface *_ifimpl_TaskQueue[] = {
    (ObjIface*)&_impl_TaskQueue_TaskQueue_ClassIf,
    NULL
};

ObjClassInfo TaskQueue_clsinfo = {
    .instsize = sizeof(TaskQueue),
    .classif = (ObjIface*)&TaskQueue_ClassIf_tmpl,
    .init = (bool(*)(void*))TaskQueue_init,
    .destroy = (void(*)(void*))TaskQueue_destroy,
    .ifimpl = _ifimpl_TaskQueue,
};

// ==================== Auto-generated section ends ======================

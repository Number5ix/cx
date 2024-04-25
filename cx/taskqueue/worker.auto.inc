// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
TaskQueueWorker_ClassIf TaskQueueWorker_ClassIf_tmpl = {
    ._size = sizeof(TaskQueueWorker_ClassIf),
};

static TaskQueueWorker_ClassIf _impl_TaskQueueWorker_TaskQueueWorker_ClassIf = {
    ._size = sizeof(TaskQueueWorker_ClassIf),
    ._implements = (ObjIface*)&TaskQueueWorker_ClassIf_tmpl,
    .start = (bool (*)(void*, TaskQueue*))TaskQueueWorker_start,
};

static ObjIface *_ifimpl_TaskQueueWorker[] = {
    (ObjIface*)&_impl_TaskQueueWorker_TaskQueueWorker_ClassIf,
    NULL
};

ObjClassInfo TaskQueueWorker_clsinfo = {
    .instsize = sizeof(TaskQueueWorker),
    .classif = (ObjIface*)&TaskQueueWorker_ClassIf_tmpl,
    .destroy = (void(*)(void*))TaskQueueWorker_destroy,
    .ifimpl = _ifimpl_TaskQueueWorker,
};

// ==================== Auto-generated section ends ======================

// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
JSONNode_ClassIf JSONNode_ClassIf_tmpl = {
    ._size = sizeof(JSONNode_ClassIf),
    ._parent = (ObjIface*)&SSDNode_ClassIf_tmpl,
};

static JSONNode_ClassIf _impl_JSONNode_JSONNode_ClassIf = {
    ._size = sizeof(JSONNode_ClassIf),
    ._implements = (ObjIface*)&JSONNode_ClassIf_tmpl,
    .createLike = (SSDNode *(*)(void*, SSDInfo*))JSONNode_createLike,
};

static ObjIface *_ifimpl_JSONNode[] = {
    (ObjIface*)&_impl_JSONNode_JSONNode_ClassIf,
    NULL
};

ObjClassInfo JSONNode_clsinfo = {
    .instsize = sizeof(JSONNode),
    .classif = (ObjIface*)&JSONNode_ClassIf_tmpl,
    .parent = &SSDNode_clsinfo,
    .init = (bool(*)(void*))JSONNode_init,
    .destroy = (void(*)(void*))JSONNode_destroy,
    .ifimpl = _ifimpl_JSONNode,
};

// ==================== Auto-generated section ends ======================

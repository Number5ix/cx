// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
SSDNode_ClassIf SSDNode_ClassIf_tmpl = {
    ._size = sizeof(SSDNode_ClassIf),
};

static SSDNode_ClassIf _impl_SSDNode_SSDNode_ClassIf = {
    ._size = sizeof(SSDNode_ClassIf),
    ._implements = (ObjIface*)&SSDNode_ClassIf_tmpl,
    .getChild = (SSDNode *(*)(void*, strref, bool, SSDLock*))SSDNode_getChild,
    .getValue = (bool (*)(void*, strref, stvar*, SSDLock*))SSDNode_getValue,
    .getPtr = (stvar *(*)(void*, strref, SSDLock*))SSDNode_getPtr,
    .setValue = (void (*)(void*, strref, stvar, SSDLock*))SSDNode_setValue,
    .setValueC = (void (*)(void*, strref, stvar*, SSDLock*))SSDNode_setValueC,
    .removeValue = (bool (*)(void*, strref, SSDLock*))SSDNode_removeValue,
};

static ObjIface *_ifimpl_SSDNode[] = {
    (ObjIface*)&_impl_SSDNode_SSDNode_ClassIf,
    NULL
};

ObjClassInfo SSDNode_clsinfo = {
    .instsize = sizeof(SSDNode),
    .classif = (ObjIface*)&SSDNode_ClassIf_tmpl,
    .init = (bool(*)(void*))SSDNode_init,
    .destroy = (void(*)(void*))SSDNode_destroy,
    .ifimpl = _ifimpl_SSDNode,
};

// ==================== Auto-generated section ends ======================

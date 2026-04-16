#include <cx/container.h>
#include <cx/fs/file.h>
#include <cx/string.h>
#include "cxautogen.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <cx/struct.h>
#include <3rdparty/pcre2/pcre2.h>

static const string autogenBegin = _S
    "// ==================== Auto-generated section begins ====================";
static const string autogenNotice = _S
    "// Do not modify the contents of this section; any changes will be lost!";
static const string autogenEnd = _S
    "// ==================== Auto-generated section ends ======================";
static const string autogenBeginShort       = _S"// Autogen begins -----";
static const string autogenEndShort         = _S"// Autogen ends -------";
static const string autogenBeginShortIndent = _S"    // Autogen begins -----";
static const string autogenEndShortIndent   = _S"    // Autogen ends -------";
static const string clangOff                = _S"// clang-format off";
static const string clangOn                 = _S"// clang-format on";

static sa_string parentmacros;

static bool striCharNoWS(striter* iter, uint8* out)
{
    uint8 ch, nextch;
    while (striChar(iter, &ch)) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' &&
            !(ch == '\\' && striPeekChar(iter, &nextch) && (nextch == '\r' || nextch == '\n'))) {
            *out = ch;
            return true;
        }
    }

    return false;
}

static bool strEqNoWS(strref s1, strref s2)
{
    striter i1, i2;
    striBorrow(&i1, s1);
    striBorrow(&i2, s2);

    uint8 ch1 = 0, ch2 = 0;
    bool ret1, ret2;
    for (;;) {
        ret1 = striCharNoWS(&i1, &ch1);
        ret2 = striCharNoWS(&i2, &ch2);

        if (!ret1 && !ret2)
            return true;    // Hit end at the same time
        if (!ret1 || !ret2)
            return false;   // One hit end but other didn't

        if (ch1 != ch2)
            return false;
    }

    return false;   // unreachable
}

static void strAppendLine(string* str, strref ln)
{
    if (!strEmpty(*str)) {
#ifdef _PLATFORM_WIN
        strAppend(str, _S"\r\n");
#else
        strAppend(str, _S"\n");
#endif
    }
    strAppend(str, ln);
}

static void writeAutoInit(StreamBuffer* bf, Class* cls);
static void writeAutoDtors(StreamBuffer* bf, Class* cls);

static void writeMethodProto(StreamBuffer* bf, Class* cls, Method* m, bool protoonly,
                             bool mixinimpl, bool forparent)
{
    string mname = 0, ln = 0, tmp = 0, temp2 = 0, annos = 0;
    methodImplName(&mname, cls, m->name);

    if (!protoonly && !forparent && !mixinimpl && !m->isfactory && !m->isinit && !m->isdestroy &&
        cls->parent) {
        Class* pclass = cls->parent;
        Method* pm    = 0;

        while (pclass) {
            int32 idx = saFind(pclass->methods, object, m);
            if (idx != -1) {
                pm = pclass->methods.a[idx];
                break;
            }
            pclass = pclass->parent;
        }

        if (pm) {
            writeMethodProto(bf, pclass, pm, true, false, true);

            string pmname = 0;
            methodImplName(&pmname, pclass, pm->name);

            if (saFind(parentmacros, string, m->name) != -1) {
                strNConcat(&ln, _S"#undef ", _S"parent_", m->name);
                sbufPWriteLine(bf, ln);
            }

            strNConcat(&ln, _S"#define ", _S"parent_", m->name, _S"(");
            saPush(&parentmacros, string, m->name, SA_Unique);
            for (int j = 0; j < saSize(pm->params); j++) {
                if (j > 0)
                    strNConcat(&ln, ln, _S", ", pm->params.a[j]->name);
                else
                    strAppend(&ln, pm->params.a[j]->name);
            }
            strNConcat(&ln, ln, _S") ", pmname, _S"((", pclass->name, _S"*)(self)");
            for (int j = 0; j < saSize(pm->params); j++) {
                strNConcat(&ln, ln, _S", ", pm->params.a[j]->name);
            }
            strAppend(&ln, _S")");

            strDestroy(&pmname);
            sbufPWriteLine(bf, ln);
            strClear(&ln);
        }
    }

    methodAnnotations(&tmp, m);
    if (!m->standalone)
        strNConcat(&ln, tmp, m->returntype, m->predecr, _S" ", mname, _S"(_In_ ", cls->name, _S"* self");
    else
        strNConcat(&ln, tmp, m->returntype, m->predecr, _S" ", mname, _S"(");

    if (forparent)
        strPrepend(_S"extern ", &ln);
    if (mixinimpl)
        strPrepend(_S"_meta_inline ", &ln);

    for (int j = 0; j < saSize(m->params); j++) {
        Param* p     = m->params.a[j];
        string ptype = p->type;
        string ppre  = p->predecr;

        if (strEq(ptype, _S"object") && strEmpty(ppre)) {
            ptype = cls->name;
            ppre  = _S"*";
        } else if (strEq(ptype, _S"weak") && strEmpty(ppre)) {
            strNConcat(&temp2, _S"Weak(", cls->name, _S")");
            ptype = temp2;
            ppre  = _S"*";
        }

        paramAnnotations(&annos, p);
        if (!m->standalone || j > 0)
            strNConcat(&tmp, _S", ", annos, ptype, ppre, _S" ", p->name, p->postdecr);
        else
            strNConcat(&tmp, annos, ptype, ppre, _S" ", p->name, p->postdecr);
        strAppend(&ln, tmp);
    }
    strAppend(&ln, _S")");
    if (protoonly)
        strAppend(&ln, _S";");

    if (forparent)
        strAppend(&ln, _S"   // parent");

    sbufPWriteLine(bf, ln);
    strDestroy(&tmp);
    strDestroy(&temp2);
    strDestroy(&annos);
    strDestroy(&ln);
}

static void writeMethods(StreamBuffer* bf, Class* cls, sa_string* seen, bool mixinimpl)
{
    string ln = 0, mname = 0;
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if (m->mixin != mixinimpl || getAnnotation(NULL, m->annotations, _S"extern"))
            continue;
        methodImplName(&mname, cls, m->name);
        if (saFind(*seen, string, mname) == -1) {
            writeMethodProto(bf, cls, m, false, mixinimpl, false);
            sbufPWriteLine(bf, _S"{");
            if (m->isinit) {
                writeAutoInit(bf, cls);
            } else if (m->isdestroy) {
                writeAutoDtors(bf, cls);
            } else if (m->isfactory) {
                strNConcat(&ln, _S"    ", cls->name, _S"* self;");
                sbufPWriteLine(bf, ln);
                strNConcat(&ln, _S"    self = objInstCreate(", cls->name, _S");");
                sbufPWriteLine(bf, ln);
                sbufPWriteEOL(bf);
                sbufPWriteLine(bf,
                               _S"    // Insert any pre-initialization object construction here");
                sbufPWriteEOL(bf);
                if (m->canfail) {
                    sbufPWriteLine(bf, _S"    if (!objInstInit(self)) {");
                    sbufPWriteLine(bf, _S"        objRelease(&self);");
                    sbufPWriteLine(bf, _S"        return NULL;");
                    sbufPWriteLine(bf, _S"    }");
                } else {
                    sbufPWriteLine(bf, _S"    objInstInit(self);");
                }
                sbufPWriteEOL(bf);
                sbufPWriteLine(bf,
                               _S"    // Insert any post-initialization object construction here");
                sbufPWriteEOL(bf);
                sbufPWriteLine(bf, _S"    return self;");
            } else if (strEq(m->name, _S"cmp")) {
                sbufPWriteLine(
                    bf,
                    _S"    // Uncomment unless this function can compare different object classes");
                sbufPWriteLine(bf, _S"    // devAssert(objClsInfo(self) == objClsInfo(other));");
                sbufPWriteEOL(bf);
                sbufPWriteLine(bf, _S"    return objDefaultCmp(self, other, flags);");
            } else if (strEq(m->name, _S"hash")) {
                sbufPWriteLine(bf, _S"    return objDefaultHash(self, flags);");
            } else {
                sbufPWriteLine(bf, _S"    #error Replace this line with your implementation");
            }
            sbufPWriteLine(bf, _S"}");
            sbufPWriteEOL(bf);
        }
    }
    strDestroy(&mname);
    strDestroy(&ln);
}

static void writeStructLifecycleFunc(StreamBuffer* bf, StructDef* str, sa_string* seen,
                                     strref funcname)
{
    string ln = 0, mname = 0;
    strNConcat(&mname, str->name, _S"_", funcname);
    if (saFind(*seen, string, mname) == -1) {
        strNConcat(&ln, _S"void ", mname, _S"(", str->name, _S"* self)");
        sbufPWriteLine(bf, ln);
        sbufPWriteLine(bf, _S"{");
        sbufPWriteLine(bf, _S"    #error Replace this line with your implementation");
        sbufPWriteLine(bf, _S"}");
        sbufPWriteEOL(bf);
    }

    strDestroy(&mname);
    strDestroy(&ln);
}

static void writeStructLifecycleFuncs(StreamBuffer* bf, StructDef* str, sa_string* seen)
{
    if (str->hasinit)
        writeStructLifecycleFunc(bf, str, seen, _S"init");
    if (str->hasdestroy)
        writeStructLifecycleFunc(bf, str, seen, _S"destroy");
}

static void writeExternMethods(StreamBuffer* bf, Class* cls)
{
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if (m->mixin || !getAnnotation(NULL, m->annotations, _S"extern"))
            continue;

        writeMethodProto(bf, cls, m, true, false, false);
    }
}

static void writeMixinStubs(StreamBuffer* bf, Class* cls, bool* wroteany)
{
    string ln = 0, mname = 0, vname = 0;
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if (!m->mixin || cls->mixin)
            continue;

        *wroteany = true;
        writeMethodProto(bf, cls, m, false, false, false);
        sbufPWriteLine(bf, _S"{");
        strDup(&ln, _S"    ");
        if (!strEq(m->returntype, _S"void"))
            strAppend(&ln, _S"return ");

        methodImplName(&mname, m->srcclass, m->name);
        mixinMemberName(&vname, m->mixinsrc);
        strNConcat(&ln, ln, mname, _S"((", m->srcclass->name, _S"*)&self->", vname);

        for (int j = 0; j < saSize(m->params); j++) {
            Param* p = m->params.a[j];
            strNConcat(&ln, ln, _S", ", p->name);
        }

        strNConcat(&ln, ln, _S");");
        sbufPWriteLine(bf, ln);
        sbufPWriteLine(bf, _S"}");
        sbufPWriteEOL(bf);
    }
    strDestroy(&vname);
    strDestroy(&mname);
    strDestroy(&ln);
}

typedef struct MethodPair {
    Class* c;
    Method* m;
} MethodPair;

static void indexMethods(Class* cls, hashtable* htbl)
{
    string mname = 0;
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        methodImplName(&mname, cls, m->name);
        htInsert(htbl, string, mname, opaque, ((MethodPair) { .c = cls, .m = m }));
    }
    strDestroy(&mname);
}

static void writeAutoInit(StreamBuffer* bf, Class* cls)
{
    string ln = 0, tmp = 0, flags = 0;
    sa_string flagarr;

    sbufPWriteLine(bf, autogenBeginShortIndent);

    for (int i = 0; i < saSize(cls->members); i++) {
        Member* m = cls->members.a[i];
        if (!m->init)
            continue;

        strClear(&ln);
        if (m->mixinsrc) {
            for (Class* mixin = m->mixinsrc; mixin; mixin = mixin->parent) {
                if (mixin->hasinit) {
                    mixinMemberName(&tmp, m->mixinsrc);
                    if (mixin->initcanfail) {
                        strNConcat(&ln,
                                   _S"    if (!",
                                   mixin->name,
                                   _S"_init((",
                                   mixin->name,
                                   _S"*)&self->",
                                   tmp,
                                   _S"))");
                        sbufPWriteLine(bf, ln);
                        sbufPWriteLine(bf, _S"        return false;");
                    } else {
                        strNConcat(&ln,
                                   _S"    ",
                                   mixin->name,
                                   _S"_init((",
                                   mixin->name,
                                   _S"*)&self->",
                                   tmp,
                                   _S");");
                        sbufPWriteLine(bf, ln);
                    }
                }
            }
            continue;
        } else if (strEq(m->vartype, _S"CondVar")) {
            saInit(&flagarr, string, 1);

            if (getAnnotation(NULL, m->annotations, _S"nospin"))
                saPush(&flagarr, string, _S"CONDVAR_NoSpin");

            strClear(&flags);
            if (saSize(flagarr) != 0) {
                strJoin(&flags, flagarr, _S" | ");
                strPrepend(_S", ", &flags);
            }
            strNConcat(&ln, _S"    cvarInit(&self->", m->name, flags, _S");");
            saDestroy(&flagarr);
        } else if (strEq(m->vartype, _S"Event")) {
            saInit(&flagarr, string, 1);

            if (getAnnotation(NULL, m->annotations, _S"spin"))
                saPush(&flagarr, string, _S"EV_Spin");
            if (getAnnotation(NULL, m->annotations, _S"uievent"))
                saPush(&flagarr, string, _S"EV_UIEvent");

            strClear(&flags);
            if (saSize(flagarr) != 0) {
                strJoin(&flags, flagarr, _S" | ");
                strPrepend(_S", ", &flags);
            }
            strNConcat(&ln, _S"    eventInit(&self->", m->name, flags, _S");");
            saDestroy(&flagarr);
        } else if (strEq(m->vartype, _S"Mutex")) {
            saInit(&flagarr, string, 1);

            if (getAnnotation(NULL, m->annotations, _S"nospin"))
                saPush(&flagarr, string, _S"MUTEX_NoSpin");

            strClear(&flags);
            if (saSize(flagarr) != 0) {
                strJoin(&flags, flagarr, _S" | ");
                strPrepend(_S", ", &flags);
            }
            strNConcat(&ln, _S"    mutexInit(&self->", m->name, flags, _S");");
            saDestroy(&flagarr);
        } else if (strEq(m->vartype, _S"RWLock")) {
            saInit(&flagarr, string, 1);

            if (getAnnotation(NULL, m->annotations, _S"nospin"))
                saPush(&flagarr, string, _S"RWLOCK_NoSpin");

            strClear(&flags);
            if (saSize(flagarr) != 0) {
                strJoin(&flags, flagarr, _S" | ");
                strPrepend(_S", ", &flags);
            }
            strNConcat(&ln, _S"    rwlockInit(&self->", m->name, flags, _S");");
            saDestroy(&flagarr);
        } else if (strEq(m->vartype, _S"Semaphore")) {
            saInit(&flagarr, string, 1);
            string count = _S"0";
            sa_string an;

            if (getAnnotation(NULL, m->annotations, _S"nospin"))
                saPush(&flagarr, string, _S"SEMA_NoSpin");

            getAnnotation(&an, m->annotations, _S"count");
            if (saSize(an) == 2)
                count = an.a[1];

            strClear(&flags);
            if (saSize(flagarr) != 0) {
                strJoin(&flags, flagarr, _S" | ");
                strPrepend(_S", ", &flags);
            }
            strNConcat(&ln, _S"    semaInit(&self->", m->name, _S", ", count, flags, _S");");
            saDestroy(&flagarr);
        } else if (m->initstr) {
            strNConcat(&ln, _S"    self->", m->name, _S" = ", m->initstr, _S";");
        } else if (m->typenode && saSize(m->typenode->params) > 0) {
            if (strEq(m->typenode->name, _S"sarray")) {
                saInit(&flagarr, string, 1);
                string size = _S"1";
                sa_string an;

                if (getAnnotation(NULL, m->annotations, _S"ref"))
                    saPush(&flagarr, string, _S"SA_Ref");
                if (getAnnotation(NULL, m->annotations, _S"sorted"))
                    saPush(&flagarr, string, _S"SA_Sorted");
                getAnnotation(&an, m->annotations, _S"grow");
                if (saSize(an) == 2) {
                    strNConcat(&tmp, _S"SA_Grow(", an.a[1], _S")");
                    saPush(&flagarr, string, tmp);
                }
                getAnnotation(&an, m->annotations, _S"size");
                if (saSize(an) == 2)
                    size = an.a[1];

                strClear(&flags);
                if (saSize(flagarr) != 0) {
                    strJoin(&flags, flagarr, _S" | ");
                    strPrepend(_S", ", &flags);
                }
                strNConcat(&ln,
                           _S"    saInit(&self->",
                           m->name,
                           _S", ",
                           m->typenode->params.a[0]->name,
                           _S", ",
                           size,
                           flags,
                           _S");");
                saDestroy(&flagarr);
            }
            if (strEq(m->typenode->name, _S"hashtable") && saSize(m->typenode->params) == 2) {
                saInit(&flagarr, string, 1);
                string size = _S"16";
                sa_string an;

                if (getAnnotation(NULL, m->annotations, _S"ref"))
                    saPush(&flagarr, string, _S"HT_Ref");
                if (getAnnotation(NULL, m->annotations, _S"refkeys"))
                    saPush(&flagarr, string, _S"HT_RefKeys");
                if (getAnnotation(NULL, m->annotations, _S"caseinsensitive"))
                    saPush(&flagarr, string, _S"HT_CaseInsensitive");
                getAnnotation(&an, m->annotations, _S"grow");
                if (saSize(an) == 2) {
                    strNConcat(&tmp, _S"HT_Grow(", an.a[1], _S")");
                    saPush(&flagarr, string, tmp);
                }
                getAnnotation(&an, m->annotations, _S"size");
                if (saSize(an) == 2)
                    size = an.a[1];

                strClear(&flags);
                if (saSize(flagarr) != 0) {
                    strJoin(&flags, flagarr, _S" | ");
                    strPrepend(_S", ", &flags);
                }
                strNConcat(&ln,
                           _S"    htInit(&self->",
                           m->name,
                           _S", ",
                           m->typenode->params.a[0]->name,
                           _S", ",
                           m->typenode->params.a[1]->name,
                           _S", ",
                           size,
                           flags,
                           _S");");
                saDestroy(&flagarr);
            }
        }

        if (!strEmpty(ln))
            sbufPWriteLine(bf, ln);
    }

    sbufPWriteLine(bf, _S"    return true;");
    sbufPWriteLine(bf, autogenEndShortIndent);
    strDestroy(&flags);
    strDestroy(&tmp);
    strDestroy(&ln);
}

static void writeAutoDtors(StreamBuffer* bf, Class* cls)
{
    if (!cls->hasautodtors)
        return;

    sbufPWriteLine(bf, autogenBeginShortIndent);

    for (int i = 0; i < saSize(cls->members); i++) {
        Member* m = cls->members.a[i];
        if (!m->destroy)
            continue;

        string mdtor = 0;

        if (m->mixinsrc) {
            for (Class* mixin = m->mixinsrc; mixin; mixin = mixin->parent) {
                if (mixin->hasdestroy) {
                    string tmp = 0;
                    mixinMemberName(&tmp, m->mixinsrc);
                    strNConcat(&mdtor,
                               _S"    ",
                               mixin->name,
                               _S"_destroy((",
                               mixin->name,
                               _S"*)&self->",
                               tmp,
                               _S");");
                    sbufPWriteLine(bf, mdtor);
                    strDestroy(&tmp);
                }
            }
            continue;
        } else if (m->typenode && saSize(m->typenode->params) > 0) {
            if (strEq(m->typenode->name, _S"sarray"))
                strNConcat(&mdtor, _S"    saDestroy(&self->", m->name, _S");");
            else if (strEq(m->typenode->name, _S"hashtable"))
                strNConcat(&mdtor, _S"    htDestroy(&self->", m->name, _S");");
            else if (strEq(m->typenode->name, _S"object"))
                strNConcat(&mdtor, _S"    objRelease(&self->", m->name, _S");");
            else if (strEq(m->typenode->name, _S"weak"))
                strNConcat(&mdtor, _S"    objDestroyWeak(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"string")) {
            strNConcat(&mdtor, _S"    strDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"hashtable")) {
            strNConcat(&mdtor, _S"    htDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"stvar")) {
            strNConcat(&mdtor,
                       _S"    _stDestroy(self->",
                       m->name,
                       _S".type, &self->",
                       m->name,
                       _S".data, 0);");
        } else if (strEq(m->vartype, _S"closure")) {
            strNConcat(&mdtor, _S"    closureDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"cchain")) {
            strNConcat(&mdtor, _S"    cchainDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"buffer")) {
            strNConcat(&mdtor, _S"    bufDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"BufChain")) {
            strNConcat(&mdtor, _S"    bufchainDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"BufRing")) {
            strNConcat(&mdtor, _S"    bufringDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"CondVar")) {
            strNConcat(&mdtor, _S"    cvarDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"Event")) {
            strNConcat(&mdtor, _S"    eventDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"Mutex")) {
            strNConcat(&mdtor, _S"    mutexDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"RWLock")) {
            strNConcat(&mdtor, _S"    rwlockDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"Semaphore")) {
            strNConcat(&mdtor, _S"    semaDestroy(&self->", m->name, _S");");
        }
        if (!strEmpty(mdtor))
            sbufPWriteLine(bf, mdtor);
        strDestroy(&mdtor);
    }

    sbufPWriteLine(bf, autogenEndShortIndent);
}

static void writeMixinProtos(StreamBuffer* bf, Class* cls)
{
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if (m->isinit || m->isdestroy) {
            writeMethodProto(bf, cls, m, true, false, false);
            continue;
        }
    }
}

static void writeIfaceTmpl(StreamBuffer* bf, Interface* iface, bool* wroteany)
{
    string ln = 0;

    *wroteany = true;
    strNConcat(&ln, iface->name, _S" ", iface->name, _S"_tmpl = {");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._size = sizeof(", iface->name, _S"),");
    sbufPWriteLine(bf, ln);
    if (iface->parent) {
        strNConcat(&ln, _S"    ._parent = (ObjIface*)&", iface->parent->name, _S"_tmpl,");
        sbufPWriteLine(bf, ln);
    }

    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);
    strDestroy(&ln);
}

static Method* findIfaceMethod(Interface* iface, string name)
{
    for (int i = 0; i < saSize(iface->allmethods); i++) {
        if (strEq(iface->allmethods.a[i]->name, name))
            return iface->allmethods.a[i];
    }
    return NULL;
}

static void writeClassIfaceTbl(StreamBuffer* bf, Class* cls, Interface* iface)
{
    string ln = 0, implname = 0;

    strNConcat(&ln, _S"static ", iface->name, _S" _impl_", cls->name, _S"_", iface->name, _S" = {");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._size = sizeof(", iface->name, _S"),");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._implements = (ObjIface*)&", iface->name, _S"_tmpl,");
    sbufPWriteLine(bf, ln);

    for (int i = 0; i < saSize(cls->methods); i++) {
        if (cls->methods.a[i]->internal)
            continue;

        // see if this class method is part of the interface
        Method* m = findIfaceMethod(iface, cls->methods.a[i]->name);
        if (!m)
            continue;

        methodImplName(&implname, cls, m->name);
        strNConcat(&ln, _S"    .", m->name, _S" = (", m->returntype, m->predecr, _S" ", _S"(*)(void*");
        for (int j = 0; j < saSize(m->params); j++) {
            Param* p     = m->params.a[j];
            string ptype = p->type;
            string ppre  = p->predecr;

            if ((strEq(ptype, _S"object") || strEq(ptype, _S"weak")) && strEmpty(ppre)) {
                ptype = _S"void";
                ppre  = _S"*";
            }

            strNConcat(&ln, ln, _S", ", ptype, ppre, p->postdecr);
        }
        strNConcat(&ln, ln, _S"))", implname, _S",");
        sbufPWriteLine(bf, ln);
    }

    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);
    strDestroy(&implname);
    strDestroy(&ln);
}

static void writeClassIfaceList(StreamBuffer* bf, Class* cls)
{
    string ln = 0;

    strNConcat(&ln, _S"static ObjIface* _ifimpl_", cls->name, _S"[] = {");
    sbufPWriteLine(bf, ln);

    for (int i = 0; i < saSize(cls->implements); i++) {
        strNConcat(&ln, _S"    (ObjIface*)&_impl_", cls->name, _S"_", cls->implements.a[i]->name, _S",");
        sbufPWriteLine(bf, ln);
    }

    sbufPWriteLine(bf, _S"    NULL");
    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);
    strDestroy(&ln);
}

static void writeClassImpl(StreamBuffer* bf, Class* cls, bool* wroteany)
{
    string ln = 0;

    *wroteany = true;

    for (int i = 0; i < saSize(cls->implements); i++) {
        writeClassIfaceTbl(bf, cls, cls->implements.a[i]);
    }
    writeClassIfaceList(bf, cls);

    strNConcat(&ln, _S"ObjClassInfo ", cls->name, _S"_clsinfo = {");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .instsize = sizeof(", cls->name, _S"),");
    sbufPWriteLine(bf, ln);
    if (cls->classif) {
        strNConcat(&ln, _S"    .classif = (ObjIface*)&", cls->classif->name, _S"_tmpl,");
        sbufPWriteLine(bf, ln);
    }
    if (cls->parent) {
        strNConcat(&ln, _S"    .parent = &", cls->parent->name, _S"_clsinfo,");
        sbufPWriteLine(bf, ln);
    }
    if (cls->hasinit) {
        strNConcat(&ln, _S"    .init = (bool(*)(void*))", cls->name, _S"_init,");
        sbufPWriteLine(bf, ln);
    }
    if (cls->hasdestroy) {
        strNConcat(&ln, _S"    .destroy = (void(*)(void*))", cls->name, _S"_destroy,");
        sbufPWriteLine(bf, ln);
    }
    if (cls->abstract) {
        sbufPWriteLine(bf, _S"    ._abstract = true,");
    }
    strNConcat(&ln, _S"    .ifimpl = _ifimpl_", cls->name, _S",");
    sbufPWriteLine(bf, ln);

    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    strDestroy(&ln);
}

bool isCompoundNode(TypeNode* node)
{
    if (!node || saSize(node->params) == 0)
        return false;
    return strEq(node->name, _S"sarray") || strEq(node->name, _S"hashtable") ||
           strEq(node->name, _S"structp");
}

bool isStructName(strref name)
{
    for (int i = 0; i < saSize(structs); i++)
        if (strEq(structs.a[i]->name, name))
            return true;
    return false;
}

bool isStructSetName(strref name)
{
    for (int i = 0; i < saSize(structsets); i++)
        if (strEq(structsets.a[i]->name, name))
            return true;
    return false;
}

void buildTypeKey(string* out, TypeNode* node)
{
    if (!isCompoundNode(node)) {
        if (strEq(node->name, _S"struct") && saSize(node->params) >= 1)
            strNConcat(out, _S"struct_", node->params.a[0]->name);
        else
            strDup(out, node->name);
        return;
    }
    string key = 0;
    if (strEq(node->name, _S"sarray")) {
        string sub = 0;
        buildTypeKey(&sub, node->params.a[0]);
        strNConcat(&key, _S"sa_", sub);
        strDestroy(&sub);
    } else if (strEq(node->name, _S"hashtable")) {
        string sub0 = 0, sub1 = 0;
        buildTypeKey(&sub0, node->params.a[0]);
        buildTypeKey(&sub1, node->params.a[1]);
        strNConcat(&key, _S"ht_", sub0, _S"_", sub1);
        strDestroy(&sub0);
        strDestroy(&sub1);
    } else if (strEq(node->name, _S"structp")) {
        strNConcat(&key, _S"structp_", node->params.a[0]->name);
    }
    strDup(out, key);
    strDestroy(&key);
}

void buildTypeName(string* out, TypeNode* node)
{
    if (!isCompoundNode(node)) {
        if (strEq(node->name, _S"struct") && saSize(node->params) >= 1)
            strNConcat(out, _S"struct(", node->params.a[0]->name, _S")");
        else
            strDup(out, node->name);
        return;
    }
    string name = 0;
    if (strEq(node->name, _S"sarray")) {
        string sub = 0;
        buildTypeName(&sub, node->params.a[0]);
        strNConcat(&name, _S"sarray[", sub, _S"]");
        strDestroy(&sub);
    } else if (strEq(node->name, _S"hashtable")) {
        string sub0 = 0, sub1 = 0;
        buildTypeName(&sub0, node->params.a[0]);
        buildTypeName(&sub1, node->params.a[1]);
        strNConcat(&name, _S"hashtable[", sub0, _S",", sub1, _S"]");
        strDestroy(&sub0);
        strDestroy(&sub1);
    } else if (strEq(node->name, _S"structp")) {
        strNConcat(&name, _S"structp[", node->params.a[0]->name, _S"]");
    }
    strDup(out, name);
    strDestroy(&name);
}

void buildCompoundDescName(string* out, strref sname, TypeNode* node)
{
    string key = 0;
    buildTypeKey(&key, node);
    strNConcat(out, sname, _S"_", key);
    strDestroy(&key);
}

// Strips transparent object/weak/struct wrapper nodes, returning the underlying TypeNode.
static TypeNode* unwrapNode(TypeNode* node)
{
    if (!node)
        return NULL;
    while (saSize(node->params) >= 1 &&
           (strEq(node->name, _S"object") || strEq(node->name, _S"weak") ||
            strEq(node->name, _S"struct"))) {
        node = node->params.a[0];
    }
    return node;
}

// Computes the flat underscore-joined type name for a sarray TypeNode, stripping
// object/weak/struct wrappers.
// e.g. sarray[object[int32]] -> "int32", sarray[sarray[int32]] -> "sarray_int32"
void buildSArrayTypeName(string* out, TypeNode* node)
{
    if (!node)
        return;
    if (strEq(node->name, _S"object") || strEq(node->name, _S"weak") ||
        strEq(node->name, _S"struct")) {
        if (saSize(node->params) >= 1)
            buildSArrayTypeName(out, node->params.a[0]);
        return;
    }
    if (strEq(node->name, _S"sarray") && saSize(node->params) >= 1) {
        string sub = 0;
        buildSArrayTypeName(&sub, node->params.a[0]);
        strNConcat(out, _S"sarray_", sub);
        strDestroy(&sub);
        return;
    }
    strDup(out, node->name);
}

// Walks a sarray TypeNode tree and registers all nested sarray levels that require
// a saDeclareType forward declaration, adding them to the global artypes list.
// Processes inner-to-outer so declarations appear in dependency order.
void collectNestedSArrayDecls(TypeNode* node, bool included)
{
    if (!node || !strEq(node->name, _S"sarray") || saSize(node->params) < 1)
        return;
    TypeNode* inner = unwrapNode(node->params.a[0]);
    if (!inner || !strEq(inner->name, _S"sarray"))
        return;
    collectNestedSArrayDecls(inner, included);
    string tname = 0;
    buildSArrayTypeName(&tname, inner);
    if (!included && !htHasKey(knownartypes, string, tname))
        saPush(&artypes, object, inner);
    htInsert(&knownartypes, string, tname, bool, true);
    strDestroy(&tname);
}

static void getStructMemberType(string* out, Member* m)
{
    if (strEq(m->vartype, _S"int8") || strEq(m->vartype, _S"int16") || strEq(m->vartype, _S"int32") ||
        strEq(m->vartype, _S"int64") || strEq(m->vartype, _S"uint8") || strEq(m->vartype, _S"uint16") ||
        strEq(m->vartype, _S"uint32") || strEq(m->vartype, _S"uint64") ||
        strEq(m->vartype, _S"float32") || strEq(m->vartype, _S"float64") || strEq(m->vartype, _S"bool") ||
        strEq(m->vartype, _S"string") || strEq(m->vartype, _S"stvar") || strEq(m->vartype, _S"hashtable"))
        strDup(out, m->vartype);

    if (m->typenode) {
        if (strEq(m->typenode->name, _S"sarray") || strEq(m->typenode->name, _S"object") ||
            strEq(m->typenode->name, _S"structp"))
            strDup(out, m->typenode->name);

        if (strEq(m->typenode->name, _S"struct") && saSize(m->typenode->params) >= 1) {
            strNConcat(out, _S"struct(", m->typenode->params.a[0]->name, _S")");
        }
    }
}

static bool writeStructMemberDesc(StreamBuffer* bf, strref sname, Member* m)
{
    if ((m->flags & STRUCT_Ignore) == STRUCT_Ignore)
        return false;   // just don't emit this one at all

    string sttypename = 0;
    getStructMemberType(&sttypename, m);
    if (strEmpty(sttypename)) {
        fprintf(
            stderr,
            "WARNING: member '%s' of struct '%s' has unsupported type '%s', should be flagged ignore\n",
            strC(m->name),
            strC(sname),
            strC(m->typenode ? m->typenode->name : m->vartype));
        return false;
    }

    string ln = 0;

    sbufPWriteLine(bf, _S"    {");
    if (m->flags & STRUCT_NoSerialize) {
        // if not serializable, don't include the name in the binary
        sbufPWriteLine(bf, _S"        .name = &_emptyStringData,");
    } else {
        strNConcat(&ln, _S"        .name = _SR(", sname, _S"_m_", m->name, _S"_name),");
        sbufPWriteLine(bf, ln);
    }
    strNConcat(&ln, _S"        .offset = offsetof(", sname, _S", ", m->name, _S"),");
    sbufPWriteLine(bf, ln);

    sa_string flagsarr;
    saInit(&flagsarr, string, 1);
    if (m->flags & STRUCT_NoCopy)
        saPush(&flagsarr, string, _S"STRUCT_NoCopy");
    if (m->flags & STRUCT_NoSerialize)
        saPush(&flagsarr, string, _S"STRUCT_NoSerialize");
    if (m->flags & STRUCT_NoDestroy)
        saPush(&flagsarr, string, _S"STRUCT_NoDestroy");

    string flags = 0;
    strJoin(&flags, flagsarr, _S" | ");
    if (!strEmpty(flags)) {
        strNConcat(&ln, _S"        .flags = ", flags, _S",");
        sbufPWriteLine(bf, ln);
    }

    strDestroy(&flags);
    saDestroy(&flagsarr);

    if (m->typenode && isCompoundNode(m->typenode)) {
        string descname = 0;
        buildCompoundDescName(&descname, sname, m->typenode);
        strNConcat(&ln, _S"        .type = &_sti_", descname, _S",");
        strDestroy(&descname);
    } else if (m->typenode && strEq(m->typenode->name, _S"struct") &&
               saSize(m->typenode->params) >= 1) {
        strNConcat(&ln, _S"        .type = &_sti_", m->typenode->params.a[0]->name, _S",");
    } else {
        strNConcat(&ln, _S"        .type = stType(", sttypename, _S"),");
    }
    sbufPWriteLine(bf, ln);

    if (!strEmpty(m->postdecr)) {
        string numstr = 0;
        strSubStr(&numstr, m->postdecr, 1, -1);
        strNConcat(&ln, _S"        .arrsize = ", numstr, _S",");
        sbufPWriteLine(bf, ln);
        strDestroy(&numstr);
    }

    sbufPWriteLine(bf, _S"    },");

    strDestroy(&sttypename);
    strDestroy(&ln);

    return true;
}

static int prepareStructMemberTbl(StreamBuffer* bf, StructDef* str, bool* wroteany)
{
    string ln = 0;
    int nmembers = 0;

    // Count members that will produce descriptors (mirrors writeStructMemberDesc skip logic)
    for (int i = 0; i < saSize(str->members); i++) {
        Member* m = str->members.a[i];
        if ((m->flags & STRUCT_Ignore) == STRUCT_Ignore)
            continue;
        string sttypename = 0;
        getStructMemberType(&sttypename, m);
        if (!strEmpty(sttypename))
            nmembers++;
        strDestroy(&sttypename);
    }

    *wroteany = true;
    strNConcat(&ln, _S"STR_CONSTR(", str->name, _S"_name, \"", str->name, _S"\");");
    sbufPWriteLine(bf, ln);
    for (int i = 0; i < saSize(str->members); i++) {
        if (str->members.a[i]->flags & STRUCT_NoSerialize)
            continue;
        strNConcat(&ln,
                   _S"STR_CONSTR(",
                   str->name,
                   _S"_m_",
                   str->members.a[i]->name,
                   _S"_name, \"",
                   str->members.a[i]->name,
                   _S"\");");
        sbufPWriteLine(bf, ln);
    }
    sbufPWriteEOL(bf);
    strDestroy(&ln);

    return nmembers;
}

static void writeStructInfo(StreamBuffer* bf, StructDef* str, int nmembers, bool hasdefaults,
                            bool* wroteany)
{
    string ln = 0, temp = 0;

    *wroteany = true;
    strNConcat(&ln, _S"const StructInfo ", str->name, _S"_structinfo = {");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .name = _SR(", str->name, _S"_name),");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .structsize = sizeof(", str->name, _S"),");
    sbufPWriteLine(bf, ln);
    strFromInt32(&temp, nmembers, 10);
    if (hasdefaults) {
        strNConcat(&ln, _S"    .defaults = (const void*)&", str->name, _S"_structdefaults,");
        sbufPWriteLine(bf, ln);
    }
    strNConcat(&ln, _S"    .nmembers = ", temp, _S",");
    sbufPWriteLine(bf, ln);
    if (str->hasinit) {
        strNConcat(&ln, _S"    .init = (void(*)(void*))", str->name, _S"_init,");
        sbufPWriteLine(bf, ln);
    }
    if (str->hasdestroy) {
        strNConcat(&ln, _S"    .destroy = (void(*)(void*))", str->name, _S"_destroy,");
        sbufPWriteLine(bf, ln);
    }
    sbufPWriteLine(bf, _S"    .members = {");
    for (int i = 0; i < saSize(str->members); i++) {
        writeStructMemberDesc(bf, str->name, str->members.a[i]);
    }
    sbufPWriteLine(bf, _S"    },");
    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    strDestroy(&temp);
    strDestroy(&ln);
}

static void writeStructSTypeInfo(StreamBuffer* bf, StructDef* str, bool* wroteany)
{
    string ln = 0;

    *wroteany = true;
    strNConcat(&ln, _S"const STypeInfo _sti_", str->name, _S" = {");
    sbufPWriteLine(bf, ln);
    sbufPWriteLine(bf, _S"    .id    = STypeId_struct,");
    sbufPWriteLine(bf, _S"    .flags = stFlag(PassPtr) | stFlag(Object),");
    strNConcat(&ln, _S"    .size  = (uint16)sizeof(", str->name, _S"),");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .name  = _SR(", str->name, _S"_name),");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .ext   = &", str->name, _S"_structinfo,");
    sbufPWriteLine(bf, ln);
    sbufPWriteLine(bf, _S"    .ops   = { .dtor = stDtor_struct,");
    sbufPWriteLine(bf, _S"               .cmp  = stCmp_struct,");
    sbufPWriteLine(bf, _S"               .hash = stHash_struct,");
    sbufPWriteLine(bf, _S"               .copy = stCopy_struct },");
    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    strDestroy(&ln);
}

static void writeCompoundSTypeInfo(StreamBuffer* bf, strref sname, TypeNode* node,
                                   hashtable* seen, bool* wroteany)
{
    string key = 0;
    buildTypeKey(&key, node);

    if (htHasKey(*seen, string, key)) {
        strDestroy(&key);
        return;
    }

    // post-order: emit param types first
    for (int i = 0; i < saSize(node->params); i++) {
        TypeNode* param = node->params.a[i];
        if (isCompoundNode(param))
            writeCompoundSTypeInfo(bf, sname, param, seen, wroteany);
    }

    htInsert(seen, string, key, bool, true);
    *wroteany = true;

    string ln = 0;
    string descname = 0;
    buildCompoundDescName(&descname, sname, node);
    string typename = 0;
    buildTypeName(&typename, node);

    strNConcat(&ln, _S"STR_CONSTR(", descname, _S"_name, \"", typename, _S"\");");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"const STypeInfo _sti_", descname, _S" = {");
    sbufPWriteLine(bf, ln);

    if (strEq(node->name, _S"sarray")) {
        sbufPWriteLine(bf, _S"    .id     = STypeId_sarray,");
        sbufPWriteLine(bf, _S"    .flags  = stFlag(Object),");
        sbufPWriteLine(bf, _S"    .size   = (uint16)sizeof(sa_ref),");
        strNConcat(&ln, _S"    .name   = _SR(", descname, _S"_name),");
        sbufPWriteLine(bf, ln);
        TypeNode* p0 = node->params.a[0];
        if (isCompoundNode(p0)) {
            string p0name = 0;
            buildCompoundDescName(&p0name, sname, p0);
            strNConcat(&ln, _S"    .param  = { &_sti_", p0name, _S", NULL },");
            strDestroy(&p0name);
        } else if (strEq(p0->name, _S"struct") && saSize(p0->params) >= 1) {
            strNConcat(&ln, _S"    .param  = { &_sti_", p0->params.a[0]->name, _S", NULL },");
        } else {
            strNConcat(&ln, _S"    .param  = { &_sti_", p0->name, _S", NULL },");
        }
        sbufPWriteLine(bf, ln);
        sbufPWriteLine(bf, _S"    .ops    = { .cmp  = stCmp_sarray,");
        sbufPWriteLine(bf, _S"               .hash = stHash_sarray,");
        sbufPWriteLine(bf, _S"               .copy = stCopy_sarray },");
    } else if (strEq(node->name, _S"hashtable")) {
        sbufPWriteLine(bf, _S"    .id     = STypeId_hashtable,");
        sbufPWriteLine(bf, _S"    .flags  = stFlag(Object),");
        sbufPWriteLine(bf, _S"    .size   = (uint16)sizeof(hashtable),");
        strNConcat(&ln, _S"    .name   = _SR(", descname, _S"_name),");
        sbufPWriteLine(bf, ln);
        TypeNode* p0 = node->params.a[0];
        TypeNode* p1 = node->params.a[1];
        string p0ref = 0, p1ref = 0;
        if (isCompoundNode(p0)) {
            string p0name = 0;
            buildCompoundDescName(&p0name, sname, p0);
            strNConcat(&p0ref, _S"&_sti_", p0name);
            strDestroy(&p0name);
        } else if (strEq(p0->name, _S"struct") && saSize(p0->params) >= 1) {
            strNConcat(&p0ref, _S"&_sti_", p0->params.a[0]->name);
        } else {
            strNConcat(&p0ref, _S"&_sti_", p0->name);
        }
        if (isCompoundNode(p1)) {
            string p1name = 0;
            buildCompoundDescName(&p1name, sname, p1);
            strNConcat(&p1ref, _S"&_sti_", p1name);
            strDestroy(&p1name);
        } else if (strEq(p1->name, _S"struct") && saSize(p1->params) >= 1) {
            strNConcat(&p1ref, _S"&_sti_", p1->params.a[0]->name);
        } else {
            strNConcat(&p1ref, _S"&_sti_", p1->name);
        }
        strNConcat(&ln, _S"    .param  = { ", p0ref, _S", ", p1ref, _S" },");
        sbufPWriteLine(bf, ln);
        strDestroy(&p0ref);
        strDestroy(&p1ref);
        sbufPWriteLine(bf, _S"    .ops    = { .cmp  = stCmp_hashtable,");
        sbufPWriteLine(bf, _S"               .hash = stHash_hashtable,");
        sbufPWriteLine(bf, _S"               .copy = stCopy_hashtable },");
    } else if (strEq(node->name, _S"structp")) {
        strref pname = node->params.a[0]->name;
        sbufPWriteLine(bf, _S"    .id    = STypeId_structp,");
        sbufPWriteLine(bf, _S"    .flags = stFlag(Object),");
        sbufPWriteLine(bf, _S"    .size  = (uint16)sizeof(StructBase*),");
        strNConcat(&ln, _S"    .name  = _SR(", descname, _S"_name),");
        sbufPWriteLine(bf, ln);
        if (isStructName(pname)) {
            strNConcat(&ln, _S"    .param = { &_sti_", pname, _S", NULL },");
            sbufPWriteLine(bf, ln);
        } else if (isStructSetName(pname)) {
            strNConcat(&ln, _S"    .ext   = &", pname, _S"_structset,");
            sbufPWriteLine(bf, ln);
        } else {
            fprintf(stderr, "WARNING: structp[%s]: '%s' is not a known struct or structset\n",
                    strC(pname), strC(pname));
            strNConcat(&ln, _S"#error \"structp[", pname, _S"]: unknown type '", pname, _S"'\"");
            sbufPWriteLine(bf, ln);
        }
        sbufPWriteLine(bf, _S"    .ops   = { .dtor = stDtor_structp, .cmp  = stCmp_structp,");
        sbufPWriteLine(bf, _S"               .hash = stHash_structp, .copy = stCopy_structp },");
    }

    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    strDestroy(&key);
    strDestroy(&descname);
    strDestroy(&typename);
    strDestroy(&ln);
}

static int strCmpCB(const void* a, const void* b)
{
    return strCmp(*(const string*)a, *(const string*)b);
}

static void writeStructSetDef(StreamBuffer* bf, StructSetDef* ss, bool* wroteany)
{
    if (saSize(ss->members) == 0)
        return;

    *wroteany = true;
    string ln = 0;

    // sort members alphabetically for binary search
    sa_string sorted;
    saInit(&sorted, string, saSize(ss->members));
    for (int i = 0; i < saSize(ss->members); i++)
        saPush(&sorted, string, ss->members.a[i]);
    qsort(sorted.a, saSize(sorted), sizeof(string), strCmpCB);

    strNConcat(&ln, _S"StructSet ", ss->name, _S"_structset = {");
    sbufPWriteLine(bf, ln);
    string nstr = 0;
    strFromInt32(&nstr, saSize(sorted), 10);
    strNConcat(&ln, _S"    .nentries = ", nstr, _S",");
    strDestroy(&nstr);
    sbufPWriteLine(bf, ln);
    sbufPWriteLine(bf, _S"    .entries  = {");
    for (int i = 0; i < saSize(sorted); i++) {
        strNConcat(&ln, _S"        &", sorted.a[i], _S"_structinfo,");
        sbufPWriteLine(bf, ln);
    }
    sbufPWriteLine(bf, _S"    },");
    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    saDestroy(&sorted);
    strDestroy(&ln);
}

static void writeCompoundSTypeInfos(StreamBuffer* bf, StructDef* str, bool* wroteany)
{
    hashtable seen;
    htInit(&seen, string, bool, 8);

    for (int i = 0; i < saSize(str->members); i++) {
        Member* m = str->members.a[i];
        if ((m->flags & STRUCT_Ignore) == STRUCT_Ignore)
            continue;
        if (isCompoundNode(m->typenode))
            writeCompoundSTypeInfo(bf, str->name, m->typenode, &seen, wroteany);
    }

    htDestroy(&seen);
}

static bool writeStructDefaults(StreamBuffer* bf, StructDef* str, bool* wroteany)
{
    string ln        = 0;
    bool hasdefaults = false;

    for (int i = 0; i < saSize(str->members); i++) {
        if (getAnnotation(NULL, str->members.a[i]->annotations, _S"default"))
            hasdefaults = true;
    }

    if (!hasdefaults)
        return false;

    *wroteany = true;
    strNConcat(&ln, _S"const ", str->name, _S" ", str->name, _S"_structdefaults = {");
    sbufPWriteLine(bf, ln);
    strNConcat(&ln, _S"    .structinfo = &", str->name, _S"_structinfo,");
    sbufPWriteLine(bf, ln);
    for (int i = 0; i < saSize(str->members); i++) {
        Member* m = str->members.a[i];
        sa_string an;
        getAnnotation(&an, m->annotations, _S"default");
        if (saSize(an) >= 2) {
            strNConcat(&ln, _S"    .", m->name, _S" = ", an.a[1], _S",");
            sbufPWriteLine(bf, ln);
        }
    }

    sbufPWriteLine(bf, _S"};");
    sbufPWriteEOL(bf);

    strDestroy(&ln);

    return true;
}

static bool fillBuf(StreamBuffer* obf, sa_string* linebuf)
{
    string ln = 0;
    while (obf && saSize(*linebuf) < 5 && lparseLine(obf, &ln)) {
        saPushC(linebuf, string, &ln);
    }
    return saSize(*linebuf) > 0;
}

static void deleteLines(sa_string* linebuf, int nlines)
{
    while (nlines > 0) {
        saRemove(linebuf, 0);
        nlines--;
    }
}

bool writeImpl(string fname, string srcpath, string binpath, bool mixinimpl)
{
    string hname = 0;
    if (!strEmpty(srcpath))
        relSrcPath(&hname, fname, srcpath);   // in cmake mode this needs to be relative because
                                              // it lands in binpath
    else
        pathFilename(&hname, fname);
    pathSetExt(&hname, hname, _S"h");
    string cname = 0;
    pathSetExt(&cname, fname, !mixinimpl ? _S"c" : _S"impl.h");
    string newcname = 0;
    strConcat(&newcname, cname, _S"new");
    string incname = 0;
    pathSetExt(&incname, fname, _S"auto.inc");
    binPath(&incname, incname, srcpath, binpath);

    FSFile* newf = fsOpen(newcname, FS_Overwrite);
    if (!newf) {
        fprintf(stderr, "Failed to open %s for writing", lazyPlatformPath(newcname));
        return false;
    }
    StreamBuffer* nbf = sbufCreate(1024);
    if (!sbufFSFileCRegisterPush(nbf, newf, true))
        return false;
    if (!sbufPRegisterPush(nbf, NULL, 0))
        return false;

    FSFile* incf      = 0;
    StreamBuffer* ibf = 0;
    if (!mixinimpl) {
        incf = fsOpen(incname, FS_Overwrite);
        if (!incf) {
            fprintf(stderr, "Failed to open %s for writing", lazyPlatformPath(incname));
            sbufPFinish(nbf);
            sbufRelease(&nbf);
            return false;
        }
        ibf = sbufCreate(1024);
        if (!sbufFSFileCRegisterPush(ibf, incf, true))
            return false;
        if (!sbufPRegisterPush(ibf, NULL, 0))
            return false;
    }

    FSFile* oldf      = fsOpen(cname, FS_Read);
    StreamBuffer* obf = NULL;
    if (oldf) {
        obf = sbufCreate(1024);
        if (!sbufFSFilePRegisterPull(obf, oldf, true))
            return false;
        if (!lparseRegisterPull(obf, 0))
            return false;
    }

    string ln      = 0;
    string olddecl = 0;
    sbufPWriteLine(nbf, autogenBegin);
    sbufPWriteLine(nbf, clangOff);
    sbufPWriteLine(nbf, autogenNotice);
    sbufPWriteLine(nbf, _S"#include <cx/obj.h>");
    sbufPWriteLine(nbf, _S"#include <cx/debug/assert.h>");
    sbufPWriteLine(nbf, _S"#include <cx/obj/objstdif.h>");
    sbufPWriteLine(nbf, _S"#include <cx/container.h>");
    sbufPWriteLine(nbf, _S"#include <cx/string.h>");
    if (!mixinimpl) {
        strNConcat(&ln, _S"#include \"", hname, _S"\"");
        sbufPWriteLine(nbf, ln);
        for (int i = 0; i < saSize(implincludes); i++) {
            strNConcat(&ln, _S"#include ", implincludes.a[i]);
            sbufPWriteLine(nbf, ln);
        }
    }
    sbufPWriteLine(nbf, clangOn);
    sbufPWriteLine(nbf, autogenEnd);

    sa_string seen;
    saInit(&seen, string, 16, SA_Sorted);
    saInit(&parentmacros, string, 16, SA_Sorted);
    hashtable implidx;
    htInit(&implidx, string, opaque(MethodPair), 16);
    hashtable structlcidx;
    htInit(&structlcidx, string, none, 16);
    int err;
    PCRE2_SIZE eoffset;
    pcre2_code* reParentProto =
        pcre2_compile((PCRE2_SPTR) "^extern [A-Za-z0-9_]+(?:\\s+\\**|\\**\\s+)([A-Za-z0-9_]+)"
                                   "\\([^;]*\\);\\s+// parent$",
                      PCRE2_ZERO_TERMINATED,
                      0,
                      &err,
                      &eoffset,
                      NULL);
    pcre2_code* reParentMacro =
        pcre2_compile((PCRE2_SPTR) "^#(?:define|undef) parent_[A-Za-z0-9_]+"
                                   "(?:\\([^;]*\\)(?:(?: \\\\\\s+)?| )[A-Za-z0-9_]+\\([^;]*\\))?$",
                      PCRE2_ZERO_TERMINATED,
                      0,
                      &err,
                      &eoffset,
                      NULL);
    pcre2_code* reProto =
        pcre2_compile((PCRE2_SPTR) "^(?:_.*_(?:\\(.*\\))?\\s+)*(?:_objfactory(?:_[a-z]+)?\\s+)?"
                                   "(?:_objinit(?:_[a-z]+)?\\s+)?(?:_meta_inline\\s+)?[A-Za-z0-9_]+"
                                   "(?:\\s+\\**|\\**\\s+)([A-Za-z0-9_]+)\\([^;]*\\)(;)?$",
                      PCRE2_ZERO_TERMINATED,
                      0,
                      &err,
                      &eoffset,
                      NULL);
    pcre2_match_data* match = pcre2_match_data_create_from_pattern(reProto, NULL);

    for (int i = 0; i < saSize(classes); i++) {
        indexMethods(classes.a[i], &implidx);
    }

    for (int i = 0; i < saSize(structs); i++) {
        StructDef* str = structs.a[i];
        if (str->hasinit) {
            strNConcat(&ln, str->name, _S"_init");
            htInsert(&structlcidx, string, ln, none, NULL);
        }
        if (str->hasdestroy) {
            strNConcat(&ln, str->name, _S"_destroy");
            htInsert(&structlcidx, string, ln, none, NULL);
        }
    }

    bool inautogen   = false;
    bool wasempty    = true;
    Class* ininit    = 0;
    Class* indestroy = 0;
    sa_string linebuf;
    saInit(&linebuf, string, 5);
    while (obf && fillBuf(obf, &linebuf)) {
        strDup(&ln, linebuf.a[0]);
        if (strEq(ln, autogenBegin) || strEq(ln, autogenBeginShort) ||
            strEq(ln, autogenBeginShortIndent))
            inautogen = true;
        if (!inautogen) {
            wasempty     = false;
            int nmatches = 0;

            for (int nline = 0; nline < saSize(linebuf); nline++) {
                if (nline > 0) {
                    // don't continue adding lines if we have one of these
                    if (strEmpty(linebuf.a[nline]) || strFind(linebuf.a[nline], 0, _S"{") != -1)
                        break;

                    // don't delete lines yet, only peek beyond first
                    strAppendLine(&ln, linebuf.a[nline]);
                }
                // ignore parent prototypes and macros
                nmatches =
                    pcre2_match(reParentProto, (PCRE2_SPTR)strC(ln), strLen(ln), 0, 0, match, NULL);
                if (nmatches >= 0) {
                    strAppendLine(&olddecl, ln);
                    deleteLines(&linebuf, nline);
                    goto nextloop;
                }
                nmatches =
                    pcre2_match(reParentMacro, (PCRE2_SPTR)strC(ln), strLen(ln), 0, 0, match, NULL);
                if (nmatches >= 0) {
                    strAppendLine(&olddecl, ln);
                    deleteLines(&linebuf, nline);
                    goto nextloop;
                }

                nmatches =
                    pcre2_match(reProto, (PCRE2_SPTR)strC(ln), strLen(ln), 0, 0, match, NULL);
                if (nmatches >= 0) {
                    // actually remove lines beyond the first
                    strAppendLine(&olddecl, ln);
                    deleteLines(&linebuf, nline);
                    break;
                }

                // don't continue adding lines if we have one of these
                if (strEmpty(linebuf.a[nline]) || strFind(linebuf.a[nline], 0, _S";") != -1)
                    break;
            }
            if (nmatches >= 0) {
                PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
                string newdecl      = 0;
                string funcname     = 0;
                strSubStr(&funcname, ln, (int32)ovector[2], (int32)ovector[3]);
                MethodPair mp;

                if (htFind(implidx, string, funcname, opaque, &mp)) {
                    if (nmatches == 2) {
                        if (mp.m->isinit)
                            ininit = mp.c;
                        if (mp.m->isdestroy)
                            indestroy = mp.c;
                    }
                    saPush(&seen, string, funcname, SA_Unique);

                    // write the proto to a string buffer and make sure we aren't rewriting it
                    // if only whitespace changed
                    StreamBuffer* ssbf = sbufStrCreatePush(&newdecl, 256);
                    if (!ssbf)
                        return false;
                    writeMethodProto(ssbf, mp.c, mp.m, nmatches == 3, mixinimpl, false);
                    sbufPFinish(ssbf);
                    sbufRelease(&ssbf);

                    if (strEqNoWS(olddecl, newdecl)) {
                        sbufPWriteLine(nbf, olddecl);
                    } else {
                        sbufPWriteStr(nbf, newdecl);   // EOL already added by writeMethodProto
                    }
                } else if (htHasKey(structlcidx, string, funcname)) {
                    // just record it as seen, we only need to create these if they don't exist
                    saPush(&seen, string, funcname, SA_Unique);
                    sbufPWriteLine(nbf, ln);
                } else {
                    sbufPWriteLine(nbf, ln);
                }
                strDestroy(&newdecl);
                strDestroy(&funcname);
            } else {
                // revert back to a single line
                strDup(&ln, linebuf.a[0]);
                strClear(&olddecl);
                if (strEq(ln, _S"}")) {
                    if (ininit) {
                        writeAutoInit(nbf, ininit);
                        ininit = 0;
                    } else if (indestroy) {
                        writeAutoDtors(nbf, indestroy);
                        indestroy = 0;
                    }
                }

                sbufPWriteLine(nbf, ln);
            }
        }
        if (strEq(ln, autogenEnd) || strEq(ln, autogenEndShort) ||
            strEq(ln, autogenEndShortIndent)) {
            inautogen = false;
        }
nextloop:
        deleteLines(&linebuf, 1);
    }

    if (wasempty) {
        // quick and dirty way to format correctly when the file started out empty
        sbufPWriteEOL(nbf);
    }

    for (int i = 0; i < saSize(classes); i++) {
        if (!classes.a[i]->included)
            writeMethods(nbf, classes.a[i], &seen, mixinimpl);
    }

    for (int i = 0; i < saSize(structs); i++) {
        if (!structs.a[i]->included)
            writeStructLifecycleFuncs(nbf, structs.a[i], &seen);
    }

    if (!mixinimpl) {
        bool wroteany = false;

        sbufPWriteLine(ibf, autogenBegin);
        sbufPWriteLine(ibf, clangOff);
        sbufPWriteLine(ibf, autogenNotice);
        sbufPWriteLine(ibf, _S"#include <cx/string.h>");

        for (int i = 0; i < saSize(classes); i++) {
            if (!classes.a[i]->included)
                writeMixinStubs(ibf, classes.a[i], &wroteany);
        }
        for (int i = 0; i < saSize(ifaces); i++) {
            if (!ifaces.a[i]->included)
                writeIfaceTmpl(ibf, ifaces.a[i], &wroteany);
        }
        for (int i = 0; i < saSize(classes); i++) {
            if (!classes.a[i]->included && !classes.a[i]->mixin)
                writeClassImpl(ibf, classes.a[i], &wroteany);
        }
        for (int i = 0; i < saSize(structs); i++) {
            if (!structs.a[i]->included) {
                bool hasdefaults = writeStructDefaults(ibf, structs.a[i], &wroteany);
                writeCompoundSTypeInfos(ibf, structs.a[i], &wroteany);
                int nmembers = prepareStructMemberTbl(ibf, structs.a[i], &wroteany);
                writeStructInfo(ibf, structs.a[i], nmembers, hasdefaults, &wroteany);
                writeStructSTypeInfo(ibf, structs.a[i], &wroteany);
            }
        }
        for (int i = 0; i < saSize(structsets); i++) {
            if (!structsets.a[i]->included)
                writeStructSetDef(ibf, structsets.a[i], &wroteany);
        }
        sbufPWriteLine(ibf, autogenEnd);

        if (wroteany) {
            sbufPWriteLine(nbf, autogenBeginShort);
            sbufPWriteLine(nbf, clangOff);
            for (int i = 0; i < saSize(classes); i++) {
                if (!classes.a[i]->included)
                    writeExternMethods(nbf, classes.a[i]);
            }
            if (!strEmpty(binpath))
                relSrcPath(&incname, incname, binpath);
            else
                pathFilename(&incname, incname);
            strNConcat(&ln, _S"#include \"", incname, _S"\"");
            sbufPWriteLine(nbf, ln);
            sbufPWriteLine(nbf, clangOn);
            sbufPWriteLine(nbf, autogenEndShort);
        } else {
            sbufPFinish(ibf);
            sbufRelease(&ibf);
            ibf = NULL;
            fsDelete(incname);
        }
    } else {
        sbufPWriteLine(nbf, autogenBeginShort);
        sbufPWriteLine(nbf, clangOff);
        for (int i = 0; i < saSize(classes); i++) {
            if (!classes.a[i]->included)
                writeMixinProtos(nbf, classes.a[i]);
        }
        sbufPWriteLine(nbf, clangOn);
        sbufPWriteLine(nbf, autogenEndShort);
    }

    if (obf) {
        sbufCFinish(obf);
        sbufRelease(&obf);
    }
    if (ibf) {
        sbufPFinish(ibf);
        sbufRelease(&ibf);
    }
    sbufPFinish(nbf);
    sbufRelease(&nbf);

    fsDelete(cname);
    fsRename(newcname, cname);

    pcre2_match_data_free(match);
    pcre2_code_free(reProto);
    saDestroy(&linebuf);
    strDestroy(&ln);
    strDestroy(&olddecl);
    strDestroy(&incname);
    strDestroy(&hname);
    strDestroy(&cname);
    strDestroy(&newcname);
    htDestroy(&implidx);
    saDestroy(&seen);
    saDestroy(&parentmacros);
    return true;
}

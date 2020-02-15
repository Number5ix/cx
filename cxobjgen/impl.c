#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/fs/file.h>
#include <cx/string.h>
#include <pcre2.h>

static const string autogenBegin =  _S"// ==================== Auto-generated section begins ====================";
static const string autogenNotice = _S"// Do not modify the contents of this section; any changes will be lost!";
static const string autogenEnd =    _S"// ==================== Auto-generated section ends ======================";

static string **parentmacros;

static void writeAutoInit(BufFile *bf, Class *cls);
static void writeAutoDtors(BufFile *bf, Class *cls);

static void writeMethodProto(BufFile *bf, Class *cls, Method *m, bool protoonly, bool mixinimpl, bool forparent)
{
    string mname = 0, ln = 0, tmp = 0;
    methodImplName(&mname, cls, m->name);

    if (!protoonly && !forparent && !mixinimpl &&
        !m->isfactory && !m->isinit && !m->isdestroy &&
        cls->parent) {
        Class *pclass = cls->parent;
        Method *pm = 0;

        while (pclass) {
            int32 idx = saFind(&pclass->methods, object, m);
            if (idx != -1) {
                pm = pclass->methods[idx];
                break;
            }
            pclass = pclass->parent;
        }

        if (pm) {
            writeMethodProto(bf, pclass, pm, true, false, true);

            string pmname = 0;
            methodImplName(&pmname, pclass, pm->name);

            if (saFind(&parentmacros, string, m->name) != -1) {
                strNConcat(&ln, _S"#undef ", _S"parent_", m->name);
                bfWriteLine(bf, ln);
            }

            strNConcat(&ln, _S"#define ", _S"parent_", m->name, _S"(");
            saPush(&parentmacros, string, m->name, Unique);
            for (int j = 0; j < saSize(&pm->params); j++) {
                if (j > 0)
                    strNConcat(&ln, ln, _S", ", pm->params[j]->name);
                else
                    strAppend(&ln, pm->params[j]->name);
            }
            strNConcat(&ln, ln, _S") ", pmname, _S"((", pclass->name, _S"*)(self)");
            for (int j = 0; j < saSize(&pm->params); j++) {
                strNConcat(&ln, ln, _S", ", pm->params[j]->name);
            }
            strAppend(&ln, _S")");

            strDestroy(&pmname);
            bfWriteLine(bf, ln);
            strClear(&ln);
        }
    }

    if (!m->standalone)
        strNConcat(&ln, m->returntype, _S" ", m->predecr, mname, _S"(", cls->name, _S" *self");
    else
        strNConcat(&ln, m->returntype, _S" ", m->predecr, mname, _S"(");

    if (forparent)
        strPrepend(_S"extern ", &ln);
    if (mixinimpl)
        strPrepend(_S"_meta_inline ", &ln);

    for (int j = 0; j < saSize(&m->params); j++) {
        Param *p = m->params[j];
        string ptype = p->type;
        string ppre = p->predecr;

        if (strEq(ptype, _S"object") && strEmpty(ppre)) {
            ptype = cls->name;
            ppre = _S"*";
        }

        if (!m->standalone || j > 0)
            strNConcat(&tmp, _S", ", ptype, _S" ", ppre, p->name, p->postdecr);
        else
            strNConcat(&tmp, ptype, _S" ", ppre, p->name, p->postdecr);
        strAppend(&ln, tmp);
    }
    strAppend(&ln, _S")");
    if (protoonly)
        strAppend(&ln, _S";");

    if (forparent)
        strAppend(&ln, _S" // parent");

    bfWriteLine(bf, ln);
    strDestroy(&tmp);
    strDestroy(&ln);
}

static void writeMethods(BufFile *bf, Class *cls, string **seen, bool mixinimpl)
{
    string ln = 0, mname = 0;
    for (int i = 0; i < saSize(&cls->methods); i++) {
        Method *m = cls->methods[i];
        if (m->mixin != mixinimpl)
            continue;
        methodImplName(&mname, cls, m->name);
        if (saFind(seen, string, mname) == -1) {
            writeMethodProto(bf, cls, m, false, mixinimpl, false);
            bfWriteLine(bf, _S"{");
            if (m->isinit) {
                writeAutoInit(bf, cls);
            } else if (m->isdestroy) {
                writeAutoDtors(bf, cls);
            } else if (m->isfactory) {
                strNConcat(&ln, _S"    ", cls->name, _S" *self;");
                bfWriteLine(bf, ln);
                strNConcat(&ln, _S"    self = objInstCreate(", cls->name, _S");");
                bfWriteLine(bf, ln);
                bfWriteLine(bf, NULL);
                bfWriteLine(bf, _S"    // Insert any pre-initialization object construction here");
                bfWriteLine(bf, NULL);
                bfWriteLine(bf, _S"    if (!objInstInit(self)) {");
                bfWriteLine(bf, _S"        objRelease(self);");
                bfWriteLine(bf, _S"        return NULL;");
                bfWriteLine(bf, _S"    }");
                bfWriteLine(bf, NULL);
                bfWriteLine(bf, _S"    // Insert any post-initialization object construction here");
                bfWriteLine(bf, NULL);
                bfWriteLine(bf, _S"    return self;");
            } else if (strEq(m->name, _S"cmp")) {
                bfWriteLine(bf, _S"    // Uncomment unless this function can compare different object classes");
                bfWriteLine(bf, _S"    // devAssert(objClsInfo(self) == objClsInfo(other));");
                bfWriteLine(bf, NULL);
                bfWriteLine(bf, _S"    return objDefaultCmp(self, other, flags);");
            } else if (strEq(m->name, _S"hash")) {
                bfWriteLine(bf, _S"    return objDefaultHash(self, flags);");
            } else {
                bfWriteLine(bf, _S"    #error Replace this line with your implementation");
            }
            bfWriteLine(bf, _S"}");
            bfWriteLine(bf, NULL);
        }
    }
    strDestroy(&mname);
    strDestroy(&ln);
}

static void writeMixinStubs(BufFile *bf, Class *cls)
{
    string ln = 0, mname = 0, vname = 0;
    for (int i = 0; i < saSize(&cls->methods); i++) {
        Method *m = cls->methods[i];
        if (!m->mixin || cls->mixin)
            continue;
        writeMethodProto(bf, cls, m, false, false, false);
        bfWriteLine(bf, _S"{");
        strDup(&ln, _S"    ");
        if (!strEq(m->returntype, _S"void"))
            strAppend(&ln, _S"return ");

        methodImplName(&mname, m->srcclass, m->name);
        mixinMemberName(&vname, m->mixinsrc);
        strNConcat(&ln, ln, mname, _S"((", m->srcclass->name, _S"*)&self->", vname);

        for (int j = 0; j < saSize(&m->params); j++) {
            Param *p = m->params[j];
            strNConcat(&ln, ln, _S", ", p->name);
        }

        strNConcat(&ln, ln, _S");");
        bfWriteLine(bf, ln);
        bfWriteLine(bf, _S"}");
        bfWriteLine(bf, NULL);
    }
    strDestroy(&vname);
    strDestroy(&mname);
    strDestroy(&ln);
}


typedef struct MethodPair {
    Class *c;
    Method *m;
} MethodPair;

static void indexMethods(Class *cls, hashtable *htbl)
{
    string mname = 0;
    for (int i = 0; i < saSize(&cls->methods); i++) {
        Method *m = cls->methods[i];
        methodImplName(&mname, cls, m->name);
        htInsert(htbl, string, mname, opaque, ((MethodPair){ .c = cls, .m = m }));
    }
    strDestroy(&mname);
}

static void writeAutoInit(BufFile *bf, Class *cls)
{
    string ln = 0, tmp = 0, flags = 0;

    bfWriteLine(bf, autogenBegin);

    for (int i = 0; i < saSize(&cls->members); i++) {
        Member *m = cls->members[i];
        if (!m->init)
            continue;

        strClear(&ln);
        if (m->mixinsrc) {
            for (Class *mixin = m->mixinsrc; mixin; mixin = mixin->parent) {
                if (mixin->hasinit) {
                    mixinMemberName(&tmp, m->mixinsrc);
                    strNConcat(&ln, _S"    if (!", mixin->name, _S"_init((", mixin->name,
                               _S"*)&self->", tmp, _S"))");
                    bfWriteLine(bf, ln);
                    bfWriteLine(bf, _S"        return false;");
                }
            }
            continue;
        } else if (m->initstr) {
            strNConcat(&ln, _S"    self->", m->name, _S" = ", m->initstr, _S";");
        } else if (saSize(&m->fulltype) > 1) {
            if (strEq(m->fulltype[0], _S"sarray")) {
                string *flagarr = saCreate(string, 1);
                string size = _S"1";
                string *an;

                if (getAnnotation(&m->annotations, _S"ref"))
                    saPush(&flagarr, string, _S"Ref");
                if (getAnnotation(&m->annotations, _S"sorted"))
                    saPush(&flagarr, string, _S"Sorted");
                an = getAnnotation(&m->annotations, _S"grow");
                if (saSize(&an) == 2) {
                    strNConcat(&tmp, _S"Grow(", an[1], _S")");
                    saPush(&flagarr, string, tmp);
                }
                an = getAnnotation(&m->annotations, _S"size");
                if (saSize(&an) == 2)
                    size = an[1];

                strClear(&flags);
                if (saSize(&flagarr) != 0) {
                    saInsert(&flagarr, 0, string, _S"");
                    strJoin(&flags, flagarr, _S", ");
                }
                strNConcat(&ln, _S"    self->", m->name, _S" = saCreate(", m->fulltype[1],
                           _S", ", size, flags, _S");");
                saDestroy(&flagarr);
            }
            if (strEq(m->fulltype[0], _S"hashtable") && saSize(&m->fulltype) == 3) {
                string *flagarr = saCreate(string, 1);
                string size = _S"16";
                string *an;

                if (getAnnotation(&m->annotations, _S"ref"))
                    saPush(&flagarr, string, _S"Ref");
                if (getAnnotation(&m->annotations, _S"refkeys"))
                    saPush(&flagarr, string, _S"RefKeys");
                if (getAnnotation(&m->annotations, _S"caseinsensitive"))
                    saPush(&flagarr, string, _S"CaseInsensitive");
                an = getAnnotation(&m->annotations, _S"grow");
                if (saSize(&an) == 2) {
                    strNConcat(&tmp, _S"Grow(", an[1], _S")");
                    saPush(&flagarr, string, tmp);
                }
                an = getAnnotation(&m->annotations, _S"size");
                if (saSize(&an) == 2)
                    size = an[1];

                strClear(&flags);
                if (saSize(&flagarr) != 0) {
                    saInsert(&flagarr, 0, string, _S"");
                    strJoin(&flags, flagarr, _S", ");
                }
                strNConcat(&ln, _S"    self->", m->name, _S" = htCreate(", m->fulltype[1], _S", ",
                           m->fulltype[2], _S", ", size, flags, _S");");
                saDestroy(&flagarr);
            }
        }

        if (!strEmpty(ln))
            bfWriteLine(bf, ln);
    }

    bfWriteLine(bf, _S"    return true;");
    bfWriteLine(bf, autogenEnd);
    strDestroy(&flags);
    strDestroy(&tmp);
    strDestroy(&ln);
}

static void writeAutoDtors(BufFile *bf, Class *cls)
{
    if (!cls->hasautodtors)
        return;

    bfWriteLine(bf, autogenBegin);

    for (int i = 0; i < saSize(&cls->members); i++) {
        Member *m = cls->members[i];
        if (!m->destroy)
            continue;

        string mdtor = 0;

        if (m->mixinsrc) {
            for (Class *mixin = m->mixinsrc; mixin; mixin = mixin->parent) {
                if (mixin->hasdestroy) {
                    string tmp = 0;
                    mixinMemberName(&tmp, m->mixinsrc);
                    strNConcat(&mdtor, _S"    ", mixin->name, _S"_destroy((", mixin->name,
                               _S"*)&self->", tmp, _S");");
                    bfWriteLine(bf, mdtor);
                    strDestroy(&tmp);
                }
            }
            continue;
        } else if (saSize(&m->fulltype) > 1) {
            if (strEq(m->fulltype[0], _S"sarray"))
                strNConcat(&mdtor, _S"    saDestroy(&self->", m->name, _S");");
            else if (strEq(m->fulltype[0], _S"hashtable"))
                strNConcat(&mdtor, _S"    htDestroy(&self->", m->name, _S");");
            else if (strEq(m->fulltype[0], _S"object"))
                strNConcat(&mdtor, _S"    objRelease(self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"string")) {
            strNConcat(&mdtor, _S"    strDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"hashtable")) {
            strNConcat(&mdtor, _S"    htDestroy(&self->", m->name, _S");");
        } else if (strEq(m->vartype, _S"stvar")) {
            strNConcat(&mdtor, _S"    _stDestroy(self->", m->name, _S".type, NULL, &self->", m->name, _S".data, 0);");
        }
        if (!strEmpty(mdtor))
            bfWriteLine(bf, mdtor);
        strDestroy(&mdtor);
    }

    bfWriteLine(bf, autogenEnd);
}

static void writeMixinProtos(BufFile *bf, Class *cls)
{
    for (int i = 0; i < saSize(&cls->methods); i++) {
        Method *m = cls->methods[i];
        if (m->isinit || m->isdestroy) {
            writeMethodProto(bf, cls, m, true, false, false);
            continue;
        }
    }
}

static void writeIfaceTmpl(BufFile *bf, Interface *iface)
{
    string ln = 0;

    strNConcat(&ln, iface->name, _S" ", iface->name, _S"_tmpl = {");
    bfWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._size = sizeof(", iface->name, _S"),");
    bfWriteLine(bf, ln);
    if (iface->parent) {
        strNConcat(&ln, _S"    ._parent = (ObjIface*)&", iface->parent->name, _S"_tmpl,");
        bfWriteLine(bf, ln);
    }

    bfWriteLine(bf, _S"};");
    bfWriteLine(bf, NULL);
    strDestroy(&ln);
}

static Method *findIfaceMethod(Interface *iface, string name)
{
    for (int i = 0; i < saSize(&iface->allmethods); i++) {
        if (strEq(iface->allmethods[i]->name, name))
            return iface->allmethods[i];
    }
    return NULL;
}

static void writeClassIfaceTbl(BufFile *bf, Class *cls, Interface *iface)
{
    string ln = 0, implname = 0;

    strNConcat(&ln, _S"static ", iface->name, _S" _impl_", cls->name, _S"_", iface->name, _S" = {");
    bfWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._size = sizeof(", iface->name, _S"),");
    bfWriteLine(bf, ln);
    strNConcat(&ln, _S"    ._implements = (ObjIface*)&", iface->name, _S"_tmpl,");
    bfWriteLine(bf, ln);

    for (int i = 0; i < saSize(&cls->methods); i++) {
        if (cls->methods[i]->internal)
            continue;

        // see if this class method is part of the interface
        Method *m = findIfaceMethod(iface, cls->methods[i]->name);
        if (!m)
            continue;

        methodImplName(&implname, cls, m->name);
        strNConcat(&ln, _S"    .", m->name, _S" = (", m->returntype, _S" ", m->predecr, _S"(*)(void*");
        for (int j = 0; j < saSize(&m->params); j++) {
            Param *p = m->params[j];
            string ptype = p->type;
            string ppre = p->predecr;

            if (strEq(ptype, _S"object") && strEmpty(ppre)) {
                ptype = _S"void";
                ppre = _S"*";
            }

            strNConcat(&ln, ln, _S", ", ptype, ppre, p->postdecr);
        }
        strNConcat(&ln, ln, _S"))", implname, _S",");
        bfWriteLine(bf, ln);
    }

    bfWriteLine(bf, _S"};");
    bfWriteLine(bf, NULL);
    strDestroy(&implname);
    strDestroy(&ln);
}

static void writeClassIfaceList(BufFile *bf, Class *cls)
{
    string ln = 0;

    strNConcat(&ln, _S"static ObjIface *_ifimpl_", cls->name, _S"[] = {");
    bfWriteLine(bf, ln);

    for (int i = 0; i < saSize(&cls->implements); i++) {
        strNConcat(&ln, _S"    (ObjIface*)&_impl_", cls->name, _S"_", cls->implements[i]->name, _S",");
        bfWriteLine(bf, ln);
    }

    bfWriteLine(bf, _S"    NULL");
    bfWriteLine(bf, _S"};");
    bfWriteLine(bf, NULL);
    strDestroy(&ln);
}

static void writeClassImpl(BufFile *bf, Class *cls)
{
    string ln = 0;

    for (int i = 0; i < saSize(&cls->implements); i++) {
        writeClassIfaceTbl(bf, cls, cls->implements[i]);
    }
    writeClassIfaceList(bf, cls);

    strNConcat(&ln, _S"ObjClassInfo ", cls->name, _S"_clsinfo = {");
    bfWriteLine(bf, ln);
    strNConcat(&ln, _S"    .instsize = sizeof(", cls->name, _S"),");
    bfWriteLine(bf, ln);
    if (cls->classif) {
        strNConcat(&ln, _S"    .classif = (ObjIface*)&", cls->classif->name, _S"_tmpl,");
        bfWriteLine(bf, ln);
    }
    if (cls->parent) {
        strNConcat(&ln, _S"    .parent = &", cls->parent->name, _S"_clsinfo,");
        bfWriteLine(bf, ln);
    }
    if (cls->hasinit) {
        strNConcat(&ln, _S"    .init = (bool(*)(void*))", cls->name, _S"_init,");
        bfWriteLine(bf, ln);
    }
    if (cls->hasdestroy) {
        strNConcat(&ln, _S"    .destroy = (void(*)(void*))", cls->name, _S"_destroy,");
        bfWriteLine(bf, ln);
    }
    if (cls->abstract) {
        bfWriteLine(bf, _S"    ._abstract = true,");
    }
    strNConcat(&ln, _S"    .ifimpl = _ifimpl_", cls->name, _S",");
    bfWriteLine(bf, ln);

    bfWriteLine(bf, _S"};");
    bfWriteLine(bf, NULL);

    strDestroy(&ln);
}

bool writeImpl(string fname, bool mixinimpl)
{
    string hname = 0;
    pathFilename(&hname, fname);
    pathSetExt(&hname, hname, _S"h");
    string cname = 0;
    pathSetExt(&cname, fname, !mixinimpl ? _S"c" : _S"impl.h");
    string newcname = 0;
    strConcat(&newcname, cname, _S"new");

    FSFile *newf = fsOpen(newcname, Overwrite);
    if (!newf) {
        fprintf(stderr, "Failed to open %s for writing", lazyPlatformPath(newcname));
        return false;
    }
    BufFile *nbf = bfCreate(newf, true);

    FSFile *oldf = fsOpen(cname, Read);
    BufFile *obf = NULL;
    if (oldf)
        obf = bfCreate(oldf, false);

    string ln = 0;
    bfWriteLine(nbf, autogenBegin);
    bfWriteLine(nbf, autogenNotice);
    bfWriteLine(nbf, _S"#include <cx/obj.h>");
    bfWriteLine(nbf, _S"#include <cx/core/objstdif.h>");
    bfWriteLine(nbf, _S"#include <cx/debug/assert.h>");
    bfWriteLine(nbf, _S"#include <cx/container.h>");
    bfWriteLine(nbf, _S"#include <cx/string.h>");
    if (!mixinimpl) {
        string phname = 0;
        pathToPlatform(&phname, hname);
        strNConcat(&ln, _S"#include \"", phname, _S"\"");
        strDestroy(&phname);
        bfWriteLine(nbf, ln);
        for (int i = 0; i < saSize(&implincludes); i++) {
            strNConcat(&ln, _S"#include ", implincludes[i]);
            bfWriteLine(nbf, ln);
        }
    }
    bfWriteLine(nbf, autogenEnd);

    string *seen = saCreate(string, 16, Sorted);
    parentmacros = saCreate(string, 16, Sorted);
    hashtable implidx = htCreate(string, opaque(MethodPair), 16);
    int err;
    PCRE2_SIZE eoffset;
    pcre2_code *reParentProto = pcre2_compile((PCRE2_SPTR)"extern [A-Za-z0-9_]+ \\**([A-Za-z0-9_]+)\\(.*\\); // parent", PCRE2_ZERO_TERMINATED, PCRE2_ANCHORED | PCRE2_ENDANCHORED, &err, &eoffset, NULL);
    pcre2_code *reParentMacro = pcre2_compile((PCRE2_SPTR)"#(?:define|undef) parent_[A-Za-z0-9_]+(?:\\(.*\\) [A-Za-z0-9_]+\\(.*\\))?", PCRE2_ZERO_TERMINATED, PCRE2_ANCHORED | PCRE2_ENDANCHORED, &err, &eoffset, NULL);
    pcre2_code *reProto = pcre2_compile((PCRE2_SPTR)"(?:_meta_inline )?[A-Za-z0-9_]+ \\**([A-Za-z0-9_]+)\\(.*\\)(;)?", PCRE2_ZERO_TERMINATED, PCRE2_ANCHORED | PCRE2_ENDANCHORED, &err, &eoffset, NULL);
    pcre2_match_data *match = pcre2_match_data_create_from_pattern(reProto, NULL);

    for (int i = 0; i < saSize(&classes); i++) {
        indexMethods(classes[i], &implidx);
    }

    bool inautogen = false;
    bool wasempty = true;
    Class *ininit = 0;
    Class *indestroy = 0;
    while (obf && bfReadLine(obf, &ln)) {
        if (strEq(ln, autogenBegin))
            inautogen = true;
        if (!inautogen) {
            wasempty = false;

            // ignore parent prototypes and macros
            int nmatches = pcre2_match(reParentProto, (PCRE2_SPTR)strC(&ln), strLen(ln), 0, 0, match, NULL);
            if (nmatches >= 0)
                continue;
            nmatches = pcre2_match(reParentMacro, (PCRE2_SPTR)strC(&ln), strLen(ln), 0, 0, match, NULL);
            if (nmatches >= 0)
                continue;

            nmatches = pcre2_match(reProto, (PCRE2_SPTR)strC(&ln), strLen(ln), 0, 0, match, NULL);
            if (nmatches >= 0) {
                PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match);
                string funcname = 0;
                strSubStr(&funcname, ln, (int32)ovector[2], (int32)ovector[3]);
                MethodPair mp;

                if (htFind(&implidx, string, funcname, opaque, &mp)) {
                    if (nmatches == 2) {
                        if (mp.m->isinit)
                            ininit = mp.c;
                        if (mp.m->isdestroy)
                            indestroy = mp.c;
                    }
                    saPush(&seen, string, funcname, Unique);
                    writeMethodProto(nbf, mp.c, mp.m, nmatches == 3, mixinimpl, false);
                } else {
                    bfWriteLine(nbf, ln);
                }
                strDestroy(&funcname);
            } else {
                if (strEq(ln, _S"}")) {
                    if (ininit) {
                        writeAutoInit(nbf, ininit);
                        ininit = 0;
                    } else if (indestroy) {
                        writeAutoDtors(nbf, indestroy);
                        indestroy = 0;
                    }
                }

                bfWriteLine(nbf, ln);
            }
        }
        if (strEq(ln, autogenEnd)) {
            inautogen = false;
        }
    }

    if (wasempty) {
        // quick and dirty way to format correctly when the file started out empty
        bfWriteLine(nbf, NULL);
    }

    for (int i = 0; i < saSize(&classes); i++) {
        if (!classes[i]->included)
            writeMethods(nbf, classes[i], &seen, mixinimpl);
    }

    if (!mixinimpl) {
        bfWriteLine(nbf, autogenBegin);
        for (int i = 0; i < saSize(&classes); i++) {
            if (!classes[i]->included)
                writeMixinStubs(nbf, classes[i]);
        }
        for (int i = 0; i < saSize(&ifaces); i++) {
            if (!ifaces[i]->included)
                writeIfaceTmpl(nbf, ifaces[i]);
        }
        for (int i = 0; i < saSize(&classes); i++) {
            if (!classes[i]->included && !classes[i]->mixin)
                writeClassImpl(nbf, classes[i]);
        }
        bfWriteLine(nbf, autogenEnd);
    } else {
        bfWriteLine(nbf, autogenBegin);
        for (int i = 0; i < saSize(&classes); i++) {
            if (!classes[i]->included)
                writeMixinProtos(nbf, classes[i]);
        }
        bfWriteLine(nbf, autogenEnd);
    }

    if (obf)
        bfClose(obf);
    bfClose(nbf);

    fsDelete(cname);
    fsRename(newcname, cname);

    pcre2_match_data_free(match);
    pcre2_code_free(reProto);
    strDestroy(&ln);
    strDestroy(&hname);
    strDestroy(&cname);
    strDestroy(&newcname);
    htDestroy(&implidx);
    saDestroy(&seen);
    saDestroy(&parentmacros);
    return true;
}

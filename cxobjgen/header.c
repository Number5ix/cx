#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/fs/file.h>
#include <cx/string.h>

static void writeUnbound(BufFile *bf, Class *cls, Class *cur, Method ***done)
{
    string ln = 0, implname = 0, callname = 0;

    for (int i = 0; i < saSize(&cur->methods); i++) {
        Method *m = cur->methods[i];
        if (!m->unbound)
            continue;

        if (saFind(done, object, m) != -1)
            continue;

        if (cls == cur) {
            methodImplName(&implname, cls, m->name);
            if (!m->standalone)
                strNConcat(&ln, m->returntype, _S" ", m->predecr, implname, _S"(", cls->name, _S" *self");
            else
                strNConcat(&ln, m->returntype, _S" ", m->predecr, implname, _S"(");

            for (int j = 0; j < saSize(&m->params); j++) {
                Param *p = m->params[j];
                string ptype = p->type;
                string ppre = p->predecr;

                if (strEq(ptype, _S"object") && strEmpty(ppre)) {
                    ptype = cls->name;
                    ppre = _S"*";
                }

                if (!m->standalone || j > 0)
                    strNConcat(&ln, ln, _S", ");
                strNConcat(&ln, ln, ptype, _S" ", ppre, p->name, p->postdecr);
            }
            strAppend(&ln, _S");");
            bfWriteLine(bf, ln);

            methodCallName(&callname, cls, m->name);
            strNConcat(&ln, _S"#define ", callname, m->standalone ? _S"(" : _S"(self");
            for (int j = 0; j < saSize(&m->params); j++) {
                if (!m->standalone || j > 0)
                    strNConcat(&ln, ln, _S", ", m->params[j]->name);
                else
                    strNConcat(&ln, ln, m->params[j]->name);
            }
            strNConcat(&ln, ln, _S") ", implname, m->standalone ? _S"(" : _S"(self");
            for (int j = 0; j < saSize(&m->params); j++) {
                string extra1 = 0, extra2 = 0;
                // use cast macro for classes that we know about
                if (htHasKey(&clsidx, string, m->params[j]->type) && strEq(m->params[j]->predecr, _S"*")) {
                    strConcat(&extra1, m->params[j]->type, _S"(");
                    extra2 = _S")";
                }
                if (!m->standalone || j > 0)
                    strNConcat(&ln, ln, _S", ", extra1, m->params[j]->name, extra2);
                else
                    strNConcat(&ln, ln, extra1, m->params[j]->name, extra2);
                strDestroy(&extra1);
            }
            strAppend(&ln, _S")");
            bfWriteLine(bf, ln);
        } else {
            // only emit standalone functions for the current class
            if (m->standalone)
                continue;

            methodCallName(&callname, cls, m->name);
            strNConcat(&ln, _S"#define ", callname, _S"(self");
            for (int j = 0; j < saSize(&m->params); j++) {
                strNConcat(&ln, ln, _S", ", m->params[j]->name);
            }
            methodImplName(&implname, cur, m->name);
            strNConcat(&ln, ln, _S") ", implname, _S"(", cur->name, _S"(self)");
            for (int j = 0; j < saSize(&m->params); j++) {
                // use cast macro for classes that we know about
                if (htHasKey(&clsidx, string, m->params[j]->type) && strEq(m->params[j]->predecr, _S"*"))
                    strNConcat(&ln, ln, _S", ", m->params[j]->type, _S"(", m->params[j]->name, _S")");
                else
                    strNConcat(&ln, ln, _S", ", m->params[j]->name);
            }
            strAppend(&ln, _S")");
            bfWriteLine(bf, ln);
        }

        saPush(done, object, m);
    }

    strDestroy(&implname);
    strDestroy(&callname);
    strDestroy(&ln);
    if (cur->parent)
        writeUnbound(bf, cls, cur->parent, done);
}

void writeIfDecl(BufFile *bf, Interface *iface)
{
    string ln = 0, tmp = 0;

    strNConcat(&ln, _S"typedef struct ", iface->name, _S" {");
    bfWriteLine(bf, ln);

    bfWriteLine(bf, _S"    ObjIface *_implements;");
    bfWriteLine(bf, _S"    ObjIface *_parent;");
    bfWriteLine(bf, _S"    size_t _size;");
    bfWriteLine(bf, NULL);

    for (int i = 0; i < saSize(&iface->allmethods); i++) {
        Method *m = iface->allmethods[i];
        strNConcat(&ln, _S"    ", m->returntype, _S" ", m->predecr, _S"(*", m->name, _S")(void *self");
        for (int j = 0; j < saSize(&m->params); j++) {
            Param *p = m->params[j];
            string ptype = p->type;
            string ppre = p->predecr;

            if (strEq(ptype, _S"object") && strEmpty(ppre)) {
                ptype = _S"void";
                ppre = _S"*";
            }

            strNConcat(&tmp, _S", ", ptype, _S" ", ppre, p->name, p->postdecr);
            strAppend(&ln, tmp);
        }
        strAppend(&ln, _S");");
        bfWriteLine(bf, ln);
    }

    strNConcat(&ln, _S"} ", iface->name, _S";");
    bfWriteLine(bf, ln);
    strNConcat(&ln, _S"extern ", iface->name, _S" ", iface->name, _S"_tmpl;");
    bfWriteLine(bf, ln);
    bfWriteLine(bf, NULL);

    strDestroy(&tmp);
    strDestroy(&ln);
}

void writeForwardDecl(BufFile *bf, string name)
{
    string ln = 0;
    strNConcat(&ln, _S"typedef struct ", name, _S" ", name, _S";");
    bfWriteLine(bf, ln);
    strDestroy(&ln);
}

static void writeClassMember(BufFile *bf, Class *cls, Member *m)
{
    string ln = 0;
    string predecr = 0;

    strDup(&predecr, m->predecr);

    for (int i = 0; i < saSize(&m->fulltype); i++) {
        if (strEq(m->fulltype[i], _S"sarray") ||
            strEq(m->fulltype[i], _S"object")) {
            strPrepend(_S"*", &predecr);
        }
    }

    strNConcat(&ln, _S"    ", m->vartype, _S" ", predecr, m->name, m->postdecr, _S";");
    bfWriteLine(bf, ln);

    strDestroy(&predecr);
    strDestroy(&ln);
}

static void writeClassTypeMarkers(BufFile *bf, Class *cls)
{
    string ln = 0;
    strNConcat(&ln, _S"        void *_is_", cls->name, _S";");
    bfWriteLine(bf, ln);
    strDestroy(&ln);

    if (cls->parent)
        writeClassTypeMarkers(bf, cls->parent);
}

void writeClassDecl(BufFile *bf, Class *cls)
{
    string ln = 0, mname = 0;

    strNConcat(&ln, _S"typedef struct ", cls->name, _S" {");
    bfWriteLine(bf, ln);

    if (!cls->mixin) {
        if (cls->classif)
            strNConcat(&ln, _S"    ", cls->name, _S"_ClassIf *_;");
        else
            strDup(&ln, _S"    ObjIface *_;");
        bfWriteLine(bf, ln);
        bfWriteLine(bf, _S"    union {");
        bfWriteLine(bf, _S"        ObjClassInfo *_clsinfo;");
        writeClassTypeMarkers(bf, cls);
        bfWriteLine(bf, _S"        void *_is_ObjInst;");
        bfWriteLine(bf, _S"    };");
        bfWriteLine(bf, _S"    atomic(intptr) _ref;");
        bfWriteLine(bf, NULL);
    }

    for (int i = 0; i < saSize(&cls->allmembers); i++) {
        Member *m = cls->allmembers[i];
        writeClassMember(bf, cls, m);
    }

    strNConcat(&ln, _S"} ", cls->name, _S";");
    bfWriteLine(bf, ln);
    if (!cls->mixin) {
        strNConcat(&ln, _S"extern ObjClassInfo ", cls->name, _S"_clsinfo;");
        bfWriteLine(bf, ln);
    }
    strNConcat(&ln, _S"#define ", cls->name, _S"(inst) ((", cls->name,
               _S"*)(&((inst)->_is_", cls->name, _S"), (inst)))");
    bfWriteLine(bf, ln);
    bfWriteLine(bf, NULL);

    if (cls->mixin)
        return;

    Method **unboundDone = saCreate(object, 16);
    writeUnbound(bf, cls, cls, &unboundDone);
    saDestroy(&unboundDone);

    for (int i = 0; i < saSize(&cls->allmethods); i++) {
        Method *m = cls->allmethods[i];
        if (m->internal)
            continue;
        methodCallName(&mname, cls, m->name);
        strNConcat(&ln, _S"#define ", mname, _S"(self");
        for (int j = 0; j < saSize(&m->params); j++) {
            strNConcat(&ln, ln, _S", ", m->params[j]->name);
        }
        strNConcat(&ln, ln, _S") (self)->_->", m->name, _S"(", cls->name, _S"(self)");
        for (int j = 0; j < saSize(&m->params); j++) {
            // use cast macro for classes that we know about
            if (htHasKey(&clsidx, string, m->params[j]->type) && strEq(m->params[j]->predecr, _S"*"))
                strNConcat(&ln, ln, _S", ", m->params[j]->type, _S"(", m->params[j]->name, _S")");
            else
                strNConcat(&ln, ln, _S", ", m->params[j]->name);
        }
        strAppend(&ln, _S")");
        bfWriteLine(bf, ln);
    }
    bfWriteLine(bf, NULL);

    strDestroy(&mname);
    strDestroy(&ln);
}

bool writeHeader(string fname)
{
    string hname = 0;
    pathSetExt(&hname, fname, _S"h");

    FSFile *file = fsOpen(hname, Overwrite);
    if (!file) {
        fprintf(stderr, "Failed to open %s for writing", lazyPlatformPath(hname));
        return false;
    }
    BufFile *bf = bfCreate(file, true);

    string ln = 0;
    bfWriteLine(bf, _S"#pragma once");
    bfWriteLine(bf, _S"// This header file is auto-generated!");
    bfWriteLine(bf, _S"// Do not make changes to this file or they will be overwritten.");
    bfWriteLine(bf, _S"#include <cx/obj.h>");
    for (int i = 0; i < saSize(&includes); i++) {
        strNConcat(&ln, _S"#include ", includes[i]);
        bfWriteLine(bf, ln);
    }
    bfWriteLine(bf, NULL);

    for (int i = 0; i < saSize(&structs); i++) {
        writeForwardDecl(bf, structs[i]);
    }

    for (int i = 0; i < saSize(&classes); i++) {
        if (!classes[i]->included)
            writeForwardDecl(bf, classes[i]->name);
    }
    if (!strEmpty(cpassthrough))
        bfWriteStr(bf, cpassthrough);
    bfWriteLine(bf, NULL);

    for (int i = 0; i < saSize(&ifaces); i++) {
        if (!ifaces[i]->included)
            writeIfDecl(bf, ifaces[i]);
    }

    for (int i = 0; i < saSize(&classes); i++) {
        if (!classes[i]->included)
            writeClassDecl(bf, classes[i]);
    }

    strDestroy(&ln);
    strDestroy(&hname);
    bfClose(bf);
    return true;
}

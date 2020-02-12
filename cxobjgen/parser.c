#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/fs/file.h>
#include <cx/string.h>
#include <stdio.h>

static string *already_parsed;

enum ParseContext {
    Context_Global,
    Context_InterfacePre,
    Context_Interface,
    Context_ClassPre,
    Context_Class,
    Context_ParamList,
};

typedef struct ParseState {
    string fname;
    FSFile *fp;
    char buf[1024];
    size_t blen;
    size_t bpos;

    bool included;
    bool allowannotations;

    string **annotations;
    string *tokstack;
    int context;
    int lastcontext;

    Interface *curif;
    Class *curcls;
    Method *curmethod;
    Param *curparam;

    string *includepath;
} ParseState;

static bool parseEnd(ParseState *ps, bool retval)
{
    if (ps->fp)
        fsClose(ps->fp);
    strDestroy(&ps->fname);
    saDestroy(&ps->tokstack);
    saDestroy(&ps->annotations);
    saDestroy(&ps->includepath);
    if (ps->curif) {
        objRelease(&ps->curif);
    }
    if (ps->curcls) {
        objRelease(&ps->curcls);
    }
    if (ps->curmethod) {
        objRelease(&ps->curmethod);
    }
    if (ps->curparam) {
        objRelease(&ps->curparam);
    }
    return retval;
}

static inline bool _isspecial(int ch)
{
    if (ch == '{' || ch == '}' || ch == ';' || ch == '(' || ch == ')' || ch == ',' || ch == '*' || ch == '[' || ch == ']')
        return true;

    return false;
}

static bool isvalidname(string str)
{
    bool ret = true;
    if (strEmpty(str))
        return false;

    striter st;
    striCreate(&st, str);
    while (st.len > 0) {
        for (uint32 i = 0; i < st.len; i++) {
            if (_isspecial(st.bytes[i])) {
                ret = false;
                goto out;
            }
        }
        striNext(&st);
    }
out:
    striDestroy(&st);
    return ret;
}

static bool onlyspecial(string str)
{
    bool ret = true;
    if (strEmpty(str))
        return false;

    striter st;
    striCreate(&st, str);
    while (st.len > 0) {
        for (uint32 i = 0; i < st.len; i++) {
            if (!_isspecial(st.bytes[i])) {
                ret = false;
                goto out;
            }
        }
        striNext(&st);
    }
out:
    striDestroy(&st);
    return ret;
}

static bool nextTok(ParseState *ps, string *tok)
{
    bool quote = false;
    char ch[2] = { 0 };

    strClear(tok);
    for (;;) {
        if (ps->bpos == ps->blen) {
            ps->bpos = 0;
            if (!fsRead(ps->fp, ps->buf, sizeof(ps->buf), &ps->blen) || ps->blen == 0)
                return false;           // eof or error
        }
        ch[0] = ps->buf[ps->bpos++];
        if (!quote && isspace(ch[0])) {
            if (strEmpty(*tok))
                continue;
            else
                return true;
        } else if (!quote && _isspecial(ch[0])) {
            if (strEmpty(*tok)) {
                strAppend(tok, (string)ch);
                return true;
            } else {
                ps->bpos--;
                return true;
            }
        }

        strAppend(tok, (string)ch);

        if (ch[0] == '"') {
            quote = !quote;
            if (!quote)
                return true;
        }
    }
}

static bool nextCustomTok(ParseState *ps, string *tok, char ends)
{
    char ch[2] = { 0 };

    strClear(tok);
    for (;;) {
        if (ps->bpos == ps->blen) {
            ps->bpos = 0;
            if (!fsRead(ps->fp, ps->buf, sizeof(ps->buf), &ps->blen) || ps->blen == 0)
                return false;           // eof or error
        }
        ch[0] = ps->buf[ps->bpos++];
        if (ch[0] == ends) {
            return true;
        }

        strAppend(tok, (string)ch);
    }
}

static bool nextCustomTok2(ParseState *ps, string *tok, string ends)
{
    char ch[2] = { 0 };
    int elen = strLen(ends);
    bool started = false;

    strClear(tok);
    for (;;) {
        if (ps->bpos == ps->blen) {
            ps->bpos = 0;
            if (!fsRead(ps->fp, ps->buf, sizeof(ps->buf), &ps->blen) || ps->blen == 0)
                return false;           // eof or error
        }
        ch[0] = ps->buf[ps->bpos++];
        if (!started && (ch[0] == '\r' || ch[0] == '\n'))
            continue;

        started = true;
        strAppend(tok, (string)ch);

        if (strRangeEq(*tok, ends, -elen, elen)) {
            strSubStrI(tok, 0, -elen);
            return true;
        }
    }
}

bool parseAnnotation(ParseState *ps, string *tok)
{
    string *anparts = saCreate(string, 2);
    string part = 0;

    strDestroy(tok);

    nextTok(ps, &part);
    if (!part || strEq(part, _S"]")) {
        saDestroy(&anparts);
        return false;
    }
    saPushC(&anparts, string, &part);

    nextCustomTok(ps, &part, ']');
    if (!strEmpty(part))
        saPushC(&anparts, string, &part);
    else
        strDestroy(&part);

    saPushC(&ps->annotations, sarray, &anparts);
    return true;
}

string* getAnnotation(string*** annotations, string afind)
{
    for (int i = 0; i < saSize(annotations); i++) {
        if (strEqi((*annotations)[i][0], afind))
            return (*annotations)[i];
    }
    return NULL;
}

bool parseGlobal(ParseState *ps, string *tok)
{
    bool abstract = false;
    bool mixin = false;
    if (strEq(*tok, _S"abstract")) {
        abstract = true;
        nextTok(ps, tok);
        if (!strEq(*tok, _S"class")) {
            fprintf(stderr, "abstract keyword may only be used with classes!\n");
            return false;
        }
    } else if (strEq(*tok, _S"mixin")) {
        mixin = true;
        nextTok(ps, tok);
        if (!strEq(*tok, _S"class")) {
            fprintf(stderr, "mixin keyword may only be used with classes!\n");
            return false;
        }
    }

    if (strEq(*tok, _S"interface")) {
        saClear(&ps->annotations);
        ps->curif = interfaceCreate();
        ps->context = Context_InterfacePre;
        ps->curif->included = ps->included;
        return true;
    } else if (strEq(*tok, _S"class")) {
        ps->curcls = classCreate();
        ps->curcls->abstract = abstract || mixin;   // mixin implies abstract
        ps->curcls->mixin = mixin;
        ps->context = Context_ClassPre;
        ps->curcls->included = ps->included;
        ps->curcls->annotations = ps->annotations;
        ps->annotations = saCreate(sarray, 4);
        return true;
    } else if (strEq(*tok, _S"#include")) {
        string fname = 0;
        bool brackets;
        nextTok(ps, &fname);
        striter it;
        striCreate(&it, fname);
        bool ret = true;

        if (it.len < 1)
            ret = false;
        else if (it.bytes[0] == '"')
            brackets = false;
        else if (it.bytes[0] == '<')
            brackets = true;
        else
            ret = false;

        striSeek(&it, 1, STRI_BYTE, STRI_END);
        if (it.len < 1 || it.bytes[0] != (brackets ? '>' : '"'))
            ret = false;
        striDestroy(&it);
        if (!ret) {
            fprintf(stderr, "Invalid include syntax '%s'\n", strC(&fname));
            return false;
        }

        strSubStrI(&fname, 1, -1);
        string ext = 0;
        pathGetExt(&ext, fname);

        if (strEqi(ext, _S"sidl")) {
            string rname = 0, realfname = 0;
            pathFromPlatform(&rname, fname);
            ret = parseFile(rname, &realfname, ps->includepath, true, true);
            pathSetExt(&fname, fname, _S"h");
            saPushC(&deps, string, &realfname, Unique);
            strDestroy(&rname);
        }

        strPrepend(brackets ? _S"<" : _S"\"", &fname);
        strAppend(&fname, brackets ? _S">" : _S"\"");
        if (!ps->included)
            saPushC(&includes, string, &fname, Unique);
        else
            strDestroy(&fname);
        strDestroy(&ext);
        return ret;
    } else if (strEq(*tok, _S"struct")) {
        nextTok(ps, tok);
        saPush(&structs, string, *tok, Unique);
        nextTok(ps, tok);
        if (!strEq(*tok, _S";")) {
            fprintf(stderr, "parse error in structure forward declaration\n");
            return false;
        }
        return true;
    }

    fprintf(stderr, "Invalid token '%s' in global context\n", strC(tok));
    return false;
}

bool parseInterfacePre(ParseState *ps, string *tok)
{
    if (strEq(*tok, _S"{")) {
        if (strEmpty(ps->curif->name)) {
            fprintf(stderr, "Interface missing a name!\n");
            return false;
        }
        saClear(&ps->annotations);
        ps->context = Context_Interface;
        return true;
    } else if (strEq(*tok, _S"extends")) {
        if (ps->curif->parent) {
            fprintf(stderr, "Can only extend one interface!\n");
            return false;
        }

        string name = 0;
        nextTok(ps, &name);
        if (!isvalidname(name)) {
            fprintf(stderr, "Invalid interface name '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        if (!htFind(&ifidx, string, name, object, &ps->curif->parent)) {
            fprintf(stderr, "Could not find interface '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        strDestroy(&name);
        return true;
    } else if (!ps->curif->name && isvalidname(*tok)) {
        strDup(&ps->curif->name, *tok);
        return true;
    }

    fprintf(stderr, "Invalid token '%s' in pre-interface context\n", strC(tok));
    return false;
}

bool parseInterface(ParseState *ps, string *tok)
{
    if (strEq(*tok, _S"}")) {
        htInsert(&ifidx, string, ps->curif->name, object, ps->curif);
        saPushC(&ifaces, object, &ps->curif);
        saClear(&ps->annotations);
        ps->context = Context_Global;
        return true;
    } else if (strEq(*tok, _S";")) {
        if (ps->curmethod) {
            if (strEmpty(ps->curmethod->name)) {
                fprintf(stderr, "Incomplete interface method declaration\n");
                return false;
            }
            saPushC(&ps->curif->methods, object, &ps->curmethod);
        }
        return true;
    } else if (!ps->curmethod) {
        if (!isvalidname(*tok)) {
            fprintf(stderr, "Invalid return type '%s'\n", strC(tok));
            return false;
        }
        ps->curmethod = methodCreate();
        ps->curmethod->annotations = ps->annotations;
        ps->curmethod->srcif = ps->curif;
        strDup(&ps->curmethod->srcfile, ps->fname);
        ps->annotations = saCreate(sarray, 4);
        strDup(&ps->curmethod->returntype, *tok);
        return true;
    } else if (strEq(*tok, _S"(")) {
        if (strEmpty(ps->curmethod->returntype) || strEmpty(ps->curmethod->name)) {
            fprintf(stderr, "Incomplete interface method declaration\n");
            return false;
        }
        ps->lastcontext = ps->context;
        saClear(&ps->annotations);
        ps->allowannotations = false;
        ps->context = Context_ParamList;
        return true;
    } else if (ps->curmethod && !ps->curmethod->name && onlyspecial(*tok)) {
        strAppend(&ps->curmethod->predecr, *tok);
        return true;
    } else if (ps->curmethod && !ps->curmethod->name && isvalidname(*tok)) {
        strDup(&ps->curmethod->name, *tok);
        return true;
    }

    fprintf(stderr, "Invalid token '%s' in interface definition\n", strC(tok));
    return false;
}

bool parseParamList(ParseState *ps, string *tok)
{
    if (strEq(*tok, _S")")) {
        if (ps->curparam) {
            if (strEmpty(ps->curparam->name)) {
                fprintf(stderr, "Incomplete method parameter\n");
                return false;
            }
            saPushC(&ps->curmethod->params, object, &ps->curparam);
        }
        saClear(&ps->annotations);
        ps->allowannotations = true;
        ps->context = ps->lastcontext;
        return true;
    } else if (strEq(*tok, _S",")) {
        if (!ps->curparam || strEmpty(ps->curparam->name)) {
            fprintf(stderr, "Incomplete method parameter\n");
            return false;
        }
        saPushC(&ps->curmethod->params, object, &ps->curparam);
        return true;
    } else if (!ps->curparam) {
        if (!isvalidname(*tok)) {
            fprintf(stderr, "Invalid parameter type '%s'\n", strC(tok));
            return false;
        }
        ps->curparam = paramCreate();
        strDup(&ps->curparam->type, *tok);
        return true;
    } else if (!ps->curparam->name && onlyspecial(*tok)) {
        strAppend(&ps->curparam->predecr, *tok);
        return true;
    } else if (!ps->curparam->name && isvalidname(*tok)) {
        strDup(&ps->curparam->name, *tok);
        return true;
    } else if (ps->curparam->name) {
        strAppend(&ps->curparam->postdecr, *tok);
        return true;
    }

    fprintf(stderr, "Parse error in parameter list\n");
    return false;
}

bool parseClassPre(ParseState *ps, string *tok)
{
    if (strEq(*tok, _S"{")) {
        if (strEmpty(ps->curcls->name)) {
            fprintf(stderr, "Class missing a name!\n");
            return false;
        }
        saClear(&ps->annotations);
        ps->context = Context_Class;
        return true;
    } else if (strEq(*tok, _S"extends")) {
        if (ps->curcls->parent) {
            fprintf(stderr, "Can only extend one class!\n");
            return false;
        }

        string name = 0;
        nextTok(ps, &name);
        if (!isvalidname(name)) {
            fprintf(stderr, "Invalid class name '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        if (!htFind(&clsidx, string, name, object, &ps->curcls->parent)) {
            fprintf(stderr, "Could not find class '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        strDestroy(&name);
        return true;
    } else if (strEq(*tok, _S"uses")) {
        string name = 0;
        Class *uses;
        nextTok(ps, &name);
        if (!isvalidname(name)) {
            fprintf(stderr, "Invalid class name '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        if (!htFind(&clsidx, string, name, object, &uses)) {
            fprintf(stderr, "Could not find class '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        if (!uses->mixin) {
            fprintf(stderr, "Cannot use non-mixin class '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        saPush(&ps->curcls->uses, object, uses, Unique);
        strDestroy(&name);
        return true;
    } else if (strEq(*tok, _S"implements")) {
        string name = 0;
        nextTok(ps, &name);
        if (!isvalidname(name)) {
            fprintf(stderr, "Invalid interface name '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        Interface *tempif = 0;
        if (!htFind(&ifidx, string, name, object, &tempif)) {
            fprintf(stderr, "Could not find interface '%s'\n", strC(&name));
            strDestroy(&name);
            return false;
        }
        saPush(&ps->curcls->implements, object, tempif, Unique);
        strDestroy(&name);
        return true;
    } else if (strEq(*tok, _S";")) {
        // this is actually a forward declaration
        if (strEmpty(ps->curcls->name)) {
            fprintf(stderr, "Class forward declaration missing a name!\n");
            return false;
        }
        saPush(&structs, string, ps->curcls->name);
        objRelease(&ps->curcls);
        saClear(&ps->annotations);
        ps->context = Context_Global;
        return true;
    } else if (!ps->curcls->name && isvalidname(*tok)) {
        strDup(&ps->curcls->name, *tok);
        return true;
    }

    fprintf(stderr, "Invalid token '%s' in pre-class context\n", strC(tok));
    return false;
}

bool parseClass(ParseState *ps, string *tok)
{
    if (strEq(*tok, _S"}")) {
        if (saSize(&ps->tokstack) > 0) {
            fprintf(stderr, "Extra junk in class definition\n");
            return false;
        }
        htInsert(&clsidx, string, ps->curcls->name, object, ps->curcls);
        saPushC(&classes, object, &ps->curcls);
        saClear(&ps->annotations);
        ps->allowannotations = true;
        ps->context = Context_Global;
        return true;
    } else if (strEq(*tok, _S";")) {
        if (ps->curmethod) {
            saPushC(&ps->curcls->methods, object, &ps->curmethod);
        } else if (saSize(&ps->tokstack) >= 2) {
            // this must be a class member
            if (!isvalidname(ps->tokstack[0])) {
                fprintf(stderr, "Invalid member type '%s'\n", strC(&ps->tokstack[0]));
                return false;
            }
            Member *nmem = memberCreate();
            nmem->annotations = ps->annotations;
            ps->annotations = saCreate(sarray, 4);

            string *vartype = 0;
            if (strSplit(&vartype, ps->tokstack[0], _S":", false) >= 2) {
                // special case for a couple things
                if (strEq(vartype[0], _S"hashtable")) {
                    strDup(&nmem->vartype, vartype[0]);         // hashtable is the actual type
                } else if (strEq(vartype[0], _S"atomic")) {
                    strNConcat(&nmem->vartype, _S, _S"atomic(", vartype[saSize(&vartype) - 1], _S")");
                } else {
                    strDup(&nmem->vartype, vartype[saSize(&vartype) - 1]);
                }

                nmem->fulltype = vartype;
                vartype = 0;
            } else {
                strDup(&nmem->vartype, ps->tokstack[0]);
            }
            saDestroy(&vartype);
            for (int32 i = 1; i < saSize(&ps->tokstack); i++) {
                if (!nmem->name && onlyspecial(ps->tokstack[i])) {
                    strAppend(&nmem->predecr, ps->tokstack[i]);
                } else if (!nmem->name && isvalidname(ps->tokstack[i])) {
                    strDup(&nmem->name, ps->tokstack[i]);
                } else if (nmem->name) {
                    strAppend(&nmem->postdecr, ps->tokstack[i]);
                } else {
                    fprintf(stderr, "Parse error in class member definition\n");
                    objRelease(&nmem);
                    return false;
                }
            }
            if (strEmpty(nmem->name)) {
                fprintf(stderr, "Incomplete class member definition\n");
                objRelease(&nmem);
                return false;
            }
            saPushC(&ps->curcls->members, object, &nmem);
        }
        saClear(&ps->tokstack);
        saClear(&ps->annotations);
        ps->allowannotations = true;
        return true;
    } else if (ps->curmethod) {
        fprintf(stderr, "Invalid token '%s' in class definition\n", strC(tok));
        return false;
    } else if (strEq(*tok, _S"(")) {
        if (saSize(&ps->tokstack) == 1) {
            if (strEq(ps->tokstack[0], _S"destroy")) {
                ps->curcls->hasdestroy = true;
                string dummy = 0;
                nextTok(ps, &dummy);
                if (!strEq(dummy, _S")")) {
                    fprintf(stderr, "destructor cannot take parameters!\n");
                    return false;
                }
                strDestroy(&dummy);
                return true;
            } else if (strEq(ps->tokstack[0], _S"init")) {
                ps->curcls->hasinit = true;
                string dummy = 0;
                nextTok(ps, &dummy);
                if (!strEq(dummy, _S")")) {
                    fprintf(stderr, "init cannot take parameters!\n");
                    return false;
                }
                strDestroy(&dummy);
                return true;
            }
        }
        if (saSize(&ps->tokstack) == 0) {
            fprintf(stderr, "Syntax error in class definition\n");
            return false;
        }

        ps->curmethod = methodCreate();
        ps->curmethod->annotations = ps->annotations;
        ps->annotations = saCreate(sarray, 4);
        ps->curmethod->srcclass = ps->curcls;
        strDup(&ps->curmethod->srcfile, ps->fname);

        if (strEq(ps->tokstack[0], _S"unbound")) {
            if (ps->curcls->mixin) {
                fprintf(stderr, "Unbound functions may not be used in mixin classes\n");
                return false;
            }
            ps->curmethod->unbound = true;
            saRemove(&ps->tokstack, 0);
        } else if (strEq(ps->tokstack[0], _S"factory")) {
            if (ps->curcls->abstract || ps->curcls->mixin) {
                fprintf(stderr, "%s class '%s' tried to declare a factory\n",
                        ps->curcls->mixin ? "Mixin" : "Abstract",
                        strC(&ps->curcls->name));
                return false;
            }

            ps->curmethod->unbound = true;
            ps->curmethod->isfactory = true;
            saRemove(&ps->tokstack, 0);
            saInsert(&ps->tokstack, 0, string, ps->curcls->name);
            saInsert(&ps->tokstack, 1, string, _S"*");
        }

        strDup(&ps->curmethod->returntype, ps->tokstack[0]);
        for (int32 i = 1; i < saSize(&ps->tokstack); i++) {
            if (!ps->curmethod->name && onlyspecial(ps->tokstack[i])) {
                strAppend(&ps->curmethod->predecr, ps->tokstack[i]);
            } else if (!ps->curmethod->name && isvalidname(ps->tokstack[i])) {
                strDup(&ps->curmethod->name, ps->tokstack[i]);
            } else {
                fprintf(stderr, "Parse error in class method definition\n");
                return false;
            }
        }
        if (strEmpty(ps->curmethod->name)) {
            fprintf(stderr, "Incomplete class method definition\n");
            return false;
        }

        ps->lastcontext = ps->context;
        saClear(&ps->annotations);
        ps->allowannotations = false;
        ps->context = Context_ParamList;
        return true;
    } else if (strEq(*tok, _S"override")) {
        string name = 0;
        nextTok(ps, &name);
        if (!isvalidname(name)) {
            fprintf(stderr, "Invalid override '%s'\n", strC(&name));
            return false;
        }
        saPushC(&ps->curcls->overrides, string, &name, Unique);
        return true;
    }

    // we don't know what this is yet, so save it until more context is available
    saPush(&ps->tokstack, string, *tok);
    ps->allowannotations = false;           // don't allow after start, so static arrays can be parsed
    return true;
}

bool parseFile(string fname, string *realfn, string *searchpath, bool included, bool required)
{
    ParseState ps = { 0 };

    string fpath = 0;
    strDup(&ps.fname, fname);
    if (pathIsAbsolute(fname)) {
        strDup(&fpath, fname);
    } else {
        for (int i = 0; i < saSize(&searchpath); i++) {
            pathJoin(&fpath, searchpath[i], fname);
            if (fsExist(fpath))
                break;
        }
        if (strEmpty(fpath) || !fsExist(fpath)) {
            pathMakeAbsolute(&fpath, fname);
        }
    }

    pathNormalize(&fpath);

    if (!already_parsed) {
        already_parsed = saCreate(string, 10, Sorted);
    }
    if (saPush(&already_parsed, string, fpath, Unique) == -1) {
        strDestroy(&fpath);
        return parseEnd(&ps, true);
    }

    ps.fp = fsOpen(fpath, Read);
    if (!ps.fp) {
        if (required)
            fprintf(stderr, "Could not open %s\n", lazyPlatformPath(fpath));
        strDestroy(&fpath);
        return parseEnd(&ps, false);
    }

    stCopy(sarray, &ps.includepath, searchpath);
    string fdir = 0;
    pathParent(&fdir, fpath);
    saPushC(&ps.includepath, string, &fdir, Unique);

    if (realfn)
        strDup(realfn, fpath);
    strDestroy(&fpath);

    ps.context = Context_Global;
    ps.tokstack = saCreate(string, 8);
    ps.annotations = saCreate(sarray, 4);
    ps.included = included;
    ps.allowannotations = true;

    string tok = 0;
    while ((nextTok(&ps, &tok))) {
        if (strEq(tok, _S"//")) {
            nextCustomTok(&ps, &tok, '\n');
            continue;
        }

        if (strEq(tok, _S"<<<")) {
            nextCustomTok2(&ps, &tok, _S">>>");
            if (!included)
                strAppend(&cpassthrough, tok);
            continue;
        }

        if (ps.allowannotations && strEq(tok, _S"[")) {
            if (!parseAnnotation(&ps, &tok))
                return parseEnd(&ps, false);
            continue;
        }

        switch (ps.context) {
        case Context_Global:
            if (!parseGlobal(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        case Context_InterfacePre:
            if (!parseInterfacePre(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        case Context_Interface:
            if (!parseInterface(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        case Context_ParamList:
            if (!parseParamList(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        case Context_ClassPre:
            if (!parseClassPre(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        case Context_Class:
            if (!parseClass(&ps, &tok))
                return parseEnd(&ps, false);
            break;
        }
    }

    return parseEnd(&ps, true);
}

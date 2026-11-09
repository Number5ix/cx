#include <ctype.h>
#include <cx/container.h>
#include <cx/string.h>
#include "cxautogen.h"

static void fillMembers(sa_Member* members, Class* cls)
{
    // add parent clases first so the order is correct
    if (cls->parent)
        fillMembers(members, cls->parent);

    for (int i = 0; i < saSize(cls->members); i++) {
        saPush(members, object, cls->members.a[i]);
    }
}

static void fillMethods(sa_Method* methods, Class* cls)
{
    // first, add in any methods from parent classes and their interfaces
    // allmethods includes inherited methods
    if (cls->parent) {
        for (int i = 0; i < saSize(cls->parent->allmethods); i++) {
            Method* m = cls->parent->allmethods.a[i];
            if (!m->unbound && !m->internal)
                saPush(methods, object, m, SA_Unique);
        }
    }

    // then add in actual implemented methods, overriding where applicable
    for (int i = 0; i < saSize(cls->methods); i++) {
        if (!cls->methods.a[i]->unbound && !cls->methods.a[i]->internal) {
            int32 idx = saFind(*methods, object, cls->methods.a[i]);
            if (idx == -1) {
                saPush(methods, object, cls->methods.a[i], SA_Unique);
            } else {
                // already exists, replace with child class version
                saRemove(methods, idx);
                saInsert(methods, idx, object, cls->methods.a[i]);
            }
        }
    }
}

static Method* findClassMethod(string name, Class* search, Interface* iface)
{
    if (!search)
        return NULL;

    // ensure the class providing the method actually implements the interface
    if (!iface || saFind(search->implements, object, iface) != -1) {
        for (int i = 0; i < saSize(search->methods); i++) {
            if (strEq(name, search->methods.a[i]->name))
                return search->methods.a[i];
        }
    }

    return search->parent ? findClassMethod(name, search->parent, iface) : NULL;
}

static Method* findInterfaceMethod(string name, Class* root, Interface* search)
{
    if (!root)
        return NULL;

    if (search) {
        for (int i = 0; i < saSize(search->methods); i++) {
            if (strEq(name, search->methods.a[i]->name))
                return search->methods.a[i];
        }
        return search->parent ? findInterfaceMethod(name, root, search->parent) : NULL;
    }

    for (int i = 0; i < saSize(root->implements); i++) {
        Method* m = findInterfaceMethod(name, root, root->implements.a[i]);
        if (m)
            return m;
    }

    return NULL;
}

static void addInterfaceImpl(Class* cls, Interface* iface)
{
    if (iface->parent)
        addInterfaceImpl(cls, iface->parent);

    for (int i = 0; i < saSize(iface->methods); i++) {
        Method* m = findClassMethod(iface->methods.a[i]->name,
                                    cls,
                                    iface->methods.a[i]->srcif);   // already have it?
        if (!m) {
            m = methodClone(iface->methods.a[i]);
            saPushC(&cls->methods, object, &m);
        }
    }
}

static void addAbstractInterfaces(sa_Method* methods, Interface* iface)
{
    if (iface->parent)
        addAbstractInterfaces(methods, iface->parent);

    for (int i = 0; i < saSize(iface->methods); i++) {
        saPush(methods, object, iface->methods.a[i], SA_Unique);
    }
}

// Class members reuse StructMemberDesc for their serialization reflection, but only the
// serialization annotation carries over. [nodestroy] is already booked for class members --
// it clears Member::destroy, which is what suppresses the generated destructor call -- and
// setting STRUCT_NoDestroy from it as well would give one annotation two unrelated meanings.
static void setClassMemberFlags(Class* cls)
{
    for (int i = 0; i < saSize(cls->members); i++) {
        Member* m = cls->members.a[i];
        if (getAnnotation(NULL, m->annotations, _S"noserialize"))
            m->flags |= STRUCT_NoSerialize;
    }
}

bool classIsSerializable(Class* cls)
{
    if (getAnnotation(NULL, cls->annotations, _S"serialize"))
        return true;

    for (int i = 0; i < saSize(cls->implements); i++) {
        if (strEq(cls->implements.a[i]->name, _S"Serializable"))
            return true;
    }
    return false;
}

bool classHasSerMembers(Class* cls)
{
    return getAnnotation(NULL, cls->annotations, _S"serialize");
}

// [serialize] and Serializable are two answers to the same question, so a class giving both
// has no defined meaning. Rejecting it at codegen beats inventing a precedence rule that one
// of the two authors will not have expected.
static bool checkSerializable(Class* cls)
{
    if (!getAnnotation(NULL, cls->annotations, _S"serialize"))
        return true;

    for (int i = 0; i < saSize(cls->implements); i++) {
        if (strEq(cls->implements.a[i]->name, _S"Serializable")) {
            fprintf(stderr,
                    "Class '%s' is both [serialize] and implements Serializable; use one or "
                    "the other\n",
                    strC(cls->name));
            return false;
        }
    }
    return true;
}

static void checkMemberInitDestroy(Class* cls)
{
    for (int i = 0; i < saSize(cls->members); i++) {
        Member* m = cls->members.a[i];

        if (strEmpty(m->postdecr)) {
            if (m->typenode && saSize(m->typenode->params) > 0 &&
                !strEq(m->typenode->name, _S"atomic")) {
                // these have enough info to auto init without help
                if (strEmpty(m->predecr) && !strEq(m->typenode->name, _S"object") &&
                    !strEq(m->typenode->name, _S"weak") && !strEq(m->typenode->name, _S"structp"))
                    m->init = true;
                m->destroy = true;
            }
            if ((strEq(m->vartype, _S"CondVar") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Event") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Mutex") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"RWLock") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Semaphore") && strEmpty(m->predecr))) {
                m->init = true;
            }
            if ((strEq(m->vartype, _S"string") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"hashtable") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"stvar") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"closure") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"cchain") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Buffer") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"BufChain") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"BufRing") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"CondVar") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Event") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Mutex") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"RWLock") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"Semaphore") && strEmpty(m->predecr))) {
                m->destroy = true;
            }
        }

        // annotations can override
        sa_string an;
        getAnnotation(&an, m->annotations, _S"init");
        if (saSize(an) >= 2) {
            strDup(&m->initstr, an.a[1]);
            m->init = true;
        }
        if (getAnnotation(NULL, m->annotations, _S"noinit")) {
            m->init = false;
        }
        if (getAnnotation(NULL, m->annotations, _S"nodestroy")) {
            m->destroy = false;
        }

        // if any member needs to be destroyed, class must have a destructor
        if (m->init)
            cls->hasinit = cls->hasautoinit = true;
        if (m->destroy)
            cls->hasdestroy = cls->hasautodtors = true;
    }
}

static void addMixin(Class* cls, _In_ Class* uses)
{
    string hfile = 0;
    // copy any methods that we don't already have
    for (int i = 0; i < saSize(uses->allmethods); i++) {
        Method* m = findClassMethod(uses->allmethods.a[i]->name, cls, NULL);   // already have it?
        if (!m) {
            // clone the method
            m           = methodClone(uses->allmethods.a[i]);
            // so that it can point at the mixin that actually pulled it in
            m->mixinsrc = uses;
            strDup(&hfile, m->srcfile);
            pathSetExt(&hfile, hfile, _S"impl.h");
            strPrepend(_S"\"", &hfile);
            strAppend(&hfile, _S"\"");
            saPushC(&implincludes, string, &hfile, SA_Unique);
            saPushC(&cls->methods, object, &m);
        }
    }
    strDestroy(&hfile);

    // if the mixin or any parent needs init/destructor, so do we
    // the runtime won't handle this since there's no parent->child relationship
    for (Class* mixin = uses; mixin; mixin = mixin->parent) {
        cls->hasautoinit |= mixin->hasautoinit;
        cls->hasinit |= mixin->hasautoinit;
        cls->hasautodtors |= mixin->hasautodtors;
        cls->hasdestroy |= mixin->hasautodtors;

        // any interfaces implemented by the mixin tree need to be copied, too
        for (int i = 0; i < saSize(mixin->implements); i++)
            saPush(&cls->implements, object, mixin->implements.a[i], SA_Unique);
    }

    // if the mixin class has any data members, embed the mixin struct
    if (saSize(uses->allmembers) > 0) {
        Member* mixindata   = memberCreate();
        mixindata->mixinsrc = uses;
        mixindata->init     = true;
        mixindata->destroy  = true;
        strDup(&mixindata->vartype, uses->name);
        mixinMemberName(&mixindata->name, uses);
        saPushC(&cls->members, object, &mixindata, SA_Unique);
    }
}

static bool implementsChild(Class* cls, Interface* iface)
{
    for (int i = saSize(cls->implements) - 1; i >= 0; --i) {
        Interface* testif = cls->implements.a[i];

        // skip self
        if (testif == iface)
            continue;

        testif = testif->parent;
        while (testif) {
            if (testif == iface)
                return true;
            testif = testif->parent;
        }
    }

    return false;
}

static void pruneInterfaces(Class* cls)
{
    // First, remove any interfaces where this class also implements one of
    // their children.
    for (int i = saSize(cls->implements) - 1; i >= 0; --i) {
        if (implementsChild(cls, cls->implements.a[i]))
            saRemove(&cls->implements, i);
    }

    // abstract classes need to keep empty interfaces to pass them on to children
    if (cls->abstract || cls->mixin)
        return;

    // Next, remove any interfaces that we don't actually implement anything
    // from. In practice, this only removes inherited interfaces that are
    // completely implemented by parent classes, otherwise stub methods would
    // have already been added.
    for (int i = saSize(cls->implements) - 1; i >= 0; --i) {
        int j;

        // don't delete the class interface
        if (cls->implements.a[i]->classif)
            continue;

        for (j = saSize(cls->methods) - 1; j >= 0; --j) {
            if (cls->methods.a[j]->internal)
                continue;

            // see if this class method is part of the interface
            if (saFind(cls->implements.a[i]->allmethods, object, cls->methods.a[j]) != -1)
                break;
        }

        // did we find one that's implemented?
        if (j < 0)
            saRemove(&cls->implements, i);   // nope
    }
}

static void checkClassInitFail(Class* cls)
{
    // check if any parent classes can fail, if so, this one can as well
    Class* pc = cls->parent;
    while (!cls->initcanfail && pc) {
        cls->initcanfail |= pc->initcanfail;
        pc = pc->parent;
    }

    // do the same for mixins
    for (int i = 0; i < saSize(cls->uses); i++) {
        checkClassInitFail(cls->uses.a[i]);
        cls->initcanfail |= cls->uses.a[i]->initcanfail;
    }
}

static void propagateCanFailToFactories(Class* cls)
{
    for (int i = saSize(cls->methods) - 1; i >= 0; --i) {
        if (cls->methods.a[i]->isfactory)
            cls->methods.a[i]->canfail |= cls->initcanfail;
    }
}

static bool isIdent(strref s)
{
    if (strEmpty(s))
        return false;
    for (uint32 i = 0; i < strLen(s); i++) {
        int ch = strGetChar(s, i);
        if (!(isalpha(ch) || ch == '_' || (i > 0 && isdigit(ch))))
            return false;
    }
    return true;
}

// A method's [prop] annotation, or the one on the parent or interface method it replaces
static bool propAnnotation(sa_string* out, Class* cls, Method* m, bool* fromiface)
{
    *fromiface = m->srcif != NULL;
    if (getAnnotation(out, m->annotations, _S"prop"))
        return true;

    if (cls->parent) {
        for (int i = 0; i < saSize(cls->parent->allmethods); i++) {
            Method* pm = cls->parent->allmethods.a[i];
            if (strEq(pm->name, m->name))
                return propAnnotation(out, cls->parent, pm, fromiface);
        }
    }

    for (int i = 0; i < saSize(cls->implements); i++) {
        Interface* iface = cls->implements.a[i];
        if (iface->classif)
            continue;
        for (int j = 0; j < saSize(iface->allmethods); j++) {
            Method* im = iface->allmethods.a[j];
            if (strEq(im->name, m->name) && getAnnotation(out, im->annotations, _S"prop")) {
                *fromiface = true;
                return true;
            }
        }
    }
    return false;
}

// Splits "NAME [ro] [enum]"
static bool parsePropWords(Class* cls, Method* m, sa_string an, string* name, bool* ro, bool* isenum)
{
    *ro = *isenum = false;
    if (saSize(an) < 2) {
        fprintf(stderr, "Class '%s': [prop] on method '%s' needs a property name\n", strC(cls->name),
                strC(m->name));
        return false;
    }

    sa_string words;
    saInit(&words, string, 4);
    strSplit(&words, an.a[1], _S" ", false);
    bool ok = saSize(words) > 0 && isIdent(words.a[0]);
    if (ok)
        strDup(name, words.a[0]);
    else
        fprintf(stderr, "Class '%s': [prop %s] on method '%s' has an invalid property name\n",
                strC(cls->name), strC(an.a[1]), strC(m->name));

    for (int i = 1; ok && i < saSize(words); i++) {
        if (strEq(words.a[i], _S"ro")) {
            *ro = true;
        } else if (strEq(words.a[i], _S"enum")) {
            *isenum = true;
        } else {
            fprintf(stderr, "Class '%s': unknown flag '%s' in [prop %s]\n", strC(cls->name),
                    strC(words.a[i]), strC(an.a[1]));
            ok = false;
        }
    }
    saDestroy(&words);
    return ok;
}

// Maps a method's C type to the stype name a property descriptor uses
static bool propType(string* sttype, string* objclass, strref type, strref predecr)
{
    static const char* const scalars[] = { "bool",   "int8",   "int16",   "int32",   "int64",
                                           "uint8",  "uint16", "uint32",  "uint64",  "intptr",
                                           "uintptr", "float32", "float64", NULL };

    strClear(objclass);
    if (strEmpty(predecr)) {
        for (int i = 0; scalars[i]; i++) {
            if (strEq(type, (strref)scalars[i])) {
                strDup(sttype, type);
                return true;
            }
        }
        if (strEq(type, _S"size_t")) {
            strDup(sttype, _S"size");
            return true;
        }
        if (strEq(type, _S"strref") || strEq(type, _S"string")) {
            strDup(sttype, _S"string");
            return true;
        }
        return false;
    }

    if (strEq(predecr, _S"*") && htHasKey(clsidx, strref, type)) {
        strDup(sttype, _S"object");
        strDup(objclass, type);
        return true;
    }
    return false;
}

static PropDef* findPropDef(sa_PropDef props, strref name)
{
    for (int i = 0; i < saSize(props); i++) {
        if (strEq(props.a[i]->name, name))
            return props.a[i];
    }
    return NULL;
}

static bool sameMethod(Method* a, Method* b)
{
    return (!a && !b) || (a && b && strEq(a->name, b->name));
}

// Checks a property's getter/setter pair once every [prop] method has been seen
static bool checkPropDef(Class* cls, PropDef* pd)
{
    const char* cname = strC(cls->name);
    const char* pname = strC(pd->name);

    if (!pd->getter) {
        fprintf(stderr, "Class '%s': property '%s' has a setter but no getter\n", cname, pname);
        return false;
    }
    if (pd->ro && pd->setter) {
        fprintf(stderr, "Class '%s': read-only property '%s' has a setter\n", cname, pname);
        return false;
    }
    if (!pd->ro && !pd->setter) {
        fprintf(stderr,
                "Class '%s': property '%s' has no setter; mark the getter [prop %s ro] if it is "
                "read-only\n",
                cname, pname, pname);
        return false;
    }

    if (!propType(&pd->sttype, &pd->objclass, pd->getter->returntype, pd->getter->predecr)) {
        fprintf(stderr, "Class '%s': property '%s' has unsupported type '%s%s'\n", cname, pname,
                strC(pd->getter->returntype), strC(pd->getter->predecr));
        return false;
    }

    if (pd->setter) {
        Param* p    = pd->setter->params.a[0];
        string st   = 0, oc = 0;
        bool sameok = propType(&st, &oc, p->type, p->predecr) && strEq(st, pd->sttype) &&
            strEq(oc, pd->objclass);
        strDestroy(&st);
        strDestroy(&oc);
        if (!sameok) {
            fprintf(stderr,
                    "Class '%s': property '%s' setter takes '%s%s' but the getter returns "
                    "'%s%s'\n",
                    cname, pname, strC(p->type), strC(p->predecr),
                    strC(pd->getter->returntype), strC(pd->getter->predecr));
            return false;
        }
        pd->setterbool = strEq(pd->setter->returntype, _S"bool");
        pd->setteropt  = getAnnotation(NULL, p->annotations, _S"opt");
    }

    if (pd->isenum && !strEq(pd->sttype, _S"int32")) {
        fprintf(stderr, "Class '%s': enum property '%s' must be int32\n", cname, pname);
        return false;
    }
    return true;
}

// Collects the [prop] methods visible on a class into the properties it adds over its parents.
// Thunks call through the method table, so a parent's entry already reaches any override and a
// subclass never needs its own copy.
static bool processClassProps(Class* cls)
{
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if ((m->isfactory || m->standalone) && getAnnotation(NULL, m->annotations, _S"prop")) {
            fprintf(stderr, "Class '%s': method '%s' can't be a property; standalone methods and "
                            "factories have no object to act on\n",
                    strC(cls->name), strC(m->name));
            return false;
        }
    }

    if (cls->mixin)
        return true;

    sa_PropDef found;
    saInit(&found, object, 4);
    sa_string enumhalves;   // names whose setter is marked enum
    saInit(&enumhalves, string, 4, SA_Sorted);
    string name = 0;
    bool ok     = true;

    // The method table's methods, and this class's own unbound ones; a parent's unbound
    // properties are in the parent's table.
    sa_Method cands;
    saInit(&cands, object, saSize(cls->allmethods) + 4);
    for (int i = 0; i < saSize(cls->allmethods); i++)
        saPush(&cands, object, cls->allmethods.a[i]);
    for (int i = 0; i < saSize(cls->methods); i++) {
        if (cls->methods.a[i]->unbound && !cls->methods.a[i]->standalone)
            saPush(&cands, object, cls->methods.a[i]);
    }

    for (int i = 0; ok && i < saSize(cands); i++) {
        Method* m = cands.a[i];
        sa_string an;
        bool fromiface, ro, isenum;
        if (!propAnnotation(&an, cls, m, &fromiface))
            continue;
        if (!parsePropWords(cls, m, an, &name, &ro, &isenum)) {
            ok = false;
            break;
        }

        bool isvoid   = strEq(m->returntype, _S"void") && strEmpty(m->predecr);
        bool isgetter = saSize(m->params) == 0 && !isvoid;
        bool issetter = saSize(m->params) == 1 &&
            (isvoid || (strEq(m->returntype, _S"bool") && strEmpty(m->predecr)));
        if (!isgetter && !issetter) {
            fprintf(stderr,
                    "Class '%s': [prop %s] method '%s' is neither a getter (no parameters) nor "
                    "a setter (one parameter, returning void or bool)\n",
                    strC(cls->name), strC(name), strC(m->name));
            ok = false;
            break;
        }

        PropDef* pd = findPropDef(found, name);
        if (!pd) {
            pd = propdefCreate();
            strDup(&pd->name, name);
            saPushC(&found, object, &pd);
            pd = found.a[saSize(found) - 1];
        }
        pd->iface |= fromiface;

        if (isgetter) {
            if (pd->getter) {
                fprintf(stderr, "Class '%s': property '%s' has two getters, '%s' and '%s'\n",
                        strC(cls->name), strC(name), strC(pd->getter->name), strC(m->name));
                ok = false;
            }
            pd->getter = m;
            pd->ro     = ro;
            pd->isenum = isenum;
        } else {
            if (ro) {
                fprintf(stderr, "Class '%s': 'ro' on property '%s' belongs on the getter\n",
                        strC(cls->name), strC(name));
                ok = false;
            } else if (pd->setter) {
                fprintf(stderr, "Class '%s': property '%s' has two setters, '%s' and '%s'\n",
                        strC(cls->name), strC(name), strC(pd->setter->name), strC(m->name));
                ok = false;
            }
            pd->setter = m;
            if (isenum)
                saPush(&enumhalves, string, name);
        }
    }

    for (int i = 0; ok && i < saSize(found); i++) {
        PropDef* pd = found.a[i];
        ok          = checkPropDef(cls, pd);
        if (ok && pd->setter && pd->isenum != (saFind(enumhalves, string, pd->name) != -1)) {
            fprintf(stderr, "Class '%s': 'enum' on property '%s' must be on both the getter and "
                            "the setter\n",
                    strC(cls->name), strC(pd->name));
            ok = false;
        }
        if (!ok)
            break;

        PropDef* inherited = NULL;
        for (Class* c = cls->parent; c && !inherited; c = c->parent)
            inherited = findPropDef(c->props, pd->name);
        if (inherited) {
            if (sameMethod(inherited->getter, pd->getter) &&
                sameMethod(inherited->setter, pd->setter))
                continue;
            fprintf(stderr,
                    "Class '%s': property '%s' is already declared by a parent class; override "
                    "its methods instead\n",
                    strC(cls->name), strC(pd->name));
            ok = false;
            break;
        }

        int32 at = 0;
        while (at < saSize(cls->props) && strCmp(cls->props.a[at]->name, pd->name) < 0) at++;
        saInsert(&cls->props, at, object, pd);
    }

    strDestroy(&name);
    saDestroy(&enumhalves);
    saDestroy(&found);
    saDestroy(&cands);
    return ok;
}

bool processClass(Class* cls)
{
    if (cls->processed)
        return true;

    // process parent classes first
    if (cls->parent)
        processClass(cls->parent);

    sa_string mpfx;
    getAnnotation(&mpfx, cls->annotations, _S"methodprefix");
    if (saSize(mpfx) == 2) {
        strDup(&cls->methodprefix, mpfx.a[1]);
    }

    // if our parent class implements any interfaces, we implement them too
    if (cls->parent) {
        for (int i = 0; i < saSize(cls->parent->implements); i++) {
            saPush(&cls->implements, object, cls->parent->implements.a[i], SA_Unique);
        }
    }

    // copy prototypes for overriden functions from parents
    for (int i = 0; i < saSize(cls->overrides); i++) {
        Method* m = findClassMethod(cls->overrides.a[i], cls->parent, NULL);
        if (!m) {
            // also allow overriding interface methods this way, mostly for abstract/mixin classes
            m = findInterfaceMethod(cls->overrides.a[i], cls, NULL);
        }
        if (!m) {
            fprintf(stderr, "Could not find method '%s' to override\n", strC(cls->overrides.a[i]));
            return false;
        }
        // clone and reset source class
        m = methodClone(m);
        // overriding an abstract method should remove the annotation as it means we explicitly
        // want to define it here; we remove extern for the same reason
        for (int i = saSize(m->annotations) - 1; i >= 0; --i) {
            if (saSize(m->annotations.a[i]) == 1 &&
                (strEq(m->annotations.a[i].a[0], _S"abstract") ||
                 strEq(m->annotations.a[i].a[0], _S"extern"))) {
                saRemove(&m->annotations, i);
            }
        }
        m->srcclass = cls;
        saPushC(&cls->methods, object, &m, SA_Unique);
    }

    // if this is not an abstract class, add any missing interface implementations
    if (!cls->abstract) {
        for (int i = 0; i < saSize(cls->implements); i++) {
            addInterfaceImpl(cls, cls->implements.a[i]);
        }
    }

    // copy stuff from mixin classes
    for (int i = 0; i < saSize(cls->uses); i++) {
        processClass(cls->uses.a[i]);
        addMixin(cls, cls->uses.a[i]);
    }

    // create the class interface
    if (!cls->mixin) {
        Interface* clsif = interfaceCreate();
        clsif->classif   = true;
        clsif->included  = cls->included;
        strDup(&clsif->name, cls->name);
        strAppend(&clsif->name, _S"_ClassIf");
        if (cls->parent) {
            string pname = 0;
            strDup(&pname, cls->parent->name);
            strAppend(&pname, _S"_ClassIf");
            htFind(ifidx, string, pname, object, &clsif->parent, HT_Borrow);
            strDestroy(&pname);
        }
        fillMethods(&clsif->methods, cls);
        fillMethods(&clsif->allmethods, cls);
        // abstract classes need to also bring in any methods from interfaces that
        // we are not implementing ourselves
        if (cls->abstract) {
            for (int i = 0; i < saSize(cls->implements); i++)
                addAbstractInterfaces(&clsif->allmethods, cls->implements.a[i]);
        }
        if (saSize(clsif->allmethods) > 0) {
            saPush(&cls->implements, object, clsif);
            htInsert(&ifidx, string, clsif->name, object, clsif, HT_Ignore);
            saPush(&ifaces, object, clsif, SA_Unique);
            cls->classif = clsif;
        } else {
            objRelease(&clsif);
        }
    } else {
        // mark mixin methods
        for (int i = 0; i < saSize(cls->methods); i++) cls->methods.a[i]->mixin = true;

        if (!cls->included)
            needmixinimpl = true;
    }

    if (!checkSerializable(cls))
        return false;

    // check for member init/destroy requirements
    checkMemberInitDestroy(cls);
    setClassMemberFlags(cls);

    // after setClassMemberFlags, so [noserialize] members are already out of the running
    if (classHasSerMembers(cls) && !checkSerMemberNames(cls->name, cls->members))
        return false;

    // check for whether class init can fail
    checkClassInitFail(cls);

    // if init can fail, so can any factories
    propagateCanFailToFactories(cls);

    int nextfactory = 0;
    // group factory methods up first
    for (int i = 0; i < saSize(cls->methods); i++) {
        Method* m = cls->methods.a[i];
        if (m->isfactory && i == nextfactory) {
            ++nextfactory;
        } else if (m->isfactory) {
            objAcquire(m);
            saRemove(&cls->methods, i);
            saInsert(&cls->methods, nextfactory, object, m);
            objRelease(&m);
            ++nextfactory;
        }
    }

    // create internal methods if needed
    if (cls->hasinit) {
        Method* m   = methodCreate();
        m->internal = true;
        m->isinit   = true;
        m->canfail  = cls->initcanfail;
        strDup(&m->returntype, _S"bool");
        strDup(&m->name, _S"init");
        saInsert(&cls->methods, nextfactory, object, m);
        objRelease(&m);
    }
    if (cls->hasdestroy) {
        Method* m    = methodCreate();
        m->internal  = true;
        m->isdestroy = true;
        strDup(&m->returntype, _S"void");
        strDup(&m->name, _S"destroy");
        saPushC(&cls->methods, object, &m);
    }
    fillMembers(&cls->allmembers, cls);
    fillMethods(&cls->allmethods, cls);

    if (cls->abstract) {
        // copy interface methods into our allmethods table since we're not
        // implementing them, but child classes still need them
        for (int i = 0; i < saSize(cls->implements); i++)
            addAbstractInterfaces(&cls->allmethods, cls->implements.a[i]);

        // delete any methods that are explictly marked abstract (no implementation)
        for (int i = saSize(cls->methods) - 1; i >= 0; --i) {
            if (getAnnotation(NULL, cls->methods.a[i]->annotations, _S"abstract"))
                saRemove(&cls->methods, i);
        }
    }

    if (!processClassProps(cls))
        return false;

    // delete redundant interfaces
    pruneInterfaces(cls);

    cls->processed = true;

    return true;
}

bool processClasses()
{
    for (int i = 0; i < saSize(classes); i++) {
        if (!processClass(classes.a[i])) {
            printf("Error processing class '%s'\n", strC(classes.a[i]->name));
            return false;
        }
    }
    return processClassSets();
}

// A class set exists to turn a wire name back into a class, so a class that has no wire name
// cannot be in one. This is checkable only for classes this run has actually seen: a set may
// name a class from a .cxh that was never parsed, which stays a link-time error the way an
// unknown struct in a structset already is.
bool processClassSets()
{
    for (int i = 0; i < saSize(classsets); i++) {
        ClassSetDef* cs = classsets.a[i];
        for (int j = 0; j < saSize(cs->members); j++) {
            Class* cls = NULL;
            if (!htFind(clsidx, string, cs->members.a[j], object, &cls, HT_Borrow))
                continue;
            if (!classIsSerializable(cls)) {
                printf("Error in classset '%s': class '%s' is neither [serialize] nor "
                       "Serializable, so it has no wire name to resolve\n",
                       strC(cs->name),
                       strC(cs->members.a[j]));
                return false;
            }
        }
    }
    return true;
}

void methodImplName(string* out, Class* cls, string mname)
{
    strNConcat(out, cls->name, _S"_", mname);
}

void methodCallName(string* out, Class* cls, string mname)
{
    string clsname2 = 0, mname2 = 0;
    if (cls->methodprefix) {
        strDup(&clsname2, cls->methodprefix);
    } else {
        strDup(&clsname2, cls->name);
        strLower(&clsname2);
    }

    strDup(&mname2, mname);
    uint8* tmp = strBuffer(&mname2, 1);
    tmp[0]     = toupper(tmp[0]);

    strConcatC(out, &clsname2, &mname2);
    strDestroy(&clsname2);
    strDestroy(&mname2);
}

void mixinMemberName(string* out, Class* cls)
{
    strConcat(out, _S"_", cls->name);
    strLower(out);
}

void methodAnnotations(string* out, Method* m)
{
    string tmp = 0;
    strClear(out);

    bool isvalid = getAnnotation(NULL, m->annotations, _S"valid");
    bool isopt   = getAnnotation(NULL, m->annotations, _S"opt");

    if (isvalid || isopt) {
        if (isopt)
            strAppend(out, _S"_Ret_opt_valid_ ");
        else
            strAppend(out, _S"_Ret_valid_ ");
    }

    sa_string sal = saInitNone;
    if (getAnnotation(&sal, m->annotations, _S"sal") && saSize(sal) >= 2) {
        strNConcat(out, *out, sal.a[1], _S" ");
    }

    if (m->isfactory)
        strAppend(out, m->canfail ? _S"_objfactory_check " : _S"_objfactory_guaranteed ");

    if (m->isinit && !m->canfail)
        strAppend(out, _S"_objinit_guaranteed ");

    strDestroy(&tmp);
}

void paramAnnotations(string* out, Param* p)
{
    string tmp = 0;
    strClear(out);

    bool isin  = getAnnotation(NULL, p->annotations, _S"in");
    bool isout = getAnnotation(NULL, p->annotations, _S"out");
    bool isopt = getAnnotation(NULL, p->annotations, _S"opt");
    if (getAnnotation(NULL, p->annotations, _S"inout"))
        isin = isout = true;

    if (isin || isout) {
        if (isin && isout)
            strDup(&tmp, _S"_Inout_");
        else if (isin)
            strDup(&tmp, _S"_In_");
        else if (isout)
            strDup(&tmp, _S"_Out_");

        if (isopt)
            strAppend(&tmp, _S"opt_");

        strNConcat(out, *out, tmp, _S" ");
    } else if (strEmpty(p->predecr) && strEq(p->type, _S"strref")) {
        strAppend(out, _S"_In_opt_ ");
    }

    sa_string sal = saInitNone;
    if (getAnnotation(&sal, p->annotations, _S"sal") && saSize(sal) >= 2) {
        strNConcat(out, *out, sal.a[1], _S" ");
    }

    strDestroy(&tmp);
}

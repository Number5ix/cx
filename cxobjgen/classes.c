#include "cxobjgen.h"
#include <cx/container.h>
#include <cx/string.h>

static void fillMembers(sa_Member *members, Class *cls)
{
    // add parent clases first so the order is correct
    if (cls->parent)
        fillMembers(members, cls->parent);

    for (int i = 0; i < saSize(&cls->members); i++) {
        saPush(members, object, cls->members.a[i]);
    }
}

static void fillMethods(sa_Method *methods, Class *cls)
{
    // first, add in any methods from parent classes and their interfaces
    // allmethods includes inherited methods
    if (cls->parent) {
        for (int i = 0; i < saSize(&cls->parent->allmethods); i++) {
            Method *m = cls->parent->allmethods.a[i];
            if (!m->unbound && !m->internal)
                saPush(methods, object, m, Unique);
        }
    }

    // then add in actual implemented methods, overriding where applicable
    for (int i = 0; i < saSize(&cls->methods); i++) {
        if (!cls->methods.a[i]->unbound && !cls->methods.a[i]->internal) {
            int32 idx = saFind(methods, object, cls->methods.a[i]);
            if (idx == -1) {
                saPush(methods, object, cls->methods.a[i], Unique);
            } else {
                // already exists, replace with child class version
                saRemove(methods, idx);
                saInsert(methods, idx, object, cls->methods.a[i]);
            }
        }
    }
}

static Method *findClassMethod(string name, Class *search, Interface *iface)
{
    if (!search)
        return NULL;

    // ensure the class providing the method actually implements the interface
    if (!iface || saFind(&search->implements, object, iface) != -1) {
        for (int i = 0; i < saSize(&search->methods); i++) {
            if (strEq(name, search->methods.a[i]->name))
                return search->methods.a[i];
        }
    }

    return search->parent ? findClassMethod(name, search->parent, iface) : NULL;
}

static Method *findInterfaceMethod(string name, Class *root, Interface *search)
{
    if (!root)
        return NULL;

    if (search) {
        for (int i = 0; i < saSize(&search->methods); i++) {
            if (strEq(name, search->methods.a[i]->name))
                return search->methods.a[i];
        }
        return search->parent ? findInterfaceMethod(name, root, search->parent) : NULL;
    }

    for (int i = 0; i < saSize(&root->implements); i++) {
        Method *m = findInterfaceMethod(name, root, root->implements.a[i]);
        if (m)
            return m;
    }

    return NULL;
}

static void addInterfaceImpl(Class *cls, Interface *iface)
{
    if (iface->parent)
        addInterfaceImpl(cls, iface->parent);

    for (int i = 0; i < saSize(&iface->methods); i++) {
        Method *m = findClassMethod(iface->methods.a[i]->name, cls, iface->methods.a[i]->srcif);  // already have it?
        if (!m) {
            m = methodClone(iface->methods.a[i]);
            saPushC(&cls->methods, object, &m);
        }
    }
}

static void addAbstractInterfaces(sa_Method *methods, Interface *iface)
{
    if (iface->parent)
        addAbstractInterfaces(methods, iface->parent);

    for (int i = 0; i < saSize(&iface->methods); i++) {
        saPush(methods, object, iface->methods.a[i], Unique);
    }
}

static void checkMemberInitDestroy(Class *cls)
{
    for (int i = 0; i < saSize(&cls->members); i++) {
        Member *m = cls->members.a[i];

        if (strEmpty(m->postdecr)) {
            if (saSize(&m->fulltype) > 1 && !strEq(m->fulltype.a[0], _S"atomic")) {
                // these have enough info to auto init without help
                if (strEmpty(m->predecr) && !strEq(m->fulltype.a[0], _S"object"))
                    m->init = true;
                m->destroy = true;
            }
            if ((strEq(m->vartype, _S"string") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"hashtable") && strEmpty(m->predecr)) ||
                (strEq(m->vartype, _S"stvar") && strEmpty(m->predecr))) {
                m->destroy = true;
            }
        }

        // annotations can override
        sa_string an;
        getAnnotation(&an, &m->annotations, _S"init");
        if (saSize(&an) >= 2) {
            strDup(&m->initstr, an.a[1]);
            m->init = true;
        }
        if (getAnnotation(NULL, &m->annotations, _S"noinit")) {
            m->init = false;
        }
        if (getAnnotation(NULL, &m->annotations, _S"nodestroy")) {
            m->destroy = false;
        }

        // if any member needs to be destroyed, class must have a destructor
        if (m->init)
            cls->hasinit = cls->hasautoinit = true;
        if (m->destroy)
            cls->hasdestroy = cls->hasautodtors = true;
    }
}

static void addMixin(Class *cls, Class *uses)
{
    string hfile = 0;
    // copy any methods that we don't already have
    for (int i = 0; i < saSize(&uses->allmethods); i++) {
        Method *m = findClassMethod(uses->allmethods.a[i]->name, cls, NULL);     // already have it?
        if (!m) {
            // clone the method
            m = methodClone(uses->allmethods.a[i]);
            // so that it can point at the mixin that actually pulled it in
            m->mixinsrc = uses;
            strDup(&hfile, m->srcfile);
            pathSetExt(&hfile, hfile, _S"impl.h");
            strPrepend(_S"\"", &hfile);
            strAppend(&hfile, _S"\"");
            saPushC(&implincludes, string, &hfile, Unique);
            saPushC(&cls->methods, object, &m);
        }
    }
    strDestroy(&hfile);

    // if the mixin or any parent needs init/destructor, so do we
    // the runtime won't handle this since there's no parent->child relationship
    for (Class *mixin = uses; mixin; mixin = mixin->parent) {
        cls->hasautoinit |= mixin->hasautoinit;
        cls->hasinit |= mixin->hasautoinit;
        cls->hasautodtors |= mixin->hasautodtors;
        cls->hasdestroy |= mixin->hasautodtors;

        // any interfaces implemented by the mixin tree need to be copied, too
        for (int i = 0; i < saSize(&mixin->implements); i++)
            saPush(&cls->implements, object, mixin->implements.a[i], Unique);
    }

    // if the mixin class has any data members, embed the mixin struct
    if (saSize(&uses->allmembers) > 0) {
        Member *mixindata = memberCreate();
        mixindata->mixinsrc = uses;
        mixindata->init = true;
        mixindata->destroy = true;
        strDup(&mixindata->vartype, uses->name);
        mixinMemberName(&mixindata->name, uses);
        saPushC(&cls->members, object, &mixindata, Unique);
    }
}

static bool implementsChild(Class *cls, Interface *iface)
{
    for (int i = saSize(&cls->implements) - 1; i >= 0; --i) {
        Interface *testif = cls->implements.a[i];

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

static void pruneInterfaces(Class *cls)
{
    // First, remove any interfaces where this class also implements one of
    // their children.
    for (int i = saSize(&cls->implements) - 1; i >= 0; --i) {
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
    for (int i = saSize(&cls->implements) - 1; i >= 0; --i) {
        int j;

        // don't delete the class interface
        if (cls->implements.a[i]->classif)
            continue;

        for (j = saSize(&cls->methods) - 1; j >= 0; --j) {
            if (cls->methods.a[j]->internal)
                continue;

            // see if this class method is part of the interface
            if (saFind(&cls->implements.a[i]->allmethods, object,
                        cls->methods.a[j]) != -1)
                break;
        }

        // did we find one that's implemented?
        if (j < 0)
            saRemove(&cls->implements, i);          // nope
    }
}

bool processClass(Class *cls)
{
    if (cls->processed)
        return true;

    // process parent classes first
    if (cls->parent)
        processClass(cls->parent);

    sa_string mpfx;
    getAnnotation(&mpfx, &cls->annotations, _S"methodprefix");
    if (saSize(&mpfx) == 2) {
        strDup(&cls->methodprefix, mpfx.a[1]);
    }

    // if our parent class implements any interfaces, we implement them too
    if (cls->parent) {
        for (int i = 0; i < saSize(&cls->parent->implements); i++) {
            saPush(&cls->implements, object, cls->parent->implements.a[i], Unique);
        }
    }

    // copy prototypes for overriden functions from parents
    for (int i = 0; i < saSize(&cls->overrides); i++) {
        Method *m = findClassMethod(cls->overrides.a[i], cls->parent, NULL);
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
        m->srcclass = cls;
        saPushC(&cls->methods, object, &m, Unique);
    }

    // if this is not an abstract class, add any missing interface implementations
    if (!cls->abstract) {
        for (int i = 0; i < saSize(&cls->implements); i++) {
            addInterfaceImpl(cls, cls->implements.a[i]);
        }
    }

    // copy stuff from mixin classes
    for (int i = 0; i < saSize(&cls->uses); i++) {
        processClass(cls->uses.a[i]);
        addMixin(cls, cls->uses.a[i]);
    }

    // create the class interface
    if (!cls->mixin) {
        Interface *clsif = interfaceCreate();
        clsif->classif = true;
        clsif->included = cls->included;
        strDup(&clsif->name, cls->name);
        strAppend(&clsif->name, _S"_ClassIf");
        if (cls->parent) {
            string pname = 0;
            strDup(&pname, cls->parent->name);
            strAppend(&pname, _S"_ClassIf");
            htFind(&ifidx, string, pname, object, &clsif->parent);
            strDestroy(&pname);
        }
        fillMethods(&clsif->methods, cls);
        fillMethods(&clsif->allmethods, cls);
        // abstract classes need to also bring in any methods from interfaces that
        // we are not implementing ourselves
        if (cls->abstract) {
            for (int i = 0; i < saSize(&cls->implements); i++)
                addAbstractInterfaces(&clsif->allmethods, cls->implements.a[i]);
        }
        if (saSize(&clsif->allmethods) > 0) {
            htInsert(&ifidx, string, clsif->name, object, clsif, Ignore);
            saPush(&cls->implements, object, clsif);
            saPush(&ifaces, object, clsif, Unique);
            cls->classif = clsif;
        } else {
            objRelease(clsif);
        }
    } else {
        // mark mixin methods
        for (int i = 0; i < saSize(&cls->methods); i++)
            cls->methods.a[i]->mixin = true;

        if (!cls->included)
            needmixinimpl = true;
    }

    // check for member init/destroy requirements
    checkMemberInitDestroy(cls);

    // create internal methods if needed
    if (cls->hasinit) {
        Method *m = methodCreate();
        m->internal = true;
        m->isinit = true;
        strDup(&m->returntype, _S"bool");
        strDup(&m->name, _S"init");
        saPushC(&cls->methods, object, &m);
    }
    if (cls->hasdestroy) {
        Method *m = methodCreate();
        m->internal = true;
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
        for (int i = 0; i < saSize(&cls->implements); i++)
            addAbstractInterfaces(&cls->allmethods, cls->implements.a[i]);

        // delete any methods that are explictly marked abstract (no implementation)
        for (int i = saSize(&cls->methods) - 1; i >= 0; --i) {
            if (getAnnotation(NULL, &cls->methods.a[i]->annotations, _S"abstract"))
                saRemove(&cls->methods, i);
        }
    }

    // delete redundant interfaces
    pruneInterfaces(cls);

    cls->processed = true;

    return true;
}

bool processClasses()
{
    for (int i = 0; i < saSize(&classes); i++) {
        if (!processClass(classes.a[i])) {
            printf("Error processing class '%s'\n", strC(classes.a[i]->name));
            return false;
        }
    }
    return true;
}

void methodImplName(string *out, Class *cls, string mname)
{
    strNConcat(out, cls->name, _S"_", mname);
}

void methodCallName(string *out, Class *cls, string mname)
{
    string clsname2 = 0, mname2 = 0;
    if (cls->methodprefix) {
        strDup(&clsname2, cls->methodprefix);
    } else {
        strDup(&clsname2, cls->name);
        strLower(&clsname2);
    }

    strDup(&mname2, mname);
    char *tmp = strBuffer(&mname2, 1);
    tmp[0] = toupper(tmp[0]);

    strConcatC(out, &clsname2, &mname2);
    strDestroy(&clsname2);
    strDestroy(&mname2);
}

void mixinMemberName(string *out, Class *cls)
{
    strConcat(out, _S"_", cls->name);
    char *tmp = strBuffer(out, 2);
    tmp[1] = tolower(tmp[1]);
}

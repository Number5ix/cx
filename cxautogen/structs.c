#include <cx/container.h>
#include <cx/string.h>
#include <cx/struct.h>
#include "cxautogen.h"

static void setMemberFlags(Member* m)
{
    if (getAnnotation(NULL, m->annotations, _S"nodestroy")) {
        m->flags |= STRUCT_NoDestroy;
    }

    if (getAnnotation(NULL, m->annotations, _S"nocopy")) {
        m->flags |= STRUCT_NoCopy;
    }

    if (getAnnotation(NULL, m->annotations, _S"noserialize")) {
        m->flags |= STRUCT_NoSerialize;
    }

    if (getAnnotation(NULL, m->annotations, _S"ignore")) {
        m->flags |= STRUCT_Ignore;
    }
}

void memberSerName(string* out, Member* m)
{
    sa_string an;

    if (getAnnotation(&an, m->annotations, _S"serializeas") && saSize(an) >= 2) {
        strTrim(out, an.a[1], NULL);
        if (!strEmpty(*out))
            return;
    }

    strDup(out, m->name);
}

bool checkSerMemberNames(strref owner, sa_Member members)
{
    bool ok = true;
    hashtable seen;
    htInit(&seen, string, bool, saSize(members));

    for (int i = 0; i < saSize(members); i++) {
        Member* m = members.a[i];
        sa_string an;

        if (m->flags & STRUCT_NoSerialize)
            continue;

        if (getAnnotation(&an, m->annotations, _S"serializeas")) {
            string given = 0;
            if (saSize(an) >= 2)
                strTrim(&given, an.a[1], NULL);

            // The name goes into a generated C string literal verbatim, and it is a wire name
            // besides -- neither survives a quote or a backslash.
            if (strEmpty(given) || strFind(given, 0, _S"\"") != -1 ||
                strFind(given, 0, _S"\\") != -1) {
                fprintf(stderr,
                        "Member '%s' of '%s' has an invalid [serializeas] name; it must be "
                        "non-empty and contain no quotes or backslashes\n",
                        strC(m->name),
                        strC(owner));
                ok = false;
            }
            strDestroy(&given);
        }

        string sername = 0;
        memberSerName(&sername, m);
        if (htHasKey(seen, string, sername)) {
            fprintf(stderr,
                    "Member '%s' of '%s' serializes as '%s', which another member already "
                    "uses\n",
                    strC(m->name),
                    strC(owner),
                    strC(sername));
            ok = false;
        }
        htInsert(&seen, string, sername, bool, true);
        strDestroy(&sername);
    }

    htDestroy(&seen);
    return ok;
}

static bool processStruct(StructDef* s)
{
    if (s->processed)
        return true;

    for (int i = 0; i < saSize(s->members); i++) {
        setMemberFlags(s->members.a[i]);
    }

    return checkSerMemberNames(s->name, s->members);
}

bool processStructs()
{
    for (int i = 0; i < saSize(structs); i++) {
        if (!processStruct(structs.a[i])) {
            printf("Error processing struct '%s'\n", strC(structs.a[i]->name));
            return false;
        }
    }
    return true;
}
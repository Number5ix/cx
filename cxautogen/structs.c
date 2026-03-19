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

static bool processStruct(Struct* s)
{
    if (s->processed)
        return true;

    for (int i = 0; i < saSize(s->members); i++) {
        setMemberFlags(s->members.a[i]);
    }

    return true;
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
// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "log/logsnapshot.h"
// clang-format on
// ==================== Auto-generated section ends ======================

_objfactory_guaranteed LogSnapshot* LogSnapshot_create(string text)
{
    LogSnapshot* self;
    self = objInstCreate(LogSnapshot);

    strDup(&self->text, text);

    objInstInit(self);

    return self;
}

bool LogSnapshot_format(_In_ LogSnapshot* self, FMTVar* v, string* out)
{
    // The format options on the placeholder are deliberately ignored: the value was rendered
    // before the template was ever parsed, which is what made it cheap.
    strDup(out, self->text);
    return true;
}

void LogSnapshot_destroy(_In_ LogSnapshot* self)
{
    // Autogen begins -----
    strDestroy(&self->text);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "log/logsnapshot.auto.inc"
// clang-format on
// Autogen ends -------

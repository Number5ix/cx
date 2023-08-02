// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "ssdnode.h"
// ==================== Auto-generated section ends ======================

void SSDNode_destroy(SSDNode *self)
{
    // Autogen begins -----
    objRelease(&self->info);
    // Autogen ends -------
}

bool SSDNode_isHashtable(SSDNode *self)
{
    return false;
}

bool SSDNode_isArray(SSDNode *self)
{
    return false;
}

// Autogen begins -----
#include "ssdnode.auto.inc"
// Autogen ends -------

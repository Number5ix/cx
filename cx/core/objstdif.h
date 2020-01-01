#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>


typedef struct Sortable {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    intptr (*cmp)(void *self, void *other, uint32 flags);
} Sortable;
extern Sortable Sortable_tmpl;

typedef struct Hashable {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    uint32 (*hash)(void *self, uint32 flags);
} Hashable;
extern Hashable Hashable_tmpl;


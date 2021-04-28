#pragma once

#include "cx/obj.h"
#include "cx/container.h"

void _objHydrateIface(ObjIface *ifimpl, sa_ObjIface *impls, hashtable *impltbl);
bool _objCheckIface(ObjIface *iface);

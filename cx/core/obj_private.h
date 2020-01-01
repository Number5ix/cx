#pragma once

#include "cx/obj.h"
#include "cx/container.h"

void _objHydrateIface(ObjIface *ifimpl, ObjIface ***impls, hashtable *impltbl);
bool _objCheckIface(ObjIface *iface);

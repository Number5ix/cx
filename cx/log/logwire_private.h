#pragma once

#include "log_private.h"
#include "logwire.h"

// Interns a field name so a decoded variant can point at it.
//
// stvarkn() pointer-copies the name it is given, so a key read off the wire has to outlive every
// variant that carries it. The table is permanent for the lifetime of the process, exactly like
// the channel registry, and capped so a sender that invents field names without bound cannot grow
// it forever. Returns NULL for an empty key, and for one arriving past the cap.
_Ret_maybenull_z_ const char* logWireInternKey(_In_opt_ strref key);

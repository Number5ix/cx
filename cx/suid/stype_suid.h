#pragma once

#include "cx/cx.h"

intptr stCmp_suid(stype st, stgeneric stgen1, stgeneric stgen2, uint32 flags);
bool stConvert_suid(stype destst, stgeneric *dest, stype srcst, stgeneric src, uint32 flags);

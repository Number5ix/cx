#include "schema.h"
#include "struct.h"

// ---------------------------------------------------------------------------------------
// Canonical schema descriptors for the unparameterized built-ins
// ---------------------------------------------------------------------------------------

#define STRTD_BUILTIN(nm) const StructTypeDetail _strtd_##nm = { .type = &_sti_##nm }

STRTD_BUILTIN(none);
STRTD_BUILTIN(bool);
STRTD_BUILTIN(int8);
STRTD_BUILTIN(int16);
STRTD_BUILTIN(int32);
STRTD_BUILTIN(int64);
STRTD_BUILTIN(uint8);
STRTD_BUILTIN(uint16);
STRTD_BUILTIN(uint32);
STRTD_BUILTIN(uint64);
STRTD_BUILTIN(float32);
STRTD_BUILTIN(float64);
STRTD_BUILTIN(string);
STRTD_BUILTIN(suid);
STRTD_BUILTIN(object);
STRTD_BUILTIN(buffer);
STRTD_BUILTIN(stvar);
STRTD_BUILTIN(sarray);
STRTD_BUILTIN(hashtable);

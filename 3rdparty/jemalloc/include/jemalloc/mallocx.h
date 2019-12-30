#pragma once

#define JEMALLOC_NO_RENAME
#include "jemalloc.h"

// custom demangling of the jemalloc extended API only
#define dallocx(ptr, flags) je_dallocx(ptr, flags)
#define mallctl(name, oldp, oldlenp, newp, newlen) je_mallctl(name, oldp, oldlenp, newp, newlen)
#define mallctlbymib(mib, miblen, oldp, oldlenp, newp, newlen) je_mallctlbymib(mib, miblen, oldp, oldlenp, newp, newlen)
#define mallctlnametomib(name, mibp, miblenp) je_mallctlnametomib(name, mibp, miblenp)
#define mallocx(size, flags) je_mallocx(size, flags)
#define nallocx(size, flags) je_nallocx(size, flags)
#define rallocx(ptr, size, flags) je_rallocx(ptr, size, flags)
#define sallocx(ptr, flags) je_sallocx(ptr, flags)
#define sdallocx(ptr, size, flags) je_sdallocx(ptr, size, flags)
#define xallocx(ptr, size, extra, flags) je_xallocx(ptr, size, extra, flags)

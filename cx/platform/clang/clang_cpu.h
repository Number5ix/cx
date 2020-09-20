#pragma once

#if defined(__aarch64__)
#define _CPU_PAUSE __asm__ volatile("yield")
#else
#define _CPU_PAUSE __asm__ volatile("pause")
#endif

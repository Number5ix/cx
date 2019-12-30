#pragma once

// We don't really like C++, but sometimes it is a necessary evil when
// header files are pulled into unusual places.

#ifdef __cplusplus
#define _EXTERN_C extern "C"
#define _EXTERN_C_BEGIN extern "C"{
#define _EXTERN_C_END   }
#else
#define _EXTERN_C
#define _EXTERN_C_BEGIN
#define _EXTERN_C_END
#endif

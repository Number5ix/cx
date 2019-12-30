#pragma once

// We don't really like C++, but sometimes it is a necessary evil when
// header files are pulled into unusual places.

#ifdef __cplusplus
#define EXTERN_C extern "C"
#define EXTERN_C_BEGIN extern "C"{
#define EXTERN_C_END   }
#else
#define EXTERN_C
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif

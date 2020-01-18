// Kernel semaphores on Windows are type HANDLE, which is the same size as void*
#define KERNEL_SEMA_SIZE sizeof(void*)

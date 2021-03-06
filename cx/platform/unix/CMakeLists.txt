set(CMAKE_EXTRA_INCLUDE_FILES semaphore.h)
check_type_size("sem_t" KERNEL_SEMA_SIZE)
unset(CMAKE_EXTRA_INCLUDE_FILES)
configure_file(unix_ksema.h.in unix_ksema.h)

set(PLATFORM_UNIX_SRCS
    platform/unix/unix_debug_crash.c
    platform/unix/unix_error.c
    platform/unix/unix_fs_file.c
    platform/unix/unix_fs_fs.c
    platform/unix/unix_ksema.c
    platform/unix/unix_os.c
    platform/unix/unix_sys_hostid.c
    platform/unix/unix_thread_thread.c
    platform/unix/unix_time.c
    platform/unix/unix_time_clock.c
    platform/unix/unix_vfs.c)
set(PLATFORM_UNIX_INCLUDES
    platform/unix/unix_hostid.h
    platform/unix/unix_ksema.h
    platform/unix/unix_os.h
    platform/unix/unix_sys_entry.h)

set(PLATFORM_INCLUDES
    ${PLATFORM_INCLUDES}
    platform/unix.h
    PARENT_SCOPE)

set(CX_SRCS
    ${CX_SRCS}
    ${PLATFORM_UNIX_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${PLATFORM_UNIX_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

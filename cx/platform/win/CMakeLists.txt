set(PLATFORM_WIN_SRCS
    platform/win/win_debug_crash.c
    platform/win/win_debug_stacktrace.c
    platform/win/win_error.c
    platform/win/win_fs_file.c
    platform/win/win_fs_fs.c
    platform/win/win_ksema.c
    platform/win/win_os.c
    platform/win/win_sys_entry.c
    platform/win/win_sys_hostid.c
    platform/win/win_thread_thread.c
    platform/win/win_time.c
    platform/win/win_time_clock.c
    platform/win/win_vfs.c)
set(PLATFORM_WIN_INCLUDES
    platform/win/win_fs.h
    platform/win/win_hostid.h
    platform/win/win_ksema.h
    platform/win/win_os.h
    platform/win/win_stacktrace.h
    platform/win/win_sys_entry.h
    platform/win/win_time.h)

set(PLATFORM_INCLUDES
    ${PLATFORM_INCLUDES}
    platform/win.h
    PARENT_SCOPE)

set(CX_SRCS
    ${CX_SRCS}
    ${PLATFORM_WIN_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${PLATFORM_WIN_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

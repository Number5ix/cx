set(CMAKE_EXTRA_INCLUDE_FILES semaphore.h)

# check_type_size doesn't seem to work with the node emulation layer
set(KERNEL_SEMA_SIZE 16)

unset(CMAKE_EXTRA_INCLUDE_FILES)
configure_file(wasm_ksema.h.in wasm_ksema.h)

set(PLATFORM_WASM_SRCS
    platform/wasm/wasm_debug_crash.c
    platform/wasm/wasm_error.c
    platform/wasm/wasm_fs_file.c
    platform/wasm/wasm_fs_fs.c
    platform/wasm/wasm_ksema.c
    platform/wasm/wasm_os.c
    platform/wasm/wasm_sys_hostid.c
    platform/wasm/wasm_thread_thread.c
    platform/wasm/wasm_time.c
    platform/wasm/wasm_time_clock.c
    platform/wasm/wasm_vfs.c)
set(PLATFORM_WASM_INCLUDES
    platform/wasm/wasm_hostid.h
    platform/wasm/wasm_ksema.h
    platform/wasm/wasm_os.h
    platform/wasm/wasm_sys_entry.h)

set(PLATFORM_INCLUDES
    ${PLATFORM_INCLUDES}
    platform/wasm.h
    PARENT_SCOPE)

set(CX_SRCS
    ${CX_SRCS}
    ${PLATFORM_WASM_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${PLATFORM_WASM_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)
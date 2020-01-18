include (CheckTypeSize)
find_package (Threads REQUIRED)

if(CX_PLATFORM_IS_UNIX)
    set(CMAKE_EXTRA_INCLUDE_FILES semaphore.h)
    check_type_size("sem_t" KERNEL_SEMA_SIZE)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    configure_file(platform/unix/unix_ksema.h.in platform/unix/unix_ksema.h)
endif()

set(CONTAINER_SRCS
    container/hashtable.c
    container/sarray.c
    container/stype_hashtable.c
    container/stype_sarray.c)
set(CONTAINER_INCLUDES
    container/hashtable.h
    container/hashtable_private.h
    container/sarray.h
    container/sarray_private.h
    container/stype_hashtable.h
    container/stype_sarray.h)
source_group("container" FILES
    ${CONTAINER_SRCS}
    ${CONTAINER_INCLUDES})

set(CORE_SRCS
    core/error.c
    core/objclass.c
    core/objiface.c
    core/objimpl.c
    core/objstdif.c
    core/stype.c
    core/stype_obj.c
    core/stype_stvar.c
    core/stype_suid.c
    core/suid.c)
set(CORE_INCLUDES
    core/alltypes.inc
    core/cpp.h
    core/error.h
    core/obj_private.h
    core/objclass.h
    core/objiface.h
    core/objimpl.h
    core/objstdif.h
    core/stype.h
    core/stype_obj.h
    core/stype_stvar.h
    core/stype_suid.h
    core/suid.h)
set(CORE_SIDL
    core/objstdif.sidl)
add_sidl_nodep(${CORE_SIDL})
source_group("core" FILES
    ${CORE_SRCS}
    ${CORE_INCLUDES}
    ${CORE_SIDL})

set(DEBUG_SRCS
    debug/assert.c
    debug/blackbox.c
    debug/crash.c
    debug/crashmem.c)
set(DEBUG_INCLUDES
    debug/assert.h
    debug/blackbox.h
    debug/crash.h
    debug/crash_private.h
    debug/stacktrace.h)
source_group("debug" FILES
    ${DEBUG_SRCS}
    ${DEBUG_INCLUDES})

set(FORMAT_SRCS
    format/fmtfloat.c
    format/fmtformat.c
    format/fmtint.c
    format/fmtobject.c
    format/fmtparse.c
    format/fmtptr.c
    format/fmtstring.c
    format/fmtsuid.c
    format/format.c
    format/formattable.c)
set(FORMAT_INCLUDES
    format/format.h
    format/format_private.h
    format/formattable.h
    format/fppowers.h)
set(FORMAT_SIDL
    format/formattable.sidl)
add_sidl(${FORMAT_SIDL})
source_group("format" FILES
    ${FORMAT_SRCS}
    ${FORMAT_INCLUDES}
    ${FORMAT_SIDL})

set(FS_SRCS
    fs/path.c
    fs/pathmatch.c
    fs/vfs.c
    fs/vfsdirsearch.c
    fs/vfsfileops.c
    fs/vfsobj.c
    fs/vfsops.c
    fs/vfsprovider.c
    fs/vfsstruct.c)
set(FS_INCLUDES
    fs/file.h
    fs/fs.h
    fs/fs_private.h
    fs/path.h
    fs/vfs.h
    fs/vfs_private.h
    fs/vfsobj.h
    fs/vfsprovider.h)
set(FS_SIDL
    fs/vfsobj.sidl
    fs/vfsprovider.sidl)
add_sidl(${FS_SIDL})
source_group("fs" FILES
    ${FS_SRCS}
    ${FS_INCLUDES}
    ${FS_SIDL})

SET(FS_VFSFS_SRCS
    fs/vfsfs/vfsfs.c
    fs/vfsfs/vfsfsdirsearch.c
    fs/vfsfs/vfsfsfile.c)
SET(FS_VFSFS_INCLUDES
    fs/vfsfs/vfsfs.h
    fs/vfsfs/vfsfsdirsearch.h
    fs/vfsfs/vfsfsfile.h)
set(FS_VFSFS_SIDL
    fs/vfsfs/vfsfs.sidl
    fs/vfsfs/vfsfsdirsearch.sidl
    fs/vfsfs/vfsfsfile.sidl)
add_sidl(${FS_VFSFS_SIDL})
source_group("fs\\vfsfs" FILES
    ${FS_VFSFS_SRCS}
    ${FS_VFSFS_INCLUDES}
    ${FS_VFSFS_SIDL})

set(FS_VFSVFS_SRCS
    fs/vfsvfs/vfsvfs.c
    fs/vfsvfs/vfsvfsdirsearch.c
    fs/vfsvfs/vfsvfsfile.c)
set(FS_VFSVFS_INCLUDES
    fs/vfsvfs/vfsvfs.h
    fs/vfsvfs/vfsvfsdirsearch.h
    fs/vfsvfs/vfsvfsfile.h)
set(FS_VFSVFS_SIDL
    fs/vfsvfs/vfsvfs.sidl
    fs/vfsvfs/vfsvfsdirsearch.sidl
    fs/vfsvfs/vfsvfsfile.sidl)
add_sidl(${FS_VFSVFS_SIDL})
source_group("fs\\vfsvfs" FILES
    ${FS_VFSVFS_SRCS}
    ${FS_VFSVFS_INCLUDES}
    ${FS_VFSVFS_SIDL})

set(MATH_SRCS
    math/pcg.c)
set(MATH_INCLUDES
    math/pcg.h)
source_group("math" FILES
    ${MATH_SRCS}
    ${MATH_INCLUDES})

set(PLATFORM_INCLUDES
    platform/base.h
    platform/cpu.h
    platform/ksema.h
    platform/os.h)

if(CX_COMPILER_IS_CLANG)
    set(PLATFORM_COMPILER_INCLUDES
        platform/clang/clang_atomic.h
        platform/clang/clang_cpu.h)
    source_group("platform\\clang" FILES
        ${PLATFORM_COMPILER_INCLUDES})
elseif(CX_COMPILER_IS_MSVC)
    set(PLATFORM_COMPILER_INCLUDES
        platform/msvc/msvc_atomic.h
        platform/msvc/msvc_cpu.h)
    source_group("platform\\msvc" FILES
        ${PLATFORM_COMPILER_INCLUDES})
endif()

if(CX_PLATFORM_IS_UNIX)
    set(PLATFORM_OS_SRCS
	platform/unix/unix_debug_crash.c
	platform/unix/unix_error.c
	platform/unix/unix_fs_file.c
	platform/unix/unix_fs_fs.c
        platform/unix/unix_ksema.c
        platform/unix/unix_os.c
	platform/unix/unix_sys_hostid.c
	platform/unix/unix_thread_rwlock.c
	platform/unix/unix_thread_thread.c
	platform/unix/unix_time_clock.c
	platform/unix/unix_vfs.c)
    set(PLATFORM_OS_INCLUDES
	platform/unix/unix_hostid.h
        platform/unix/unix_ksema.h
	platform/unix/unix_os.h
	platform/unix/unix_sys_entry.h)
    set(PLATFORM_INCLUDES
	${PLATFORM_INCLUDES}
	platform/unix.h)
    source_group("platform\\unix" FILES
        ${PLATFORM_OS_SRCS}
        ${PLATFORM_OS_INCLUDES})
elseif(CX_PLATFORM_IS_WINDOWS)
    set(PLATFORM_OS_SRCS
	platform/win/win_debug_crash.c
	platform/win/win_debug_stacktrace.c
	platform/win/win_error.c
	platform/win/win_fs_file.c
	platform/win/win_fs_fs.c
        platform/win/win_ksema.c
	platform/win/win_os.c
	platform/win/win_sys_entry.c
	platform/win/win_sys_hostid.c
	platform/win/win_thread_rwlock.c
	platform/win/win_thread_thread.c
	platform/win/win_time_clock.c
	platform/win/win_vfs.c)
    set(PLATFORM_OS_INCLUDES
	platform/win/win_fs.h
	platform/win/win_hostid.h
        platform/win/win_ksema.h
	platform/win/win_os.h
	platform/win/win_stacktrace.h
	platform/win/win_sys_entry.h
	platform/win/win_time.h)
    set(PLATFORM_INCLUDES
	${PLATFORM_INCLUDES}
	platform/win.h)
    source_group("platform\\win" FILES
        ${PLATFORM_OS_SRCS}
        ${PLATFORM_OS_INCLUDES})
endif()
source_group("platform" FILES
    ${PLATFORM_SRCS}
    ${PLATFORM_INCLUDES})

set(STRING_SRCS
    string/strbase.c
    string/strbase64.c
    string/strcase.c
    string/strcompare.c
    string/strconcat.c
    string/strencoding.c
    string/strfind.c
    string/striter.c
    string/strjoin.c
    string/strnum_fp.c
    string/strnum_int.c
    string/strrope.c
    string/strsplit.c
    string/strsubstr.c
    string/stype_string.c)
set(STRING_INCLUDES
    string/strbase.h
    string/strcompare.h
    string/strencoding.h
    string/strfind.h
    string/string_private.h
    string/string_private_utf8.h
    string/striter.h
    string/strmanip.h
    string/strnum.h
    string/strtest.h
    string/stype_string.h)
source_group("string" FILES
    ${STRING_SRCS}
    ${STRING_INCLUDES})

set(SYS_SRCS
    sys/entry.c
    sys/hostid.c)
set(SYS_INCLUDES
    sys/entry.h
    sys/hostid.h
    sys/hostid_private.h)
source_group("sys" FILES
    ${SYS_SRCS}
    ${SYS_INCLUDES})

set(THREAD_SRCS
    thread/atomic.c
    thread/mutex.c
    thread/sema.c
    thread/thread.c)
set(THREAD_INCLUDES
    thread/atomic.h
    thread/mutex.h
    thread/rwlock.h
    thread/sema.h
    thread/thread.h
    thread/thread_private.h)
source_group("thread" FILES
    ${THREAD_SRCS}
    ${THREAD_INCLUDES})

set(TIME_INCLUDES
    time/clock.h
    time/time.h)
source_group("time" FILES
    ${TIME_INCLUDES})

set(UTILS_SRCS
    utils/cbhandle.c
    utils/lazyinit.c
    utils/murmur.c
    utils/scratch.c)
set(UTILS_INCLUDES
    utils/cbhandle.h
    utils/compare.h
    utils/lazyinit.h
    utils/macros.h
    utils/murmur.h
    utils/scratch.h)
source_group("utils" FILES
    ${UTILS_SRCS}
    ${UTILS_INCLUDES})

set(CX_INCLUDES
    common.h
    container.h
    cx.h
    debug.h
    fs.h
    math.h
    obj.h
    platform.h
    static_config.h
    string.h
    sys.h
    thread.h
    time.h
    utils.h)
source_group("include" FILES
    ${CX_INCLUDES})

add_library(cx STATIC
    ${CONTAINER_SRCS}
    ${CONTAINER_INCLUDES}
    ${CORE_SRCS}
    ${CORE_INCLUDES}
    ${CORE_SIDL}
    ${DEBUG_SRCS}
    ${DEBUG_INCLUDES}
    ${FORMAT_SRCS}
    ${FORMAT_INCLUDES}
    ${FORMAT_SIDL}
    ${FS_SRCS}
    ${FS_INCLUDES}
    ${FS_SIDL}
    ${FS_VFSFS_SRCS}
    ${FS_VFSFS_INCLUDES}
    ${FS_VFSFS_SIDL}
    ${FS_VFSVFS_SRCS}
    ${FS_VFSVFS_INCLUDES}
    ${FS_VFSVFS_SIDL}
    ${MATH_SRCS}
    ${MATH_INCLUDES}
    ${PLATFORM_SRCS}
    ${PLATFORM_INCLUDES}
    ${PLATFORM_COMPILER_INCLUDES}
    ${PLATFORM_OS_SRCS}
    ${PLATFORM_OS_INCLUDES}
    ${STRING_SRCS}
    ${STRING_INCLUDES}
    ${SYS_SRCS}
    ${SYS_INCLUDES}
    ${THREAD_SRCS}
    ${THREAD_INCLUDES}
    ${TIME_INCLUDES}
    ${UTILS_SRCS}
    ${UTILS_INCLUDES}
    ${CX_INCLUDES})

target_link_libraries(cx ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} cxmem mbedtls)
target_include_directories(cx SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
target_compile_definitions(cx PRIVATE UNICODE _UNICODE)

target_compile_definitions(cx PUBLIC
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<OR:$<CONFIG:Dev>,$<CONFIG:Debug>>:DEV_BUILD>
    $<$<CONFIG:Release>:RELEASE_BUILD>
    $<$<CONFIG:Debug>:DEBUG_LEVEL=2>
    $<$<CONFIG:Dev>:DEBUG_LEVEL=1>
    $<$<CONFIG:Release>:DEBUG_LEVEL=0>)

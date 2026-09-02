include (CheckTypeSize)
include (CheckCSourceCompiles)
find_package (Threads REQUIRED)

include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)
include (${CX_TOP_SOURCE_DIR}/cmake/buildtype.cmake)

add_subdirectory(buffer)
add_subdirectory(closure)
add_subdirectory(console)
add_subdirectory(container)
add_subdirectory(debug)
add_subdirectory(digest)
add_subdirectory(format)
add_subdirectory(fs)
add_subdirectory(log)
add_subdirectory(math)
add_subdirectory(meta)
add_subdirectory(net)
add_subdirectory(obj)
add_subdirectory(parse)
add_subdirectory(platform)
add_subdirectory(serialize)
add_subdirectory(settings)
add_subdirectory(ssdtree)
add_subdirectory(string)
add_subdirectory(struct)
add_subdirectory(stype)
add_subdirectory(suid)
add_subdirectory(sys)
add_subdirectory(taskqueue)
add_subdirectory(thread)
add_subdirectory(time)
add_subdirectory(utils)
add_subdirectory(xalloc)

set(CX_BASE_INCLUDES
    include/cx/buffer.h
    include/cx/closure.h
    include/cx/common.h
    include/cx/container.h
    include/cx/cx.h
    include/cx/debug.h
    include/cx/fs.h
    include/cx/json.h
    include/cx/math.h
    include/cx/meta.h
    include/cx/net.h
    include/cx/obj.h
    include/cx/parse.h
    include/cx/platform.h
    include/cx/serialize.h
    include/cx/ssdtree.h
    include/cx/static_config.h
    include/cx/string.h
    include/cx/sys.h
    include/cx/taskqueue.h
    include/cx/thread.h
    include/cx/time.h
    include/cx/utils.h
    include/cx/xalloc.h)

if (CX_COMPILER_IS_MSVC)
    set(CX_NATVIS_FILES
        cx.natvis)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${CX_BASE_INCLUDES}
    ${CX_SRCS}
    ${CX_AUTOGEN}
    ${CX_INCLUDES}
    ${CX_NATVIS_FILES}
    ${CX_CMAKE_FILES})

add_cxautogen(${CX_AUTOGEN})

add_library(cx STATIC
    ${CX_SRCS}
    ${CX_AUTOGEN}
    ${CX_INCLUDES}
    ${CX_BASE_INCLUDES}
    ${CX_NATVIS_FILES}
    ${CX_CMAKE_FILES})

target_link_libraries(cx ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})

# gcc/clang lower 8-byte atomic ops to __atomic builtins that are inline on most
# targets, but on some (notably 32-bit x86 without -mcx16-style assumptions) the
# compiler emits calls into libatomic instead. Only 32-bit is expected to need this.
if ((CX_COMPILER_IS_GNU OR CX_COMPILER_IS_CLANG) AND NOT CX_PLATFORM_IS_WASM)
    set(CX_ATOMIC64_TEST_SRC [[
#include <stdint.h>
int main(void) {
    long long v = 0;
    long long r = __atomic_load_n(&v, __ATOMIC_SEQ_CST);
    __atomic_store_n(&v, r + 1, __ATOMIC_SEQ_CST);
    long long e = 0;
    __atomic_compare_exchange_n(&v, &e, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return (int)__atomic_fetch_add(&v, 1, __ATOMIC_SEQ_CST);
}
]])
    check_c_source_compiles("${CX_ATOMIC64_TEST_SRC}" CX_HAVE_ATOMIC64_BUILTIN)
    if (NOT CX_HAVE_ATOMIC64_BUILTIN)
        set(CMAKE_REQUIRED_LIBRARIES atomic)
        check_c_source_compiles("${CX_ATOMIC64_TEST_SRC}" CX_HAVE_ATOMIC64_BUILTIN_WITH_LIBATOMIC)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if (NOT CX_HAVE_ATOMIC64_BUILTIN_WITH_LIBATOMIC)
            message(FATAL_ERROR "8-byte __atomic builtins are unavailable, even linked against libatomic; int64/uint64 atomics cannot be built on this target")
        endif()
        target_link_libraries(cx atomic)
    endif()
endif()

target_include_directories(cx SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_SOURCE_DIR}/cx/include ${CX_TOP_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(cx PRIVATE UNICODE _UNICODE)
if (CX_PLATFORM_IS_UNIX OR CX_PLATFORM_IS_WASM)
    # 64-bit off_t even in 32-bit builds. This changes libc struct layouts, so it has to be set
    # for every translation unit rather than at the top of the one that opens files.
    target_compile_definitions(cx PRIVATE _FILE_OFFSET_BITS=64)
endif()
if (CX_COMPILER_IS_MSVC)
    target_link_options(cx PUBLIC /NATVIS:${CMAKE_CURRENT_SOURCE_DIR}/cx.natvis)
endif()

if (CX_MIMALLOC)
    target_link_libraries(cx mimalloc)
else()
    target_compile_definitions(cx PRIVATE XALLOC_USE_SYSTEM_MALLOC)
endif()

if (CX_XP_COMPAT)
    target_compile_definitions(cx PRIVATE CX_XP_COMPAT)
endif()

target_compile_definitions(cx PUBLIC
    $<${isdebug}:DEBUG_BUILD>
    $<${isdevordebug}:DEV_BUILD>
    $<${isrelease}:RELEASE_BUILD>
    $<${isdebug}:DEBUG_LEVEL=2>
    $<${isdev}:DEBUG_LEVEL=1>
    $<${isrelease}:DEBUG_LEVEL=0>)

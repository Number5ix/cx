include (CheckTypeSize)
find_package (Threads REQUIRED)

add_subdirectory(container)
add_subdirectory(debug)
add_subdirectory(format)
add_subdirectory(fs)
add_subdirectory(log)
add_subdirectory(math)
add_subdirectory(meta)
add_subdirectory(obj)
add_subdirectory(platform)
add_subdirectory(serialize)
add_subdirectory(ssdtree)
add_subdirectory(string)
add_subdirectory(stype)
add_subdirectory(suid)
add_subdirectory(sys)
add_subdirectory(thread)
add_subdirectory(time)
add_subdirectory(utils)
add_subdirectory(xalloc)

set(CX_BASE_INCLUDES
    common.h
    container.h
    cx.h
    debug.h
    fs.h
    json.h
    math.h
    meta.h
    obj.h
    platform.h
    serialize.h
    ssdtree.h
    static_config.h
    string.h
    sys.h
    thread.h
    time.h
    utils.h
    xalloc.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${CX_BASE_INCLUDES}
    ${CX_SRCS}
    ${CX_SIDL}
    ${CX_SIDL_NODEP}
    ${CX_INCLUDES}
    ${CX_CMAKE_FILES})

add_sidl(${CX_SIDL})
add_sidl_nodep(${CX_SIDL_NODEP})

add_library(cx STATIC
    ${CX_SRCS}
    ${CX_SIDL}
    ${CX_SIDL_NODEP}
    ${CX_INCLUDES}
    ${CX_BASE_INCLUDES}
    ${CX_CMAKE_FILES})

target_link_libraries(cx ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} mimalloc mbedtls mbedx509 mbedcrypto)
target_include_directories(cx SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
target_compile_definitions(cx PRIVATE UNICODE _UNICODE)

target_compile_definitions(cx PUBLIC
    $<$<CONFIG:debug>:DEBUG_BUILD>
    $<$<OR:$<CONFIG:dev>,$<CONFIG:debug>>:DEV_BUILD>
    $<$<CONFIG:release>:RELEASE_BUILD>
    $<$<CONFIG:debug>:DEBUG_LEVEL=2>
    $<$<CONFIG:dev>:DEBUG_LEVEL=1>
    $<$<CONFIG:release>:DEBUG_LEVEL=0>)

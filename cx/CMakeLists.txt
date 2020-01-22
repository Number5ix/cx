include (CheckTypeSize)
find_package (Threads REQUIRED)

add_subdirectory(container)
add_subdirectory(core)
add_subdirectory(debug)
add_subdirectory(format)
add_subdirectory(fs)
add_subdirectory(math)
add_subdirectory(platform)
add_subdirectory(string)
add_subdirectory(sys)
add_subdirectory(thread)
add_subdirectory(time)
add_subdirectory(utils)

set(CX_BASE_INCLUDES
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
    ${CX_BASE_INCLUDES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${CX_SRCS}
    ${CX_SIDL}
    ${CX_INCLUDES}
    ${CX_CMAKE_FILES})

add_library(cx STATIC
    ${CX_SRCS}
    ${CX_SIDL}
    ${CX_INCLUDES}
    ${CX_BASE_INCLUDES}
    ${CX_CMAKE_FILES})

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

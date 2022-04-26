set(CSTRUTIL_SRCS
    cstrutil/cstrutil.c
    cstrutil/cstrutil.h)
source_group("cstrutil" FILES ${CSTRUTIL_SRCS})

set(XALLOC_SRCS
    xalloc/xalloc.h)

if (${CX_ALLOCATOR} STREQUAL "jemalloc")
    set(XALLOC_USE_JEMALLOC 1)
    set(XALLOC_SRCS
        ${XALLOC_SRCS}
        xalloc/xalloc_jemalloc.c
        xalloc/xalloc_jemalloc.h)
elseif(${CX_ALLOCATOR} STREQUAL "mimalloc")
    set(XALLOC_USE_MIMALLOC 1)
    set(XALLOC_SRCS
        ${XALLOC_SRCS}
        xalloc/xalloc_mimalloc.c
        xalloc/xalloc_mimalloc.h)
elseif (${CX_ALLOCATOR} STREQUAL "msvcrt")
    set(XALLOC_USE_MSVCRT 1)
    set(XALLOC_SRCS
        ${XALLOC_SRCS}
        xalloc/xalloc_msvcrt.c
        xalloc/xalloc_msvcrt.h)
elseif (${CX_ALLOCATOR} STREQUAL "emmalloc")
    set(XALLOC_USE_EMMALLOC 1)
    set(XALLOC_SRCS
        ${XALLOC_SRCS}
        xalloc/xalloc_emmalloc.c
        xalloc/xalloc_emmalloc.h)
else()
    message(FATAL_ERROR "No allocator selected")
endif()

source_group("xalloc" FILES ${XALLOC_SRCS})

add_library (cxmem STATIC
    ${CSTRUTIL_SRCS}
    ${XALLOC_SRCS})

configure_file(config.h.in config.h)

if (${CX_ALLOCATOR} STREQUAL "jemalloc" AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR} ${CX_TOP_SOURCE_DIR}/3rdparty/jemalloc/include/jemalloc ${CX_TOP_BINARY_DIR}/3rdparty/jemalloc/include/jemalloc)
    target_link_libraries(cxmem jemalloc)
elseif(${CX_ALLOCATOR} STREQUAL "mimalloc")
    target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
    target_link_libraries(cxmem mimalloc-static)
else()
    target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
endif()

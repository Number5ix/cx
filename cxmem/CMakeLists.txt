set(CSTRUTIL_SRCS
    cstrutil/cstrutil.c
    cstrutil/cstrutil.h)
source_group("cstrutil" FILES ${CSTRUTIL_SRCS})

set(XALLOC_SRCS
    xalloc/xalloc_jemalloc.c
    xalloc/xalloc_msvcrt.c
    xalloc/xalloc.h
    xalloc/xalloc_jemalloc.h
    xalloc/xalloc_msvcrt.h)
source_group("xalloc" FILES ${XALLOC_SRCS})

add_library (cxmem STATIC
    ${CSTRUTIL_SRCS}
    ${XALLOC_SRCS})

target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_SOURCE_DIR}/3rdparty/jemalloc/include/jemalloc ${CX_TOP_BINARY_DIR}/3rdparty/jemalloc/include/jemalloc)
target_link_libraries(cxmem jemalloc)

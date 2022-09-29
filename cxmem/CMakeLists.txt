set(CSTRUTIL_SRCS
    cstrutil/cstrutil.c
    cstrutil/cstrutil.h)
source_group("cstrutil" FILES ${CSTRUTIL_SRCS})

set(XALLOC_SRCS
    xalloc/xalloc.h)

set(XALLOC_SRCS
    ${XALLOC_SRCS}
    xalloc/xalloc_mimalloc.c
    xalloc/xalloc_mimalloc.h)

source_group("xalloc" FILES ${XALLOC_SRCS})

add_library (cxmem STATIC
    ${CSTRUTIL_SRCS}
    ${XALLOC_SRCS})

target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
target_link_libraries(cxmem mimalloc-static)
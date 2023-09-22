set(CSTRUTIL_SRCS
    cstrutil/cstrutil.c
    cstrutil/cstrutil.h)
source_group("cstrutil" FILES ${CSTRUTIL_SRCS})

set(XALLOC_SRCS
    xalloc_core/xalloc_core.c
    xalloc_core/xalloc_core.h
    xalloc_core/xalloc_core_private.h)

source_group("xalloc_core" FILES ${XALLOC_SRCS})

set(CXMEM_SRCS
    cxmem.h)

source_group("" FILES ${CXMEM_SRCS})

add_library (cxmem OBJECT
    ${CXMEM_SRCS}
    ${CSTRUTIL_SRCS}
    ${XALLOC_SRCS})

target_include_directories(cxmem SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})
target_link_libraries(cxmem mimalloc)
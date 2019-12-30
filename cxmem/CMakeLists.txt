set(CXMEM_SRCS
    cstrutil/cstrutil.c
    xalloc/xalloc_jemalloc.c
    xalloc/xalloc_msvcrt.c)

set(CXMEM_INCLUDES
    cstrutil/cstrutil.h
    xalloc/xalloc.h
    xalloc/xalloc_jemalloc.h
    xalloc/xalloc_msvcrt.h)

add_library (cxmem STATIC
    ${CXMEM_SRCS}
    ${CXMEM_INCLUDES})

target_include_directories(cxmem PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_SOURCE_DIR}/3rdparty/jemalloc/include/jemalloc ${CX_TOP_BINARY_DIR}/3rdparty/jemalloc/include/jemalloc)
target_link_libraries(cxmem jemalloc)

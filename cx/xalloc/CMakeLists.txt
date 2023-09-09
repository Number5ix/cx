set(XALLOC_SRCS
    xalloc/xalloc.c)
set(XALLOC_INCLUDES
    xalloc/xalloc.h)

set(CX_SRCS
    ${CX_SRCS}
    ${XALLOC_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${XALLOC_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

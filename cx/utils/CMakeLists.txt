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
    utils/refcount.h
    utils/scratch.h)

set(CX_SRCS
    ${CX_SRCS}
    ${UTILS_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${UTILS_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

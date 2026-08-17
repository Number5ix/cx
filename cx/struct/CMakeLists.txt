set(STRUCTSRCS
    struct/struct.c
    struct/stype_struct.c)
set(STRUCTINCLUDES
    struct/struct.h
    struct/stype_struct.h)

set(CX_SRCS
    ${CX_SRCS}
    ${STRUCTSRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${STRUCTINCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

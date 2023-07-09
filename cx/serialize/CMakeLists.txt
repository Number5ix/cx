set(SERIALIZE_SRCS
    serialize/sbfile.c
    serialize/sbstring.c
    serialize/streambuf.c)
set(SERIALIZE_INCLUDES
    serialize/sbfile.h
    serialize/sbstring.h
    serialize/streambuf.h)

set(CX_SRCS
    ${CX_SRCS}
    ${SERIALIZE_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${SERIALIZE_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

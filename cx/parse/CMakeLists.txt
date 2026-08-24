set(PARSE_SRCS
    parse/parsebind.c
    parse/parsecompile.c
    parse/parsematch.c
    parse/parsable.c)
set(PARSE_INCLUDES
    parse/parse.h
    parse/parse_private.h)
set(PARSE_AUTOGEN
    parse/parsable.cxh)

set(CX_SRCS
    ${CX_SRCS}
    ${PARSE_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${PARSE_INCLUDES}
    PARENT_SCOPE)
set(CX_AUTOGEN
    ${CX_AUTOGEN}
    ${PARSE_AUTOGEN}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

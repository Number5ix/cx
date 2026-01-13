set(BUFFER_SRCS
    buffer/bufchain.c)
set(BUFFER_INCLUDES
    buffer/bufchain.h)

set(CX_SRCS
    ${CX_SRCS}
    ${BUFFER_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${BUFFER_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

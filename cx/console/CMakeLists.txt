set(CONSOLE_SRCS
    console/concaps.c
    console/concursor.c
    console/conin.c
    console/console.c
    console/constyle.c
    console/conout.c)
set(CONSOLE_INCLUDES
    console/concursor.h
    console/conin.h
    console/console.h
    console/console_private.h
    console/constyle.h)

set(CX_SRCS
    ${CX_SRCS}
    ${CONSOLE_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${CONSOLE_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

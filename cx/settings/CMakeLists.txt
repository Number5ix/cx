set(SETTINGS_SRCS
    settings/settings_thread.c
    settings/settings_write.c
    settings/settings.c
    settings/settingshashnode.c
    settings/settingstree.c)
set(SETTINGS_INCLUDES
    settings/settings_private.h
    settings/settings.h
    settings/settingshashnode.h
    settings/settingstree.h)
set(SETTINGS_SIDL
    settings/settingshashnode.sidl
    settings/settingstree.sidl)

set(CX_SRCS
    ${CX_SRCS}
    ${SETTINGS_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${SETTINGS_INCLUDES}
    PARENT_SCOPE)
set(CX_SIDL
    ${CX_SIDL}
    ${SETTINGS_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

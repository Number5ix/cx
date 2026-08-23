if(CX_STANDALONE)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        htbench.c)

    add_executable(htbench htbench.c)
    set(SQRT_LIBS "")
    include(CheckLibraryExists)
    check_library_exists(m sqrt "" LIBM)
    if (LIBM)
        target_link_libraries(htbench m)
    endif()
    target_link_libraries(htbench cx)
    set_property(TARGET htbench PROPERTY FOLDER "tests")

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        condemo.c)

    add_executable(condemo condemo.c)
    target_link_libraries(condemo cx)
    set_property(TARGET condemo PROPERTY FOLDER "tests")
endif()

include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

if(CX_STANDALONE AND CX_TLS)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        tlsdemo.c)

    add_executable(tlsdemo tlsdemo.c)
    target_link_libraries(tlsdemo cxtls)
    set_property(TARGET tlsdemo PROPERTY FOLDER "tests")
endif()

if(CX_STANDALONE AND CX_HTTP)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        httpdemo.c)

    add_executable(httpdemo httpdemo.c)
    target_link_libraries(httpdemo cxhttp)
    set_property(TARGET httpdemo PROPERTY FOLDER "tests")
endif()

set(PRESETS_MAKER_SRCS
    presetsmaker.c
    )

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${PRESETS_MAKER_SRCS})

add_executable(presetsmaker ${PRESETS_MAKER_SRCS})
target_link_libraries(presetsmaker cx)
set_property(TARGET presetsmaker PROPERTY FOLDER "utils")

if(CX_LUA)
   if(NOT CMAKE_CROSSCOMPILING)
       add_executable(luacmd luacmd.c)
       target_link_libraries(luacmd cxlua)
       set_property(TARGET luacmd PROPERTY FOLDER "utils")
       export(TARGETS luacmd FILE "${CMAKE_BINARY_DIR}/luacmd-config.cmake")
   else()
       find_package(luacmd REQUIRED GLOBAL)
   endif()
endif()

if(CX_STANDALONE)
    add_custom_target(cx_cmake_presets_regenerate
        COMMAND presetsmaker ${CX_TOP_SOURCE_DIR}/CMakePresets.in.json ${CX_TOP_SOURCE_DIR}/CMakePresets.json
        DEPENDS presetsmaker)
    set_property(TARGET cx_cmake_presets_regenerate PROPERTY FOLDER "generate")
endif()

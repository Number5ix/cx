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

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        logfwddemo.c)

    # cx/net is part of core cx, so this needs no build option of its own -- unlike tlsdemo and
    # httpdemo, which depend on components that can be switched off.
    add_executable(logfwddemo logfwddemo.c)
    target_link_libraries(logfwddemo cx)
    set_property(TARGET logfwddemo PROPERTY FOLDER "tests")
endif()

include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

if(CX_STANDALONE AND CX_TLS)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        tlsdemo.c)

    add_executable(tlsdemo tlsdemo.c)
    target_link_libraries(tlsdemo cxtls)
    set_property(TARGET tlsdemo PROPERTY FOLDER "tests")
endif()

if(CX_STANDALONE AND CX_QUIC)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        quicinterop.c)

    add_executable(quicinterop quicinterop.c)
    target_link_libraries(quicinterop cxquic)
    set_property(TARGET quicinterop PROPERTY FOLDER "tests")
endif()

if(CX_STANDALONE AND CX_HTTP)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        httpdemo.c
        httpsrvdemo.c)

    add_executable(httpdemo httpdemo.c)
    target_link_libraries(httpdemo cxhttp)
    set_property(TARGET httpdemo PROPERTY FOLDER "tests")

    add_executable(httpsrvdemo httpsrvdemo.c)
    target_link_libraries(httpsrvdemo cxhttp)
    set_property(TARGET httpsrvdemo PROPERTY FOLDER "tests")
endif()

if(CX_STANDALONE AND CX_UI)
    set(CXUIGALLERY_SRCS
        cxuigallery/gallery.h
        cxuigallery/main.c
        cxuigallery/page_canvas.c
        cxuigallery/page_desktop.c
        cxuigallery/page_input.c
        cxuigallery/page_drawn.c
        cxuigallery/page_grid.c
        cxuigallery/page_layout.c
        cxuigallery/page_tabs.c
        cxuigallery/page_modals.c
        cxuigallery/page_native.c
        cxuigallery/page_values.c
        cxuigallery/page_text.c
        cxuigallery/page_windows.c)
    if(CX_PLATFORM_IS_WINDOWS)
        list(APPEND CXUIGALLERY_SRCS cxuigallery/cxuigallery.rc)
    endif()
    if(CX_PLATFORM_IS_MACOS)
        enable_language(OBJC)
        list(APPEND CXUIGALLERY_SRCS cxuigallery/render_metal.m)
        set_source_files_properties(cxuigallery/render_metal.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CXUIGALLERY_SRCS})

    if(CX_LUA)
        list(APPEND CXUIGALLERY_SRCS cxuigallery/page_lua.c)
    endif()
    add_executable(cxuigallery ${CXUIGALLERY_SRCS})
    target_link_libraries(cxuigallery cxui)
    if(CX_PLATFORM_IS_MACOS)
        target_link_libraries(cxuigallery "-framework Metal")
    endif()
    if(CX_LUA)
        target_link_libraries(cxuigallery cxuilua)
        target_compile_definitions(cxuigallery PRIVATE CXUIGALLERY_LUA=1)
    endif()
    set_property(TARGET cxuigallery PROPERTY FOLDER "tests")

    add_executable(cxuidrawbench cxuidrawbench.c)
    target_link_libraries(cxuidrawbench cxui)
    set_property(TARGET cxuidrawbench PROPERTY FOLDER "tests")

    add_executable(cxuigridbench cxuigridbench.c)
    target_link_libraries(cxuigridbench cxui)
    if (CX_MIMALLOC)
        target_compile_definitions(cxuigridbench PRIVATE GRIDBENCH_MIMALLOC)
    endif()
    set_property(TARGET cxuigridbench PROPERTY FOLDER "tests")

    add_executable(cxuirowview cxuirowview.c)
    target_link_libraries(cxuirowview cxui)
    set_property(TARGET cxuirowview PROPERTY FOLDER "tests")
endif()

# The HTTP/3 interop peer, which quicinterop is not: that one links cxquic and tests the transport,
# this one links cxhttp and only exists in a build that has HTTP/3 in it.
if(CX_STANDALONE AND CX_HTTP3)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
        h3interop.c)

    add_executable(h3interop h3interop.c)
    target_link_libraries(h3interop cxhttp cxquic)
    set_property(TARGET h3interop PROPERTY FOLDER "tests")
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

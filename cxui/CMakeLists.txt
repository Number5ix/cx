include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

add_cxautogen(core/cxuipalette.cxh core/cxuiwidget.cxh core/cxuibox.cxh core/cxuiwindow.cxh core/cxuicanvas.cxh core/cxuisash.cxh core/cxuicontainers.cxh core/cxuifont.cxh core/cxuiimage.cxh core/cxuitimer.cxh core/cxuidrawn.cxh core/cxuitext.cxh core/cxuicontrols.cxh core/cxuiprogress.cxh core/cxuigrid.cxh core/cxuigridstore.cxh core/cxuitreestore.cxh core/cxuimenu.cxh platform/null/cxuitestwidget.cxh)

add_library(cxui STATIC
    include/cxui.h
    include/cxui/canvas.h
    include/cxui/cxui_shared.h
    include/cxui/desktop.h
    include/cxui/dialog.h
    include/cxui/draw.h
    include/cxui/drawn.h
    include/cxui/entry.h
    include/cxui/font.h
    include/cxui/grid.h
    include/cxui/image.h
    include/cxui/events.h
    include/cxui/keys.h
    include/cxui/layout.h
    include/cxui/menu.h
    include/cxui/runtime.h
    include/cxui/text.h
    include/cxui/controls.h
    include/cxui/theme.h
    include/cxui/timer.h
    include/cxui/widget.h
    include/cxui/window.h
    include/cxui/test/cxuitest.h
    core/cxui.c
    core/cxuiloop.c
    core/cxuibox.c
    core/cxuibox.cxh
    core/cxuicanvas.c
    core/cxuicontainers.c
    core/cxuicontainers.cxh
    core/cxuicanvas.cxh
    core/cxuidesktop.c
    core/cxuidialog.c
    core/cxuidraw.c
    core/cxuidrawn.c
    core/cxuidrawn.cxh
    core/cxuidraw_private.h
    core/cxuifont.c
    core/cxuigrid.c
    core/cxuigrid.cxh
    core/cxuigridstore.c
    core/cxuigridstore.cxh
    core/cxuitreestore.c
    core/cxuitreestore.cxh
    core/cxuifont.cxh
    core/cxuiimage.c
    core/cxuiimage.cxh
    core/cxuievent.c
    core/cxuiinput.c
    core/cxuikeys.c
    core/cxuipalette.c
    core/cxuisash.c
    core/cxuisash.cxh
    core/cxuipalette.cxh
    core/cxuitext.c
    core/cxuitext.cxh
    core/cxuicontrols.c
    core/cxuicontrols.cxh
    core/cxuimenu.c
    core/cxuimenu.cxh
    core/cxuiprogress.c
    core/cxuiprogress.cxh
    core/cxuilayout.c
    core/cxuitimer.c
    core/cxuitimer.cxh
    core/cxuiwidget.c
    core/cxuiwidget.cxh
    core/cxuiwindow.c
    core/cxuiwindow.cxh
    core/cxuibackend.h
    core/cxui_private.h
    platform/null/null_backend.c
    platform/null/cxuitestwidget.c
    platform/null/cxuitestwidget.cxh)

if(CX_PLATFORM_IS_WINDOWS)
    target_sources(cxui PRIVATE
        platform/win32/win32_ui.h
        platform/win32/win32_init.c
        platform/win32/win32_canvas.c
        platform/win32/win32_controls.c
        platform/win32/win32_input.c
        platform/win32/win32_menu.c
        platform/win32/win32_draw_gdi.c
        platform/win32/win32_draw_d2d.cpp
        platform/win32/win32_dialog.c
        platform/win32/win32_file.c
        platform/win32/win32_desktop.c
        platform/win32/win32_text.c
        platform/win32/win32_window.c)
    target_compile_definitions(cxui PRIVATE CXUI_BACKEND_WIN32)
elseif(CX_PLATFORM_IS_MACOS)
    enable_language(OBJC)
    set(CXUI_APPKIT_SRCS
        platform/appkit/appkit_canvas.m
        platform/appkit/appkit_controls.m
        platform/appkit/appkit_desktop.m
        platform/appkit/appkit_dialog.m
        platform/appkit/appkit_draw_cg.m
        platform/appkit/appkit_init.m
        platform/appkit/appkit_menu.m
        platform/appkit/appkit_panel.m
        platform/appkit/appkit_text.m
        platform/appkit/appkit_window.m)
    target_sources(cxui PRIVATE platform/appkit/appkit_ui.h ${CXUI_APPKIT_SRCS})
    set_source_files_properties(${CXUI_APPKIT_SRCS} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(cxui PRIVATE CXUI_BACKEND_APPKIT)
    target_link_libraries(cxui PUBLIC "-framework AppKit" "-framework ColorSync" "-framework CoreText"
                                      "-framework QuartzCore" "-framework UniformTypeIdentifiers")
elseif(CX_PLATFORM_IS_UNIX AND NOT CX_PLATFORM_IS_MACOS)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK4 IMPORTED_TARGET gtk4>=4.14)
    endif()
    if(NOT GTK4_FOUND)
        message(FATAL_ERROR "cxui needs GTK 4.14 or newer and its development files (pkg-config "
                            "module gtk4). Install them, or set -DCX_UI=OFF.")
    endif()
    target_sources(cxui PRIVATE
        platform/gtk4/gtk4_ui.h
        platform/gtk4/gtk4_init.c
        platform/gtk4/gtk4_canvas.c
        platform/gtk4/gtk4_controls.c
        platform/gtk4/gtk4_input.c
        platform/gtk4/gtk4_menu.c
        platform/gtk4/gtk4_draw_gsk.c
        platform/gtk4/gtk4_dialog.c
        platform/gtk4/gtk4_desktop.c
        platform/gtk4/gtk4_text.c
        platform/gtk4/gtk4_surface.c
        platform/gtk4/gtk4_window.c)
    target_compile_definitions(cxui PRIVATE CXUI_BACKEND_GTK4)
    target_link_libraries(cxui PRIVATE PkgConfig::GTK4)
    # A render view's surface is the windowing system's own: a Wayland subsurface, or an X11
    # window. Either can be missing, which leaves render views without a surface there.
    pkg_check_modules(GTK4_WAYLAND IMPORTED_TARGET gtk4-wayland wayland-client)
    if(GTK4_WAYLAND_FOUND)
        target_compile_definitions(cxui PRIVATE CXUI_GTK4_WAYLAND)
        target_link_libraries(cxui PRIVATE PkgConfig::GTK4_WAYLAND)

        # Pointer lock on Wayland takes two protocols GTK doesn't offer; without them it isn't
        # available there.
        find_program(CXUI_WAYLAND_SCANNER wayland-scanner)
        pkg_get_variable(CXUI_WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
        if(CXUI_WAYLAND_SCANNER AND CXUI_WAYLAND_PROTOCOLS)
            foreach(proto pointer-constraints relative-pointer)
                set(xml ${CXUI_WAYLAND_PROTOCOLS}/unstable/${proto}/${proto}-unstable-v1.xml)
                set(out ${CMAKE_CURRENT_BINARY_DIR}/${proto}-unstable-v1)
                add_custom_command(
                    OUTPUT ${out}-client-protocol.h ${out}-protocol.c
                    COMMAND ${CXUI_WAYLAND_SCANNER} client-header ${xml} ${out}-client-protocol.h
                    COMMAND ${CXUI_WAYLAND_SCANNER} private-code ${xml} ${out}-protocol.c
                    DEPENDS ${xml}
                    VERBATIM)
                target_sources(cxui PRIVATE ${out}-client-protocol.h ${out}-protocol.c)
            endforeach()
            target_compile_definitions(cxui PRIVATE CXUI_GTK4_WLLOCK)
        endif()
    endif()
    pkg_check_modules(GTK4_X11 IMPORTED_TARGET gtk4-x11 x11 xext)
    if(GTK4_X11_FOUND)
        target_compile_definitions(cxui PRIVATE CXUI_GTK4_X11)
        target_link_libraries(cxui PRIVATE PkgConfig::GTK4_X11)
    endif()
endif()

target_link_libraries(cxui PUBLIC cx)
if(CX_PLATFORM_IS_UNIX)
    target_link_libraries(cxui PRIVATE m)
endif()

target_include_directories(cxui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(CX_PLATFORM_IS_WINDOWS)
    target_compile_definitions(cxui PRIVATE UNICODE _UNICODE)
    if(CX_XP_COMPAT)
        target_compile_definitions(cxui PRIVATE CX_XP_COMPAT)
    endif()
endif()

# The cxui Lua module, on the cxlua bridge
if(CX_LUA)
    add_library(cxuilua STATIC
        lua/include/cxuilua.h
        lua/cxuilua.c)
    target_link_libraries(cxuilua cxui cxlua)
    target_include_directories(cxuilua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua/include)
endif()

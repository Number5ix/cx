add_library(lua STATIC
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c)

target_link_libraries(lua cx)
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT CX_PLATFORM_IS_WINDOWS)
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(lua ${MATH_LIBRARY})
    endif()
endif()

if(CX_PLATFORM_IS_WINDOWS)
    target_compile_definitions(lua PRIVATE LUA_USE_WINDOWS)
elseif(CX_PLATFORM_IS_UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
endif()

# Under LTO, GCC inlines str_gsub -> add_value -> add_s -> luaL_addlstring and loses the
# invariant that the match end pointer is never below the match start, so it sees a possible
# memcpy bound of (size_t)-2 and reports an overflow that cannot happen. The diagnostic is
# raised at link time and so ignores a -Wno- on lua's own compile line; keeping lua out of
# LTO stops the inlining that produces it, which is preferable to suppressing the warning
# across the whole project just to accommodate vendored code.
set_property(TARGET lua PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)

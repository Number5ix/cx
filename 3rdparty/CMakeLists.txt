# 3rdparty CMake philosohpy:
# Try to make as few changes as possible to the included library's CMakeLists.txt and override
# things here instead. While not always possible and sometimes edits need to be made, this
# helps reduce merge conflicts.

include (${CX_TOP_SOURCE_DIR}/cmake/buildtype.cmake)

if (CX_MIMALLOC)
set(MI_OVERRIDE          OFF CACHE INTERNAL "Override the standard malloc interface (e.g. define entry points for malloc() etc)" FORCE)
set(MI_OSX_INTERPOSE     OFF CACHE INTERNAL "Use interpose to override standard malloc on macOS" FORCE)
set(MI_OSX_ZONE          OFF CACHE INTERNAL "Use malloc zone to override standard malloc on macOS" FORCE)
set(MI_WIN_REDIRECT      OFF CACHE INTERNAL "Use redirection module ('mimalloc-redirect') on Windows if compiling mimalloc as a DLL" FORCE)
set(MI_WIN_USE_FIXED_TLS OFF CACHE INTERNAL "Use a fixed TLS slot on Windows to avoid extra tests in the malloc fast path" FORCE)
set(MI_BUILD_SHARED      OFF CACHE INTERNAL "Build shared library" FORCE)
set(MI_BUILD_STATIC      OFF CACHE INTERNAL "Build static library" FORCE)
set(MI_BUILD_OBJECT      ON  CACHE INTERNAL "Build object library" FORCE)
set(MI_BUILD_TESTS       OFF CACHE INTERNAL "Build test executables" FORCE)
if(DIAGNOSTIC)
    set(MI_GUARDED       ON  CACHE INTERNAL "Build with guard pages behind certain object allocations (implies MI_NO_PADDING=ON)" FORCE)
endif()
add_subdirectory(mimalloc)
target_compile_definitions(mimalloc-obj PRIVATE
    $<${isdebug}:MI_DEBUG=3>
    $<${isdev}:MI_DEBUG=2>
    $<${isrelease}:MI_DEBUG=0>
    $<${isrelease}:MI_SECURE=4>)
add_library(mimalloc ALIAS mimalloc-obj)
endif() # CX_MIMALLOC

if (CX_TLS)
set(ENABLE_PROGRAMS OFF CACHE INTERNAL "Build Mbed TLS programs." FORCE)
set(ENABLE_TESTING  OFF CACHE INTERNAL "Build Mbed TLS tests." FORCE)
# The vendored tree comes from the release tarball, which ships every generated
# source. Nothing here needs Perl, Python or jinja2.
set(GEN_FILES       OFF CACHE INTERNAL "Generate the auto-generated files as needed" FORCE)
# Forwarded to TF_PSA_CRYPTO_FATAL_WARNINGS by mbedtls' CMakeLists.txt. cx's own
# warning flags reach these targets, and upstream's -Werror on top of them turns
# any new cx header warning into a third-party build failure.
set(MBEDTLS_FATAL_WARNINGS OFF CACHE INTERNAL "Compiler warnings treated as errors" FORCE)
set(INSTALL_MBEDTLS_HEADERS       OFF CACHE INTERNAL "Install Mbed TLS headers." FORCE)
set(INSTALL_TF_PSA_CRYPTO_HEADERS OFF CACHE INTERNAL "Install TF PSA Crypto headers." FORCE)

# USE_STATIC_* stays ON: all the target-naming logic in objlib.cmake,
# drivers/driver.cmake and the everest/p256-m CMakeLists is keyed on it.
set(USE_STATIC_MBEDTLS_LIBRARY       ON CACHE INTERNAL "Build Mbed TLS static library." FORCE)
set(USE_OBJECT_MBEDTLS_LIBRARY       ON CACHE INTERNAL "Build Mbed TLS object library." FORCE)
set(USE_STATIC_TF_PSA_CRYPTO_LIBRARY ON CACHE INTERNAL "Build TF-PSA-Crypto static library." FORCE)
set(USE_OBJECT_TF_PSA_CRYPTO_LIBRARY ON CACHE INTERNAL "Build TF-PSA-Crypto object library." FORCE)

set(MBEDTLS_USER_CONFIG_FILE       "${CX_TOP_SOURCE_DIR}/cxtls/include/cxtls_mbedtls_config.h"
    CACHE FILEPATH "Mbed TLS user config file (appended to default)." FORCE)
set(TF_PSA_CRYPTO_USER_CONFIG_FILE "${CX_TOP_SOURCE_DIR}/cxtls/include/cxtls_crypto_config.h"
    CACHE FILEPATH "TF-PSA-Crypto user config file (appended to default)." FORCE)

add_subdirectory(mbedtls EXCLUDE_FROM_ALL)

set(cx_mbedtls_targets
    tfpsacrypto mbedx509 mbedtls
    platform extras utilities
    builtin everest p256-m pqcp)
foreach(t IN LISTS cx_mbedtls_targets)
    target_include_directories(${t} PRIVATE ${CX_TOP_SOURCE_DIR} ${CX_TOP_SOURCE_DIR}/cx/include)
    target_compile_definitions(${t} PRIVATE
        $<${isdebug}:DEBUG_LEVEL=2>
        $<${isdev}:DEBUG_LEVEL=1>
        $<${isrelease}:DEBUG_LEVEL=0>)
endforeach()
endif() # CX_TLS

set(PCRE2_SHOW_REPORT     OFF CACHE INTERNAL "Show the final configuration report" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE INTERNAL "Build pcre2grep" FORCE)
set(PCRE2_BUILD_TESTS     OFF CACHE INTERNAL "Build the tests" FORCE)
set(BUILD_STATIC_LIBS     ON  CACHE INTERNAL "Build static libraries." FORCE)
set(BUILD_OBJECT_LIBS     ON  CACHE INTERNAL "Build object libraries." FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(pcre2)
target_include_directories(pcre2-8-static PRIVATE ${CX_TOP_SOURCE_DIR})
target_link_libraries(pcre2-8-static cx)
target_include_directories(pcre2-8-object PRIVATE ${CX_TOP_SOURCE_DIR})

if (CX_LUA)
add_subdirectory(lua)
endif() # CX_LUA

organize_targets(CLEAR_FOLDER PREPEND 3rdparty)

cmake_minimum_required (VERSION 3.24)

set(CX_TOP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE STRING "Location of CX source files" FORCE)
set(CX_TOP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE STRING "Location of CX build files" FORCE)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Minimum macOS version")

project (cx C CXX)

if(PROJECT_IS_TOP_LEVEL)
  if(CMAKE_CONFIGURATION_TYPES)
     list(APPEND CMAKE_CONFIGURATION_TYPES Dev DevNoOpt)
  endif()

  set(CMAKE_C_FLAGS_DEV "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  set(CMAKE_CXX_FLAGS_DEV "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  set(CMAKE_EXE_LINKER_FLAGS_DEV "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
  set(CMAKE_SHARED_LINKER_FLAGS_DEV "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
  set(CMAKE_MODULE_LINKER_FLAGS_DEV "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")

  set(CMAKE_C_FLAGS_DEVNOOPT "${CMAKE_C_FLAGS_DEBUG}")
  set(CMAKE_CXX_FLAGS_DEVNOOPT "${CMAKE_CXX_FLAGS_DEBUG}")
  set(CMAKE_EXE_LINKER_FLAGS_DEVNOOPT "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
  set(CMAKE_SHARED_LINKER_FLAGS_DEVNOOPT "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
  set(CMAKE_MODULE_LINKER_FLAGS_DEVNOOPT "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")

  set(CX_LUA_DEFAULT ON)
  set(CX_TLS_DEFAULT ON)
  set(CX_HTTP_DEFAULT ON)
  set(CX_QUIC_DEFAULT ON)
  set(CX_STANDALONE 1)
else()
  set(CX_LUA_DEFAULT OFF)
  set(CX_TLS_DEFAULT ON)
  set(CX_HTTP_DEFAULT ON)
  set(CX_QUIC_DEFAULT ON)
endif()

option(CX_LUA "Build Lua scripting engine and cxlua library" ${CX_LUA_DEFAULT})
option(CX_TLS "Build mbedTLS cryptography library" ${CX_TLS_DEFAULT})
option(CX_HTTP "Build cxhttp, the HTTP/1.1 client and server library" ${CX_HTTP_DEFAULT})
option(CX_QUIC "Build cxquic, the QUIC transport library" ${CX_QUIC_DEFAULT})
option(CX_MIMALLOC "Use mimalloc rather than system allocator" ON)
option(CX_MIMALLOC_OPT_ARCH "Let mimalloc compile itself for a newer processor than the rest of the program" OFF)

# HTTP/3 is cxhttp's second framing layer rather than a second stack, so it needs both halves
# present. On by default whenever they are.
if(CX_HTTP AND CX_QUIC)
    set(CX_HTTP3_DEFAULT ON)
else()
    set(CX_HTTP3_DEFAULT OFF)
endif()
option(CX_HTTP3 "Build HTTP/3 support into cxhttp" ${CX_HTTP3_DEFAULT})

# cxhttp speaks https through cxtls rather than branching on whether it is available, so the
# dependency is hard. Fail at configure time rather than at link time.
if(CX_HTTP AND NOT CX_TLS)
    message(FATAL_ERROR "CX_HTTP requires CX_TLS (cxhttp uses cxtls for https). "
                        "Set -DCX_HTTP=OFF to build without HTTP support.")
endif()

# QUIC's packet protection and handshake are TLS 1.3 by definition, so cxquic is built
# on cxtls unconditionally. Fail at configure time rather than at link time.
if(CX_QUIC AND NOT CX_TLS)
    message(FATAL_ERROR "CX_QUIC requires CX_TLS (cxquic uses cxtls for TLS 1.3 and packet "
                        "protection). Set -DCX_QUIC=OFF to build without QUIC support.")
endif()

# HTTP/3 is HTTP over QUIC, so it is both halves or neither.
if(CX_HTTP3 AND NOT (CX_HTTP AND CX_QUIC))
    message(FATAL_ERROR "CX_HTTP3 requires both CX_HTTP and CX_QUIC. "
                        "Set -DCX_HTTP3=OFF to build cxhttp without HTTP/3 support.")
endif()

include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/cxauto.cmake)
include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/organize_targets.cmake)

string(REGEX MATCH "Clang" CX_COMPILER_IS_CLANG "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "GNU" CX_COMPILER_IS_GNU "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "MSVC" CX_COMPILER_IS_MSVC "${CMAKE_C_COMPILER_ID}")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CX_PLATFORM_IS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(CX_PLATFORM_IS_WASM 1)
else()
    set(CX_PLATFORM_IS_UNIX 1)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(CX_PLATFORM_IS_MACOS 1)
    endif()
endif()

if(CX_PLATFORM_IS_MACOS)
    list(LENGTH CMAKE_OSX_ARCHITECTURES CX_OSX_ARCH_COUNT)
    if(CX_OSX_ARCH_COUNT GREATER 1)
        message(FATAL_ERROR "Universal builds are not supported; set CMAKE_OSX_ARCHITECTURES to a single architecture.")
    endif()
    add_compile_definitions(__APPLE_USE_RFC_3542)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # accept4() and recvmmsg() (cx/platform/unix/unix_net_socket.c, unix_net_epoll.c) are glibc
    # extensions gated behind _GNU_SOURCE. Defined globally rather than per-file: source file
    # properties are directory-scoped in CMake and would not reach the `cx` target, which is
    # created in a different directory than where those sources are listed.
    add_compile_definitions(_GNU_SOURCE)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -sMALLOC=emmalloc -sINITIAL_MEMORY=2047MB")
endif()

if (CX_PLATFORM_IS_WINDOWS)
    option (CX_XP_COMPAT "Build with Windows XP compatibility" OFF)
endif()

# cxui has no wasm backend, and on Linux and the BSDs it is on by default only where the GTK 4
# development files are installed.
set(CX_UI_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL AND NOT CX_PLATFORM_IS_WASM)
    set(CX_UI_DEFAULT ON)
    if(CX_PLATFORM_IS_UNIX AND NOT CX_PLATFORM_IS_MACOS)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(CX_UI_GTK4_PROBE QUIET gtk4>=4.14)
        endif()
        if(NOT CX_UI_GTK4_PROBE_FOUND)
            set(CX_UI_DEFAULT OFF)
        endif()
    endif()
endif()
option(CX_UI "Build cxui, the user interface library" ${CX_UI_DEFAULT})
if(CX_UI AND CX_PLATFORM_IS_WASM)
    message(FATAL_ERROR "cxui does not support wasm. Set -DCX_UI=OFF.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)            # for MSVC
set(CMAKE_CODELITE_USE_TARGETS ON)

add_subdirectory(3rdparty)
add_subdirectory(cx)
add_subdirectory(cxautogen)
add_subdirectory(misc)

if(CX_LUA)
  add_subdirectory(cxlua)
endif()

if(CX_TLS)
  add_subdirectory(cxtls)
endif()

if(CX_HTTP)
  add_subdirectory(cxhttp)
endif()

if(CX_QUIC)
  add_subdirectory(cxquic)
endif()

if(CX_UI)
  add_subdirectory(cxui)
endif()

if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
  add_subdirectory(tests)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/doxygen.cmake)
endif()

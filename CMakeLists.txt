cmake_minimum_required (VERSION 3.21)

set(CX_TOP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CX_TOP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

project (cx C CXX)

include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/sidl.cmake)

string(REGEX MATCH "Clang" CX_COMPILER_IS_CLANG "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "GNU" CX_COMPILER_IS_GNU "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "MSVC" CX_COMPILER_IS_MSVC "${CMAKE_C_COMPILER_ID}")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CX_PLATFORM_IS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(CX_PLATFORM_IS_WASM 1)
else()
    set(CX_PLATFORM_IS_UNIX 1)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -sMALLOC=emmalloc -sINITIAL_MEMORY=2047MB")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)            # for MSVC
set(CMAKE_CODELITE_USE_TARGETS ON)

add_subdirectory(3rdparty)
add_subdirectory(cx)
add_subdirectory(cxobjgen)
#add_subdirectory(misc)

if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()

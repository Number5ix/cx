cmake_minimum_required (VERSION 3.9)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "dev" CACHE
        STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "debug" "dev" "release")

set(CX_TOP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CX_TOP_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

project (cx)
enable_language(C)

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

if(CX_PLATFORM_IS_WASM)
    # emcc needs to handle debug info differently in release builds,
    # but it's technically clang so check for it first
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g -DDEBUG")
    set(CMAKE_C_FLAGS_DEV "-O1 -g -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
elseif(CX_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Werror")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -DDEBUG")
    set(CMAKE_C_FLAGS_DEV "-O1 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
elseif(CX_COMPILER_IS_GNU)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Werror -fno-strict-aliasing")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -DDEBUG")
    set(CMAKE_C_FLAGS_DEV "-O1 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
elseif(CX_COMPILER_IS_MSVC)
    set(CMAKE_C_FLAGS "/MP /Zi /Gm- /fp:precise /fp:except- /Zc:forScope /GR- /Gd /Oi /MD /FC /EHsc- /DWIN32 /D_WINDOWS /W3 /WX")
    set(CMAKE_C_FLAGS_DEBUG "/Od /Ob1 /GF- /RTC1 /DDEBUG /D_DEBUG /MDd")
    set(CMAKE_C_FLAGS_DEV "/O1 /Ob1 /GF /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /Zc:inline /fp:precise /fp:except- /GF /Ot /GS /guard:cf /Gy /GL /DNDEBUG")
    set(CMAKE_CXX_FLAGS "/MP /Zi /Gm- /fp:precise /fp:except- /Zc:forScope /GR- /Gd /Oi /MD /FC /EHsc- /DWIN32 /D_WINDOWS /W3 /WX")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Ob1 /GF- /RTC1 /DDEBUG /D_DEBUG /MDd")
    set(CMAKE_CXX_FLAGS_DEV "/O1 /Ob1 /GF /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Zc:inline /fp:precise /fp:except- /GF /Ot /GS /guard:cf /Gy /GL /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_DEV ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /debug")
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

enable_testing()
add_subdirectory(tests)

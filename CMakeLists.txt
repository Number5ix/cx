cmake_minimum_required (VERSION 3.9)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Dev" CACHE
        STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug" "Dev" "Release")

#include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/sidl.cmake)

set(CX_TOP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CX_TOP_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

project (cx)
enable_language(C)

string(REGEX MATCH "Clang" CMAKE_COMPILER_IS_CLANG "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "GNU" CMAKE_COMPILER_IS_GNU "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "MSVC" CMAKE_COMPILER_IS_MSVC "${CMAKE_C_COMPILER_ID}")

if(CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -DDEV_BUILD -DDEBUG_BUILD -DDEBUG_LEVEL=2")
    set(CMAKE_C_FLAGS_DEV "-O1 -DDEV_BUILD -DDEBUG_LEVEL=1")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DRELEASE_BUILD -DDEBUG_LEVEL=0")
elseif(CMAKE_COMPILER_IS_MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/Zi /Gm- /Od /Ob1 /Zc:inline /fp:precise /fp:except- /GF- /WX /Zc:forScope /RTC1 /GR- /Gd /FC /EHsc- /DDEV_BUILD /DDEBUG_BUILD /DDEBUG_LEVEL=2 /DDEBUG /D_DEBUG")
    set(CMAKE_C_FLAGS_DEV "/Zi /Gm- /O1 /Ob1 /GF /WX /Zc:forScope /GR- /Gd /Oi /MD /FC /EHsc- /DDEV_BUILD /DDEBUG_LEVEL=1 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/Zi /Gm- /O2 /Ob2 /Zc:inline /fp:precise /fp:except- /GF /WX /Zc:forScope /GR- /Gd /Oi /MD /FC /EHsc- /Ot /GS /guard:cf /Gy /GL /DRELEASE_BUILD /DDEBUG_LEVEL=0 /DNDEBUG")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)            # for MSVC
set(CMAKE_CODELITE_USE_TARGETS ON)

add_subdirectory(3rdparty)
#add_subdirectory(cx)
add_subdirectory(cxmem)
#add_subdirectory(cxobjgen)

cmake_minimum_required (VERSION 3.9)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Dev" CACHE
        STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug" "Dev" "Release")

include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/sidl.cmake)

set(CX_TOP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CX_TOP_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

project (cx)
enable_language(C)

string(REGEX MATCH "Clang" CMAKE_COMPILER_IS_CLANG "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "GNU" CMAKE_COMPILER_IS_GNU "${CMAKE_C_COMPILER_ID}")
string(REGEX MATCH "MSVC" CMAKE_COMPILER_IS_MSVC "${CMAKE_C_COMPILER_ID}")

if(CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
    set(CMAKE_C_FLAGS_DEBUG "-O0")
    set(CMAKE_C_FLAGS_DEV "-O1")
    set(CMAKE_C_FLAGS_RELEASE "-O3")
elseif(CMAKE_COMPILER_IS_MSVC)
    set(CMAKE_C_FLAGS "/MP /Zi /Gm- /fp:precise /fp:except- /Zc:forScope /GR- /Gd /Oi /MD /FC /EHsc- /DWIN32 /D_WINDOWS /W3 /WX")
    set(CMAKE_C_FLAGS_DEBUG "/Od /Ob1 /GF- /RTC1 /DDEBUG /D_DEBUG")
    set(CMAKE_C_FLAGS_DEV "/O1 /Ob1 /GF /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /Zc:inline /fp:precise /fp:except- /GF /Ot /GS /guard:cf /Gy /GL /DNDEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_DEV ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)            # for MSVC
set(CMAKE_CODELITE_USE_TARGETS ON)

add_subdirectory(3rdparty)
add_subdirectory(cx)
add_subdirectory(cxmem)
add_subdirectory(cxobjgen)

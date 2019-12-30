cmake_minimum_required (VERSION 3.9)

#include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/sidl.cmake)

set(CX_TOP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CX_TOP_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

project (cx)
enable_language(C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)            # for MSVC
set(CMAKE_CODELITE_USE_TARGETS ON)

add_subdirectory(3rdparty)
#add_subdirectory(cx)
add_subdirectory(cxmem)
#add_subdirectory(cxobjgen)

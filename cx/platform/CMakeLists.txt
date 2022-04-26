set(PLATFORM_INCLUDES
    platform/base.h
    platform/cpu.h
    platform/ksema.h
    platform/os.h)

if(CX_COMPILER_IS_CLANG)
    add_subdirectory(clang)
elseif(CX_COMPILER_IS_MSVC)
    add_subdirectory(msvc)
endif()

if(CX_PLATFORM_IS_UNIX)
    add_subdirectory(unix)
elseif(CX_PLATFORM_IS_WINDOWS)
    add_subdirectory(win)
elseif(CX_PLATFORM_IS_WASM)
    add_subdirectory(wasm)
endif()

set(CX_SRCS
    ${CX_SRCS}
    ${PLATFORM_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${PLATFORM_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

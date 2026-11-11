include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

add_library(cxlua STATIC
    include/cxlua.h
    cxlua.c)

target_link_libraries(cxlua cx lua)
target_include_directories(cxlua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

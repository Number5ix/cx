source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    htbench.c)

add_executable(htbench htbench.c)
target_link_libraries(htbench cx)

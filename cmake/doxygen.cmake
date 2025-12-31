find_package(Doxygen)

if(DOXYGEN_FOUND)
    # Configure Doxyfile with build directory path
    set(DOXYGEN_BUILD_DIR "${CMAKE_BINARY_DIR}")
    configure_file(
        ${CMAKE_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_BINARY_DIR}/Doxyfile
        @ONLY
    )
    
    # Create a custom target that depends on all cxautogen outputs
    add_custom_target(cx_create_docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        DEPENDS cx  # This ensures all cxautogen runs first since cx depends on it
    )
    set_property(TARGET cx_create_docs PROPERTY FOLDER "generate")
endif()
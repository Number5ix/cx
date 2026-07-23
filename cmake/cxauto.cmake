if(CMAKE_CROSSCOMPILING)
find_package(cxautogen)
function(add_cxautogen)
    set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
    foreach(idir ${EXTRA_CXAUTOGEN_INCLUDE_DIRS})
        set(EXTRA_CXAUTOGEN_ARGS ${EXTRA_CXAUTOGEN_ARGS} -I${idir})
    endforeach()
    foreach(arg ${ARGN})
        string(REGEX REPLACE "\\.[^.]*$" "" argbase ${arg})
        add_custom_command(
            COMMAND cxautogen -I${CX_TOP_SOURCE_DIR} -I${CX_TOP_SOURCE_DIR}/cx/include -S${CMAKE_CURRENT_SOURCE_DIR} -B${CMAKE_CURRENT_BINARY_DIR} -M${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d ${EXTRA_CXAUTOGEN_ARGS} -f ${arg}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h
            )
    endforeach()
endfunction()
else()
function(add_cxautogen)
    set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
    foreach(idir ${EXTRA_CXAUTOGEN_INCLUDE_DIRS})
        set(EXTRA_CXAUTOGEN_ARGS ${EXTRA_CXAUTOGEN_ARGS} -I${idir})
    endforeach()
    foreach(arg ${ARGN})
        string(REGEX REPLACE "\\.[^.]*$" "" argbase ${arg})
        add_custom_command(
            COMMAND cxautogen -I${CX_TOP_SOURCE_DIR} -I${CX_TOP_SOURCE_DIR}/cx/include -S${CMAKE_CURRENT_SOURCE_DIR} -B${CMAKE_CURRENT_BINARY_DIR} -M${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d ${EXTRA_CXAUTOGEN_ARGS} -f ${arg}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            DEPENDS cxautogen
            DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h
            )
    endforeach()
endfunction()
endif()

function(add_cxautogen_nodep)
    set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
    foreach(idir ${EXTRA_CXAUTOGEN_INCLUDE_DIRS})
        set(EXTRA_CXAUTOGEN_ARGS ${EXTRA_CXAUTOGEN_ARGS} -I${idir})
    endforeach()
    foreach(arg ${ARGN})
        string(REGEX REPLACE "\\.[^.]*$" "" argbase ${arg})
        add_custom_command(
            COMMAND cxautogen -I${CX_TOP_SOURCE_DIR} -I${CX_TOP_SOURCE_DIR}/cx/include -S${CMAKE_CURRENT_SOURCE_DIR} -B${CMAKE_CURRENT_BINARY_DIR} -M${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d ${EXTRA_CXAUTOGEN_ARGS} -f ${arg}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h.d
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${argbase}.h
            )
    endforeach()
endfunction()

function(add_sidl)
    set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
    foreach(arg ${ARGN})
        string(REGEX REPLACE "\\.[^.]*$" "" argbase ${arg})
        add_custom_command(
            COMMAND cxobjgen -I${CX_TOP_SOURCE_DIR} -f ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            DEPENDS cxobjgen
            IMPLICIT_DEPENDS "C" ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${argbase}.h
            )
    endforeach()
endfunction()

function(add_sidl_nodep)
    set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
    foreach(arg ${ARGN})
        string(REGEX REPLACE "\\.[^.]*$" "" argbase ${arg})
        add_custom_command(
            COMMAND cxobjgen -I${CX_TOP_SOURCE_DIR} -f ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${arg}
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${argbase}.h
            )
    endforeach()
endfunction()

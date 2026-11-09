# Runs cxautogen on every fixture in FIXTURE_DIR and checks that it fails with the message in the
# fixture's "// expect:" line. Each fixture is copied to WORK_DIR first, because cxautogen writes
# .c stubs next to its input.
file(GLOB fixtures ${FIXTURE_DIR}/*.cxh)
set(failed FALSE)
foreach(fixture ${fixtures})
    get_filename_component(fname ${fixture} NAME)
    get_filename_component(fbase ${fixture} NAME_WE)
    file(STRINGS ${fixture} expect REGEX "^// expect: " LIMIT_COUNT 1)
    string(REPLACE "// expect: " "" expect "${expect}")

    set(fdir ${WORK_DIR}/${fbase})
    file(REMOVE_RECURSE ${fdir})
    file(COPY ${fixture} DESTINATION ${fdir})
    execute_process(COMMAND ${CXAUTOGEN} -S${fdir} -B${fdir} -f ${fname}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(rc EQUAL 0 OR NOT out MATCHES "${expect}")
        message("${fbase}: expected a failure matching '${expect}', got exit code ${rc}:\n${out}")
        set(failed TRUE)
    endif()
endforeach()

if(failed)
    message(FATAL_ERROR "cxautogen accepted or misreported a bad declaration")
endif()

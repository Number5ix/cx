find_package (Threads REQUIRED)

if(NOT CMAKE_CROSSCOMPILING)
if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CX_PLATFORM_SRCS
	../cx/platform/unix/unix_error.c
	../cx/platform/unix/unix_fs_file.c
	../cx/platform/unix/unix_fs_fs.c
	../cx/platform/unix/unix_ksema.c
	../cx/platform/unix/unix_os.c
	../cx/platform/unix/unix_time_clock.c)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(CX_PLATFORM_SRCS
            ${CX_PLATFORM_SRCS}
            ../cx/platform/unix/linux_futex.c)
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
        set(CX_PLATFORM_SRCS
            ${CX_PLATFORM_SRCS}
            ../cx/platform/unix/freebsd_futex.c)
    endif()
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CX_PLATFORM_SRCS
	../cx/platform/win/win_error.c
	../cx/platform/win/win_fs_file.c
	../cx/platform/win/win_fs_fs.c
	../cx/platform/win/win_futex.c
	../cx/platform/win/win_ksema.c
	../cx/platform/win/win_os.c
	../cx/platform/win/win_time_clock.c)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

set(CXOBJGEN_SRCS
    buffile.c
    classes.c
    header.c
    impl.c
    interfaces.c
    objtypes.c
    parser.c
    cxobjgen.c
    stub.c)
set(CX_SRCS
    ../cx/container/hashtable.c
    ../cx/container/sarray.c
    ../cx/container/stype_hashtable.c
    ../cx/container/stype_sarray.c
    ../cx/core/error.c
    ../cx/core/objclass.c
    ../cx/core/objiface.c
    ../cx/core/objimpl.c
    ../cx/core/objstdif.c
    ../cx/core/stype.c
    ../cx/core/stype_obj.c
    ../cx/core/stype_stvar.c
    ${CX_PLATFORM_SRCS}
    ../cx/string/strbase.c
    ../cx/string/strbase64.c
    ../cx/string/strcase.c
    ../cx/string/strcompare.c
    ../cx/string/strconcat.c
    ../cx/string/strencoding.c
    ../cx/string/strfind.c
    ../cx/string/striter.c
    ../cx/string/strjoin.c
    ../cx/string/strrope.c
    ../cx/string/strsplit.c
    ../cx/string/strsubstr.c
    ../cx/string/stype_string.c
    ../cx/fs/path.c
    ../cx/fs/pathmatch.c
    ../cx/thread/mutex.c
    ../cx/thread/rwlock.c
    ../cx/thread/sema.c
    ../cx/utils/lazyinit.c
    ../cx/utils/murmur.c
    ../cx/utils/scratch.c
    )
source_group("cx" FILES
    ${CX_SRCS})

set(CXOBJGEN_INCLUDES
    buffile.h
    objtypes.h
    cxobjgen.h
    objtypes.sidl
    )

source_group("" FILES
    ${CXOBJGEN_SRCS}
    ${CXOBJGEN_INCLUDES})

add_executable(cxobjgen
    ${CXOBJGEN_SRCS}
    ${CXOBJGEN_INCLUDES}
    ${CX_SRCS})

target_link_libraries(cxobjgen ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} cxmem mbedtls pcre2-8)
target_include_directories(cxobjgen SYSTEM PUBLIC ${CX_TOP_SOURCE_DIR} ${CX_TOP_BINARY_DIR})

export(TARGETS cxobjgen FILE "${CMAKE_BINARY_DIR}/cxobjgen-config.cmake")

endif()     # NOT CMAKE_CROSSCOMPILING

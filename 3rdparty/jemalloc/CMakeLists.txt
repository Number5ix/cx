include (CheckTypeSize)
include (CheckIncludeFiles)
include(${CMAKE_CURRENT_SOURCE_DIR}/Utilities.cmake)
find_package (Threads REQUIRED)

set (jemalloc_version "4.5.0-3-g17c897976c60b0e6e4f4a365c751027244dada7a")
set (jemalloc_version_major 4)
set (jemalloc_version_minor 5)
set (jemalloc_version_bugfix 0)
set (jemalloc_version_nrev 3)
set (jemalloc_version_gid "17c897976c60b0e6e4f4a365c751027244dada7a")

# whether malloc_usable_size definition can use const argument
CHECK_INCLUDE_FILES (malloc.h HAVE_MALLOC_H)
if(HAVE_MALLOC_H)
    set(JEMALLOC_USABLE_SIZE_CONST const)
else()
    set(JEMALLOC_USABLE_SIZE_CONST " ")
endif()
CHECK_INCLUDE_FILES (stdatomic.h JEMALLOC_C11_ATOMICS)

UtilCheckTypeSize(void* SIZEOF_VOID_P)
if(SIZEOF_VOID_P)
  lg(${SIZEOF_VOID_P} "LG_SIZEOF_PTR")
  if((NOT ${LG_SIZEOF_PTR} EQUAL 3) AND
     (NOT ${LG_SIZEOF_PTR} EQUAL 2))
    message(FATAL_ERROR "Unsupported pointer size :${LG_SIZEOF_PTR}")
  endif()
endif()

UtilCheckTypeSize(int SIZEOF_INT)
if(SIZEOF_INT)
  lg(${SIZEOF_INT} "LG_SIZEOF_INT")
  if((NOT ${LG_SIZEOF_INT} EQUAL 3) AND
     (NOT ${LG_SIZEOF_INT} EQUAL 2))
    message(FATAL_ERROR "Unsupported int size :${LG_SIZEOF_INT}")
  endif()
endif()

UtilCheckTypeSize(long SIZEOF_LONG)
if(SIZEOF_LONG)
  lg(${SIZEOF_LONG} "LG_SIZEOF_LONG")
  if((NOT ${LG_SIZEOF_LONG} EQUAL 3) AND
     (NOT ${LG_SIZEOF_LONG} EQUAL 2))
    message(FATAL_ERROR "Unsupported long size :${LG_SIZEOF_LONG}")
  endif()
endif()

UtilCheckTypeSize("long long" SIZEOF_LONG_LONG)
if(SIZEOF_LONG_LONG)
  lg(${SIZEOF_LONG_LONG} "LG_SIZEOF_LONG_LONG")
  if((NOT ${LG_SIZEOF_LONG_LONG} EQUAL 3) AND
     (NOT ${LG_SIZEOF_LONG_LONG} EQUAL 2))
    message(FATAL_ERROR "Unsupported long size :${LG_SIZEOF_LONG_LONG}")
  endif()
endif()

UtilCheckTypeSize(intmax_t SIZEOF_INTMAX_T)
if(SIZEOF_INTMAX_T)
  lg(${SIZEOF_INTMAX_T} "LG_SIZEOF_INTMAX_T")
  if((NOT ${LG_SIZEOF_INTMAX_T} EQUAL 4) AND
     (NOT ${LG_SIZEOF_INTMAX_T} EQUAL 3) AND
     (NOT ${LG_SIZEOF_INTMAX_T} EQUAL 2))
    message(FATAL_ERROR "Unsupported long size :${LG_SIZEOF_INTMAX_T}")
  endif()
endif()

set(JEMALLOC_PREFIX "je_")
set(JEMALLOC_CPREFIX "\"JE_\"")
set(private_namespace je_)
set(je_ "je_")
set(JEMALLOC_EXPORT " ")

set(JEMALLOC_CONFIG_MALLOC_CONF "\"\"")
set(JEMALLOC_CC_SILENCE 1)
#set(JEMALLOC_DEBUG 1)
#set(JEMALLOC_IVSALLOC 1)
set(JEMALLOC_STATS 1)
set(JEMALLOC_TCACHE 1)
set(JEMALLOC_PREFIX_JET jet_)
set(JEMALLOC_DSS 0)
set(JEMALLOC_FILL 1)
set(JEMALLOC_UTRACE 0)
set(JEMALLOC_VALGRIND 0)
set(JEMALLOC_XMALLOC 0)
set(JEMALLOC_CACHE_OBLIVIOUS 1)

set(LG_TINY_MIN 3)
set(LG_QUANTA 3 4)

# All currently supported systems have 4k page size
set(LG_PAGE 12)
set(LG_PAGE_SIZES 12)
set(LG_SIZE_CLASS_GROUP 2)

set(JEMALLOC_HAVE_SECURE_GETENV 0)
set(JEMALLOC_LAZY_LOCK 0)
set(JEMALLOC_CODE_COVERAGE 0)
set(JEMALLOC_PROF 0)
set(JEMALLOC_PROF_LIBUNWIND 0)
set(JEMALLOC_PROF_LIBGCC 0)
set(JEMALLOC_PROF_GCC 0)
set(JEMALLOC_HAS_RESTRICT 1)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(JEMALLOC_HAS_ALLOCA_H 1)
    set(JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY 1)
    set(JEMALLOC_PURGE_MADVISE_DONTNEED 1)
    set(JEMALLOC_THREADED_INIT 1)
    set(JEMALLOC_MUNMAP 0)
    set(JEMALLOC_MAPS_COALESCE 1)
    set(JEMALLOC_HAVE_ATTR 1)
    set(CPU_SPINWAIT "__asm__ volatile(\"pause\")")
    set(JEMALLOC_INTERNAL_FFSL __builtin_ffsl)
    set(JEMALLOC_INTERNAL_FFS __builtin_ffs)
    set(JEMALLOC_INTERNAL_FFSLL __builtin_ffsll)
    set(JEMALLOC_HAVE_ISSETUGID 0)
    set(JEMALLOC_MALLOC_THREAD_CLEANUP 0)
    set(JEMALLOC_MUTEX_INIT_CB 0)
    set(JEMALLOC_TLS 1)
    set(JEMALLOC_ATOMIC9 0)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_4 1)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_8 1)
    set(JEMALLOC_HAVE_BUILTIN_CLZ 1)
    set(JEMALLOC_HAVE_MADVISE 1)
    set(JEMALLOC_HAVE_PTHREAD_ATFORK 1)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC_COARSE 1)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC 1)
    set(JEMALLOC_TLS_MODEL "__attribute__((tls_model(\"initial-exec\")))")
    set(JEMALLOC_INTERNAL_UNREACHABLE "__builtin_unreachable")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    set(JEMALLOC_SYSCTL_VM_OVERCOMMIT 1)
    set(JEMALLOC_PURGE_MADVISE_FREE 1)
    set(JEMALLOC_LAZY_LOCK 1)
    set(JEMALLOC_MUNMAP 1)
    set(JEMALLOC_MAPS_COALESCE 1)
    set(JEMALLOC_HAVE_ATTR 1)
    set(CPU_SPINWAIT "__asm__ volatile(\"pause\")")
    set(JEMALLOC_INTERNAL_FFSL __builtin_ffsl)
    set(JEMALLOC_INTERNAL_FFS __builtin_ffs)
    set(JEMALLOC_INTERNAL_FFSLL __builtin_ffsll)
    set(JEMALLOC_HAVE_ISSETUGID 1)
    set(JEMALLOC_MALLOC_THREAD_CLEANUP 1)
    set(JEMALLOC_MUTEX_INIT_CB 1)
    set(JEMALLOC_TLS 1)
    set(JEMALLOC_ATOMIC9 1)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_4 1)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_8 1)
    set(JEMALLOC_HAVE_BUILTIN_CLZ 1)
    set(JEMALLOC_HAVE_MADVISE 1)
    set(JEMALLOC_HAVE_PTHREAD_ATFORK 1)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC_COARSE 0)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC 1)
    set(JEMALLOC_TLS_MODEL "__attribute__((tls_model(\"initial-exec\")))")
    set(JEMALLOC_INTERNAL_UNREACHABLE "__builtin_unreachable")
    set(JEMALLOC_PROF 1)
    set(JEMALLOC_PROF_GCC 1)
    set(JEMALLOC_EXTRA_LIBS m)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(JEMALLOC_MUNMAP 1)
    set(JEMALLOC_MAPS_COALESCE 0)
    set(JEMALLOC_HAVE_ATTR 0)
    set(CPU_SPINWAIT "_mm_pause()")
    set(JEMALLOC_INTERNAL_FFSL ffsl)
    set(JEMALLOC_INTERNAL_FFS ffs)
    set(JEMALLOC_INTERNAL_FFSLL ffsll)
    set(JEMALLOC_HAVE_ISSETUGID 0)
    set(JEMALLOC_MALLOC_THREAD_CLEANUP 0)
    set(JEMALLOC_MUTEX_INIT_CB 0)
    set(JEMALLOC_TLS 0)
    set(JEMALLOC_ATOMIC9 0)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_4 0)
    set(JE_FORCE_SYNC_COMPARE_AND_SWAP_8 0)
    set(JEMALLOC_HAVE_BUILTIN_CLZ 0)
    set(JEMALLOC_HAVE_MADVISE 0)
    set(JEMALLOC_HAVE_PTHREAD_ATFORK 0)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC_COARSE 0)
    set(JEMALLOC_HAVE_CLOCK_MONOTONIC 0)
    set(JEMALLOC_INTERNAL_UNREACHABLE "abort")
    set(JEMALLOC_PROF 1)
    set(JEMALLOC_PROF_WINNT 1)
endif()

set(PUBLIC_SYM
  malloc_conf
  malloc_message
  malloc
  calloc
  posix_memalign
  aligned_alloc
  realloc
  free
  mallocx
  rallocx
  xallocx
  sallocx
  dallocx
  sdallocx
  nallocx
  mallctl
  mallctlnametomib
  mallctlbymib
  malloc_stats_print
  malloc_usable_size
)

set(PUBLIC_SYM_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/public_symbols.txt")

# Generate headers
foreach(public_in jemalloc_macros.h jemalloc_defs.h jemalloc_protos.h jemalloc_typedefs.h)
    configure_file("include/jemalloc/${public_in}.in"
        "include/jemalloc/${public_in}")
endforeach(public_in)

set(JEMALLOC_RENAME_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/jemalloc_rename.h")
set(JEMALLOC_MANGLE_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/jemalloc_mangle.h")
# Needed for tests
set(JEMALLOC_MANGLE_JET_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/jemalloc_mangle_jet.h")

# Generate main public header
set(JEMALLOC_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/jemalloc${install_suffix}.h")

set(JEMALLOC_HDR_LIST
   jemalloc_defs.h
   jemalloc_rename.h
   jemalloc_macros.h
   jemalloc_protos.h
   jemalloc_typedefs.h
   jemalloc_mangle.h
)

set(PUBLIC_NAMESPACE_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/public_namespace.h")
set(PUBLIC_UNNAMESPACE_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/public_unnamespace.h")

# This file comes with repo
set(PRIVATE_SYM_FILE "${CMAKE_CURRENT_SOURCE_DIR}/include/jemalloc/internal/private_symbols.txt")
set(PRIVATE_NAMESPACE_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/private_namespace.h")
set(PRIVATE_UNNAMESPACE_FILE "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/private_unnamespace.h")

# Configure internal headers

# Main internal header does not require #define expansion otherwise it affects real #undefs
configure_file("include/jemalloc/internal/jemalloc_internal.h.in"
    "include/jemalloc/internal/jemalloc_internal.h")

foreach(internal_in jemalloc_internal_defs.h)
  configure_file("include/jemalloc/internal/${internal_in}.in"
    "include/jemalloc/internal/${internal_in}")
endforeach(internal_in)

set(SIZE_CLASSES_HDR "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/internal/size_classes.h")

# To generate protos_jet
set(je_ jet_)
# replace prefix only
configure_file("include/jemalloc/jemalloc_protos.h.in"
    "include/jemalloc/jemalloc_protos_jet.h")
# revert
set(je_ je_)

set(GenerateCache "${CMAKE_CURRENT_BINARY_DIR}/GenerateCache.cmake")
file (WRITE "${GenerateCache}" "")
get_cmake_property(Vars VARIABLES)
foreach(Var ${Vars})
    set(VarValue "")
    if(${Var})
        string(REPLACE "\\" "\\\\" VarValue "${${Var}}")
        string(REPLACE "\"" "\\\"" VarValue "${VarValue}")
    endif()
    file(APPEND "${GenerateCache}" "set(${Var} \"${VarValue}\")\n")
endforeach()

add_custom_command(OUTPUT ${SIZE_CLASSES_HDR}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=SizeClasses -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_custom_command(OUTPUT ${JEMALLOC_HDR}
    DEPENDS ${PUBLIC_NAMESPACE_FILE} ${PUBLIC_UNNAMESPACE_FILE}
    ${PRIVATE_NAMESPACE_FILE} ${PRIVATE_UNNAMESPACE_FILE}
    ${JEMALLOC_RENAME_HDR} ${JEMALLOC_MANGLE_HDR}
    ${SIZE_CLASSES_HDR}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=JemallocHeaders -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_custom_command(OUTPUT ${PUBLIC_SYM_FILE}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=PublicSymbols -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_custom_command(OUTPUT ${PUBLIC_NAMESPACE_FILE} ${PUBLIC_UNNAMESPACE_FILE}
    DEPENDS ${PUBLIC_SYM_FILE}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=PublicNamespace -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_custom_command(OUTPUT ${PRIVATE_NAMESPACE_FILE} ${PRIVATE_UNNAMESPACE_FILE}
    DEPENDS ${PRIVATE_SYM_FILE}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=PrivateNamespace -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_custom_command(OUTPUT ${JEMALLOC_RENAME_HDR} ${JEMALLOC_MANGLE_HDR} ${JEMALLOC_MANGLE_JET_HDR}
    DEPENDS ${PUBLIC_SYM_FILE}
    COMMAND ${CMAKE_COMMAND} -DGENERATE=NameHeaders -P "${CMAKE_CURRENT_SOURCE_DIR}/Generate.cmake" VERBATIM)

add_library(jemalloc_generate STATIC
    jemalloc_generate.c
    ${SIZE_CLASSES_HDR}
    ${JEMALLOC_HDR}
    ${PUBLIC_NAMESPACE_FILE}
    ${PUBLIC_UNNAMESPACE_FILE}
    ${PRIVATE_NAMESPACE_FILE}
    ${PRIVATE_UNNAMESPACE_FILE}
    ${JEMALLOC_RENAME_HDR}
    ${JEMALLOC_MANGLE_HDR}
    ${JEMALLOC_MANGLE_JET_HDR})

set(JEMALLOC_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/jemalloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/arena.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/atomic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bitmap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chunk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_dss.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chunk_mmap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ckh.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ctl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/extent.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/huge.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/nstime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pages.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/prng.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/prof.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/quarantine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rtree.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spin.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stats.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcache.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ticker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tsd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/witness.c)

add_library(jemalloc STATIC
    ${JEMALLOC_SRCS})

target_include_directories(jemalloc PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jemalloc ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} ${JEMALLOC_EXTRA_LIBS})

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
target_include_directories(jemalloc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/msvc_compat)
endif()

set_property(TARGET jemalloc PROPERTY FOLDER "3rdparty")
set_property(TARGET jemalloc_generate PROPERTY FOLDER "3rdparty")

add_dependencies(jemalloc jemalloc_generate)
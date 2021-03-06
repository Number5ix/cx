include(GenerateCache.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/Utilities.cmake)

if (${GENERATE} STREQUAL "JemallocHeaders")
    CreateJemallocHeader("${JEMALLOC_HDR_LIST}" "${JEMALLOC_HDR}")
elseif (${GENERATE} STREQUAL "MSVCCompat")
    message(STATUS "Copying MSVC compatibility headers")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/include/msvc_compat/strings.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/msvc_compat/windows_extra.h
	DESTINATION ${TOP_BINARY_DIR}/include)
elseif (${GENERATE} STREQUAL "PublicSymbols")
    message(STATUS "Generating public symbols list")
    GeneratePublicSymbolsList("${PUBLIC_SYM}" "${MANGLING_MAP}" "${JEMALLOC_PREFIX}"  "${PUBLIC_SYM_FILE}")
elseif (${GENERATE} STREQUAL "PublicNamespace")
    message(STATUS "Generating public namespace headers")
    PublicNamespace(${PUBLIC_SYM_FILE} "${PUBLIC_NAMESPACE_FILE}")
    PublicUnnamespace(${PUBLIC_SYM_FILE} "${PUBLIC_UNNAMESPACE_FILE}")
elseif (${GENERATE} STREQUAL "PrivateNamespace")
    message(STATUS "Generating private namespace headers")
    PrivateNamespace("${PRIVATE_SYM_FILE}" "${PRIVATE_NAMESPACE_FILE}")
    PrivateUnnamespace("${PRIVATE_SYM_FILE}" "${PRIVATE_UNNAMESPACE_FILE}")
elseif (${GENERATE} STREQUAL "NameHeaders")
    message(STATUS "Generating rename/mangle headers")
    GenerateJemallocRename("${PUBLIC_SYM_FILE}" ${JEMALLOC_RENAME_HDR})
    GenerateJemallocMangle("${PUBLIC_SYM_FILE}" "${JEMALLOC_PREFIX}" ${JEMALLOC_MANGLE_HDR})
    GenerateJemallocMangle("${PUBLIC_SYM_FILE}" "${JEMALLOC_PREFIX_JET}" ${JEMALLOC_MANGLE_JET_HDR})
endif()

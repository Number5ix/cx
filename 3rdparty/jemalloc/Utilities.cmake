# Utilities.cmake
# Supporting functions to build Jemalloc

########################################################################
# CheckTypeSize
function(UtilCheckTypeSize type OUTPUT_VAR_NAME)

CHECK_TYPE_SIZE(${type} ${OUTPUT_VAR_NAME} LANGUAGE C)

if(${${OUTPUT_VAR_NAME}})
  set(${OUTPUT_VAR_NAME} ${${OUTPUT_VAR_NAME}} PARENT_SCOPE)
else()
  message(FATAL_ERROR "Can not determine ${type} size")
endif()

endfunction(UtilCheckTypeSize)

#########################################################################
# Logarithm base 2
# returns result in a VAR whose name is in RESULT_NAME
function (lg x RESULT_NAME)
  set(lg_result 0)
  while ( ${x} GREATER 1 )
    math(EXPR lg_result "${lg_result} + 1")
    math(EXPR x "${x} / 2")
  endwhile ( ${x} GREATER 1 )
  set(${RESULT_NAME} ${lg_result} PARENT_SCOPE)
endfunction(lg)

#############################################
# Read one file and append it to another
function (AppendFileContents input output)
file(READ ${input} buffer)
file(APPEND ${output} "${buffer}")
endfunction (AppendFileContents)


#############################################
# Generate public symbols list
function (GeneratePublicSymbolsList public_sym_list mangling_map symbol_prefix output_file)

file(REMOVE "${output_file}")

# First remove from public symbols those that appear in the mangling map
if(mangling_map)
  foreach(map_entry ${mangling_map})
    # Extract the symbol
    string(REGEX REPLACE "([^ \t]*):[^ \t]*" "\\1" sym ${map_entry})
    list(REMOVE_ITEM  public_sym_list ${sym})
    file(APPEND "${output_file}" "${map_entry}\n")
  endforeach(map_entry)
endif()  

foreach(pub_sym ${public_sym_list})
  file(APPEND "${output_file}" "${pub_sym}:${symbol_prefix}${pub_sym}\n")
endforeach(pub_sym)

endfunction(GeneratePublicSymbolsList)

#####################################################################
# Decorate symbols with a prefix
#
# This is per jemalloc_mangle.sh script.
#
# IMHO, the script has a bug that is currently reflected here
# If the public symbol as alternatively named in a mangling map it is not
# reflected here. Instead, all symbols are #defined using the passed symbol_prefix
function (GenerateJemallocMangle public_sym_list symbol_prefix output_file)

# Header
file(WRITE "${output_file}"
"/*\n * By default application code must explicitly refer to mangled symbol names,\n"
" * so that it is possible to use jemalloc in conjunction with another allocator\n"
" * in the same application.  Define JEMALLOC_MANGLE in order to cause automatic\n"
" * name mangling that matches the API prefixing that happened as a result of\n"
" * --with-mangling and/or --with-jemalloc-prefix configuration settings.\n"
" */\n"
"#ifdef JEMALLOC_MANGLE\n"
"#  ifndef JEMALLOC_NO_DEMANGLE\n"
"#    define JEMALLOC_NO_DEMANGLE\n"
"#  endif\n"
)

file(STRINGS "${public_sym_list}" INPUT_STRINGS)

foreach(line ${INPUT_STRINGS})
  string(REGEX REPLACE "([^ \t]*):[^ \t]*" "#  define \\1 ${symbol_prefix}\\1" output ${line})      
  file(APPEND "${output_file}" "${output}\n")
endforeach(line)

file(APPEND "${output_file}"
"#endif\n\n"
"/*\n"
" * The ${symbol_prefix}* macros can be used as stable alternative names for the\n"
" * public jemalloc API if JEMALLOC_NO_DEMANGLE is defined.  This is primarily\n"
" * meant for use in jemalloc itself, but it can be used by application code to\n"
" * provide isolation from the name mangling specified via --with-mangling\n"
" * and/or --with-jemalloc-prefix.\n"
" */\n"
"#ifndef JEMALLOC_NO_DEMANGLE\n"
)

foreach(line ${INPUT_STRINGS})
  string(REGEX REPLACE "([^ \t]*):[^ \t]*" "#  undef ${symbol_prefix}\\1" output ${line})      
  file(APPEND "${output_file}" "${output}\n")
endforeach(line)

# Footer
file(APPEND "${output_file}" "#endif\n")

endfunction (GenerateJemallocMangle)

########################################################################
# Generate jemalloc_rename.h per jemalloc_rename.sh
function (GenerateJemallocRename public_sym_list_file file_path)
# Header
file(WRITE "${file_path}"
  "/*\n * Name mangling for public symbols is controlled by --with-mangling and\n"
  " * --with-jemalloc-prefix.  With" "default settings the je_" "prefix is stripped by\n"
  " * these macro definitions.\n"
  " */\n#ifndef JEMALLOC_NO_RENAME\n\n"
)

file(STRINGS "${public_sym_list_file}" INPUT_STRINGS)
foreach(line ${INPUT_STRINGS})
  string(REGEX REPLACE "([^ \t]*):([^ \t]*)" "#define je_\\1 \\2" output ${line})
  file(APPEND "${file_path}" "${output}\n")
endforeach(line)

# Footer
file(APPEND "${file_path}"
  "#endif\n"
)
endfunction (GenerateJemallocRename)

###############################################################
# Create a jemalloc.h header by concatenating the following headers
# Mimic processing from jemalloc.sh
# This is a Windows specific function
function (CreateJemallocHeader header_list output_file)

file(REMOVE ${output_file})

message(STATUS "Creating public header ${output_file}")

file(TO_NATIVE_PATH "${output_file}" ntv_output_file)

# File Header
file(WRITE "${ntv_output_file}"
  "#ifndef JEMALLOC_H_\n"
  "#define	JEMALLOC_H_\n"
  "#ifdef __cplusplus\n"
  "extern \"C\" {\n"
  "#endif\n\n"
)

foreach(pub_hdr ${header_list} )
  set(HDR_PATH "${CMAKE_CURRENT_BINARY_DIR}/include/jemalloc/${pub_hdr}")
  file(TO_NATIVE_PATH "${HDR_PATH}" ntv_pub_hdr)
  AppendFileContents(${ntv_pub_hdr} ${ntv_output_file})
endforeach(pub_hdr)

# Footer
file(APPEND "${ntv_output_file}"
  "#ifdef __cplusplus\n"
  "}\n"
  "#endif\n"
  "#endif /* JEMALLOC_H_ */\n"
)

endfunction(CreateJemallocHeader)

############################################################################
# Redefines public symbols prefxied with je_ via a macro
# Based on public_namespace.sh which echoes the result to a stdout
function(PublicNamespace public_sym_list_file output_file)

file(REMOVE ${output_file})
file(STRINGS "${public_sym_list_file}" INPUT_STRINGS)
foreach(line ${INPUT_STRINGS})
  string(REGEX REPLACE "([^ \t]*):[^ \t]*" "#define	je_\\1 JEMALLOC_N(\\1)" output ${line})
  file(APPEND ${output_file} "${output}\n")
endforeach(line)
  
endfunction(PublicNamespace)

############################################################################
# #undefs public je_prefixed symbols
# Based on public_unnamespace.sh which echoes the result to a stdout
function(PublicUnnamespace public_sym_list_file output_file)

file(REMOVE ${output_file})
file(STRINGS "${public_sym_list_file}" INPUT_STRINGS)
foreach(line ${INPUT_STRINGS})
  string(REGEX REPLACE "([^ \t]*):[^ \t]*" "#undef	je_\\1" output ${line})
  file(APPEND ${output_file} "${output}\n")
endforeach(line)

endfunction(PublicUnnamespace)


####################################################################
# Redefines a private symbol via a macro
# Based on private_namespace.sh
function(PrivateNamespace private_sym_list_file output_file)

file(REMOVE ${output_file})
file(STRINGS ${private_sym_list_file} INPUT_STRINGS)
foreach(line ${INPUT_STRINGS})
  file(APPEND ${output_file} "#define	${line} JEMALLOC_N(${line})\n")
endforeach(line)

endfunction(PrivateNamespace)

####################################################################
# Redefines a private symbol via a macro
# Based on private_namespace.sh
function(PrivateUnnamespace private_sym_list_file output_file)

file(REMOVE ${output_file})
file(STRINGS ${private_sym_list_file} INPUT_STRINGS)
foreach(line ${INPUT_STRINGS})
  file(APPEND ${output_file} "#undef ${line}\n")
endforeach(line)

endfunction(PrivateUnnamespace)

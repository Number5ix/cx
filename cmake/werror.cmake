# Warnings as errors
if(CX_PLATFORM_IS_WASM)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
elseif(CX_COMPILER_IS_CLANG)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Werror")
elseif(CX_COMPILER_IS_GNU)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Werror -fno-strict-aliasing")
elseif(CX_COMPILER_IS_MSVC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3 /WX")
endif()
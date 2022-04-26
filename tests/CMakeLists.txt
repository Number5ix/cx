create_test_sourcelist(testsrc
    test_runner.c
    cxmemtest.c
    fmttest.c
    logtest.c
    objtest.c
    sarraytest.c
    strtest.c
    thrtest.c
    alltests.c)
add_sidl(fmttestobj.sidl
    objtestobj.sidl)
set(test_runner_files
    ${testsrc}
    fmttestobj.c
    fmttestobj.h
    fmttestobj.sidl
    objtestobj.c
    objtestobj.h
    objtestobj.sidl
    common.h)

source_group("" FILES ${test_runner_files})

add_executable(test_runner ${test_runner_files})
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_runner cx)

if(CX_PLATFORM_IS_WASM)
    target_link_options(test_runner PRIVATE "-sPTHREAD_POOL_SIZE=16" "-sPROXY_TO_PTHREAD" "-sEXIT_RUNTIME")
endif()

set_property(TARGET test_runner PROPERTY FOLDER "tests")

add_test(NAME "\"cxmem: Allocation\"" COMMAND test_runner cxmemtest alloc)
add_test(NAME "\"cxmem: Usable Size\"" COMMAND test_runner cxmemtest usable_size)
add_test(NAME "\"cxmem: Free\"" COMMAND test_runner cxmemtest free)
add_test(NAME "\"cxmem: Reallocation\"" COMMAND test_runner cxmemtest realloc)
add_test(NAME "\"format: Integers\"" COMMAND test_runner fmttest int)
add_test(NAME "\"format: Floating Point\"" COMMAND test_runner fmttest float)
add_test(NAME "\"format: Objects\"" COMMAND test_runner fmttest object)
add_test(NAME "\"format: Pointers\"" COMMAND test_runner fmttest ptr)
add_test(NAME "\"format: Strings\"" COMMAND test_runner fmttest string)
add_test(NAME "\"format: SUID\"" COMMAND test_runner fmttest suid)
add_test(NAME "\"format: Array indexing\"" COMMAND test_runner fmttest array)
add_test(NAME "\"format: Hashtable lookup\"" COMMAND test_runner fmttest hash)
add_test(NAME "\"format: Defaults\"" COMMAND test_runner fmttest default)
add_test(NAME "\"format: Error handling\"" COMMAND test_runner fmttest error)
add_test(NAME "\"log: Log Levels\"" COMMAND test_runner logtest levels)
add_test(NAME "\"log: Batch\"" COMMAND test_runner logtest batch)
add_test(NAME "\"log: Shutdown\"" COMMAND test_runner logtest shutdown)
add_test(NAME "\"object: Interface\"" COMMAND test_runner objtest iface)
add_test(NAME "\"object: Class Inheritance\"" COMMAND test_runner objtest inherit)
add_test(NAME "\"object: Interface Inheritance\"" COMMAND test_runner objtest ifinherit)
add_test(NAME "\"object: Override\"" COMMAND test_runner objtest override)
add_test(NAME "\"object: Abstract Class\"" COMMAND test_runner objtest abstract)
add_test(NAME "\"object: Dynamic Cast\"" COMMAND test_runner objtest dyncast)
add_test(NAME "\"object: Object Array\"" COMMAND test_runner objtest obj_array)
add_test(NAME "\"sarray: Int Array\"" COMMAND test_runner sarraytest int)
add_test(NAME "\"sarray: Sorted Int Array\"" COMMAND test_runner sarraytest sorted_int)
add_test(NAME "\"sarray: String Array\"" COMMAND test_runner sarraytest string)
add_test(NAME "\"sarray: Sort\"" COMMAND test_runner sarraytest sort)
add_test(NAME "\"sarray: String Sort\"" COMMAND test_runner sarraytest string_sort)
add_test(NAME "\"string: Join\"" COMMAND test_runner strtest join)
add_test(NAME "\"string: Append/Prepend\"" COMMAND test_runner strtest append)
add_test(NAME "\"string: Substrings\"" COMMAND test_runner strtest substr)
add_test(NAME "\"string: Compare\"" COMMAND test_runner strtest compare)
add_test(NAME "\"string: Long Strings\"" COMMAND test_runner strtest longstring)
add_test(NAME "\"string: Ropes\"" COMMAND test_runner strtest rope)
add_test(NAME "\"thread: Basic multithreading\"" COMMAND test_runner thrtest basic)
add_test(NAME "\"thread: Semaphore\"" COMMAND test_runner thrtest sema)
add_test(NAME "\"thread: Semaphore with timeout\"" COMMAND test_runner thrtest timeout)
add_test(NAME "\"thread: Mutex\"" COMMAND test_runner thrtest mutex)
add_test(NAME "\"thread: Reader/writer lock\"" COMMAND test_runner thrtest rwlock)
add_test(NAME "\"thread: Event\"" COMMAND test_runner thrtest event)
add_test(NAME "\"thread: Event (with spin)\"" COMMAND test_runner thrtest event_s)

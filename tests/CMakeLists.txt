create_test_sourcelist(testsrc
    test_runner.c
    cxmemtest.c
    objtest.c
    sarraytest.c
    strtest.c)
add_sidl(objtestobj.sidl)
add_executable(test_runner
    ${testsrc}
    objtestobj.c
    objtestobj.h
    objtestobj.sidl
    common.h)
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_runner cx)

set_property(TARGET test_runner PROPERTY FOLDER "tests")

add_test(NAME "\"cxmem: Allocation\"" COMMAND test_runner cxmemtest alloc)
add_test(NAME "\"cxmem: Usable Size\"" COMMAND test_runner cxmemtest usable_size)
add_test(NAME "\"cxmem: Free\"" COMMAND test_runner cxmemtest free)
add_test(NAME "\"cxmem: Reallocation\"" COMMAND test_runner cxmemtest realloc)
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

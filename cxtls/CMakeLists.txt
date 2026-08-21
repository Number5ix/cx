# mbedTLS is built as ten OBJECT libraries (see 3rdparty/CMakeLists.txt) whose
# objects are absorbed here, so downstream consumers link one cxtls.a rather
# than a pile of archives.
#
# The $<TARGET_OBJECTS:> list is written out in full on purpose. CMake only
# contributes objects from object libraries named *directly*, and does not
# gather them transitively through nested ones -- so the eight targets that
# tfpsacrypto merely links against would silently vanish if this list were
# trimmed to the three aggregates.
add_library(cxtls STATIC
    cxtls.c
    cxtls_entropy.c
    $<TARGET_OBJECTS:tfpsacrypto>
    $<TARGET_OBJECTS:mbedx509>
    $<TARGET_OBJECTS:mbedtls>
    $<TARGET_OBJECTS:platform>
    $<TARGET_OBJECTS:extras>
    $<TARGET_OBJECTS:utilities>
    $<TARGET_OBJECTS:builtin>
    $<TARGET_OBJECTS:everest>
    $<TARGET_OBJECTS:p256-m>
    $<TARGET_OBJECTS:pqcp>)

# Linked for usage requirements only -- include paths, the *_CONFIG_FILE
# defines and the platform link libraries. The objects come from the list above.
target_link_libraries(cxtls cx tfpsacrypto mbedx509 mbedtls)
target_include_directories(cxtls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

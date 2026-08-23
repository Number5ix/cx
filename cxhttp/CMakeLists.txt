include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

add_cxautogen(httprequest.cxh httpconn.cxh httpcookie.cxh httpclient.cxh)

add_library(cxhttp STATIC
    httpinit.c
    httpurl.c
    httpheaders.c
    httpdate.c
    httpparse.c
    httpchunk.c
    httprequest.c
    httprequest.cxh
    httpconn.c
    httpconn.cxh
    httpcookie.c
    httpcookie.cxh
    httpclient.c
    httpclient.cxh
    httpform.c
    include/cxhttp.h
    include/cxhttp/http_shared.h
    http_private.h)

# cxtls brings cx transitively, along with the include paths for both.
target_link_libraries(cxhttp cxtls)

target_include_directories(cxhttp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

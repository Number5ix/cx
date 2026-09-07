include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

# The HTTP/3 framing layer, built only when cxquic is there to carry it. Nothing in a public
# header is conditional on this: the version-policy API is always declared, and a build without
# HTTP/3 gets http3stub.c, whose _http3Available() answers false.
set(http3src "")
if(CX_HTTP3)
    set(http3src_cxh http3srvconn.cxh http3conn.cxh)
    set(http3src
        http3_private.h
        http3wire.c
        qpackhuff.c
        qpack.c
        http3session.c
        http3msg.c
        http3srvconn.c
        http3srvconn.cxh
        http3conn.c
        http3conn.cxh)
else()
    set(http3src http3stub.c)
endif()

add_cxautogen(httprequest.cxh httpconn.cxh httpcookie.cxh httpclient.cxh
              httpserverreq.cxh httpserverconn.cxh httpserver.cxh ${http3src_cxh})

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
    httpaltsvc.c
    httpcookie.c
    httpcookie.cxh
    httpclient.c
    httpclient.cxh
    httpform.c
    httpserverreq.c
    httpserverreq.cxh
    httpserverconn.c
    httpserverconn.cxh
    httpserver.c
    httpserver.cxh
    include/cxhttp.h
    include/cxhttp/http_shared.h
    http_private.h
    ${http3src})

# cxtls brings cx transitively, along with the include paths for both.
target_link_libraries(cxhttp cxtls)

if(CX_HTTP3)
    target_link_libraries(cxhttp cxquic)
endif()

target_include_directories(cxhttp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

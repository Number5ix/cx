include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)

add_cxautogen(quicsocket.cxh)

add_library(cxquic STATIC
    quicinit.c
    wire.c
    frame.c
    crypto.c
    params.c
    reasm.c
    token.c
    sendbuf.c
    stream.c
    recovery.c
    conn.c
    qlog.c
    quicsocket.c
    include/cxquic.h
    include/cxquic/quic_shared.h
    include/cxquic/quicnet.h
    quicsocket.cxh
    quic_private.h
    conn_private.h
    qlog_private.h
    stream_private.h)

# cxtls brings cx transitively, along with the include paths for both, and the PSA
# crypto headers cxquic's packet protection is built on.
target_link_libraries(cxquic cxtls)

target_include_directories(cxquic
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

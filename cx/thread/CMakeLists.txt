set(THREAD_SRCS
    thread/atomic.c
    thread/event.c
    thread/mutex.c
    thread/rwlock.c
    thread/sema.c
    thread/systhread.c
    thread/thread.c)
set(THREAD_INCLUDES
    thread/atomic.h
    thread/event.h
    thread/mutex.h
    thread/rwlock.h
    thread/sema.h
    thread/thread.h
    thread/thread_private.h)

set(CX_SRCS
    ${CX_SRCS}
    ${THREAD_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${THREAD_INCLUDES}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

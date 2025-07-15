set(TQ_RESOURCE_SRCS
    taskqueue/resource/taskresource.c
    taskqueue/resource/trfifo.c
    taskqueue/resource/trlifo.c
    taskqueue/resource/trmutex.c
    )
set(TQ_RESOURCE_SIDL
    taskqueue/resource/taskresource.sidl
    taskqueue/resource/trfifo.sidl
    taskqueue/resource/trlifo.sidl
    taskqueue/resource/trmutex.sidl
    )

set(TASKQUEUE_SRCS
    ${TASKQUEUE_SRCS}
    ${TQ_RESOURCE_SRCS}
    PARENT_SCOPE)
set(TASKQUEUE_SIDL
    ${TASKQUEUE_SIDL}
    ${TQ_RESOURCE_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

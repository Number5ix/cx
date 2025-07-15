set(TQ_QUEUE_SRCS
    taskqueue/queue/tqcomplex.c
    taskqueue/queue/tqueue.c
    )
set(TQ_QUEUE_SIDL
    taskqueue/queue/tqcomplex.sidl
    taskqueue/queue/tqueue.sidl
    )

set(TASKQUEUE_SRCS
    ${TASKQUEUE_SRCS}
    ${TQ_QUEUE_SRCS}
    PARENT_SCOPE)
set(TASKQUEUE_SIDL
    ${TASKQUEUE_SIDL}
    ${TQ_QUEUE_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

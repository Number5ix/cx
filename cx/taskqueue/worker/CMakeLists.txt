set(TQ_WORKER_SRCS
    taskqueue/worker/tqmanualworker.c
    taskqueue/worker/tqthreadworker.c
    taskqueue/worker/tqworker.c
    )
set(TQ_WORKER_SIDL
    taskqueue/worker/tqmanualworker.sidl
    taskqueue/worker/tqthreadworker.sidl
    taskqueue/worker/tqworker.sidl
    )

set(TASKQUEUE_SRCS
    ${TASKQUEUE_SRCS}
    ${TQ_WORKER_SRCS}
    PARENT_SCOPE)
set(TASKQUEUE_SIDL
    ${TASKQUEUE_SIDL}
    ${TQ_WORKER_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

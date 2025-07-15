set(TQ_TASK_SRCS
    taskqueue/task/basictask.c
    taskqueue/task/complextask.c
    taskqueue/task/mptask.c
    taskqueue/task/task.c
    )
set(TQ_TASK_SIDL
    taskqueue/task/basictask.sidl
    taskqueue/task/complextask.sidl
    taskqueue/task/mptask.sidl
    taskqueue/task/task.sidl
    )

set(TASKQUEUE_SRCS
    ${TASKQUEUE_SRCS}
    ${TQ_TASK_SRCS}
    PARENT_SCOPE)
set(TASKQUEUE_SIDL
    ${TASKQUEUE_SIDL}
    ${TQ_TASK_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

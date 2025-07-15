set(TQ_REQUIRES_SRCS
    taskqueue/requires/taskrequires.c
    taskqueue/requires/taskrequiresgate.c
    taskqueue/requires/taskrequiresresource.c
    taskqueue/requires/taskrequirestask.c
    )
set(TQ_TREQUIRES_SIDL
    taskqueue/requires/taskrequires.sidl
    taskqueue/requires/taskrequiresgate.sidl
    taskqueue/requires/taskrequiresresource.sidl
    taskqueue/requires/taskrequirestask.sidl
    )

set(TASKQUEUE_SRCS
    ${TASKQUEUE_SRCS}
    ${TQ_REQUIRES_SRCS}
    PARENT_SCOPE)
set(TASKQUEUE_SIDL
    ${TASKQUEUE_SIDL}
    ${TQ_TREQUIRES_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

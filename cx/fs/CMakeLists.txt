set(FS_SRCS
    fs/path.c
    fs/pathmatch.c
    fs/vfs.c
    fs/vfsdirsearch.c
    fs/vfsfileops.c
    fs/vfsobj.c
    fs/vfsops.c
    fs/vfsprovider.c
    fs/vfsstruct.c)
set(FS_INCLUDES
    fs/file.h
    fs/fs.h
    fs/fs_private.h
    fs/path.h
    fs/vfs.h
    fs/vfs_private.h
    fs/vfsobj.h
    fs/vfsprovider.h)
set(FS_SIDL
    fs/vfsobj.sidl
    fs/vfsprovider.sidl)

add_subdirectory(vfsfs)
add_subdirectory(vfsvfs)

set(CX_SRCS
    ${CX_SRCS}
    ${FS_SRCS}
    PARENT_SCOPE)
set(CX_INCLUDES
    ${CX_INCLUDES}
    ${FS_INCLUDES}
    PARENT_SCOPE)
set(CX_SIDL
    ${CX_SIDL}
    ${FS_SIDL}
    PARENT_SCOPE)

set(CX_CMAKE_FILES
    ${CX_CMAKE_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    PARENT_SCOPE)

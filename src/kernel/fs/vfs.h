#ifndef FS_VFS_H
#define FS_VFS_H
#include "../../user/fsabi.h"
#include "../kklibc/ctypes.h"
#define VFS_PATH_MAX 256
#define VFS_NAME_MAX 13

enum {
    VFS_OK = 0,
    VFS_ENOENT = -1,
    VFS_EIO = -2,
    VFS_EINVAL = -3,
    VFS_ENOSPC = -4,
    VFS_EEXIST = -5,
    VFS_ENOTDIR = -6,
    VFS_EISDIR = -7,
    VFS_ENOTEMPTY = -8,
    VFS_EROFS = -9,
    VFS_ECORRUPT = -10,
    VFS_ENOMEM = -11,
    VFS_EBUSY = -12
};

typedef kos_stat vfs_stat_t;
typedef kos_dirent vfs_dirent_t;

typedef struct {
    char path[VFS_PATH_MAX];
    u32 offset, generation;
    u8 open;
} vfs_file_t;

int vfs_open(const char* path, vfs_file_t* file);
int vfs_file_read(vfs_file_t* file, void* buffer, u32 capacity);
int vfs_file_write(vfs_file_t* file, const void* buffer, u32 size);
int vfs_seek(vfs_file_t* file, u32 offset);
void vfs_close(vfs_file_t* file);

typedef struct {
    int (*stat)(const char*, vfs_stat_t*);
    int (*read)(const char*, u32, void*, u32);
    int (*replace)(const char*, const void*, u32);
    int (*create)(const char*, int);
    int (*remove)(const char*, int);
    int (*rename)(const char*, const char*);
    int (*readdir)(const char*, u32*, vfs_dirent_t*);
} vfs_ops_t;

void vfs_mount(const vfs_ops_t* ops);
int vfs_normalize(const char* path, char out[VFS_PATH_MAX]);
const char* vfs_getcwd(void);
int vfs_chdir(const char* path);
int vfs_stat(const char* path, vfs_stat_t* stat);
int vfs_read(const char* path, u32 offset, void* buffer, u32 capacity);
int vfs_replace(const char* path, const void* data, u32 size);
int vfs_write(const char* path, u32 offset, const void* data, u32 size);
int vfs_append(const char* path, const void* data, u32 size);
int vfs_truncate(const char* path, u32 size);
int vfs_create(const char* path);
int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_rmdir(const char* path);
int vfs_rename(const char* source, const char* destination);
int vfs_copy(const char* source, const char* destination);
/* readdir: 1 entry, 0 end, negative error. Cursor is opaque to callers. */
int vfs_readdir(const char* path, u32* cursor, vfs_dirent_t* entry);
const char* vfs_error(int error);
#endif

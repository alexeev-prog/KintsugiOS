#ifndef FS_VFS_H
#define FS_VFS_H
#include "../../user/fsabi.h"
#include "../kklibc/ctypes.h"
#define VFS_PATH_MAX 256
#define VFS_NAME_MAX 256 /* long names; FAT12 enforces 8.3 inside its own driver */
#define VFS_LONG_NAME_MAX KOS_LONG_NAME_MAX

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
typedef kos_long_dirent vfs_long_dirent_t;

typedef struct {
    void* object;
    u32 offset, generation;
    u8 open;
} vfs_file_t;

int vfs_open(const char* path, vfs_file_t* file);
int vfs_file_read(vfs_file_t* file, void* buffer, u32 capacity);
int vfs_file_write(vfs_file_t* file, const void* buffer, u32 size);
int vfs_file_stat(vfs_file_t* file, vfs_stat_t* stat);
int vfs_file_truncate(vfs_file_t* file, u32 size);
const char* vfs_file_path(const vfs_file_t* file);
void vfs_file_clone(vfs_file_t* destination, const vfs_file_t* source);
int vfs_seek(vfs_file_t* file, u32 offset);
void vfs_close(vfs_file_t* file);

typedef struct {
    int (*stat)(const char*, vfs_stat_t*);
    int (*read)(const char*, u32, void*, u32);
    int (*write)(const char*, u32, const void*, u32);
    int (*replace)(const char*, const void*, u32);
    int (*create)(const char*, int);
    int (*remove)(const char*, int);
    int (*rename)(const char*, const char*);
    int (*readdir)(const char*, u32*, vfs_dirent_t*);
    int (*readdir_long)(const char*, u32*, vfs_long_dirent_t*);
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
int vfs_readdir_long(const char* path, u32* cursor, vfs_long_dirent_t* entry);
/* Read-only change journal exposed as /proc/events. */
int vfs_events_read(char* buffer, u32 capacity);
const char* vfs_error(int error);
#endif

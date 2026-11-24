#ifndef KERNEL_VFS_META_H
#define KERNEL_VFS_META_H
#include "../kklibc/ctypes.h"

/* Per-path owner/mode overrides. KFS2 inodes have no owner field, so the
 * POSIX metadata lives in a plain file (/etc/fsmeta) like any other setting. */
#define VFS_META_MAX 64

typedef struct {
    char path[256];
    u32 uid;
    u16 mode;
    int used;
} vfs_meta_entry;

int vfs_meta_load(void);
int vfs_meta_save(void);
int vfs_meta_get(const char* path, u32* uid, u16* mode);
int vfs_meta_set(const char* path, u32 uid, u16 mode);

#endif

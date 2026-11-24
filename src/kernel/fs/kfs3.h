#ifndef FS_KFS3_H
#define FS_KFS3_H
#include "../kklibc/ctypes.h"
#include "../../user/fsabi.h"

/* KFS3 - POSIX compatibility and security layer over the mounted KFS2/FAT12
 * backend. It owns the user namespace, path translation, permission policy
 * and synthetic files (/proc, /dev) so the system follows the rule that
 * everything is a file. */
void kfs3_init(void);
int kfs3_is_active(void);
int kfs3_translate(const char* in, char out[256]);
int kfs3_ensure_home(const char* user);
int kfs3_permit(const char* absolute, int write);

/* Synthetic filesystem. Each function returns 1 when it handled the path. */
int kfs3_pseudo_stat(const char* absolute, kos_stat* stat);
int kfs3_pseudo_read(const char* absolute, u32 offset, void* buffer, u32 capacity, int* handled);
int kfs3_pseudo_write(const char* absolute, u32 offset, const void* buffer, u32 size, int* handled);
u32 kfs3_owner(const char* absolute);
int kfs3_pseudo_readdir(const char* absolute, u32* cursor, kos_dirent* entry);
int kfs3_pseudo_readdir_long(const char* absolute, u32* cursor, kos_long_dirent* entry);
#endif

#ifndef FS_BLOCK_H
#define FS_BLOCK_H

#include "../kklibc/ctypes.h"

#define FS_BLOCK_SIZE 512u

int block_read(u32 sector, void* buffer);
int block_write(u32 sector, const void* buffer);
int block_sync(void);
void block_invalidate(void);

#endif

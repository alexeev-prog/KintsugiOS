#ifndef FS_EXT2_H
#define FS_EXT2_H
#include "../kklibc/ctypes.h"

int ext2_init(void);
int ext2_is_mounted(void);
void ext2_cleanup(void);
void ext2_info(void);
void ext2_usage(u32* sectors, u32* bytes_per_block, u32* blocks, u32* free_blocks);

#endif

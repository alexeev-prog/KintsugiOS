#ifndef FS_FAT12_H
#define FS_FAT12_H
#include "../kklibc/ctypes.h"

typedef struct __attribute__((packed)) {
    u8 jmp[3];
    char oem[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 root_entries;
    u16 total_sectors;
    u8 media_type;
    u16 sectors_per_fat;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 large_sector_count;
    u8 drive_number, reserved, boot_signature;
    u32 volume_id;
    char volume_label[11], fs_type[8];
} fat12_boot_sector_t;

typedef struct __attribute__((packed)) {
    char filename[8], extension[3];
    u8 attributes, reserved[10];
    u16 time_created, date_created, first_cluster;
    u32 file_size;
} fat12_dir_entry_t;

typedef struct {
    u32 fat_start_sector, fat_size_sectors;
    u32 root_dir_start_sector, root_dir_size_sectors;
    u32 data_start_sector, total_clusters;
    u8* fat_buffer;
} fat12_context_t;

/* File operations are exposed exclusively through vfs.h. */
void fat12_init(void);
void fat12_cleanup(void);
int fat12_is_mounted(void);
int fat12_read_boot_sector(fat12_boot_sector_t* boot);
void print_fat12_info(void);
int fat12_check(void);
void fat12_usage(u32* sectors, u32* cluster_bytes, u32* clusters, u32* free_clusters);
#endif
